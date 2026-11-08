/* Android backend: Vulkan (docs/ANDROID_PLAN.md section 3), executing the frames built by gx_render.c on a
 * render thread. Ported from the Switch's deko3d backend (platform/switch/render_deko.c): same EFB model
 * (an image of up to EFB_SCALE x 640x528, top row first, the internal resolution in its top-left corner),
 * same register-driven ubershader (runtime/shaders, SPIR-V from glslc: tools/embed_shaders.py spv), the
 * same two frame slots of host-visible memory (vertices, indices, register blocks, XF images, texture
 * staging) reused once their fence has passed.
 *
 * Vulkan specifics:
 *   - dynamic rendering (no render pass objects); one pipeline per fixed-function state (blend, colour
 *     mask, depth, cull, primitive, shader), cached in memory and on disk (vk_pipeline_cache.bin);
 *   - set 0 = the register block and the state's XF image (dynamic uniform offsets), set 1 = 8 textures
 *     (push descriptors when the driver has them, else a per-frame pool);
 *   - EFB copies end the rendering, blit, and start it again; clears that keep some channels are a quad;
 *   - the picture goes to the screen as a quad (letterboxed to the display aspect), then the menu image
 *     and the touch controls (touch_android.c) over it;
 *   - the swapchain is made again when the surface is lost (the app went to the background).
 * Not yet: specialized shaders (the ubershader runs every draw), GPU timestamps, pre-rotation. */
#include <SDL.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_shaders_spv.h"  /* generated: tools/embed_shaders.py spv */
#include "gx_shader_key.h"
#include "gx_spec_spv.h"     /* generated: tools/gen_shaders.py --vk (specialized fragment shaders) */
#include "overlay.h"
#include "runtime.h"
#include "settings.h"
#include "touch_android.h"
#include "touch_icons.h"     /* generated: tools/embed_icons.py (android/touch_icons) */

void input_init(void);
void input_pump(void);

#define EFB_SCALE 2
static float g_s = EFB_SCALE;
#define SC(v) ((uint32_t)((float)(v) * g_s + 0.5f))
/* the wide screen modes (gx_render.h GX_XR_WIDE_K): X is also scaled by this frame's k; the EFB is
 * allocated for up to 32:9 */
#define WIDE_K_MAX (1708.0f / 640.0f)
static float g_k = 1.0f;
#define XSC(v) ((uint32_t)((float)(v) * g_k * g_s + 0.5f))
#define EFB_W ((int)(EFB_WIDTH * WIDE_K_MAX + 0.5f) * EFB_SCALE)
#define EFB_H (EFB_HEIGHT * EFB_SCALE)
#define MAX_TEX 8192

#define SLOT_VTX   (48u << 20)
#define SLOT_UBO   (24u << 20)
#define SLOT_STAGE (32u << 20)
#define SLOT_XFI   (8u << 20)
#define OFF_VTX    0u
#define OFF_UBO    SLOT_VTX
#define OFF_STAGE  (SLOT_VTX + SLOT_UBO)
#define OFF_XFI    (SLOT_VTX + SLOT_UBO + SLOT_STAGE)
#define SLOT_SIZE  (SLOT_VTX + SLOT_UBO + SLOT_STAGE + SLOT_XFI)
#define XF_IMAGE_CHUNKS 12
#define XF_IMAGE_BYTES (XF_IMAGE_CHUNKS * GX_XFM_CHUNK * 4)
static const uint8_t k_xf_image_chunks[XF_IMAGE_CHUNKS] = {0, 1, 2, 3, 16, 17, 20, 21, 22, 23, 24, 25};
#define IMAGE_POOL (384u << 20)
#define PIPE_CACHE_FILE "vk_pipeline_cache.bin"
#define PIPE_KEYS_FILE "vk_pipeline_keys.bin"  /* every pipeline key seen, prebuilt at boot (prewarm_thread) */
#define PIPE_KEYS_MAGIC 0x314B5056u            /* "VPK1" */

#define VKC(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) rt_fatal("[vk] %s failed: %d", #x, (int)r_); } while (0)

/* ---- device ---- */
static SDL_Window* g_win;
static VkInstance g_inst;
static VkPhysicalDevice g_pd;
static VkPhysicalDeviceProperties g_props;
static VkPhysicalDeviceMemoryProperties g_mem;
static VkDevice g_dev;
static VkQueue g_q;
static uint32_t g_qfi;
static int g_push_desc, g_dual_src;
static PFN_vkCmdBeginRenderingKHR p_begin_rendering;
static PFN_vkCmdEndRenderingKHR p_end_rendering;
static PFN_vkCmdPushDescriptorSetKHR p_push_desc;
static VkFormat g_depth_fmt;
static int g_core13;  /* Vulkan 1.3 core on both sides (else the dynamic rendering extension) */

/* ---- swapchain ---- */
static VkSurfaceKHR g_surf;
static VkSwapchainKHR g_swap;
static VkFormat g_swap_fmt = VK_FORMAT_UNDEFINED;
static VkExtent2D g_ext;      /* the screen as the game sees it (landscape) */
static VkExtent2D g_img_ext;  /* the swapchain images: g_ext, or its sides swapped when pre-rotated */
static float g_rot[4] = {1, 0, 0, 1};  /* clip-space rotation of the screen quads (pre-rotation, SR_VK_PRETRANSFORM=1) */
static uint32_t g_nimg;
static VkImage g_simg[8];
static VkImageView g_sview[8];
static VkImageLayout g_slayout[8];
static VkSemaphore g_sdone[8];
static int g_swap_can_read;
static int g_swap_bad;  /* needs making again before the next present */
/* VK_ERROR_DEVICE_LOST seen (the GPU was reset): the frame is dropped and execute() rebuilds the device first.
 * Seen on Adreno while the screen is recorded (docs/notes/android.md "Open: device lost"). */
static int g_lost;
/* device-lost bisect tools (2026-11-06, docs/notes/android.md): SR_VK_DRAWS=lo-hi sends only draws lo..hi-1 of
 * each frame to the GPU; SR_VK_ONEPIPE=1 keeps a pass's first pipeline (wrong picture) */
static unsigned g_draw_lo, g_draw_hi = ~0u;
static int g_one_pipe;
static unsigned g_fr_binds, g_binds_sum;  /* GX pipeline switches this frame, and summed for the report */
static uint64_t g_last_gpu_ns;  /* the last measured frame's GPU time (for the [hitch] line) */

/* a call's result where the device can be lost: 1 if it worked, 0 if lost (noted), else a fatal error */
static int vk_ok(VkResult r, const char* what) {
    if (r == VK_SUCCESS) return 1;
    if (r == VK_ERROR_DEVICE_LOST) {
        if (!g_lost) rt_log("[vk] device lost (%s)", what);
        g_lost = 1;
        return 0;
    }
    rt_fatal("[vk] %s failed: %d", what, (int)r);
}

/* ---- frame slots ---- */
static VkBuffer g_sbuf[2];
static VkDeviceMemory g_smem[2];
static uint8_t* g_smap[2];
static VkCommandPool g_cpool;
static VkCommandBuffer g_cb[2];
static VkCommandBuffer g_cmd;  /* this frame's */
static VkFence g_fence[2];
static int g_fenced[2];
static VkSemaphore g_acq[2];
static VkDescriptorPool g_dpool_static, g_dpool_frame[2];
static VkDescriptorSet g_ubo_set[2];
static unsigned g_slot;
static int* g_retired[2];
static uint32_t g_nretired[2];
static uint32_t g_stage_off;

/* ---- layouts, shaders, pipelines ---- */
static VkDescriptorSetLayout g_dsl_ubo, g_dsl_tex, g_dsl_one;
static VkPipelineLayout g_pl_gx, g_pl_quad;
static VkShaderModule g_sm_vs, g_sm_fs, g_sm_fs_ind, g_sm_qvs, g_sm_tex, g_sm_fill, g_sm_touch;
/* Specialized fragment shaders (as on the Switch): one per recorded state key, with the TEV setup folded
 * into constants. A GX pipeline key carries SPEC_FLAG and the low 24 bits of the state's key hash in
 * bits 36-59; states without one use the ubershader, and their keys go to shader_keys.txt in the app
 * folder for the next build (tools/gen_shaders.py, build/shaderkeys/). SR_NOSPEC=1: ubershader only. */
#define SPEC_FLAG (1ull << 60)
#define SPEC_SHIFT 36
static VkShaderModule g_spec_sm[GX_SPEC_COUNT > 0 ? GX_SPEC_COUNT : 1];
static int g_spec_on = 1;
static unsigned long long g_spec_hits, g_spec_misses;  /* states, for the log */
static VkPipelineCache g_pcache;
static unsigned g_pipes_new, g_pipes_total;
static uint64_t g_pcache_saved_ns;
static uint64_t* g_keys;          /* the keys in PIPE_KEYS_FILE plus the ones first built this run */
static uint32_t g_nkeys, g_keys_cap, g_keys_saved;
/* this frame's render-thread costs, for the [hitch] line */
static uint64_t g_fr_pipe_ns, g_fr_wait_ns, g_fr_img_ns, g_fr_acq_ns;
/* frame phases summed over the 300-frame report: copy-in, recording, submit, present */
static uint64_t g_ph_in, g_ph_rec, g_ph_sub, g_ph_pres, g_ph_acq;
/* GPU time per frame: a timestamp at the start and the end of each slot's command buffer */
static VkQueryPool g_qpool;
static int g_ts_written[2];
static uint64_t g_gpu_ns_sum, g_gpu_ns_max;
static unsigned g_gpu_frames, g_passes, g_passes_sum;
static unsigned g_fr_pipe_n, g_fr_img_n;
static VkSampler g_samp[36], g_samp_lin, g_samp_near;

/* ---- images ---- */
typedef struct VkImg {
    VkImage img;
    VkImageView view;
    VkImageLayout layout;
    VkDeviceMemory mem;  /* own memory (images outside the pool), else VK_NULL_HANDLE */
    uint32_t off, size, w, h;
    int live;
    uint32_t gen;
} VkImg;
static VkDeviceMemory g_pool_mem;
static uint32_t g_pool_type = UINT32_MAX;
static VkImg g_efb, g_depth, g_ov, g_dummy, g_icons;
static VkImg* g_tex;
static int g_tex_cap;
static unsigned g_ov_version = ~0u;
static int g_ov_box[4];
static unsigned g_presented, g_frames_done;

/* ---------------------------------------------------------------- small helpers */
static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags prefer) {
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < g_mem.memoryTypeCount; i++) {
            VkMemoryPropertyFlags f = g_mem.memoryTypes[i].propertyFlags;
            if ((bits & (1u << i)) && (f & want) == want && (pass || (f & prefer) == prefer)) return i;
        }
    rt_fatal("[vk] no memory type for bits %X flags %X", bits, want);
}

static VkShaderModule shader(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, bytes, code};
    VkShaderModule m;
    VKC(vkCreateShaderModule(g_dev, &ci, NULL, &m));
    return m;
}

static void barrier(VkImage img, VkImageAspectFlags aspect, VkImageLayout* cur, VkImageLayout to) {
    if (*cur == to && to != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && to != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) return;
    /* everything before, everything after: simple and correct; finer stages later if it shows in profiles */
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, VK_ACCESS_MEMORY_WRITE_BIT,
                              VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, *cur, to, VK_QUEUE_FAMILY_IGNORED,
                              VK_QUEUE_FAMILY_IGNORED, img, {aspect, 0, 1, 0, 1}};
    vkCmdPipelineBarrier(g_cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
    *cur = to;
}
static void to_layout(VkImg* t, VkImageLayout to) {
    VkImageAspectFlags a = t == &g_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    if (t == &g_depth && (g_depth_fmt == VK_FORMAT_D24_UNORM_S8_UINT || g_depth_fmt == VK_FORMAT_D32_SFLOAT_S8_UINT))
        a |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (t->layout == to) return;
    barrier(t->img, a, &t->layout, to);
}

/* ---- image pool: first-fit free list (as render_deko.c) ---- */
typedef struct Block { uint32_t off, size; struct Block* next; } Block;
static Block* g_free_list;

static uint32_t pool_alloc(uint32_t size, uint32_t align) {
    for (Block** pp = &g_free_list; *pp; pp = &(*pp)->next) {
        Block* b = *pp;
        uint32_t start = (b->off + align - 1) & ~(align - 1);
        if (start + size > b->off + b->size) continue;
        uint32_t end = start + size, b_end = b->off + b->size;
        if (start > b->off) {
            b->size = start - b->off;
            if (end < b_end) {
                Block* n = malloc(sizeof *n);
                n->off = end; n->size = b_end - end; n->next = b->next;
                b->next = n;
            }
        } else if (end < b_end) {
            b->off = end; b->size = b_end - end;
        } else {
            *pp = b->next;
            free(b);
        }
        return start;
    }
    rt_fatal("image pool exhausted (%u bytes)", size);
}

static void pool_free(uint32_t off, uint32_t size) {
    Block* n = malloc(sizeof *n);
    n->off = off; n->size = size;
    Block** pp = &g_free_list;
    while (*pp && (*pp)->off < off) pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
    if (n->next && n->off + n->size == n->next->off) {
        Block* x = n->next;
        n->size += x->size; n->next = x->next; free(x);
    }
    for (Block* p = g_free_list; p && p->next; p = p->next)
        if (p->next == n && p->off + p->size == n->off) {
            p->size += n->size; p->next = n->next; free(n);
            break;
        }
}

