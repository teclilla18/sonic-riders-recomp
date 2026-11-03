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
#define EFB_W (EFB_WIDTH * EFB_SCALE)
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

/* ---- swapchain ---- */
static VkSurfaceKHR g_surf;
static VkSwapchainKHR g_swap;
static VkFormat g_swap_fmt = VK_FORMAT_UNDEFINED;
static VkExtent2D g_ext;
static uint32_t g_nimg;
static VkImage g_simg[8];
static VkImageView g_sview[8];
static VkImageLayout g_slayout[8];
static VkSemaphore g_sdone[8];
static int g_swap_can_read;
static int g_swap_bad;  /* needs making again before the next present */

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
static VkPipelineCache g_pcache;
static unsigned g_pipes_new, g_pipes_total;
static uint64_t g_pcache_saved_ns;
static VkSampler g_samp[36], g_samp_lin, g_samp_near;

/* ---- images ---- */
typedef struct VkImg {
    VkImage img;
    VkImageView view;
    VkImageLayout layout;
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
    VKC(vkCreateGraphicsPipelines(g_dev, g_pcache, 1, &gi, NULL, &p));
    g_pipes_new++;
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
static VkPipeline gx_pipe(uint64_t key) {
    VkPipeline p = pipe_find(key);
    if (p) return p;
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
    p = pipe_build(g_sm_vs, ind ? g_sm_fs_ind : g_sm_fs, g_pl_gx, 1, topo, &b, &ds, cull, TGT_EFB);
    pipe_put(key, p);
    return p;
}

/* a screen quad's pipeline: kind (Q_*), target */
static VkPipeline quad_pipe(int kind, int target, VkColorComponentFlags mask) {
    uint64_t key = (1ull << 63) | (uint64_t)kind | (uint64_t)target << 4 | (uint64_t)mask << 8;
    VkPipeline p = pipe_find(key);
    if (p) return p;
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
    p = pipe_build(g_sm_qvs, fs, g_pl_quad, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, &b, &ds, VK_CULL_MODE_NONE, target);
    pipe_put(key, p);
    return p;
}

static void pcache_save(int force) {
    uint64_t now = platform_now_ns();
    if (!g_pipes_new || (!force && now - g_pcache_saved_ns < 10000000000ull)) return;
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
    VkCompositeAlphaFlagBitsKHR ca = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
                                         ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    uint32_t n = caps.minImageCount + 1;
    if (caps.maxImageCount && n > caps.maxImageCount) n = caps.maxImageCount;
    if (n > 8) n = 8;
    g_swap_can_read = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    VkSwapchainCreateInfoKHR ci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, g_surf, n, fmt.format, fmt.colorSpace, ext, 1,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (g_swap_can_read ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
                                   VK_SHARING_MODE_EXCLUSIVE, 0, NULL, tf, ca, VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, VK_NULL_HANDLE};
    if (vkCreateSwapchainKHR(g_dev, &ci, NULL, &g_swap) != VK_SUCCESS) return 0;
    g_ext = ext;
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
    rt_log("[vk] screen %ux%u, format %d, %u images, transform %X (current %X)", ext.width, ext.height, fmt.format, g_nimg, tf,
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
    }
    free(de);
    int core13 = g_props.apiVersion >= VK_API_VERSION_1_3 && iv >= VK_API_VERSION_1_3;
    if (!core13 && !has_dr) rt_fatal("[vk] this GPU's driver has no dynamic rendering (Vulkan 1.3 or VK_KHR_dynamic_rendering)");
    if (getenv("SR_VK_NOPUSH")) g_push_desc = 0;
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
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, g_qfi, 1, &prio};
    const char* dext[4];
    uint32_t ndx = 0;
    dext[ndx++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
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
    VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64};
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
    VkRenderingInfoKHR ri = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR, NULL, 0, {{0, 0}, {EFB_W, EFB_H}}, 1, 0, 1, &ca, &da, NULL};
    p_begin_rendering(g_cmd, &ri);
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
    VkViewport vp = {0, 0, w, h, 0, 1};
    VkRect2D sc = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
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
    float pc[16] = {px[0] / w * 2 - 1, px[1] / h * 2 - 1, px[2] / w * 2 - 1, px[3] / h * 2 - 1};
    static const float uv01[4] = {0, 0, 1, 1}, white[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 0};
    memcpy(pc + 4, uv ? uv : uv01, 16);
    memcpy(pc + 8, color ? color : white, 16);
    memcpy(pc + 12, shape ? shape : zero, 16);
    vkCmdPushConstants(g_cmd, g_pl_quad, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 64, pc);
    vkCmdDraw(g_cmd, 4, 1, 0, 0);
}