static void image_make(VkImg* t, uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage, int in_pool) {
    uint64_t t0 = platform_now_ns();
    memset(t, 0, sizeof *t);
    VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, fmt, {w, h, 1}, 1, 1,
                            VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, usage, VK_SHARING_MODE_EXCLUSIVE, 0, NULL,
                            VK_IMAGE_LAYOUT_UNDEFINED};
    VKC(vkCreateImage(g_dev, &ci, NULL, &t->img));
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(g_dev, t->img, &mr);
    if (in_pool) {
        if (!(mr.memoryTypeBits & (1u << g_pool_type))) rt_fatal("[vk] an image cannot live in the image pool's memory");
        t->size = (uint32_t)mr.size;
        t->off = pool_alloc(t->size, (uint32_t)mr.alignment);
        VKC(vkBindImageMemory(g_dev, t->img, g_pool_mem, t->off));
    } else {
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                   mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0)};
        VkDeviceMemory m;
        VKC(vkAllocateMemory(g_dev, &ai, NULL, &m));
        VKC(vkBindImageMemory(g_dev, t->img, m, 0));
        t->mem = m;
    }
    int depth = fmt == g_depth_fmt && (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, t->img, VK_IMAGE_VIEW_TYPE_2D, fmt,
                                {0, 0, 0, 0}, {depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VKC(vkCreateImageView(g_dev, &vi, NULL, &t->view));
    t->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    t->w = w;
    t->h = h;
    t->live = 1;
    t->gen = 0xFFFFFFFFu;
    g_fr_img_ns += platform_now_ns() - t0;
    g_fr_img_n++;
}

static void image_free(VkImg* t) {
    if (!t->live) return;
    vkDestroyImageView(g_dev, t->view, NULL);
    vkDestroyImage(g_dev, t->img, NULL);
    pool_free(t->off, t->size);
    t->live = 0;
}

/* ---- textures ---- */
static VkImg* tex_slot(int id) {
    if (id >= g_tex_cap) {
        int n = g_tex_cap ? g_tex_cap : 1024;
        while (n <= id) n *= 2;
        g_tex = realloc(g_tex, n * sizeof *g_tex);
        memset(g_tex + g_tex_cap, 0, (n - g_tex_cap) * sizeof *g_tex);
        g_tex_cap = n;
    }
    return &g_tex[id];
}

static void tex_destroy(int id) {
    if (id < g_tex_cap) image_free(&g_tex[id]);
}

/* texture `id` exists and holds its current pixels; outside rendering (uploads are transfer commands) */
static VkImg* tex_prepare(int id) {
    GxTexture* gt = gx_texture_get(id);
    if (!gt) return NULL;
    VkImg* t = tex_slot(id);
    float cs = settings_internal_scale();
    uint32_t w = gt->is_copy ? (uint32_t)(gt->width * cs + 0.5f) : gt->width;
    uint32_t h = gt->is_copy ? (uint32_t)(gt->height * cs + 0.5f) : gt->height;
    if (!w) w = 1;
    if (!h) h = 1;
    if (!t->live)
        image_make(t, w, h, VK_FORMAT_R8G8B8A8_UNORM,
                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       (gt->is_copy ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : 0), 1);
    if (!gt->is_copy && t->gen != gt->gen) {
        uint32_t bytes = gt->width * gt->height * 4;
        if (g_stage_off + bytes > SLOT_STAGE) return t;  /* staging full this frame: next frame */
        memcpy(g_smap[g_slot] + OFF_STAGE + g_stage_off, gt->rgba, bytes);
        to_layout(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy r = {OFF_STAGE + g_stage_off, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {gt->width, gt->height, 1}};
        vkCmdCopyBufferToImage(g_cmd, g_sbuf[g_slot], t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
        g_stage_off = (g_stage_off + bytes + 255) & ~255u;
        t->gen = gt->gen;
    }
    if (t->layout == VK_IMAGE_LAYOUT_UNDEFINED || t->layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        to_layout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return t;
}

/* ---------------------------------------------------------------- pipelines */
enum { TGT_EFB, TGT_TEX, TGT_SWAP };
enum { Q_TEX_OPAQUE, Q_TEX_OVER, Q_FILL, Q_TOUCH };

typedef struct { uint64_t key; VkPipeline p; } PipeEntry;
static PipeEntry* g_pipes;
static uint32_t g_pipes_cap;

static VkPipeline pipe_find(uint64_t key) {
    if (!g_pipes_cap) return VK_NULL_HANDLE;
    for (uint32_t i = (uint32_t)(key * 0x9E3779B97F4A7C15ull >> 40) & (g_pipes_cap - 1);; i = (i + 1) & (g_pipes_cap - 1)) {
        if (!g_pipes[i].p) return VK_NULL_HANDLE;
        if (g_pipes[i].key == key) return g_pipes[i].p;
    }
}
static void pipe_put(uint64_t key, VkPipeline p) {
    if ((g_pipes_total + 1) * 2 > g_pipes_cap) {
        PipeEntry* old = g_pipes;
        uint32_t oc = g_pipes_cap;
        g_pipes_cap = oc ? oc * 2 : 1024;
        g_pipes = calloc(g_pipes_cap, sizeof *g_pipes);
        g_pipes_total = 0;
        for (uint32_t i = 0; i < oc; i++)
            if (old[i].p) pipe_put(old[i].key, old[i].p);
        free(old);
    }
    uint32_t i = (uint32_t)(key * 0x9E3779B97F4A7C15ull >> 40) & (g_pipes_cap - 1);
    while (g_pipes[i].p) i = (i + 1) & (g_pipes_cap - 1);
    g_pipes[i].key = key;
    g_pipes[i].p = p;
    g_pipes_total++;
}

static VkPipeline pipe_build(VkShaderModule vs, VkShaderModule fs, VkPipelineLayout layout, int gx_vertices,
                             VkPrimitiveTopology topo, const VkPipelineColorBlendAttachmentState* blend,
                             const VkPipelineDepthStencilStateCreateInfo* ds, VkCullModeFlags cull, int target) {
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", NULL}};
    VkVertexInputBindingDescription vb = {0, sizeof(GxVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    static const uint32_t offs[10] = {
        offsetof(GxVertex, pos), offsetof(GxVertex, nrm), offsetof(GxVertex, col[0]), offsetof(GxVertex, col[1]),
        offsetof(GxVertex, tex[0]), offsetof(GxVertex, tex[2]), offsetof(GxVertex, tex[4]), offsetof(GxVertex, tex[6]),
        offsetof(GxVertex, tmtx[0]), offsetof(GxVertex, tmtx[4])};
    VkVertexInputAttributeDescription va[10];
    for (int i = 0; i < 10; i++) va[i] = (VkVertexInputAttributeDescription){(uint32_t)i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offs[i]};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, NULL, 0,
                                               gx_vertices ? 1u : 0u, &vb, gx_vertices ? 10u : 0u, va};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, topo, VK_FALSE};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cull;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;  /* GX front faces are clockwise on screen (as render_deko.c) */
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, blend};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
    VkFormat cf = target == TGT_SWAP ? g_swap_fmt : VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfoKHR ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR, NULL, 0, 1, &cf,
                                           target == TGT_EFB ? g_depth_fmt : VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED};
    VkPipelineDepthStencilStateCreateInfo no_ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkGraphicsPipelineCreateInfo gi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri, 0, 2, st, &vi, &ia, NULL, &vp, &rs, &ms,
                                       target == TGT_EFB ? (ds ? ds : &no_ds) : NULL, &cb, &dy, layout, VK_NULL_HANDLE, 0,
                                       VK_NULL_HANDLE, -1};
    VkPipeline p;
    uint64_t t0 = platform_now_ns();
    VKC(vkCreateGraphicsPipelines(g_dev, g_pcache, 1, &gi, NULL, &p));  /* the cache is internally synchronized */
    __atomic_fetch_add(&g_pipes_new, 1, __ATOMIC_RELAXED);
    uint64_t dt = platform_now_ns() - t0;
    if (dt > 20000000ull) rt_log("[vk] a pipeline took %.0f ms to make", dt / 1e6);
    return p;
}

static VkBlendFactor src1_ok(VkBlendFactor f) {  /* without dual-source blending: the nearest single-source factor */
    if (g_dual_src) return f;
    if (f == VK_BLEND_FACTOR_SRC1_ALPHA) return VK_BLEND_FACTOR_SRC_ALPHA;
    if (f == VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA) return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    return f;
}

/* the pipeline for a GX state: key bits (see gx_pipe) */
static VkPipeline gx_pipe_make(uint64_t key) {
    int ind = key & 1, blend_on = (key >> 1) & 1, revsub = (key >> 2) & 1;
    VkPipelineColorBlendAttachmentState b = {0};
    b.blendEnable = blend_on;
    b.srcColorBlendFactor = src1_ok((VkBlendFactor)((key >> 3) & 31));
    b.dstColorBlendFactor = src1_ok((VkBlendFactor)((key >> 8) & 31));
    b.srcAlphaBlendFactor = src1_ok((VkBlendFactor)((key >> 13) & 31));
    b.dstAlphaBlendFactor = src1_ok((VkBlendFactor)((key >> 18) & 31));
    b.colorBlendOp = b.alphaBlendOp = revsub ? VK_BLEND_OP_REVERSE_SUBTRACT : VK_BLEND_OP_ADD;
    b.colorWriteMask = (VkColorComponentFlags)((key >> 23) & 15);
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = (key >> 27) & 1;
    ds.depthWriteEnable = (key >> 28) & 1;
    ds.depthCompareOp = (VkCompareOp)((key >> 29) & 7);
    VkCullModeFlags cull = (VkCullModeFlags)((key >> 32) & 3);
    VkPrimitiveTopology topo = (VkPrimitiveTopology)((key >> 34) & 3);
    VkShaderModule fs = ind ? g_sm_fs_ind : g_sm_fs;
    if (key & SPEC_FLAG) {  /* a key from an older table that no longer matches keeps the ubershader */
        uint32_t tag = (uint32_t)(key >> SPEC_SHIFT) & 0xFFFFFFu;
        for (int i = 0; i < GX_SPEC_COUNT; i++)
            if ((uint32_t)(k_spec_keys[i] & 0xFFFFFFu) == tag && g_spec_sm[i]) { fs = g_spec_sm[i]; break; }
    }
    return pipe_build(g_sm_vs, fs, g_pl_gx, 1, topo, &b, &ds, cull, TGT_EFB);
}

/* a screen quad's pipeline: kind (Q_*), target */
static VkPipeline quad_pipe_make(uint64_t key) {
    int kind = (int)(key & 15), target = (int)((key >> 4) & 15);
    VkColorComponentFlags mask = (VkColorComponentFlags)((key >> 8) & 15);
    VkPipelineColorBlendAttachmentState b = {0};
    b.colorWriteMask = mask;
    if (kind == Q_TEX_OVER || kind == Q_TOUCH) {  /* premultiplied alpha over */
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        b.colorBlendOp = b.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkShaderModule fs = kind == Q_FILL ? g_sm_fill : kind == Q_TOUCH ? g_sm_touch : g_sm_tex;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    return pipe_build(g_sm_qvs, fs, g_pl_quad, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, &b, &ds, VK_CULL_MODE_NONE, target);
}

/* ---- pipeline prewarm: the keys of every pipeline made in earlier runs (PIPE_KEYS_FILE) are built on a
 * helper thread at boot, so a state's first draw does not wait 50-250 ms for the driver's compiler. The
 * finished pipelines are handed to the render thread (pipe_drain); a key it needs before the helper got
 * there is built by the render thread itself, and the helper's copy is dropped. */
static mtx_t g_pw_lock;
static PipeEntry* g_pw_done;
static uint32_t g_pw_ndone, g_pw_cap;
static volatile int g_pw_running;

static VkPipeline pipe_make(uint64_t key) { return (key >> 63) ? quad_pipe_make(key) : gx_pipe_make(key); }

static void key_note(uint64_t key) {  /* a key first built this run: saved with the pipeline cache */
    if (g_nkeys == g_keys_cap) {
        g_keys_cap = g_keys_cap ? g_keys_cap * 2 : 256;
        g_keys = realloc(g_keys, g_keys_cap * sizeof *g_keys);
    }
    g_keys[g_nkeys++] = key;
}

static void keys_save(void) {
    if (g_nkeys == g_keys_saved) return;
    FILE* f = fopen(PIPE_KEYS_FILE, "wb");
    if (!f) return;
    uint32_t hd[2] = {PIPE_KEYS_MAGIC, g_nkeys};
    fwrite(hd, sizeof hd, 1, f);
    fwrite(g_keys, sizeof *g_keys, g_nkeys, f);
    fclose(f);
    g_keys_saved = g_nkeys;
}

static void pipe_drain(void) {  /* render thread: take the helper's finished pipelines */
    if (!__atomic_load_n(&g_pw_ndone, __ATOMIC_ACQUIRE)) return;
    mtx_lock(&g_pw_lock);
    for (uint32_t i = 0; i < g_pw_ndone; i++) {
        if (pipe_find(g_pw_done[i].key)) vkDestroyPipeline(g_dev, g_pw_done[i].p, NULL);
        else pipe_put(g_pw_done[i].key, g_pw_done[i].p);
    }
    g_pw_ndone = 0;
    mtx_unlock(&g_pw_lock);
}

static uint64_t* g_pw_keys;  /* the helper's own copy of the recorded keys */
static uint32_t g_pw_nkeys;

static int prewarm_thread(void* arg) {
    (void)arg;
    rt_thread_lower_priority();
    uint64_t t0 = platform_now_ns();
    uint32_t n = 0;
    for (uint32_t i = 0; i < g_pw_nkeys; i++) {
        uint64_t key = g_pw_keys[i];
        if ((key >> 63) && ((key >> 4) & 15) == TGT_SWAP && g_swap_fmt == VK_FORMAT_UNDEFINED) continue;
        VkPipeline p = pipe_make(key);
        mtx_lock(&g_pw_lock);
        if (g_pw_ndone == g_pw_cap) {
            g_pw_cap = g_pw_cap ? g_pw_cap * 2 : 64;
            g_pw_done = realloc(g_pw_done, g_pw_cap * sizeof *g_pw_done);
        }
        g_pw_done[g_pw_ndone] = (PipeEntry){key, p};
        __atomic_store_n(&g_pw_ndone, g_pw_ndone + 1, __ATOMIC_RELEASE);
        mtx_unlock(&g_pw_lock);
        n++;
    }
    rt_log("[vk] prewarm: %u of %u recorded pipelines built in %.0f ms", n, g_pw_nkeys, (platform_now_ns() - t0) / 1e6);
    free(g_pw_keys);
    g_pw_running = 0;
    return 0;
}

static void prewarm_start(void) {
    FILE* f = fopen(PIPE_KEYS_FILE, "rb");
    uint32_t hd[2] = {0, 0};
    if (f && fread(hd, sizeof hd, 1, f) == 1 && hd[0] == PIPE_KEYS_MAGIC && hd[1] < (1u << 20)) {
        g_keys_cap = hd[1] + 256;
        g_keys = malloc(g_keys_cap * sizeof *g_keys);
        g_nkeys = (uint32_t)fread(g_keys, sizeof *g_keys, hd[1], f);
    }
    if (f) fclose(f);
    g_keys_saved = g_nkeys;
    if (!g_nkeys) {
        rt_log("[vk] prewarm: no recorded pipelines yet");
        return;
    }
    g_pw_nkeys = g_nkeys;
    g_pw_keys = malloc(g_nkeys * sizeof *g_pw_keys);
    memcpy(g_pw_keys, g_keys, g_nkeys * sizeof *g_pw_keys);
    g_pw_running = 1;
    rt_thread_start(prewarm_thread, NULL);
}

/* the pipeline for a key: from the table, else made now (render thread) */
static VkPipeline pipe_get(uint64_t key) {
    VkPipeline p = pipe_find(key);
    if (p) return p;
    pipe_drain();
    if ((p = pipe_find(key))) return p;
    uint64_t t0 = platform_now_ns();
    p = pipe_make(key);
    g_fr_pipe_ns += platform_now_ns() - t0;
    g_fr_pipe_n++;
    pipe_put(key, p);
    key_note(key);
    return p;
}
static VkPipeline gx_pipe(uint64_t key) { return pipe_get(key); }
static VkPipeline quad_pipe(int kind, int target, VkColorComponentFlags mask) {
    return pipe_get((1ull << 63) | (uint64_t)kind | (uint64_t)target << 4 | (uint64_t)mask << 8);
}

static void pcache_save(int force) {
    uint64_t now = platform_now_ns();
    if (!force && now - g_pcache_saved_ns < 10000000000ull) return;
    keys_save();
    if (!g_pipes_new) return;
    size_t n = 0;
    if (vkGetPipelineCacheData(g_dev, g_pcache, &n, NULL) != VK_SUCCESS || !n) return;
    void* d = malloc(n);
    if (vkGetPipelineCacheData(g_dev, g_pcache, &n, d) == VK_SUCCESS) {
        FILE* f = fopen(PIPE_CACHE_FILE, "wb");
        if (f) {
            fwrite(d, 1, n, f);
            fclose(f);
        }
        rt_log("[vk] pipeline cache saved: %u pipelines, %zu KB (%u new)", g_pipes_total, n >> 10, g_pipes_new);
    }
    free(d);
    g_pipes_new = 0;
    g_pcache_saved_ns = now;
}

/* ---------------------------------------------------------------- setup */
static void swap_destroy(void) {
    if (!g_swap) return;
    vkDeviceWaitIdle(g_dev);
    for (uint32_t i = 0; i < g_nimg; i++) {
        vkDestroyImageView(g_dev, g_sview[i], NULL);
        vkDestroySemaphore(g_dev, g_sdone[i], NULL);
    }
    vkDestroySwapchainKHR(g_dev, g_swap, NULL);
    g_swap = VK_NULL_HANDLE;
    g_nimg = 0;
}

static int swap_create(void) {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_pd, g_surf, &caps) != VK_SUCCESS) return 0;
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_pd, g_surf, &nf, NULL);
    VkSurfaceFormatKHR fmts[32];
    if (nf > 32) nf = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_pd, g_surf, &nf, fmts);
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < nf; i++)
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM || fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
            fmt = fmts[i];
            break;
        }
    if (g_swap_fmt != VK_FORMAT_UNDEFINED && fmt.format != g_swap_fmt)
        rt_log("[vk] the screen's format changed (%d -> %d)", g_swap_fmt, fmt.format);  /* pipelines are per format */
    g_swap_fmt = fmt.format;
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX || !ext.width) {
        int w, h;
        SDL_Vulkan_GetDrawableSize(g_win, &w, &h);
        ext.width = (uint32_t)w;
        ext.height = (uint32_t)h;
    }
    if (!ext.width || !ext.height) return 0;
    /* identity: the system rotates the picture to the panel (pre-rotation is a later optimisation) */
    VkSurfaceTransformFlagBitsKHR tf = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                                           ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    /* SR_VK_PRETRANSFORM=1: draw already rotated to the panel, so the compositor need not rotate the picture */
    const char* pt = getenv("SR_VK_PRETRANSFORM");
    VkExtent2D iext = ext;
    g_rot[0] = 1; g_rot[1] = 0; g_rot[2] = 0; g_rot[3] = 1;
    if (pt && atoi(pt) && (caps.supportedTransforms & caps.currentTransform)) {
        tf = caps.currentTransform;
        if (tf == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR || tf == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) {
            iext.width = ext.height;
            iext.height = ext.width;
        }
        /* (x, y) -> (r0 x + r2 y, r1 x + r3 y): 90 = (-y, x), 180 = (-x, -y), 270 = (y, -x) */
        if (tf == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) { g_rot[0] = 0; g_rot[1] = 1; g_rot[2] = -1; g_rot[3] = 0; }
        if (tf == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) { g_rot[0] = -1; g_rot[3] = -1; }
        if (tf == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR) { g_rot[0] = 0; g_rot[1] = -1; g_rot[2] = 1; g_rot[3] = 0; }
    }
    VkCompositeAlphaFlagBitsKHR ca = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                         ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    uint32_t n = caps.minImageCount + 1;
    if (caps.maxImageCount && n > caps.maxImageCount) n = caps.maxImageCount;
    if (n > 8) n = 8;
    const char* si_env = getenv("SR_VK_SWAPIMAGES");  /* test: swapchain length */
    if (si_env && atoi(si_env) >= (int)caps.minImageCount && atoi(si_env) <= 8) n = (uint32_t)atoi(si_env);
    g_swap_can_read = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, g_surf, n, fmt.format, fmt.colorSpace, iext, 1,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (g_swap_can_read ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
                                   VK_SHARING_MODE_EXCLUSIVE, 0, NULL, tf, ca, VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, VK_NULL_HANDLE};
    if (vkCreateSwapchainKHR(g_dev, &ci, NULL, &g_swap) != VK_SUCCESS) return 0;
    g_ext = ext;
    g_img_ext = iext;
    g_nimg = 8;
    vkGetSwapchainImagesKHR(g_dev, g_swap, &g_nimg, g_simg);
    for (uint32_t i = 0; i < g_nimg; i++) {
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, g_simg[i], VK_IMAGE_VIEW_TYPE_2D, fmt.format,
                                    {0, 0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VKC(vkCreateImageView(g_dev, &vi, NULL, &g_sview[i]));
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKC(vkCreateSemaphore(g_dev, &si, NULL, &g_sdone[i]));
        g_slayout[i] = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    rt_log("[vk] screen %ux%u (images %ux%u), format %d, %u images, transform %X (current %X)", ext.width, ext.height, iext.width, iext.height, fmt.format, g_nimg, tf,
           caps.currentTransform);
    g_swap_bad = 0;
    return 1;
}

/* the surface is gone (background) or out of date: make it again */
static int swap_recreate(void) {
    swap_destroy();
    if (!swap_create()) {
        if (g_surf) vkDestroySurfaceKHR(g_inst, g_surf, NULL);
        g_surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(g_win, g_inst, &g_surf)) {
            g_surf = VK_NULL_HANDLE;
            return 0;
        }
        return swap_create();
    }
    return 1;
}

static void device_setup(void);
#ifdef __APPLE__
/* MoltenVK (iOS): a "portability" implementation; the spec requires enabling VK_KHR_portability_subset
 * when the device lists it */
static int g_portability;
#endif

static void vk_setup(void) {
    unsigned n = 0;
    SDL_Vulkan_GetInstanceExtensions(g_win, &n, NULL);
    const char* ie[16];
    if (n > 16) n = 16;
    SDL_Vulkan_GetInstanceExtensions(g_win, &n, ie);
    uint32_t iv = VK_API_VERSION_1_1;
    vkEnumerateInstanceVersion(&iv);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "sonicriders", 1, "sonicriders", 1,
                             iv >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, n, ie};
    VKC(vkCreateInstance(&ici, NULL, &g_inst));
    uint32_t npd = 1;
    VkResult er = vkEnumeratePhysicalDevices(g_inst, &npd, &g_pd);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || !npd) rt_fatal("[vk] no GPU");
    vkGetPhysicalDeviceProperties(g_pd, &g_props);
    vkGetPhysicalDeviceMemoryProperties(g_pd, &g_mem);
    VkPhysicalDeviceFeatures f;
    vkGetPhysicalDeviceFeatures(g_pd, &f);
    g_dual_src = f.dualSrcBlend;
    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(g_pd, NULL, &nde, NULL);
    VkExtensionProperties* de = calloc(nde ? nde : 1, sizeof *de);
    vkEnumerateDeviceExtensionProperties(g_pd, NULL, &nde, de);
    int has_dr = 0;
    for (uint32_t i = 0; i < nde; i++) {
        if (!strcmp(de[i].extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) g_push_desc = 1;
        if (!strcmp(de[i].extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) has_dr = 1;
#ifdef __APPLE__
        if (!strcmp(de[i].extensionName, "VK_KHR_portability_subset")) g_portability = 1;
#endif
    }
    free(de);
    int core13 = g_core13 = g_props.apiVersion >= VK_API_VERSION_1_3 && iv >= VK_API_VERSION_1_3;
    if (!core13 && !has_dr) rt_fatal("[vk] this GPU's driver has no dynamic rendering (Vulkan 1.3 or VK_KHR_dynamic_rendering)");
    if (getenv("SR_VK_NOPUSH")) g_push_desc = 0;
    g_one_pipe = getenv("SR_VK_ONEPIPE") != NULL;
    if (getenv("SR_VK_DRAWS")) {
        sscanf(getenv("SR_VK_DRAWS"), "%u-%u", &g_draw_lo, &g_draw_hi);
        rt_log("[vk] SR_VK_DRAWS: only draws %u-%u of each frame", g_draw_lo, g_draw_hi);
    }
    if (!SDL_Vulkan_CreateSurface(g_win, g_inst, &g_surf)) rt_fatal("[vk] surface: %s", SDL_GetError());
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &nq, NULL);
    VkQueueFamilyProperties qf[16];
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &nq, qf);
    g_qfi = UINT32_MAX;
    for (uint32_t i = 0; i < nq && g_qfi == UINT32_MAX; i++) {
        VkBool32 pres = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_pd, i, g_surf, &pres);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) g_qfi = i;
    }
    if (g_qfi == UINT32_MAX) rt_fatal("[vk] no graphics queue that can present");
    mtx_init(&g_pw_lock, mtx_plain);
    device_setup();
    prewarm_start();
}