/* ---------------------------------------------------------------- per-draw state */
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

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
    return ind | blend << 1 | revsub << 2 | s << 3 | d << 8 | sa << 13 | dA << 18 | mask << 23 | zt << 27 | zw << 28 | zc << 29 |
           cv << 32 | topo << 34;
}

static void apply_dynamic(const GxUniforms* u) {
    float xs = f32(u->xfr[0x1A]), ys = f32(u->xfr[0x1B]), xo = f32(u->xfr[0x1D]), yo = f32(u->xfr[0x1E]);
    int sox = (int)(u->bp[0x59] & 0x3FF) * 2, soy = (int)((u->bp[0x59] >> 10) & 0x3FF) * 2;
    float ax = xs < 0 ? -xs : xs, ay = ys < 0 ? -ys : ys;
    VkViewport vp = {(xo - ax - (float)sox) * g_s, (yo - ay - (float)soy) * g_s, 2 * ax * g_s, 2 * ay * g_s, 0.0f, 1.0f};
    if (vp.width < 1) vp.width = 1;
    if (vp.height < 1) vp.height = 1;
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    VkRect2D sc = {{(int32_t)SC(x0), (int32_t)SC(y0)}, {SC(x1 > x0 ? x1 - x0 : 0), SC(y1 > y0 ? y1 - y0 : 0)}};
    if (sc.offset.x > EFB_W) sc.offset.x = EFB_W;
    if (sc.offset.y > EFB_H) sc.offset.y = EFB_H;
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
    VkResult r = vkAcquireNextImageKHR(g_dev, g_swap, 1000000000ull, g_acq[g_slot], VK_NULL_HANDLE, &idx);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        if (!swap_recreate()) return 0;
        r = vkAcquireNextImageKHR(g_dev, g_swap, 1000000000ull, g_acq[g_slot], VK_NULL_HANDLE, &idx);
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
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
    float aspect = settings_display_aspect();
    float sy = (float)SC(y), sh = (float)SC(h);
    if (c->copy[11]) {  /* online: this player's part of a split screen (gx_render.c), same scale both ways */
        sy = (float)SC(y + (c->copy[11] >> 16));
        sh = (float)SC(c->copy[11] & 0xFFFF);
        aspect *= (float)h / (float)(c->copy[11] & 0xFFFF);
    }
    float uv[4] = {(float)SC(x) / EFB_W, sy / EFB_H, (float)(SC(x) + SC(w)) / EFB_W, (sy + sh) / EFB_H};
    float px[4];
    picture_rect(aspect, px);
    to_layout(&g_efb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    target_begin(g_sview[g_swap_idx], g_ext.width, g_ext.height, 1);
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
    target_begin(g_sview[g_swap_idx], g_ext.width, g_ext.height, 0);
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

static int shot_wanted(unsigned n) {
    static int init;
    static unsigned list[64], nlist;
    if (!init) {
        init = 1;
        const char* e = getenv("SR_SHOTS") ? getenv("SR_SHOTS") : getenv("SR_SCREENSHOT");
        for (const char* q = e; q && *q && nlist < 64; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
            if (atoi(q) > 0) list[nlist++] = (unsigned)atoi(q);
    }
    int want = overlay_take_screenshot_request();
    for (unsigned i = 0; i < nlist; i++) want |= list[i] == n;
    return want;
}

static void shot_request(void) {
    unsigned n = g_presented;
    if (!shot_wanted(n) || !g_swap_can_read) return;
    uint32_t bytes = g_ext.width * g_ext.height * 4;
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
    g_shot_w = g_ext.width;
    g_shot_h = g_ext.height;
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
        uint32_t row = sh->w * 3, size = 54 + row * sh->h;
        uint8_t hd[54] = {'B', 'M'};
        memcpy(hd + 2, &size, 4);
        hd[10] = 54; hd[14] = 40;
        int32_t w = (int32_t)sh->w, hh = (int32_t)sh->h;
        memcpy(hd + 18, &w, 4); memcpy(hd + 22, &hh, 4);
        hd[26] = 1; hd[28] = 24;
        uint8_t* img = malloc((size_t)size);
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
        vkWaitForFences(g_dev, 1, &g_fence[slot], VK_TRUE, UINT64_MAX);
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
            VkImageBlit b = {{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{(int32_t)SC(x), (int32_t)SC(y), 0}, {(int32_t)(SC(x) + SC(w)), (int32_t)(SC(y) + SC(h)), 1}},
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
        VkClearRect rect = {{{(int32_t)SC(x), (int32_t)SC(y)}, {SC(w), SC(h)}}, 0, 1};
        if (mask == 15) {
            VkClearAttachment ca = {VK_IMAGE_ASPECT_COLOR_BIT, 0, {{{col[0], col[1], col[2], col[3]}}}};
            vkCmdClearAttachments(g_cmd, 1, &ca, 1, &rect);
        } else if (mask) {  /* only some channels: a quad with that write mask */
            VkPipeline p = quad_pipe(Q_FILL, TGT_EFB, mask);
            (void)p;
            float px[4] = {(float)SC(x), (float)SC(y), (float)(SC(x) + SC(w)), (float)(SC(y) + SC(h))};
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

static void execute(GxFrame* f) {
    uint64_t t_start = platform_now_ns();
    slot_reclaim(g_slot);
    g_s = settings_internal_scale();
    g_cmd = g_cb[g_slot];
    vkResetCommandBuffer(g_cmd, 0);
    if (!g_push_desc) vkResetDescriptorPool(g_dev, g_dpool_frame[g_slot], 0);
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VKC(vkBeginCommandBuffer(g_cmd, &bi));
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

    const GxUniforms* cur = NULL;
    uint32_t cur_regs = ~0u, cur_xtab = ~0u;
    uint32_t dyn[2] = {0, 0};
    int sets_bound = 0;
    unsigned draws = 0;
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
            uint64_t key = state_key(cur, c->prim);
            if (!g_bound || key != g_cur_key) {
                VkPipeline p = gx_pipe(key);
                if (p != g_bound) {
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
            vkCmdDrawIndexed(g_cmd, c->b, 1, c->a, 0, 0);
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
        draw_overlay_and_touch();
        shot_request();
        barrier(g_simg[g_swap_idx], VK_IMAGE_ASPECT_COLOR_BIT, &g_slayout[g_swap_idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
    VKC(vkEndCommandBuffer(g_cmd));
    VkPipelineStageFlags wst = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, presenting ? 1u : 0u, &g_acq[g_slot], &wst, 1, &g_cmd,
                       presenting ? 1u : 0u, presenting ? &g_sdone[g_swap_idx] : NULL};
    VKC(vkQueueSubmit(g_q, 1, &si, g_fence[g_slot]));
    g_fenced[g_slot] = 1;
    if (presenting) {
        uint32_t idx = (uint32_t)g_swap_idx;
        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &g_sdone[idx], 1, &g_swap, &idx, NULL};
        VkResult r = vkQueuePresentKHR(g_q, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) g_swap_bad = 1;
        g_slayout[idx] = VK_IMAGE_LAYOUT_UNDEFINED;  /* contents not kept between frames */
        g_swap_idx = -1;
    }
    if (g_shot_pending) {
        vkWaitForFences(g_dev, 1, &g_fence[g_slot], VK_TRUE, UINT64_MAX);
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
    uint64_t busy = platform_now_ns() - t_start;
    g_busy_ns += busy;
    g_draws_sum += draws;
    if (busy > 50000000ull) rt_log("[hitch] render thread: a frame took %.0f ms (%u draws)", busy / 1e6, draws);
    if (++g_busy_frames == 300) {
        rt_log("[vk] %.2f ms/frame on the render thread, %u draws/frame, %u pipelines, %u frames presented", g_busy_ns / 300 / 1e6,
               g_draws_sum / 300, g_pipes_total, g_presented);
        g_busy_ns = 0;
        g_draws_sum = 0;
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

static int render_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) rt_fatal("SDL video: %s", SDL_GetError());
    g_win = SDL_CreateWindow("Sonic Riders", 0, 0, 0, 0, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    if (!g_win) rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
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
    rt_thread_start(render_thread, NULL);
}