/* everything that belongs to the VkDevice: made at start-up and again after a device loss (device_recover) */
static void device_setup(void) {
    int core13 = g_core13;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, g_qfi, 1, &prio};
    const char* dext[5];
    uint32_t ndx = 0;
    dext[ndx++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
#ifdef __APPLE__
    if (g_portability) dext[ndx++] = "VK_KHR_portability_subset";
#endif
    if (g_push_desc) dext[ndx++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;
    if (!core13) {
        dext[ndx++] = VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME;
        dext[ndx++] = VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME;  /* required by it on 1.1 (with create_renderpass2) */
    }
    VkPhysicalDeviceDynamicRenderingFeaturesKHR drf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR, NULL, VK_TRUE};
    VkPhysicalDeviceFeatures want = {0};
    want.dualSrcBlend = g_dual_src;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &drf, 0, 1, &qci, 0, NULL, ndx, dext, &want};
    VKC(vkCreateDevice(g_pd, &dci, NULL, &g_dev));
    vkGetDeviceQueue(g_dev, g_qfi, 0, &g_q);
    p_begin_rendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(g_dev, core13 ? "vkCmdBeginRendering" : "vkCmdBeginRenderingKHR");
    p_end_rendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(g_dev, core13 ? "vkCmdEndRendering" : "vkCmdEndRenderingKHR");
    if (g_push_desc) p_push_desc = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(g_dev, "vkCmdPushDescriptorSetKHR");
    if (!p_begin_rendering || !p_end_rendering) rt_fatal("[vk] dynamic rendering entry points missing");
    if (g_push_desc && !p_push_desc) g_push_desc = 0;
    rt_log("[vk] %s, Vulkan %u.%u.%u, driver %08X; dual-source blending %s, push descriptors %s", g_props.deviceName,
           VK_VERSION_MAJOR(g_props.apiVersion), VK_VERSION_MINOR(g_props.apiVersion), VK_VERSION_PATCH(g_props.apiVersion),
           g_props.driverVersion, g_dual_src ? "yes" : "NO (approximated)", g_push_desc ? "yes" : "no (descriptor pool)");

    static const VkFormat depths[3] = {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D32_SFLOAT};
    for (int i = 0; i < 3 && g_depth_fmt == VK_FORMAT_UNDEFINED; i++) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_pd, depths[i], &fp);
        if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) g_depth_fmt = depths[i];
    }

    /* shaders */
    g_sm_vs = shader(k_gx_vert_spv, sizeof k_gx_vert_spv);
    g_sm_fs = shader(k_gx_frag_spv, sizeof k_gx_frag_spv);
    g_sm_fs_ind = shader(k_gx_frag_ind_spv, sizeof k_gx_frag_ind_spv);
    g_sm_qvs = shader(k_vk_quad_vert_spv, sizeof k_vk_quad_vert_spv);
    g_sm_tex = shader(k_vk_tex_frag_spv, sizeof k_vk_tex_frag_spv);
    g_sm_fill = shader(k_vk_fill_frag_spv, sizeof k_vk_fill_frag_spv);
    g_sm_touch = shader(k_vk_touch_frag_spv, sizeof k_vk_touch_frag_spv);
    if (getenv("SR_NOSPEC") && atoi(getenv("SR_NOSPEC"))) g_spec_on = 0;
    for (int i = 0; i < GX_SPEC_COUNT; i++) {
        int dup = 0;  /* two keys with the same low 24 bits: the second is left out */
        for (int j = 0; j < i; j++) dup |= (k_spec_keys[j] & 0xFFFFFFu) == (k_spec_keys[i] & 0xFFFFFFu);
        if (!dup) g_spec_sm[i] = shader(k_spec_spv + k_spec_off[i], (size_t)k_spec_words[i] * 4);
    }
    rt_log("[vk] %d specialized fragment shaders%s", GX_SPEC_COUNT, g_spec_on ? "" : " (off: SR_NOSPEC)");

    /* descriptor layouts */
    VkDescriptorSetLayoutBinding ub[2] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    VkDescriptorSetLayoutCreateInfo uli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 2, ub};
    VKC(vkCreateDescriptorSetLayout(g_dev, &uli, NULL, &g_dsl_ubo));
    VkDescriptorSetLayoutBinding tb[8];
    for (int i = 0; i < 8; i++)
        tb[i] = (VkDescriptorSetLayoutBinding){(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateFlags pf = g_push_desc ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;
    VkDescriptorSetLayoutCreateInfo tli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, pf, 8, tb};
    VKC(vkCreateDescriptorSetLayout(g_dev, &tli, NULL, &g_dsl_tex));
    VkDescriptorSetLayoutCreateInfo oli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, pf, 1, tb};
    VKC(vkCreateDescriptorSetLayout(g_dev, &oli, NULL, &g_dsl_one));
    VkDescriptorSetLayout gl[2] = {g_dsl_ubo, g_dsl_tex};
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 2, gl, 0, NULL};
    VKC(vkCreatePipelineLayout(g_dev, &pli, NULL, &g_pl_gx));
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80};
    VkPipelineLayoutCreateInfo qli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &g_dsl_one, 1, &pcr};
    VKC(vkCreatePipelineLayout(g_dev, &qli, NULL, &g_pl_quad));

    /* pipeline cache from the last run */
    {
        void* data = NULL;
        size_t len = 0;
        FILE* fc = fopen(PIPE_CACHE_FILE, "rb");
        if (fc) {
            fseek(fc, 0, SEEK_END);
            len = (size_t)ftell(fc);
            fseek(fc, 0, SEEK_SET);
            data = malloc(len ? len : 1);
            if (fread(data, 1, len, fc) != len) len = 0;
            fclose(fc);
        }
        VkPipelineCacheCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, NULL, 0, len, data};
        if (vkCreatePipelineCache(g_dev, &ci, NULL, &g_pcache) != VK_SUCCESS) {
            ci.initialDataSize = 0;
            ci.pInitialData = NULL;
            VKC(vkCreatePipelineCache(g_dev, &ci, NULL, &g_pcache));
            len = 0;
        }
        rt_log("[vk] pipeline cache: %zu KB from the last run", len >> 10);
        free(data);
        g_pcache_saved_ns = platform_now_ns();
    }

    /* samplers: wrap S x wrap T x mag x min (as render_deko.c), and two for the screen quads */
    static const VkSamplerAddressMode wrap[3] = {VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_REPEAT,
                                                 VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    for (int ws = 0; ws < 3; ws++)
        for (int wt = 0; wt < 3; wt++)
            for (int mag = 0; mag < 2; mag++)
                for (int mn = 0; mn < 2; mn++) {
                    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
                    si.magFilter = mag ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                    si.minFilter = mn ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                    si.addressModeU = wrap[ws];
                    si.addressModeV = wrap[wt];
                    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    si.maxLod = 0.25f;
                    VKC(vkCreateSampler(g_dev, &si, NULL, &g_samp[ws * 12 + wt * 4 + mag * 2 + mn]));
                }
    g_samp_lin = g_samp[0 * 12 + 0 * 4 + 1 * 2 + 1];
    g_samp_near = g_samp[0];

    /* frame slots: host-visible memory (unified on phones) */
    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, SLOT_SIZE,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_SHARING_MODE_EXCLUSIVE};
        VKC(vkCreateBuffer(g_dev, &bi, NULL, &g_sbuf[i]));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(g_dev, g_sbuf[i], &mr);
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                   mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)};
        VKC(vkAllocateMemory(g_dev, &ai, NULL, &g_smem[i]));
        VKC(vkBindBufferMemory(g_dev, g_sbuf[i], g_smem[i], 0));
        VKC(vkMapMemory(g_dev, g_smem[i], 0, SLOT_SIZE, 0, (void**)&g_smap[i]));
        VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VKC(vkCreateFence(g_dev, &fi, NULL, &g_fence[i]));
        VkSemaphoreCreateInfo si = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKC(vkCreateSemaphore(g_dev, &si, NULL, &g_acq[i]));
    }
    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, g_qfi};
    VKC(vkCreateCommandPool(g_dev, &cpi, NULL, &g_cpool));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, g_cpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
    VKC(vkAllocateCommandBuffers(g_dev, &cai, g_cb));

    /* descriptor sets: the uniform set per slot; without push descriptors, a pool per slot for textures */
    VkDescriptorPoolSize dps = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 4};
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 2, 1, &dps};
    VKC(vkCreateDescriptorPool(g_dev, &dpi, NULL, &g_dpool_static));
    VkDescriptorSetLayout two[2] = {g_dsl_ubo, g_dsl_ubo};
    VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_dpool_static, 2, two};
    VKC(vkAllocateDescriptorSets(g_dev, &dai, g_ubo_set));
    for (int i = 0; i < 2; i++) {
        VkDescriptorBufferInfo b0 = {g_sbuf[i], 0, (sizeof(GxUniforms) + 15) & ~15u};
        VkDescriptorBufferInfo b1 = {g_sbuf[i], 0, XF_IMAGE_BYTES};
        VkWriteDescriptorSet w[2] = {
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, g_ubo_set[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &b0, NULL},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, g_ubo_set[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &b1, NULL}};
        vkUpdateDescriptorSets(g_dev, 2, w, 0, NULL);
        if (!g_push_desc) {
            VkDescriptorPoolSize ts = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 * 16384};
            VkDescriptorPoolCreateInfo tpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 16384, 1, &ts};
            VKC(vkCreateDescriptorPool(g_dev, &tpi, NULL, &g_dpool_frame[i]));
        }
    }

    /* the image pool, then the EFB, its depth, the overlay image and the white dummy texture */
    {
        VkImg probe;
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, NULL, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {64, 64, 1}, 1, 1,
                                VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_IMAGE_LAYOUT_UNDEFINED};
        VKC(vkCreateImage(g_dev, &ci, NULL, &probe.img));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(g_dev, probe.img, &mr);
        vkDestroyImage(g_dev, probe.img, NULL);
        g_pool_type = mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, IMAGE_POOL, g_pool_type};
        VKC(vkAllocateMemory(g_dev, &ai, NULL, &g_pool_mem));
        while (g_free_list) {  /* after a device loss: the old pool's free list */
            Block* b = g_free_list;
            g_free_list = b->next;
            free(b);
        }
        g_free_list = malloc(sizeof *g_free_list);
        g_free_list->off = 0; g_free_list->size = IMAGE_POOL; g_free_list->next = NULL;
    }
    image_make(&g_efb, EFB_W, EFB_H, VK_FORMAT_R8G8B8A8_UNORM,
               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
    image_make(&g_depth, EFB_W, EFB_H, g_depth_fmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0);
    image_make(&g_ov, OVERLAY_W, OVERLAY_H, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
    image_make(&g_dummy, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);
    image_make(&g_icons, TOUCH_ATLAS_W, TOUCH_ATLAS_H, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0);

    /* first commands: the dummy white, a transparent overlay, the EFB black with depth at the far plane */
    g_cmd = g_cb[0];
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VKC(vkBeginCommandBuffer(g_cmd, &bi));
    memset(g_smap[0], 0xFF, 4);
    to_layout(&g_dummy, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy r = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {1, 1, 1}};
    vkCmdCopyBufferToImage(g_cmd, g_sbuf[0], g_dummy.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
    to_layout(&g_dummy, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    /* the touch controls' pictures, once */
    memcpy(g_smap[0] + OFF_STAGE, k_touch_atlas, sizeof k_touch_atlas);
    to_layout(&g_icons, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy ir = {OFF_STAGE, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {TOUCH_ATLAS_W, TOUCH_ATLAS_H, 1}};
    vkCmdCopyBufferToImage(g_cmd, g_sbuf[0], g_icons.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ir);
    to_layout(&g_icons, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkImageSubresourceRange cr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    to_layout(&g_ov, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue clear0 = {{0, 0, 0, 0}};
    vkCmdClearColorImage(g_cmd, g_ov.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear0, 1, &cr);
    to_layout(&g_ov, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    to_layout(&g_efb, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue black = {{0, 0, 0, 1}};
    vkCmdClearColorImage(g_cmd, g_efb.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &cr);
    to_layout(&g_efb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    to_layout(&g_depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearDepthStencilValue far = {1.0f, 0};
    VkImageSubresourceRange dr = {VK_IMAGE_ASPECT_DEPTH_BIT | (g_depth_fmt == VK_FORMAT_D32_SFLOAT ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT), 0, 1, 0, 1};
    vkCmdClearDepthStencilImage(g_cmd, g_depth.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &far, 1, &dr);
    to_layout(&g_depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VKC(vkEndCommandBuffer(g_cmd));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 0, NULL, NULL, 1, &g_cmd, 0, NULL};
    VKC(vkQueueSubmit(g_q, 1, &si, VK_NULL_HANDLE));
    VKC(vkQueueWaitIdle(g_q));
    if (!swap_create()) rt_log("[vk] no screen yet");
    if (g_props.limits.timestampComputeAndGraphics) {
        VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, NULL, 0, VK_QUERY_TYPE_TIMESTAMP, 4, 0};
        if (vkCreateQueryPool(g_dev, &qi, NULL, &g_qpool) != VK_SUCCESS) g_qpool = VK_NULL_HANDLE;
    }
    rt_log("[vk] ready: EFB %dx%d, depth format %d, image pool %u MiB", EFB_W, EFB_H, g_depth_fmt, IMAGE_POOL >> 20);
}

/* ---------------------------------------------------------------- recording state */
static int g_in_efb;       /* inside rendering into the EFB */
static VkPipeline g_bound;  /* the pipeline bound in this rendering */
static uint64_t g_cur_key;
static VkViewport g_cur_vp;
static VkRect2D g_cur_sc;
static int g_dyn_valid;
static VkImageView g_cur_tex[8];
static VkSampler g_cur_samp[8];
static int g_tex_valid;
static int g_swap_idx = -1;  /* the screen image acquired this frame */

static void efb_begin(void) {
    if (g_in_efb) return;
    to_layout(&g_efb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    to_layout(&g_depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    VkRenderingAttachmentInfoKHR ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR, NULL, g_efb.view,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE,
                                       VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0}}}};
    VkRenderingAttachmentInfoKHR da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR, NULL, g_depth.view,
                                       VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE,
                                       VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {{{0}}}};
    /* only the part the internal resolution uses: a tiled GPU loads and stores the whole render area each
     * time rendering starts again (after every EFB copy) */
    uint32_t aw = XSC(EFB_WIDTH), ah = SC(EFB_HEIGHT);
    static int full = -1;  /* SR_VK_FULLAREA=1: the whole image (A/B test) */
    if (full < 0) full = getenv("SR_VK_FULLAREA") != NULL;
    if (full) aw = EFB_W, ah = EFB_H;
    VkRenderingInfoKHR ri = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, NULL, 0, {{0, 0}, {aw < EFB_W ? aw : EFB_W, ah < EFB_H ? ah : EFB_H}},
                             1, 0, 1, &ca, &da, NULL};
    p_begin_rendering(g_cmd, &ri);
    g_passes++;
    g_in_efb = 1;
    g_bound = VK_NULL_HANDLE;
    g_dyn_valid = 0;
}

static void efb_end(void) {
    if (!g_in_efb) return;
    p_end_rendering(g_cmd);
    g_in_efb = 0;
}

/* rendering into a colour image of its own (a copy texture, the screen) */
static void target_begin(VkImageView v, uint32_t w, uint32_t h, int clear) {
    VkRenderingAttachmentInfoKHR ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR, NULL, v, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED,
                                       clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
                                       {{{0, 0, 0, 1}}}};
    VkRenderingInfoKHR ri = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, NULL, 0, {{0, 0}, {w, h}}, 1, 0, 1, &ca, NULL, NULL};
    p_begin_rendering(g_cmd, &ri);
}

/* one screen-space quad (vk_quad.vert); rect in pixels of a w x h target */
static void quad(int kind, int target, VkColorComponentFlags mask, VkImageView tex, VkSampler samp, float w, float h,
                 const float px[4], const float uv[4], const float color[4], const float shape[4]) {
    vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, quad_pipe(kind, target, mask));
    g_bound = VK_NULL_HANDLE;
    int rot = target == TGT_SWAP;  /* the screen: pre-rotated images are g_img_ext */
    float vw = rot ? (float)g_img_ext.width : w, vh = rot ? (float)g_img_ext.height : h;
    VkViewport vp = {0, 0, vw, vh, 0, 1};
    VkRect2D sc = {{0, 0}, {(uint32_t)vw, (uint32_t)vh}};
    vkCmdSetViewport(g_cmd, 0, 1, &vp);
    vkCmdSetScissor(g_cmd, 0, 1, &sc);
    g_dyn_valid = 0;
    VkImageView v = tex ? tex : g_dummy.view;
    VkDescriptorImageInfo ii = {samp ? samp : g_samp_near, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (g_push_desc) {
        VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 0, 0, 1,
                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL, NULL};
        p_push_desc(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl_quad, 0, 1, &wd);
    } else {
        VkDescriptorSet s;
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_dpool_frame[g_slot], 1, &g_dsl_one};
        if (vkAllocateDescriptorSets(g_dev, &ai, &s) != VK_SUCCESS) return;
        VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, s, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &ii, NULL, NULL};
        vkUpdateDescriptorSets(g_dev, 1, &wd, 0, NULL);
        vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl_quad, 0, 1, &s, 0, NULL);
    }
    float pc[20] = {px[0] / w * 2 - 1, px[1] / h * 2 - 1, px[2] / w * 2 - 1, px[3] / h * 2 - 1};
    static const float uv01[4] = {0, 0, 1, 1}, white[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 0};
    memcpy(pc + 4, uv ? uv : uv01, 16);
    memcpy(pc + 8, color ? color : white, 16);
    memcpy(pc + 12, shape ? shape : zero, 16);
    static const float ident[4] = {1, 0, 0, 1};
    memcpy(pc + 16, rot ? g_rot : ident, 16);
    vkCmdPushConstants(g_cmd, g_pl_quad, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 80, pc);
    vkCmdDraw(g_cmd, 4, 1, 0, 0);
}

/* ---------------------------------------------------------------- per-draw state */
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* the specialized shader's pipeline-key bits for a state (0: the ubershader) */
static uint64_t spec_bits(const GxUniforms* u) {
    if (!g_spec_on || !GX_SPEC_COUNT) return 0;
    GxFragKey k;
    gx_frag_key(u->bp, &k);
    int lo = 0, hi = GX_SPEC_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (k_spec_keys[mid] == k.hash) {
            if (!g_spec_sm[mid]) break;
            g_spec_hits++;
            return SPEC_FLAG | (k.hash & 0xFFFFFFull) << SPEC_SHIFT;
        }
        if (k_spec_keys[mid] < k.hash) lo = mid + 1; else hi = mid - 1;
    }
    g_spec_misses++;
    gx_frag_key_record(&k, "shader_keys.txt");
    return 0;
}

static uint64_t state_key(const GxUniforms* u, int prim) {
    uint32_t cm = u->bp[0x41];
    int efb_alpha = (u->bp[0x43] & 7) == 1;  /* RGBA6_Z24: destination alpha exists */
    VkBlendFactor da = efb_alpha ? VK_BLEND_FACTOR_DST_ALPHA : VK_BLEND_FACTOR_ONE;
    VkBlendFactor ida = efb_alpha ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA : VK_BLEND_FACTOR_ZERO;
    const VkBlendFactor sf[8] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_DST_COLOR, VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                 VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA, da, ida};
    const VkBlendFactor df[8] = {VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR, VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                 VK_BLEND_FACTOR_SRC1_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA, da, ida};
    int const_alpha = (u->bp[0x42] >> 8) & 1;
    uint64_t ind = ((u->bp[0x00] >> 16) & 7) != 0;
    uint64_t blend = cm & 1, revsub = 0, s, d, sa, dA;
    if ((cm >> 11) & 1) {
        revsub = 1;
        s = d = sa = dA = VK_BLEND_FACTOR_ONE;
    } else {
        s = sf[(cm >> 8) & 7];
        d = df[(cm >> 5) & 7];
        sa = const_alpha ? VK_BLEND_FACTOR_ONE : s;
        dA = const_alpha ? VK_BLEND_FACTOR_ZERO : d;
    }
    uint64_t mask = ((cm >> 3) & 1 ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT) : 0) |
                    ((cm >> 4) & 1 ? VK_COLOR_COMPONENT_A_BIT : 0);
    static const VkCompareOp zf[8] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL,
                                      VK_COMPARE_OP_GREATER, VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_ALWAYS};
    uint32_t z = u->bp[0x40];
    uint64_t zt = z & 1, zw = (z >> 4) & 1, zc = zf[(z >> 1) & 7];
    uint32_t cull = (u->bp[0x00] >> 14) & 3;
    uint64_t cv = cull == 0 ? VK_CULL_MODE_NONE : cull == 1 ? VK_CULL_MODE_BACK_BIT : cull == 2 ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_FRONT_AND_BACK;
    uint64_t topo = prim == GXP_TRIANGLES ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : prim == GXP_LINES ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                                                    : VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    static int force = -1;  /* SR_VK_FORCE=cull,depth,dtest,dwrite,dalways,blend (test): fixed fields */
    if (force < 0) {
        const char* e = getenv("SR_VK_FORCE");
        force = !e ? 0 : (strstr(e, "cull") ? 1 : 0) | (strstr(e, "depth") ? 2 : 0) | (strstr(e, "blend") ? 4 : 0) |
                         (strstr(e, "dtest") ? 8 : 0) | (strstr(e, "dwrite") ? 16 : 0) | (strstr(e, "dalways") ? 32 : 0);
        if (force) rt_log("[vk] SR_VK_FORCE=%s: those pipeline fields fixed", e);
    }
    if (force & 1) cv = VK_CULL_MODE_NONE;
    if (force & 2) zt = 1, zw = 1, zc = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (force & 8) zt = 1;
    if (force & 16) zw = 1;
    if ((force & 32) && !zt) zt = 1, zc = VK_COMPARE_OP_ALWAYS;  /* test off = test on, always passing */
    if (force & 4) blend = 1, revsub = 0, s = sa = VK_BLEND_FACTOR_SRC_ALPHA, d = dA = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, mask = 15;
    return ind | blend << 1 | revsub << 2 | s << 3 | d << 8 | sa << 13 | dA << 18 | mask << 23 | zt << 27 | zw << 28 | zc << 29 |
           cv << 32 | topo << 34;
}

static void apply_dynamic(const GxUniforms* u) {
    float xs = f32(u->xfr[0x1A]), ys = f32(u->xfr[0x1B]), xo = f32(u->xfr[0x1D]), yo = f32(u->xfr[0x1E]);
    int sox = (int)(u->bp[0x59] & 0x3FF) * 2, soy = (int)((u->bp[0x59] >> 10) & 0x3FF) * 2;
    float ax = xs < 0 ? -xs : xs, ay = ys < 0 ? -ys : ys;
    VkViewport vp = {(xo - ax - (float)sox) * g_k * g_s, (yo - ay - (float)soy) * g_s, 2 * ax * g_k * g_s, 2 * ay * g_s, 0.0f, 1.0f};
    if (vp.width < 1) vp.width = 1;
    if (vp.height < 1) vp.height = 1;
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    VkRect2D sc = {{(int32_t)XSC(x0), (int32_t)SC(y0)}, {x1 > x0 ? XSC(x1) - XSC(x0) : 0, SC(y1 > y0 ? y1 - y0 : 0)}};
    if (sc.offset.x > EFB_W) sc.offset.x = EFB_W;
    if (sc.offset.y > EFB_H) sc.offset.y = EFB_H;
    static int tiny = -1;  /* SR_VK_TINYSCISSOR=1 (GPU cost test): one pixel per draw, so only vertex and per-draw work remain */
    if (tiny < 0) tiny = getenv("SR_VK_TINYSCISSOR") != NULL;
    if (tiny) sc.extent.width = sc.extent.width ? 1 : 0, sc.extent.height = sc.extent.height ? 1 : 0;
    if (!g_dyn_valid || memcmp(&vp, &g_cur_vp, sizeof vp)) {
        vkCmdSetViewport(g_cmd, 0, 1, &vp);
        g_cur_vp = vp;
    }
    if (!g_dyn_valid || memcmp(&sc, &g_cur_sc, sizeof sc)) {
        vkCmdSetScissor(g_cmd, 0, 1, &sc);
        g_cur_sc = sc;
    }
    g_dyn_valid = 1;
}

static void bind_textures(const GxCmd* c, const GxUniforms* u) {
    VkImageView v[8];
    VkSampler s[8];
    for (int m = 0; m < 8; m++) {
        VkImg* t = c->tex[m] >= 0 && c->tex[m] < g_tex_cap ? &g_tex[c->tex[m]] : NULL;
        v[m] = t && t->live && t->layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? t->view : g_dummy.view;
        uint32_t mode0 = u->bp[(m >= 4 ? 0xA0 : 0x80) + (m & 3)];
        uint32_t ws = mode0 & 3, wt = (mode0 >> 2) & 3;
        if (ws > 2) ws = 1;
        if (wt > 2) wt = 1;
        s[m] = g_samp[ws * 12 + wt * 4 + ((mode0 >> 4) & 1) * 2 + (((mode0 >> 5) & 7) >= 4 ? 1 : 0)];
    }
    if (g_tex_valid && !memcmp(v, g_cur_tex, sizeof v) && !memcmp(s, g_cur_samp, sizeof s)) return;
    memcpy(g_cur_tex, v, sizeof v);
    memcpy(g_cur_samp, s, sizeof s);
    g_tex_valid = 1;
    VkDescriptorImageInfo ii[8];
    for (int m = 0; m < 8; m++) ii[m] = (VkDescriptorImageInfo){s[m], v[m], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    if (g_push_desc) {
        VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, VK_NULL_HANDLE, 0, 0, 8,
                                   VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ii, NULL, NULL};
        p_push_desc(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl_gx, 1, 1, &wd);
    } else {
        VkDescriptorSet set;
        VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, g_dpool_frame[g_slot], 1, &g_dsl_tex};
        if (vkAllocateDescriptorSets(g_dev, &ai, &set) != VK_SUCCESS) return;
        VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ii, NULL, NULL};
        vkUpdateDescriptorSets(g_dev, 1, &wd, 0, NULL);
        vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl_gx, 1, 1, &set, 0, NULL);
    }
}

/* ---------------------------------------------------------------- presentation */
static int acquire(void) {
    if (g_swap_idx >= 0) return 1;
    if (g_swap_bad || !g_swap) {
        if (!swap_recreate()) return 0;
    }
    uint32_t idx;
    uint64_t t0 = platform_now_ns();
    VkResult r = vkAcquireNextImageKHR(g_dev, g_swap, 1000000000ull, g_acq[g_slot], VK_NULL_HANDLE, &idx);
    g_fr_acq_ns += platform_now_ns() - t0;
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        if (!swap_recreate()) return 0;
        r = vkAcquireNextImageKHR(g_dev, g_swap, 1000000000ull, g_acq[g_slot], VK_NULL_HANDLE, &idx);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        if (r == VK_ERROR_DEVICE_LOST) vk_ok(r, "vkAcquireNextImageKHR");
        g_swap_bad = 1;
        return 0;
    }
    g_swap_idx = (int)idx;
    return 1;
}

/* the 16:9 (or 4:3) picture area centred on the screen */
static void picture_rect(float aspect, float out[4]) {
    float W = (float)g_ext.width, H = (float)g_ext.height, dw = W, dh = H;
    if (W > H * aspect) dw = H * aspect;
    else dh = W / aspect;
    out[0] = (W - dw) / 2;
    out[1] = (H - dh) / 2;
    out[2] = out[0] + dw;
    out[3] = out[1] + dh;
}

/* the XFB copy: the EFB's picture onto the screen image, letterboxed to the chosen aspect */
static void present_picture(const GxCmd* c) {
    if (!acquire()) return;
    uint32_t x = c->copy[0], y = c->copy[1], w = c->copy[2], h = c->copy[3];
    float aspect = g_k > 1.0f ? 4.0f / 3.0f * g_k : settings_display_aspect();
    float sy = (float)SC(y), sh = (float)SC(h);
    if (c->copy[11]) {  /* online: this player's part of a split screen (gx_render.c), same scale both ways */
        sy = (float)SC(y + (c->copy[11] >> 16));
        sh = (float)SC(c->copy[11] & 0xFFFF);
        aspect *= (float)h / (float)(c->copy[11] & 0xFFFF);
    }
    float uv[4] = {(float)XSC(x) / EFB_W, sy / EFB_H, (float)XSC(x + w) / EFB_W, (sy + sh) / EFB_H};
    float px[4];
    picture_rect(aspect, px);
    to_layout(&g_efb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    target_begin(g_sview[g_swap_idx], g_img_ext.width, g_img_ext.height, 1);
    quad(Q_TEX_OPAQUE, TGT_SWAP, 15, g_efb.view, g_samp_lin, (float)g_ext.width, (float)g_ext.height, px, uv, NULL, NULL);
    p_end_rendering(g_cmd);
    to_layout(&g_efb, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

/* the settings menu / FPS counter, and the touch controls, over the presented picture */
static void draw_overlay_and_touch(void) {
    unsigned version;
    int box[4];
    const uint8_t* img = overlay_image(&version, box);
    if (img && version != g_ov_version) {
        int u[4] = {box[0], box[1], box[2], box[3]};
        if (g_ov_box[0] < g_ov_box[2]) {  /* also what the old content covered */
            if (u[0] >= u[2]) memcpy(u, g_ov_box, sizeof u);
            else {
                u[0] = g_ov_box[0] < u[0] ? g_ov_box[0] : u[0];
                u[1] = g_ov_box[1] < u[1] ? g_ov_box[1] : u[1];
                u[2] = g_ov_box[2] > u[2] ? g_ov_box[2] : u[2];
                u[3] = g_ov_box[3] > u[3] ? g_ov_box[3] : u[3];
            }
        }
        uint32_t uw = u[2] > u[0] ? (uint32_t)(u[2] - u[0]) : 0, uh = u[3] > u[1] ? (uint32_t)(u[3] - u[1]) : 0;
        uint32_t bytes = uw * uh * 4;
        if (!bytes) {
            g_ov_version = version;
        } else if (g_stage_off + bytes <= SLOT_STAGE) {
            uint8_t* dst = g_smap[g_slot] + OFF_STAGE + g_stage_off;
            for (uint32_t yy = 0; yy < uh; yy++) memcpy(dst + yy * uw * 4, img + ((size_t)(u[1] + yy) * OVERLAY_W + u[0]) * 4, uw * 4);
            to_layout(&g_ov, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkBufferImageCopy r = {OFF_STAGE + g_stage_off, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {u[0], u[1], 0}, {uw, uh, 1}};
            vkCmdCopyBufferToImage(g_cmd, g_sbuf[g_slot], g_ov.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            to_layout(&g_ov, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            g_stage_off = (g_stage_off + bytes + 255) & ~255u;
            g_ov_version = version;
            memcpy(g_ov_box, box, sizeof g_ov_box);
        }
    }
    if (!img) memset(g_ov_box, 0, sizeof g_ov_box);
    float W = (float)g_ext.width, H = (float)g_ext.height;
    float ov[4];
    picture_rect(16.0f / 9.0f, ov);  /* the menu keeps its own 16:9 shape */
    touch_set_overlay_rect(ov[0], ov[1], ov[2], ov[3]);
    TouchShape ts[16];
    int nt = touch_shapes(ts, 16, (int)W, (int)H);
    if (!(img && g_ov_box[0] < g_ov_box[2]) && !nt) return;
    barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    target_begin(g_sview[g_swap_idx], g_img_ext.width, g_img_ext.height, 0);
    if (img && g_ov_box[0] < g_ov_box[2]) quad(Q_TEX_OVER, TGT_SWAP, 15, g_ov.view, g_samp_lin, W, H, ov, NULL, NULL, NULL);
    for (int i = 0; i < nt; i++) {
        float px[4] = {ts[i].cx - ts[i].r, ts[i].cy - ts[i].r, ts[i].cx + ts[i].r, ts[i].cy + ts[i].r};
        if (ts[i].image >= 0 && ts[i].image < TOUCH_ICONS) {  /* a picture: premultiplied, faded by its opacity */
            const uint16_t* ic = k_touch_icon[ts[i].image];
            float uv[4] = {(float)ic[0] / TOUCH_ATLAS_W, 0, (float)(ic[0] + ic[1]) / TOUCH_ATLAS_W, (float)ic[2] / TOUCH_ATLAS_H};
            float fade[4] = {ts[i].alpha, ts[i].alpha, ts[i].alpha, ts[i].alpha};
            quad(Q_TEX_OVER, TGT_SWAP, 15, g_icons.view, g_samp_lin, W, H, px, uv, fade, NULL);
            continue;
        }
        float col[4] = {ts[i].tint[0], ts[i].tint[1], ts[i].tint[2], ts[i].alpha};
        float shape[4] = {(float)ts[i].kind, (float)ts[i].icon, (float)ts[i].pressed, 1.0f};
        quad(Q_TOUCH, TGT_SWAP, 15, NULL, NULL, W, H, px, NULL, col, shape);
    }
    p_end_rendering(g_cmd);
}

/* ---------------------------------------------------------------- screenshots (SR_SCREENSHOT / SR_SHOTS / the menu) */
static VkBuffer g_shot_buf;
static VkDeviceMemory g_shot_mem;
static uint8_t* g_shot_map;
static unsigned g_shot_pending;
static uint32_t g_shot_w, g_shot_h;
static int g_menu_shot;  /* the menu's "Save screenshot", taken before the overlay is drawn */

static int shot_wanted(unsigned n) {
    static int init;
    static unsigned list[64], nlist;
    if (!init) {
        init = 1;
        const char* e = getenv("SR_SHOTS") ? getenv("SR_SHOTS") : getenv("SR_SCREENSHOT");
        for (const char* q = e; q && *q && nlist < 64; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
            if (atoi(q) > 0) list[nlist++] = (unsigned)atoi(q);
    }
    int want = g_menu_shot;
    g_menu_shot = 0;
    for (unsigned i = 0; i < nlist; i++) want |= list[i] == n;
    return want;
}

static void shot_request(void) {
    unsigned n = g_presented;
    if (!shot_wanted(n) || !g_swap_can_read) return;
    uint32_t bytes = g_img_ext.width * g_img_ext.height * 4;
    if (!g_shot_buf || g_shot_w * g_shot_h * 4 < bytes) {
        if (g_shot_buf) {
            vkDestroyBuffer(g_dev, g_shot_buf, NULL);
            vkFreeMemory(g_dev, g_shot_mem, NULL);
        }
        VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_SHARING_MODE_EXCLUSIVE};
        VKC(vkCreateBuffer(g_dev, &bi, NULL, &g_shot_buf));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(g_dev, g_shot_buf, &mr);
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size,
                                   mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT)};
        VKC(vkAllocateMemory(g_dev, &ai, NULL, &g_shot_mem));
        VKC(vkBindBufferMemory(g_dev, g_shot_buf, g_shot_mem, 0));
        VKC(vkMapMemory(g_dev, g_shot_mem, 0, bytes, 0, (void**)&g_shot_map));
    }
    g_shot_w = g_img_ext.width;  /* pre-rotated: the picture as the panel gets it */
    g_shot_h = g_img_ext.height;
    barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy r = {0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {g_shot_w, g_shot_h, 1}};
    vkCmdCopyImageToBuffer(g_cmd, g_simg[g_swap_idx], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_shot_buf, 1, &r);
    g_shot_pending = n;
}

typedef struct { uint8_t* px; unsigned frame, w, h; int bgra; } Shot;
static int shot_thread(void* arg) {
    Shot* sh = arg;
    rt_thread_lower_priority();
    char path[64];
    snprintf(path, sizeof path, "shot_%u.bmp", sh->frame);
    FILE* f = fopen(path, "wb");
    if (f) {
        uint32_t row = (sh->w * 3 + 3) & ~3u, size = 54 + row * sh->h;  /* BMP rows are padded to 4 bytes */
        uint8_t hd[54] = {'B', 'M'};
        memcpy(hd + 2, &size, 4);
        hd[10] = 54; hd[14] = 40;
        int32_t w = (int32_t)sh->w, hh = (int32_t)sh->h;
        memcpy(hd + 18, &w, 4); memcpy(hd + 22, &hh, 4);
        hd[26] = 1; hd[28] = 24;
        uint8_t* img = calloc(1, (size_t)size);
        memcpy(img, hd, 54);
        for (int y = (int)sh->h - 1, oy = 0; y >= 0; y--, oy++) {
            const uint8_t* s = sh->px + (size_t)y * sh->w * 4;
            uint8_t* line = img + 54 + (size_t)oy * row;
            for (unsigned x = 0; x < sh->w; x++) {
                uint8_t r = s[4 * x], g = s[4 * x + 1], b = s[4 * x + 2];
                if (sh->bgra) { uint8_t t = r; r = b; b = t; }
                line[3 * x] = b; line[3 * x + 1] = g; line[3 * x + 2] = r;
            }
        }
        fwrite(img, 1, size, f);
        free(img);
        fclose(f);
        rt_log("[vk] saved %s", path);
    }
    free(sh->px);
    free(sh);
    return 0;
}

/* ---------------------------------------------------------------- one frame */
static void slot_reclaim(unsigned slot) {
    if (g_fenced[slot]) {
        uint64_t t0 = platform_now_ns();
        VkResult wr = vkWaitForFences(g_dev, 1, &g_fence[slot], VK_TRUE, UINT64_MAX);
        g_fr_wait_ns += platform_now_ns() - t0;
        if (wr != VK_SUCCESS) {
            vk_ok(wr, "vkWaitForFences");
            g_ts_written[slot] = 0;
        }
        if (g_qpool && g_ts_written[slot]) {
            uint64_t ts[2];
            if (vkGetQueryPoolResults(g_dev, g_qpool, slot * 2, 2, sizeof ts, ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && ts[1] > ts[0]) {
                uint64_t ns = (uint64_t)((double)(ts[1] - ts[0]) * g_props.limits.timestampPeriod);
                g_gpu_ns_sum += ns;
                g_last_gpu_ns = ns;
                if (ns > g_gpu_ns_max) g_gpu_ns_max = ns;
                g_gpu_frames++;
            }
            g_ts_written[slot] = 0;
        }
        vkResetFences(g_dev, 1, &g_fence[slot]);
        g_fenced[slot] = 0;
    }
    for (uint32_t i = 0; i < g_nretired[slot]; i++) {
        tex_destroy(g_retired[slot][i]);
        gx_texture_free(g_retired[slot][i]);
    }
    g_nretired[slot] = 0;
}

static void efb_copy(const GxCmd* c, const GxUniforms* u) {
    uint32_t x = c->copy[0], y = c->copy[1], w = c->copy[2], h = c->copy[3];
    efb_end();
    if (c->copy[4]) {
        present_picture(c);
    } else if ((int)c->copy[8] >= 0) {
        VkImg* t = tex_prepare((int)c->copy[8]);
        if (t && t->live) {
            to_layout(&g_efb, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            to_layout(t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{(int32_t)XSC(x), (int32_t)SC(y), 0}, {(int32_t)XSC(x + w), (int32_t)(SC(y) + SC(h)), 1}},
                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)t->w, (int32_t)t->h, 1}}};
            vkCmdBlitImage(g_cmd, g_efb.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                           VK_FILTER_LINEAR);
            uint32_t exec = c->copy[9];
            if ((((exec >> 4) & 7) | ((exec >> 3) & 1) << 3) == 4) {  /* RGB565 target: no alpha, it samples as 1 */
                to_layout(t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                target_begin(t->view, t->w, t->h, 0);
                float px[4] = {0, 0, (float)t->w, (float)t->h}, one[4] = {0, 0, 0, 1};
                quad(Q_FILL, TGT_TEX, VK_COLOR_COMPONENT_A_BIT, NULL, NULL, (float)t->w, (float)t->h, px, NULL, one, NULL);
                p_end_rendering(g_cmd);
            }
            to_layout(t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            g_tex_valid = 0;
        }
    }
    if (c->copy[5]) {  /* clear the copied rectangle */
        uint32_t argb = c->copy[6], cm = u->bp[0x41];
        efb_begin();
        float col[4] = {((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, (argb >> 24) / 255.0f};
        VkColorComponentFlags mask = ((cm >> 3) & 1 ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT) : 0) |
                                     ((cm >> 4) & 1 ? VK_COLOR_COMPONENT_A_BIT : 0);
        VkClearRect rect = {{{(int32_t)XSC(x), (int32_t)SC(y)}, {XSC(x + w) - XSC(x), SC(h)}}, 0, 1};
        if (mask == 15) {
            VkClearAttachment ca = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {{{col[0], col[1], col[2], col[3]}}}};
            vkCmdClearAttachments(g_cmd, 1, &ca, 1, &rect);
        } else if (mask) {  /* only some channels: a quad with that write mask */
            VkPipeline p = quad_pipe(Q_FILL, TGT_EFB, mask);
            (void)p;
            float px[4] = {(float)XSC(x), (float)SC(y), (float)XSC(x + w), (float)(SC(y) + SC(h))};
            quad(Q_FILL, TGT_EFB, mask, NULL, NULL, EFB_W, EFB_H, px, NULL, col, NULL);
        }
        if ((u->bp[0x40] >> 4) & 1) {
            VkClearAttachment da = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, {.depthStencil = {(float)c->copy[7] / 16777215.0f, 0}}};
            vkCmdClearAttachments(g_cmd, 1, &da, 1, &rect);
        }
    }
    g_bound = VK_NULL_HANDLE;
    g_dyn_valid = 0;
}

static uint64_t g_busy_ns;
static unsigned g_busy_frames, g_draws_sum;

/* ---------------------------------------------------------------- device loss */
/* SR_VK_FAKELOST=<frame>[,<frame>...]: act as if the device were lost after those frames (tests the rebuild) */
static int fake_lost_due(void) {
    static unsigned at[8], n;
    static int parsed;
    if (!parsed) {
        parsed = 1;
        const char* e = getenv("SR_VK_FAKELOST");
        while (e && *e && n < 8) {
            char* end;
            unsigned long v = strtoul(e, &end, 10);
            if (end == e) break;
            at[n++] = (unsigned)v;
            e = *end == ',' ? end + 1 : end;
        }
        if (n) rt_log("[vk] SR_VK_FAKELOST: %u fake device losses", n);
    }
    for (unsigned i = 0; i < n; i++)
        if (at[i] == g_frames_done) return 1;
    return 0;
}

static void img_destroy(VkImg* t) {  /* an image outside the pool, or (pool gone) one inside it */
    if (!t->live) return;
    vkDestroyImageView(g_dev, t->view, NULL);
    vkDestroyImage(g_dev, t->img, NULL);
    if (t->mem) vkFreeMemory(g_dev, t->mem, NULL);
    memset(t, 0, sizeof *t);
}

/* everything device_setup made (and what frames made since), in reverse order; the instance and surface stay */
static void device_teardown(void) {
    vkDeviceWaitIdle(g_dev);  /* returns at once on a lost device */
    for (uint32_t i = 0; i < g_pipes_cap; i++)
        if (g_pipes[i].p) vkDestroyPipeline(g_dev, g_pipes[i].p, NULL);
    if (g_pipes) memset(g_pipes, 0, g_pipes_cap * sizeof *g_pipes);
    g_pipes_total = 0;
    for (int i = 0; i < g_tex_cap; i++) img_destroy(&g_tex[i]);
    for (int s = 0; s < 2; s++) {  /* textures waiting for their slot's fence: nothing on the GPU any more */
        for (uint32_t i = 0; i < g_nretired[s]; i++) gx_texture_free(g_retired[s][i]);
        g_nretired[s] = 0;
    }
    img_destroy(&g_efb);
    img_destroy(&g_depth);
    img_destroy(&g_ov);
    img_destroy(&g_dummy);
    img_destroy(&g_icons);
    vkFreeMemory(g_dev, g_pool_mem, NULL);
    g_pool_mem = VK_NULL_HANDLE;
    swap_destroy();
    if (g_shot_buf) {
        vkDestroyBuffer(g_dev, g_shot_buf, NULL);
        vkFreeMemory(g_dev, g_shot_mem, NULL);
        g_shot_buf = VK_NULL_HANDLE;
        g_shot_mem = VK_NULL_HANDLE;
        g_shot_w = g_shot_h = 0;
    }
    if (g_qpool) vkDestroyQueryPool(g_dev, g_qpool, NULL);
    g_qpool = VK_NULL_HANDLE;
    vkDestroyDescriptorPool(g_dev, g_dpool_static, NULL);
    for (int i = 0; i < 2; i++) {
        if (g_dpool_frame[i]) vkDestroyDescriptorPool(g_dev, g_dpool_frame[i], NULL);
        g_dpool_frame[i] = VK_NULL_HANDLE;
    }
    vkDestroyCommandPool(g_dev, g_cpool, NULL);
    for (int i = 0; i < 2; i++) {
        vkUnmapMemory(g_dev, g_smem[i]);
        vkDestroyBuffer(g_dev, g_sbuf[i], NULL);
        vkFreeMemory(g_dev, g_smem[i], NULL);
        vkDestroyFence(g_dev, g_fence[i], NULL);
        vkDestroySemaphore(g_dev, g_acq[i], NULL);
        g_cb[i] = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 36; i++) vkDestroySampler(g_dev, g_samp[i], NULL);
    vkDestroyPipelineCache(g_dev, g_pcache, NULL);
    vkDestroyPipelineLayout(g_dev, g_pl_gx, NULL);
    vkDestroyPipelineLayout(g_dev, g_pl_quad, NULL);
    vkDestroyDescriptorSetLayout(g_dev, g_dsl_ubo, NULL);
    vkDestroyDescriptorSetLayout(g_dev, g_dsl_tex, NULL);
    vkDestroyDescriptorSetLayout(g_dev, g_dsl_one, NULL);
    for (int i = 0; i < GX_SPEC_COUNT; i++)
        if (g_spec_sm[i]) vkDestroyShaderModule(g_dev, g_spec_sm[i], NULL);
    memset(g_spec_sm, 0, sizeof g_spec_sm);
    VkShaderModule sm[7] = {g_sm_vs, g_sm_fs, g_sm_fs_ind, g_sm_qvs, g_sm_tex, g_sm_fill, g_sm_touch};
    for (int i = 0; i < 7; i++) vkDestroyShaderModule(g_dev, sm[i], NULL);
    vkDestroyDevice(g_dev, NULL);
    g_dev = VK_NULL_HANDLE;
    /* per-frame state that pointed at the old objects */
    g_cmd = VK_NULL_HANDLE;
    g_slot = 0;
    g_fenced[0] = g_fenced[1] = 0;
    g_ts_written[0] = g_ts_written[1] = 0;
    g_swap_idx = -1;
    g_swap_bad = 0;
    g_shot_pending = 0;
    g_ov_version = ~0u;
    g_in_efb = 0;
    g_bound = VK_NULL_HANDLE;
    g_dyn_valid = 0;
    g_tex_valid = 0;
}

static void pump(void);

/* after VK_ERROR_DEVICE_LOST: a new device with everything on it, the textures upload again from their pixels
 * (EFB copies come back with the game's next copy), the recorded pipelines are rebuilt before going on */
static void device_recover(void) {
    static uint64_t lost_at[3];
    static unsigned nlost;
    uint64_t t0 = platform_now_ns();
    if (nlost >= 3 && t0 - lost_at[nlost % 3] < 10000000000ull)
        rt_fatal("[vk] device lost %u times within %.1f s (VK_ERROR_DEVICE_LOST): giving up", 4u, (t0 - lost_at[nlost % 3]) / 1e9);
    lost_at[nlost % 3] = t0;
    nlost++;
    rt_log("[vk] device lost after frame %u: rebuilding the renderer", g_frames_done);
    keys_save();
    while (g_pw_running) {
        struct timespec ts = {0, 2000000};
        thrd_sleep(&ts, NULL);
    }
    pipe_drain();
    device_teardown();
    g_lost = 0;
    device_setup();
    uint64_t t_dev = platform_now_ns();
    if (g_nkeys) {  /* the pipelines this run used, before the game goes on (the "short pause") */
        g_pw_nkeys = g_nkeys;
        g_pw_keys = malloc(g_nkeys * sizeof *g_pw_keys);
        memcpy(g_pw_keys, g_keys, g_nkeys * sizeof *g_pw_keys);
        g_pw_running = 1;
        rt_thread_start(prewarm_thread, NULL);
        while (g_pw_running && platform_now_ns() - t_dev < 5000000000ull) {
            pump();
            pipe_drain();
            struct timespec ts = {0, 4000000};
            thrd_sleep(&ts, NULL);
        }
        pipe_drain();
    }
    rt_log("[vk] device lost, recovered (%u): device %.0f ms, pipelines %.0f ms%s", nlost, (t_dev - t0) / 1e6,
           (platform_now_ns() - t_dev) / 1e6, g_pw_running ? " (prewarm still running)" : "");
}

static void execute(GxFrame* f) {
    uint64_t t_start = platform_now_ns();
    g_fr_pipe_ns = g_fr_wait_ns = g_fr_img_ns = g_fr_acq_ns = 0;
    g_fr_pipe_n = g_fr_img_n = 0;
    if (g_lost) device_recover();
    pipe_drain();
    slot_reclaim(g_slot);
    if (g_lost) device_recover();  /* found by the fence wait */
    g_s = settings_internal_scale();
    settings_set_window_aspect((float)g_ext.width / (float)g_ext.height);  /* the fit mode: the screen's */
    g_k = f->wide_k > 1.0f ? (f->wide_k < WIDE_K_MAX ? f->wide_k : WIDE_K_MAX) : 1.0f;
    g_cmd = g_cb[g_slot];
    vkResetCommandBuffer(g_cmd, 0);
    if (!g_push_desc) vkResetDescriptorPool(g_dev, g_dpool_frame[g_slot], 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VKC(vkBeginCommandBuffer(g_cmd, &bi));
    if (g_qpool) {
        vkCmdResetQueryPool(g_cmd, g_qpool, g_slot * 2, 2);
        vkCmdWriteTimestamp(g_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g_qpool, g_slot * 2);
    }
    g_passes = 0;
    g_stage_off = 0;
    g_in_efb = 0;
    g_tex_valid = 0;
    g_swap_idx = -1;
    uint8_t* cpu = g_smap[g_slot];

    /* vertices, then the index lists (draws past what fits are dropped) */
    uint32_t vbytes = f->nverts * (uint32_t)sizeof(GxVertex);
    if (vbytes > SLOT_VTX / 2) {
        rt_log("[vk] frame has %u vertices, more than the buffer holds; truncated", f->nverts);
        vbytes = SLOT_VTX / 2 / sizeof(GxVertex) * sizeof(GxVertex);
    }
    memcpy(cpu + OFF_VTX, f->verts, vbytes);
    uint32_t ioff = (OFF_VTX + vbytes + 255) & ~255u;
    uint32_t icap = (SLOT_VTX - ioff) / 4, ncopy = f->nidx < icap ? f->nidx : icap;
    memcpy(cpu + ioff, f->idx, (size_t)ncopy * 4);
    /* XF memory images, one per chunk table (as render_deko.c) */
    uint32_t nximg = f->nxtabs < SLOT_XFI / XF_IMAGE_BYTES ? f->nxtabs : SLOT_XFI / XF_IMAGE_BYTES;
    for (uint32_t t = 0; t < nximg; t++) {
        uint8_t* dst = cpu + OFF_XFI + (size_t)t * XF_IMAGE_BYTES;
        for (int k = 0; k < XF_IMAGE_CHUNKS; k++) {
            uint32_t pc = f->xtabs[t].xtab[k_xf_image_chunks[k]];
            if (pc < f->nxchunks) memcpy(dst + k * GX_XFM_CHUNK * 4, f->xchunks + (size_t)pc * GX_XFM_CHUNK, GX_XFM_CHUNK * 4);
            else memset(dst + k * GX_XFM_CHUNK * 4, 0, GX_XFM_CHUNK * 4);
        }
    }
    /* register blocks at the uniform alignment */
    uint32_t align = (uint32_t)g_props.limits.minUniformBufferOffsetAlignment;
    if (align < 16) align = 16;
    const uint32_t ustride = ((uint32_t)sizeof(GxUniforms) + align - 1) & ~(align - 1);
    int ubo_ok = f->nunis * ustride <= SLOT_UBO;
    if (ubo_ok)
        for (uint32_t i = 0; i < f->nunis; i++) memcpy(cpu + OFF_UBO + i * ustride, &f->unis[i], sizeof(GxUniforms));

    /* textures this frame uses, before any rendering */
    for (uint32_t i = 0; i < f->ncmds; i++)
        if (f->cmds[i].type == GXC_DRAW)
            for (int m = 0; m < 8; m++)
                if (f->cmds[i].tex[m] >= 0) tex_prepare(f->cmds[i].tex[m]);

    uint64_t t_rec = platform_now_ns();
    const GxUniforms* cur = NULL;
    uint32_t cur_regs = ~0u, cur_xtab = ~0u;
    uint64_t cur_spec = 0;
    uint32_t dyn[2] = {0, 0};
    int sets_bound = 0;
    unsigned draws = 0;
    g_fr_binds = 0;
    for (uint32_t i = 0; i < f->ncmds; i++) {
        const GxCmd* c = &f->cmds[i];
        switch (c->type) {
        case GXC_STATE:
            if (!ubo_ok) { cur = NULL; break; }
            if (c->b != cur_xtab) {
                cur_xtab = c->b;
                uint32_t t = nximg ? (c->b < nximg ? c->b : nximg - 1) : 0;
                dyn[1] = OFF_XFI + t * XF_IMAGE_BYTES;
                sets_bound = 0;
            }
            if (c->a != cur_regs) {
                cur_regs = c->a;
                cur = &f->unis[c->a];
                cur_spec = spec_bits(cur);
                dyn[0] = OFF_UBO + c->a * ustride;
                sets_bound = 0;
            }
            break;
        case GXC_DRAW: {
            if (!cur || c->a + c->b > ncopy || vbytes < f->nverts * (uint32_t)sizeof(GxVertex)) break;
            int fresh = !g_in_efb;
            efb_begin();
            if (fresh) {
                VkDeviceSize zero = 0;
                vkCmdBindVertexBuffers(g_cmd, 0, 1, &g_sbuf[g_slot], &zero);
                vkCmdBindIndexBuffer(g_cmd, g_sbuf[g_slot], ioff, VK_INDEX_TYPE_UINT32);
                g_tex_valid = 0;
                sets_bound = 0;
            }
            uint64_t key = state_key(cur, c->prim) | cur_spec;
            if (!g_bound || key != g_cur_key) {
                VkPipeline p = gx_pipe(key);
                if (p != g_bound && !(g_bound && g_one_pipe)) {  /* SR_VK_ONEPIPE (test) */
                    g_fr_binds++;
                    vkCmdBindPipeline(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p);
                    g_bound = p;
                }
                g_cur_key = key;
            }
            if (!sets_bound) {
                vkCmdBindDescriptorSets(g_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pl_gx, 0, 1, &g_ubo_set[g_slot], 2, dyn);
                sets_bound = 1;
            }
            apply_dynamic(cur);
            bind_textures(c, cur);
            if (draws >= g_draw_lo && draws < g_draw_hi) vkCmdDrawIndexed(g_cmd, c->b, 1, c->a, 0, 0);
            draws++;
            break;
        }
        case GXC_COPY:
            if (cur) {
                efb_copy(c, cur);
                sets_bound = 0;
                g_tex_valid = 0;
            }
            break;
        case GXC_TEXBLIT: {  /* an EFB copy into the top left of a larger copy texture (gx_render.h) */
            efb_end();
            int sid = (int)c->copy[0], did = (int)c->copy[1];
            VkImg* s = tex_prepare(sid);
            VkImg* d = tex_prepare(did);
            GxTexture* sg = gx_texture_get(sid);
            GxTexture* dg = gx_texture_get(did);
            if (!s || !d || !s->live || !d->live || !sg || !dg || !sg->width || !dg->width || !sg->height || !dg->height) break;
            uint32_t sw = c->copy[2] * s->w / sg->width, sh = c->copy[3] * s->h / sg->height;
            uint32_t dw = c->copy[2] * d->w / dg->width, dh = c->copy[3] * d->h / dg->height;
            if (sw > s->w) sw = s->w;
            if (sh > s->h) sh = s->h;
            if (dw > d->w) dw = d->w;
            if (dh > d->h) dh = d->h;
            if (!sw || !sh || !dw || !dh) break;
            to_layout(s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            to_layout(d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)sw, (int32_t)sh, 1}},
                             {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {(int32_t)dw, (int32_t)dh, 1}}};
            vkCmdBlitImage(g_cmd, s->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                           sw == dw && sh == dh ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
            to_layout(s, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            to_layout(d, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            g_tex_valid = 0;
            break;
        }
        default: break;
        }
    }
    efb_end();
    int presenting = g_swap_idx >= 0;
    if (presenting) {
        g_presented++;
        g_menu_shot |= overlay_take_screenshot_request();  /* before the overlay: the next frame, menu closed */
        draw_overlay_and_touch();
        shot_request();
        barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    if (g_qpool) {
        vkCmdWriteTimestamp(g_cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g_qpool, g_slot * 2 + 1);
        g_ts_written[g_slot] = 1;
    }
    int ended = vk_ok(vkEndCommandBuffer(g_cmd), "vkEndCommandBuffer");
    uint64_t t_sub = platform_now_ns();
    VkPipelineStageFlags wst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, presenting ? 1u : 0u, &g_acq[g_slot], &wst, 1, &g_cmd,
                       presenting ? 1u : 0u, presenting ? &g_sdone[g_swap_idx] : NULL};
    if (ended && vk_ok(vkQueueSubmit(g_q, 1, &si, g_fence[g_slot]), "vkQueueSubmit")) g_fenced[g_slot] = 1;
    else presenting = 0;  /* lost: the frame is dropped */
    uint64_t t_pres = platform_now_ns();
    if (presenting) {
        uint32_t idx = (uint32_t)g_swap_idx;
        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &g_sdone[idx], 1, &g_swap, &idx, NULL};
        VkResult r = vkQueuePresentKHR(g_q, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) g_swap_bad = 1;
        if (r == VK_ERROR_DEVICE_LOST) vk_ok(r, "vkQueuePresentKHR");
        g_slayout[idx] = VK_IMAGE_LAYOUT_UNDEFINED;  /* contents not kept between frames */
        g_swap_idx = -1;
    }
    if (fake_lost_due()) vk_ok(VK_ERROR_DEVICE_LOST, "SR_VK_FAKELOST");  /* test: everything below as after a real loss */
    if (g_shot_pending && (g_lost || !g_fenced[g_slot] ||
                           vkWaitForFences(g_dev, 1, &g_fence[g_slot], VK_TRUE, UINT64_MAX) != VK_SUCCESS))
        g_shot_pending = 0;  /* no picture from a lost frame */
    if (g_shot_pending) {
        Shot* sh = malloc(sizeof *sh);
        sh->w = g_shot_w;
        sh->h = g_shot_h;
        sh->frame = g_shot_pending;
        sh->bgra = g_swap_fmt == VK_FORMAT_B8G8R8A8_UNORM;
        sh->px = malloc((size_t)sh->w * sh->h * 4);
        memcpy(sh->px, g_shot_map, (size_t)sh->w * sh->h * 4);
        g_shot_pending = 0;
        rt_thread_start(shot_thread, sh);
    }
    g_retired[g_slot] = realloc(g_retired[g_slot], (f->nretired ? f->nretired : 1) * sizeof(int));
    memcpy(g_retired[g_slot], f->retired, f->nretired * sizeof(int));
    g_nretired[g_slot] = f->nretired;
    g_slot ^= 1;
    pcache_save(0);
    uint64_t t_end = platform_now_ns(), busy = t_end - t_start;
    g_ph_in += t_rec - t_start;
    g_ph_rec += t_sub - t_rec - g_fr_acq_ns;
    g_ph_acq += g_fr_acq_ns;
    g_ph_sub += t_pres - t_sub;
    g_ph_pres += t_end - t_pres;
    g_busy_ns += busy;
    g_draws_sum += draws;
    g_binds_sum += g_fr_binds;
    g_passes_sum += g_passes;
    if (busy > 50000000ull)
        rt_log("[hitch] render thread: a frame took %.0f ms (%u draws): %u new pipelines %.0f ms, GPU wait %.0f ms, "
               "%u new images %.0f ms, %u KB staged, %u pipeline switches; copy-in %.0f, recording %.0f, acquire %.0f, submit %.0f, present %.0f ms; last GPU frame %.1f ms%s",
               busy / 1e6, draws, g_fr_pipe_n, g_fr_pipe_ns / 1e6, g_fr_wait_ns / 1e6, g_fr_img_n, g_fr_img_ns / 1e6,
               g_stage_off >> 10, g_fr_binds, (t_rec - t_start) / 1e6, (t_sub - t_rec - g_fr_acq_ns) / 1e6, g_fr_acq_ns / 1e6,
               (t_pres - t_sub) / 1e6, (t_end - t_pres) / 1e6, g_last_gpu_ns / 1e6, g_pw_running ? " (prewarm still running)" : "");
    if (++g_busy_frames == 300) {
        rt_log("[vk] %.2f ms/frame on the render thread, %u draws/frame (%u pipeline switches), %u pipelines, %u frames presented; "
               "specialized shaders for %llu of %llu states", g_busy_ns / 300 / 1e6,
               g_draws_sum / 300, g_binds_sum / 300, g_pipes_total, g_presented, g_spec_hits, g_spec_hits + g_spec_misses);
        g_spec_hits = g_spec_misses = 0;
        rt_log("[vk]   parts, ms/frame: copy-in %.2f, recording %.2f, acquire %.2f, submit %.2f, present %.2f",
               g_ph_in / 300 / 1e6, g_ph_rec / 300 / 1e6, g_ph_acq / 300 / 1e6, g_ph_sub / 300 / 1e6, g_ph_pres / 300 / 1e6);
        g_ph_in = g_ph_rec = g_ph_acq = g_ph_sub = g_ph_pres = 0;
        if (g_gpu_frames)
            rt_log("[vk]   GPU %.2f ms/frame (max %.2f), %.1f EFB passes/frame", g_gpu_ns_sum / g_gpu_frames / 1e6, g_gpu_ns_max / 1e6,
                   g_passes_sum / 300.0);
        g_gpu_ns_sum = g_gpu_ns_max = 0;
        g_gpu_frames = g_passes_sum = 0;
        g_busy_ns = 0;
        g_draws_sum = 0;
        g_binds_sum = 0;
        g_busy_frames = 0;
    }
    g_frames_done++;
}

/* ---------------------------------------------------------------- frame queue, events, thread */
static mtx_t g_lock;
static cnd_t g_cv;
static GxFrame g_frames[2];
static GxFrame* g_free[2];
static int g_nfree;
static GxFrame* g_pending;
static volatile int g_quit;

void render_dump(void) {}

GxFrame* render_take_free_frame(void) {
    mtx_lock(&g_lock);
    while (!g_nfree) cnd_wait(&g_cv, &g_lock);
    GxFrame* f = g_free[--g_nfree];
    mtx_unlock(&g_lock);
    return f;
}

void render_submit(GxFrame* f) {
    mtx_lock(&g_lock);
    while (g_pending) cnd_wait(&g_cv, &g_lock);
    g_pending = f;
    cnd_broadcast(&g_cv);
    mtx_unlock(&g_lock);
}

int render_poll_quit(void) { return g_quit; }

#ifdef SR_IOS
/* iOS refuses GPU work from an app in the background: MoltenVK then reports a lost device, and rebuilding it
 * in the background crashed (vkCreateDevice -> ~MVKDevice). Android's SDL blocks the event pump while the app is
 * paused; on iOS we do it here: finish the GPU work, then wait (main thread, UIKit keeps running) until the app
 * is active again. The game thread stops by itself when no frame slot comes free. */
static void ios_wait_in_background(int w, int h) {
    if (g_dev) vkDeviceWaitIdle(g_dev);
    rt_log("[ios] paused (in the background)");
    SDL_Event e;
    for (;;) {
        if (!SDL_WaitEventTimeout(&e, 250)) continue;
        touch_event(&e, w, h);  /* fingers lifted meanwhile */
        if (e.type == SDL_QUIT || e.type == SDL_APP_TERMINATING) { g_quit = 1; return; }
        if (e.type == SDL_APP_DIDENTERFOREGROUND) break;
    }
    rt_log("[ios] active again");
    g_swap_bad = 1;
}
#endif

static void pump(void) {
    SDL_Event e;
    int w = (int)g_ext.width, h = (int)g_ext.height;
    if (!w) SDL_Vulkan_GetDrawableSize(g_win, &w, &h);
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: g_quit = 1; break;
        case SDL_APP_WILLENTERBACKGROUND:
            rt_log("[android] going to the background");
            if (g_dev) pcache_save(1);
#ifdef SR_IOS
            ios_wait_in_background(w, h);
#endif
            break;
        case SDL_APP_DIDENTERFOREGROUND:
            rt_log("[android] back in the foreground");
            g_swap_bad = 1;  /* the surface is new */
            break;
        case SDL_APP_LOWMEMORY: rt_log("[android] the system is low on memory"); break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) g_swap_bad = 1;
            break;
        case SDL_KEYDOWN:
            /* the phone's Back (button or gesture): the settings menu instead of leaving the game */
            if (e.key.keysym.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat) overlay_menu_toggle();
            break;
        default: break;
        }
        touch_event(&e, w, h);
    }
    input_pump();
}

#ifdef SR_IOS
void ios_attach_window(SDL_Window* win);
#endif

static int render_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#ifdef SR_IOS
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");  /* edge swipes need a second swipe to leave the game */
#endif
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) rt_fatal("SDL video: %s", SDL_GetError());
#ifdef SR_IOS
    /* full pixel size (e.g. 2622 x 1206 on an iPhone 17 Pro); without the flag the screen image is in points
     * (874 x 402) and iOS stretches it 3x: jagged touch controls and a soft picture */
    g_win = SDL_CreateWindow("Sonic Riders", 0, 0, 0, 0, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI);
#else
    g_win = SDL_CreateWindow("Sonic Riders", 0, 0, 0, 0, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
#endif
    if (!g_win) rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
#ifdef SR_IOS
    ios_attach_window(g_win);  /* platform/ios/ios_scene.m: iOS 27 needs the window on a scene */
#endif
    SDL_DisableScreenSaver();  /* the screen stays on while the game runs */
    vk_setup();
    for (;;) {
        mtx_lock(&g_lock);
        while (!g_pending) {
            mtx_unlock(&g_lock);
            pump();
            mtx_lock(&g_lock);
            if (!g_pending) {
                struct timespec ts;
                timespec_get(&ts, TIME_UTC);
                ts.tv_nsec += 4000000;
                if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
                cnd_timedwait(&g_cv, &g_lock, &ts);
            }
        }
        GxFrame* f = g_pending;
        mtx_unlock(&g_lock);
        pump();
        execute(f);
        mtx_lock(&g_lock);
        g_pending = NULL;
        g_free[g_nfree++] = f;
        cnd_broadcast(&g_cv);
        mtx_unlock(&g_lock);
        if (overlay_take_quit_request()) g_quit = 1;
        if (g_quit) {
            pcache_save(1);
            rt_log("[android] quit");
            fflush(stderr);
            exit(0);
        }
    }
    return 0;
}

void render_start(void) {
    mtx_init(&g_lock, mtx_plain);
    cnd_init(&g_cv);
    g_free[0] = &g_frames[0];
    g_free[1] = &g_frames[1];
    g_nfree = 2;
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) rt_fatal("SDL_Init: %s", SDL_GetError());
    input_init();
#ifdef SR_IOS
    /* iOS: UIKit wants the window and the event pump on the main thread; main() runs render_main_loop()
     * there after starting the game thread */
#else
    rt_thread_start(render_thread, NULL);
#endif
}

#ifdef SR_IOS
void render_main_loop(void) { render_thread(NULL); }
#endif
