/* Switch backend: deko3d, executing the frames built by gx_render.c on a render thread. Mirrors the
 * GL backend (platform/host/render_gl.c); the shaders are the same GLSL, compiled ahead of time with
 * uam (tools/embed_shaders.py). The EFB is an image of up to EFB_SCALE x 640x528 (the internal
 * resolution, settings.h, renders into its top-left 640x528 x g_s), stored top row first
 * like GX memory: with deko3d 0.5's upper-left origin, clip-space +y is the top row, so the shader
 * does not flip y (CLIP_Y) as it does for GL, and front faces keep the GX winding (CCW).
 *
 * Memory: a code block for the shaders; two frame slots (command memory, vertices, uniforms,
 * texture staging, XF memory chunks), used alternately; an image pool with a first-fit allocator; descriptor tables
 * for up to MAX_IMAGES textures and the 36 sampler variants (wrap S x wrap T x mag x min). */
#include <deko3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <switch.h>
#include <threads.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_shaders_dksh.h"  /* generated: tools/embed_shaders.py dksh */
#include "gx_spec_dksh.h"     /* generated: tools/gen_shaders.py (specialized fragment shaders) */
#include "gx_shader_key.h"
#include "overlay.h"
#include "runtime.h"
#include "settings.h"

void input_init(void);
void input_pump(void);

#define EFB_SCALE 2  /* the largest internal resolution; g_s is this frame's (1, 1.5 or 2) */
static float g_s = EFB_SCALE;
#define SC(v) ((uint32_t)((float)(v) * g_s + 0.5f))
/* the wide screen modes (gx_render.h GX_XR_WIDE_K): X is also scaled by this frame's k; the EFB is
 * allocated for 16:9 (the widest a Switch screen gets) */
#define WIDE_K_MAX (854.0f / 640.0f)
static float g_k = 1.0f;
#define XSC(v) ((uint32_t)((float)(v) * g_k * g_s + 0.5f))
#define EFB_W ((int)(EFB_WIDTH * WIDE_K_MAX + 0.5f) * EFB_SCALE)
#define EFB_H (EFB_HEIGHT * EFB_SCALE)
#define FB_W 1280
#define FB_H 720
#define MAX_IMAGES 4096
#define NUM_SAMPLERS 36

#define SLOT_CMD   (4u << 20)
#define SLOT_VTX   (48u << 20)
#define SLOT_UBO   (24u << 20)  /* ~14000 states (1792 bytes each) */
#define SLOT_STAGE (32u << 20)
#define SLOT_XFM   (8u << 20)   /* the frame's XF memory chunk pool (storage buffer; SR_GPU_AB's old shader only) */
#define SLOT_XFI   (8u << 20)   /* the states' XF memory images (uniform buffers, XF_IMAGE_BYTES each) */
#define SLOT_SIZE  (SLOT_CMD + SLOT_VTX + SLOT_UBO + SLOT_STAGE + SLOT_XFM + SLOT_XFI)
/* a state's XF memory as its vertex shader reads it (gx_common.glsl XF_UBO): chunks 0-3 (matrices), 16-17
 * (normal matrices), 20-25 (post matrices, lights) of GX_XFM_CHUNK words */
#define XF_IMAGE_CHUNKS 12
#define XF_IMAGE_BYTES (XF_IMAGE_CHUNKS * GX_XFM_CHUNK * 4)
static const uint8_t k_xf_image_chunks[XF_IMAGE_CHUNKS] = {0, 1, 2, 3, 16, 17, 20, 21, 22, 23, 24, 25};
#define IMAGE_POOL (384u << 20)

static DkDevice g_dev;
static DkQueue g_queue;
static DkCmdBuf g_cmd;
static DkMemBlock g_code, g_slots[2], g_images, g_desc;
static DkShader g_vsh, g_fsh, g_fsh_ind, g_ovs, g_ofs;  /* the GX ubershader (+ indirect variant); the overlay */
static DkShader g_vsh_ssbo, g_vsh_nolight, g_vsh_notexgen;
static DkShader g_qvs, g_q_bright, g_q_blur, g_q_mix, g_q_bgrade, g_q_grade, g_q_easu, g_q_rcas;  /* picture options (quads) */  /* the old vertex shader (XF memory from a storage buffer), for SR_GPU_AB */

/* SR_GPU_AB=1 (hardware testing): alternates every frame between the vertex shader reading XF memory
 * from uniform buffers (normal) and the old one reading it from a storage buffer, and logs the GPU
 * time of each every AB_LOG frames. Both draw the same picture. */
/* SR_GPU_AB=2: rotates every frame through the normal picture and the picture options one by one off (bloom,
 * colour grading, FSR, all three). SR_GPU_AB=3: the normal picture, the vertex shader without lighting, without
 * texgen matrices, with the tiled cache on, and without the picture options. What each costs in the same
 * views. Test only: the picture flickers. */
#define AB_MODES 5
#define AB_FRAMES 1
#define AB_LOG 600
static int g_ab_on = -1;  /* SR_GPU_AB's value: 0 off, 1 or 2 */
static int g_ab_count;    /* modes in rotation */
static unsigned g_ab_frame;
static int g_ab_mode;
static int g_ts_mode[2];
#define AB_SSBO (g_ab_on == 1 && g_ab_mode == 1)
#define AB_TILED (g_ab_on == 3 && g_ab_mode == 3)
#define AB_OLDPP (g_ab_on == 4 && g_ab_mode == 1)  /* SR_GPU_AB=4: the picture options' passes as before 2026-11-13 */
#define AB_NOPOST (g_ab_mode == 4 && (g_ab_on == 2 || g_ab_on == 3))
#define AB_NOBLOOM (AB_NOPOST || (g_ab_on == 2 && g_ab_mode == 1))
#define AB_NOGRADE (AB_NOPOST || (g_ab_on == 2 && g_ab_mode == 2))
#define AB_NOFSR (AB_NOPOST || (g_ab_on == 2 && g_ab_mode == 3))
static unsigned g_ab_posts;  /* post_process runs, for the [gpu-ab] lines */
static const DkShader* ab_vsh(void) {
    if (AB_SSBO) return &g_vsh_ssbo;
    if (g_ab_on == 3 && g_ab_mode == 1) return &g_vsh_nolight;
    if (g_ab_on == 3 && g_ab_mode == 2) return &g_vsh_notexgen;
    return &g_vsh;
}
static double g_ab_ms[AB_MODES], g_ab_vs[AB_MODES], g_ab_fs[AB_MODES], g_ab_pr[AB_MODES];
static unsigned g_ab_n[AB_MODES];
static const char* const k_ab_names1[2] = {"XF in uniform buffers (new)", "XF in a storage buffer (old)"};
static const char* const k_ab_names2[AB_MODES] = {"normal", "no bloom", "no colour grading", "no FSR", "picture options off"};
static const char* const k_ab_names4[2] = {"fewer passes (new)", "old passes"};
static const char* const k_ab_names3[AB_MODES] = {"normal", "vertex shader without lighting", "vertex shader without texgen",
                                                  "tiled cache on", "picture options off"};
static void pt_mark(int label);
/* a barrier; with the tiled cache on (SR_GPU_AB=2's test mode) binned primitives are flushed first */
static void bar(DkBarrier mode, uint32_t inv) {
    if (AB_TILED) dkCmdBufTiledCacheOp(g_cmd, DkTiledCacheOp_Flush);
    dkCmdBufBarrier(g_cmd, mode, inv);
}
static const DkShader* g_fsh_bound;  /* the fragment shader bound in this frame's command list */
static DkShader g_spec[GX_SPEC_COUNT > 0 ? GX_SPEC_COUNT : 1];  /* specialized fragment shaders */
static int g_spec_on = 1;              /* SR_NOSPEC=1: the ubershader only */
static unsigned long long g_spec_hits, g_spec_misses;  /* states, for the log */

/* the fragment shader for a state: its specialized shader if the table has the key, else the
 * ubershader (with indirect texturing only when the state uses it); a missing key is logged to the
 * SD card for the next build's table */
static const DkShader* frag_shader_for(const GxUniforms* u) {
    int ind = ((u->bp[0x00] >> 16) & 7) != 0;
    const DkShader* uber = ind ? &g_fsh_ind : &g_fsh;
    if (!g_spec_on) return uber;
    GxFragKey k;
    gx_frag_key(u->bp, &k);
    int lo = 0, hi = GX_SPEC_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (k_spec_keys[mid] == k.hash) { g_spec_hits++; return &g_spec[mid]; }
        if (k_spec_keys[mid] < k.hash) lo = mid + 1; else hi = mid - 1;
    }
    g_spec_misses++;
    gx_frag_key_record(&k, "sdmc:/switch/sonicriders/shader_keys.txt");
    return uber;
}
#define OVERLAY_SLOT 1                         /* image descriptor of the overlay image */
static DkImage g_ov_image;
static unsigned g_ov_version = ~0u;
static DkSwapchain g_swap;
static DkImage g_fb[2], g_efb, g_efb_depth;
static DkImageView g_efb_view, g_efb_depth_view;
static unsigned g_slot;
static DkFence g_slot_fence[2];      /* signalled when the GPU has finished a slot's frame */
static int g_slot_fenced[2];
static int* g_retired[2];            /* texture ids retired by a slot's frame: freed after its fence */
static uint32_t g_nretired[2];
static uint32_t g_vtx_off, g_stage_off;
static uint64_t g_busy_ticks, g_gpu_ticks, g_upload_bytes;  /* for the [deko3d] timing line */
static uint64_t g_last_gpu;      /* this frame's GPU wait ([hitch]) */
static uint32_t g_last_upload;   /* this frame's texture upload bytes ([hitch]) */
static unsigned g_busy_frames;

/* GPU time per frame: two timestamps around each slot's commands (DkCounter_Timestamp is written when
 * all earlier work is done, so the first one marks the end of the previous frame or of idle time) */
static DkMemBlock g_ts;              /* per slot: 4 counters x (start, end) reports of 16 bytes (value, timestamp) */
static const DkCounter k_counters[4] = {DkCounter_Timestamp, DkCounter_VertexShaderInvocations,
                                        DkCounter_FragmentShaderInvocations, DkCounter_InputPrimitives};
static double g_cnt_sum[3];          /* vertex shader runs, fragment shader runs, primitives (for the log) */
static int g_ts_valid[2];
/* GPU timestamps between the picture options' passes (pt_mark), from 1024 + slot * 1024 in g_ts; each mark's
 * time since the previous one is charged to its label, for the [deko3d] / [gpu-ab] lines */
enum { PT_START, PT_BRIGHT, PT_BLUR, PT_MIX, PT_GRADE, PT_FSR0, PT_FSR_COPY, PT_FSR_END, PT_N };
#define PT_MAX 32
static const char* const k_pt_names[PT_N] = {"", "bright pass", "blur", "bloom mix", "grading (with its copy)", "",
                                             "FSR copy/EASU", "FSR sharpen"};
static uint8_t g_pt_lab[2][PT_MAX];
static unsigned g_pt_n[2];
static double g_pt_ms[5][PT_N];  /* per SR_GPU_AB mode */
static unsigned g_pt_frames[5];
static void pt_mark(int label) {
    unsigned n = g_pt_n[g_slot];
    if (n >= PT_MAX) return;
    dkCmdBufReportCounter(g_cmd, DkCounter_Timestamp, dkMemBlockGetGpuAddr(g_ts) + 1024 + g_slot * 1024 + n * 16);
    g_pt_lab[g_slot][n] = (uint8_t)label;
    g_pt_n[g_slot] = n + 1;
}
/* the picture options' GPU ms per frame since the last call, of SR_GPU_AB mode m (0 when off) */
static void pt_log(int m, const char* what) {
    if (!g_pt_frames[m]) return;
    double n = g_pt_frames[m], sum = 0;
    char line[320];
    int k = snprintf(line, sizeof line, "[deko3d]   picture options%s%s, GPU ms/frame:", *what ? " " : "", what);
    for (int l = 0; l < PT_N; l++)
        if (k_pt_names[l][0] && g_pt_ms[m][l] > 0 && k < (int)sizeof line - 40) {
            k += snprintf(line + k, sizeof line - k, " %s %.2f,", k_pt_names[l], g_pt_ms[m][l] / n);
            sum += g_pt_ms[m][l] / n;
        }
    snprintf(line + k, sizeof line - k, " total %.2f", sum);
    rt_log("%s", line);
    memset(g_pt_ms[m], 0, sizeof g_pt_ms[m]);
    g_pt_frames[m] = 0;
}
static uint64_t g_gpu_frame_ns;      /* the last measured frame */
static uint64_t g_gpu_ns_sum, g_gpu_ns_max;
static unsigned g_gpu_n;
static float g_s_sum;

static volatile double g_gpu_ms_avg = -1.0;  /* smoothed, for frame_pacing.c */
double platform_gpu_frame_ms(void) { return g_gpu_ms_avg; }


/* ---- image pool: first-fit free list ---- */
typedef struct Block { uint32_t off, size; struct Block* next; } Block;
static Block* g_free_list;

static uint32_t pool_alloc(uint32_t size, uint32_t align) {
    for (Block** pp = &g_free_list; *pp; pp = &(*pp)->next) {
        Block* b = *pp;
        uint32_t start = (b->off + align - 1) & ~(align - 1);
        if (start + size > b->off + b->size) continue;
        uint32_t end = start + size, b_end = b->off + b->size;
        if (start > b->off) {  /* keep the gap before */
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
    /* merge with the next and previous blocks */
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

/* ---- textures ---- */
typedef struct DkTex {
    int live;
    DkImage image;
    uint32_t off, size, slot, gen, w, h;
} DkTex;
static DkTex* g_tex;
static int g_tex_cap;
static uint32_t g_slot_free[MAX_IMAGES];
static int g_nslot_free;

static DkImageDescriptor* descriptors(void) { return (DkImageDescriptor*)dkMemBlockGetCpuAddr(g_desc); }

static void make_image(DkImage* img, uint32_t* off, uint32_t* size, uint32_t w, uint32_t h, uint32_t flags,
                       DkImageFormat fmt) {
    DkImageLayoutMaker lm;
    dkImageLayoutMakerDefaults(&lm, g_dev);
    lm.flags = flags;
    lm.format = fmt;
    lm.dimensions[0] = w;
    lm.dimensions[1] = h;
    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &lm);
    *size = (uint32_t)dkImageLayoutGetSize(&layout);
    *off = pool_alloc(*size, dkImageLayoutGetAlignment(&layout));
    dkImageInitialize(img, &layout, g_images, *off);
}

static DkTex* tex_slot(int id) {
    if (id >= g_tex_cap) {
        int n = g_tex_cap ? g_tex_cap : 1024;
        while (n <= id) n *= 2;
        g_tex = realloc(g_tex, n * sizeof *g_tex);
        memset(g_tex + g_tex_cap, 0, (n - g_tex_cap) * sizeof *g_tex);
        g_tex_cap = n;
    }
    return &g_tex[id];
}

static void tex_create(DkTex* t, uint32_t w, uint32_t h, int render_target) {
    if (!g_nslot_free) rt_fatal("out of texture descriptors");
    t->slot = g_slot_free[--g_nslot_free];
    t->w = w; t->h = h;
    make_image(&t->image, &t->off, &t->size, w, h,
               render_target ? (DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine) : DkImageFlags_Usage2DEngine,
               DkImageFormat_RGBA8_Unorm);
    DkImageView v;
    dkImageViewDefaults(&v, &t->image);
    dkImageDescriptorInitialize(&descriptors()[t->slot], &v, false, false);
    t->live = 1;
    t->gen = 0xFFFFFFFFu;
}

static void tex_destroy(int id) {
    if (id >= g_tex_cap || !g_tex[id].live) return;
    DkTex* t = &g_tex[id];
    pool_free(t->off, t->size);
    g_slot_free[g_nslot_free++] = t->slot;
    t->live = 0;
}

/* make sure texture `id` exists and holds its current pixels (uploads go through the stage area) */
static int tex_prepare(int id) {
    GxTexture* gt = gx_texture_get(id);
    if (!gt) return -1;
    DkTex* t = tex_slot(id);
    /* copy textures at the chosen resolution, whatever this frame's scale: the copy is scaled into them */
    float cs = settings_internal_scale();
    uint32_t w = gt->is_copy ? (uint32_t)(gt->width * cs + 0.5f) : gt->width;
    uint32_t h = gt->is_copy ? (uint32_t)(gt->height * cs + 0.5f) : gt->height;
    if (!t->live) tex_create(t, w, h, gt->is_copy);
    if (!gt->is_copy && t->gen != gt->gen) {
        uint32_t bytes = gt->width * gt->height * 4;
        if (g_stage_off + bytes > SLOT_STAGE) return (int)t->slot;  /* staging full this frame: upload next */
        uint8_t* base = (uint8_t*)dkMemBlockGetCpuAddr(g_slots[g_slot]) + SLOT_CMD + SLOT_VTX + SLOT_UBO;
        memcpy(base + g_stage_off, gt->rgba, bytes);
        DkCopyBuf src = {dkMemBlockGetGpuAddr(g_slots[g_slot]) + SLOT_CMD + SLOT_VTX + SLOT_UBO + g_stage_off, 0, 0};
        DkImageView v;
        dkImageViewDefaults(&v, &t->image);
        DkImageRect r = {0, 0, 0, gt->width, gt->height, 1};
        dkCmdBufCopyBufferToImage(g_cmd, &src, &v, &r, 0);
        g_stage_off = (g_stage_off + bytes + 255) & ~255u;
        g_upload_bytes += bytes;
        g_last_upload += bytes;
        t->gen = gt->gen;
    }
    return (int)t->slot;
}

/* ---- setup ---- */
static void shader_load(DkShader* s, const uint8_t* code, uint32_t size, uint32_t* off) {
    memcpy((uint8_t*)dkMemBlockGetCpuAddr(g_code) + *off, code, size);
    DkShaderMaker sm;
    dkShaderMakerDefaults(&sm, g_code, *off);
    dkShaderInitialize(s, &sm);
    *off = (*off + size + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
}

static DkMemBlock block(uint32_t size, uint32_t flags) {
    DkMemBlockMaker m;
    dkMemBlockMakerDefaults(&m, g_dev, (size + DK_MEMBLOCK_ALIGNMENT - 1) & ~(DK_MEMBLOCK_ALIGNMENT - 1));
    m.flags = flags;
    DkMemBlock b = dkMemBlockCreate(&m);
    if (!b) rt_fatal("dkMemBlockCreate(%u) failed", size);
    return b;
}

static const DkWrapMode k_wrap[3] = {DkWrapMode_ClampToEdge, DkWrapMode_Repeat, DkWrapMode_MirroredRepeat};

static void dk_setup(void) {
    DkDeviceMaker dm;
    dkDeviceMakerDefaults(&dm);
    dm.flags = DkDeviceFlags_DepthMinusOneToOne | DkDeviceFlags_OriginUpperLeft;  /* same clip depth as GL */
    g_dev = dkDeviceCreate(&dm);
    DkQueueMaker qm;
    dkQueueMakerDefaults(&qm, g_dev);
    qm.flags = DkQueueFlags_Graphics;
    g_queue = dkQueueCreate(&qm);
    DkCmdBufMaker cm;
    dkCmdBufMakerDefaults(&cm, g_dev);
    g_cmd = dkCmdBufCreate(&cm);

    g_code = block(sizeof k_gx_vert_dksh + sizeof k_gx_frag_dksh + sizeof k_gx_frag_ind_dksh + sizeof k_overlay_vert_dksh +
                       sizeof k_overlay_frag_dksh + sizeof k_gx_vert_ssbo_dksh + sizeof k_gx_vert_nolight_dksh + sizeof k_gx_vert_notexgen_dksh + 8 * DK_SHADER_CODE_ALIGNMENT + sizeof k_spec_blob + DK_SHADER_CODE_ALIGNMENT +
                       sizeof k_dk_quad_vert_dksh + sizeof k_dk_bright_frag_dksh + sizeof k_dk_blur_frag_dksh + sizeof k_dk_grade_frag_dksh +
                       sizeof k_dk_easu_frag_dksh + sizeof k_dk_rcas_frag_dksh + sizeof k_dk_bloom_mix_frag_dksh + sizeof k_dk_bloom_grade_frag_dksh + 8 * DK_SHADER_CODE_ALIGNMENT,
                   DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    uint32_t off = 0;
    shader_load(&g_vsh, k_gx_vert_dksh, sizeof k_gx_vert_dksh, &off);
    shader_load(&g_fsh, k_gx_frag_dksh, sizeof k_gx_frag_dksh, &off);
    shader_load(&g_fsh_ind, k_gx_frag_ind_dksh, sizeof k_gx_frag_ind_dksh, &off);
    shader_load(&g_ovs, k_overlay_vert_dksh, sizeof k_overlay_vert_dksh, &off);
    shader_load(&g_ofs, k_overlay_frag_dksh, sizeof k_overlay_frag_dksh, &off);
    shader_load(&g_vsh_ssbo, k_gx_vert_ssbo_dksh, sizeof k_gx_vert_ssbo_dksh, &off);
    shader_load(&g_vsh_nolight, k_gx_vert_nolight_dksh, sizeof k_gx_vert_nolight_dksh, &off);
    shader_load(&g_vsh_notexgen, k_gx_vert_notexgen_dksh, sizeof k_gx_vert_notexgen_dksh, &off);
    shader_load(&g_qvs, k_dk_quad_vert_dksh, sizeof k_dk_quad_vert_dksh, &off);  /* picture options */
    shader_load(&g_q_bright, k_dk_bright_frag_dksh, sizeof k_dk_bright_frag_dksh, &off);
    shader_load(&g_q_blur, k_dk_blur_frag_dksh, sizeof k_dk_blur_frag_dksh, &off);
    shader_load(&g_q_mix, k_dk_bloom_mix_frag_dksh, sizeof k_dk_bloom_mix_frag_dksh, &off);
    shader_load(&g_q_bgrade, k_dk_bloom_grade_frag_dksh, sizeof k_dk_bloom_grade_frag_dksh, &off);
    shader_load(&g_q_grade, k_dk_grade_frag_dksh, sizeof k_dk_grade_frag_dksh, &off);
    shader_load(&g_q_easu, k_dk_easu_frag_dksh, sizeof k_dk_easu_frag_dksh, &off);
    shader_load(&g_q_rcas, k_dk_rcas_frag_dksh, sizeof k_dk_rcas_frag_dksh, &off);
    for (int i = 0; i < GX_SPEC_COUNT; i++) shader_load(&g_spec[i], k_spec_blob + k_spec_off[i], k_spec_size[i], &off);
    if (getenv("SR_NOSPEC")) g_spec_on = 0;
    rt_log("[deko3d] %d specialized fragment shaders%s", GX_SPEC_COUNT, g_spec_on ? "" : " (off: SR_NOSPEC)");

    for (int i = 0; i < 2; i++) g_slots[i] = block(SLOT_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    g_ts = block(4096, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    g_images = block(IMAGE_POOL, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    g_free_list = malloc(sizeof *g_free_list);
    g_free_list->off = 0; g_free_list->size = IMAGE_POOL; g_free_list->next = NULL;
    g_desc = block(MAX_IMAGES * sizeof(DkImageDescriptor) + NUM_SAMPLERS * sizeof(DkSamplerDescriptor),
                   DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    /* slot 0: the dummy texture, OVERLAY_SLOT: the overlay */
    for (uint32_t i = MAX_IMAGES - 1; i > OVERLAY_SLOT; i--) g_slot_free[g_nslot_free++] = i;

    /* samplers */
    DkSamplerDescriptor* sd = (DkSamplerDescriptor*)((uint8_t*)dkMemBlockGetCpuAddr(g_desc) + MAX_IMAGES * sizeof(DkImageDescriptor));
    for (int ws = 0; ws < 3; ws++)
        for (int wt = 0; wt < 3; wt++)
            for (int mag = 0; mag < 2; mag++)
                for (int min = 0; min < 2; min++) {
                    DkSampler s;
                    dkSamplerDefaults(&s);
                    s.wrapMode[0] = k_wrap[ws]; s.wrapMode[1] = k_wrap[wt]; s.wrapMode[2] = DkWrapMode_ClampToEdge;
                    s.magFilter = mag ? DkFilter_Linear : DkFilter_Nearest;
                    s.minFilter = min ? DkFilter_Linear : DkFilter_Nearest;
                    s.mipFilter = DkMipFilter_None;
                    dkSamplerDescriptorInitialize(&sd[ws * 12 + wt * 4 + mag * 2 + min], &s);
                }

    /* EFB, its depth buffer, the swapchain images */
    uint32_t o, sz;
    make_image(&g_efb, &o, &sz, EFB_W, EFB_H, DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine, DkImageFormat_RGBA8_Unorm);
    make_image(&g_efb_depth, &o, &sz, EFB_W, EFB_H, DkImageFlags_UsageRender | DkImageFlags_HwCompression, DkImageFormat_Z24S8);
    dkImageViewDefaults(&g_efb_view, &g_efb);
    dkImageViewDefaults(&g_efb_depth_view, &g_efb_depth);
    DkImage const* fbs[2];
    for (int i = 0; i < 2; i++) {
        make_image(&g_fb[i], &o, &sz, FB_W, FB_H,
                   DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_Usage2DEngine,
                   DkImageFormat_RGBA8_Unorm);
        fbs[i] = &g_fb[i];
    }
    DkSwapchainMaker sm;
    dkSwapchainMakerDefaults(&sm, g_dev, nwindowGetDefault(), fbs, 2);
    g_swap = dkSwapchainCreate(&sm);

    /* the overlay image */
    {
        uint32_t o2, sz2;
        make_image(&g_ov_image, &o2, &sz2, OVERLAY_W, OVERLAY_H, DkImageFlags_Usage2DEngine, DkImageFormat_RGBA8_Unorm);
        DkImageView ov;
        dkImageViewDefaults(&ov, &g_ov_image);
        dkImageDescriptorInitialize(&descriptors()[OVERLAY_SLOT], &ov, false, false);
    }

    /* the 1x1 white dummy texture in descriptor slot 0 */
    DkTex dummy;
    memset(&dummy, 0, sizeof dummy);
    make_image(&dummy.image, &dummy.off, &dummy.size, 1, 1, DkImageFlags_Usage2DEngine, DkImageFormat_RGBA8_Unorm);
    DkImageView dv;
    dkImageViewDefaults(&dv, &dummy.image);
    dkImageDescriptorInitialize(&descriptors()[0], &dv, false, false);
    uint32_t white = 0xFFFFFFFFu;
    memcpy(dkMemBlockGetCpuAddr(g_slots[0]), &white, 4);
    dkCmdBufAddMemory(g_cmd, g_slots[0], 0x1000, 0x10000);
    DkCopyBuf src = {dkMemBlockGetGpuAddr(g_slots[0]), 0, 0};
    DkImageRect r = {0, 0, 0, 1, 1, 1};
    dkCmdBufCopyBufferToImage(g_cmd, &src, &dv, &r, 0);
    /* the EFB starts black with depth at the far plane (pool memory is not cleared) */
    DkImageView const* efb_targets[1] = {&g_efb_view};
    dkCmdBufBindRenderTargets(g_cmd, efb_targets, 1, &g_efb_depth_view);
    DkScissor full = {0, 0, EFB_W, EFB_H};
    dkCmdBufSetScissors(g_cmd, 0, &full, 1);
    float black[4] = {0, 0, 0, 1};
    dkCmdBufClearColor(g_cmd, 0, DkColorMask_RGBA, black);
    dkCmdBufClearDepthStencil(g_cmd, true, 1.0f, 0, 0);
    dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(g_cmd));
    dkQueueWaitIdle(g_queue);
    dkCmdBufClear(g_cmd);
    rt_log("[deko3d] ready: EFB %dx%d, image pool %u MiB", EFB_W, EFB_H, IMAGE_POOL >> 20);
}

/* ---- per-draw state ---- */
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* the fixed-function state last emitted by apply_state: draws mostly change only uniforms, so state
 * that is unchanged is not emitted again (render-thread CPU and GPU command processing). Anything
 * else that binds state (frame start, EFB copies, the overlay) clears g_st_valid. */
static struct {
    DkViewport vp;
    DkScissor sc;
    DkDepthStencilState ds;
    DkColorState cs;
    DkBlendState bs;
    DkColorWriteState cw;
    DkRasterizerState rs;
} g_st;
static int g_st_valid;
#define ST_CHANGED(field, val) (!g_st_valid || memcmp(&g_st.field, &(val), sizeof(val)) != 0 ? (g_st.field = (val), 1) : 0)

static void apply_state(const GxUniforms* u) {
    float xs = f32(u->xfr[0x1A]), ys = f32(u->xfr[0x1B]), xo = f32(u->xfr[0x1D]), yo = f32(u->xfr[0x1E]);
    int sox = (int)(u->bp[0x59] & 0x3FF) * 2, soy = (int)((u->bp[0x59] >> 10) & 0x3FF) * 2;
    float ax = xs < 0 ? -xs : xs, ay = ys < 0 ? -ys : ys;
    DkViewport vp = {(xo - ax - (float)sox) * g_k * g_s, (yo - ay - (float)soy) * g_s, 2 * ax * g_k * g_s, 2 * ay * g_s, 0.0f, 1.0f};
    if (ST_CHANGED(vp, vp)) dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    DkScissor sc = {XSC(x0), SC(y0), x1 > x0 ? XSC(x1) - XSC(x0) : 0, SC(y1 > y0 ? y1 - y0 : 0)};
    if (ST_CHANGED(sc, sc)) dkCmdBufSetScissors(g_cmd, 0, &sc, 1);

    static const DkCompareOp zf[8] = {DkCompareOp_Never, DkCompareOp_Less, DkCompareOp_Equal, DkCompareOp_Lequal,
                                      DkCompareOp_Greater, DkCompareOp_NotEqual, DkCompareOp_Gequal, DkCompareOp_Always};
    uint32_t z = u->bp[0x40];
    DkDepthStencilState ds;
    memset(&ds, 0, sizeof ds);  /* padding too: the struct is compared with memcmp */
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = z & 1;
    ds.depthWriteEnable = (z >> 4) & 1;
    ds.depthCompareOp = zf[(z >> 1) & 7];
    if (ST_CHANGED(ds, ds)) dkCmdBufBindDepthStencilState(g_cmd, &ds);

    uint32_t cm = u->bp[0x41];
    /* source alpha = the shader's second output (the TEV alpha; the first carries the stored alpha,
     * which BP 0x42 can make a constant); destination alpha reads 1 when the EFB format has none */
    int efb_alpha = (u->bp[0x43] & 7) == 1;  /* RGBA6_Z24 */
    const DkBlendFactor da = efb_alpha ? DkBlendFactor_DstAlpha : DkBlendFactor_One;
    const DkBlendFactor ida = efb_alpha ? DkBlendFactor_InvDstAlpha : DkBlendFactor_Zero;
    const DkBlendFactor sf[8] = {DkBlendFactor_Zero, DkBlendFactor_One, DkBlendFactor_DstColor, DkBlendFactor_InvDstColor,
                                 DkBlendFactor_Src1Alpha, DkBlendFactor_InvSrc1Alpha, da, ida};
    const DkBlendFactor df[8] = {DkBlendFactor_Zero, DkBlendFactor_One, DkBlendFactor_SrcColor, DkBlendFactor_InvSrcColor,
                                 DkBlendFactor_Src1Alpha, DkBlendFactor_InvSrc1Alpha, da, ida};
    int const_alpha = (u->bp[0x42] >> 8) & 1;
    DkColorState cs;
    memset(&cs, 0, sizeof cs);
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, cm & 1);
    if (ST_CHANGED(cs, cs)) dkCmdBufBindColorState(g_cmd, &cs);
    DkBlendState bs;
    memset(&bs, 0, sizeof bs);
    dkBlendStateDefaults(&bs);
    if ((cm >> 11) & 1) {
        dkBlendStateSetOps(&bs, DkBlendOp_RevSub, DkBlendOp_RevSub);
        dkBlendStateSetFactors(&bs, DkBlendFactor_One, DkBlendFactor_One, DkBlendFactor_One, DkBlendFactor_One);
    } else {
        dkBlendStateSetOps(&bs, DkBlendOp_Add, DkBlendOp_Add);
        DkBlendFactor s = sf[(cm >> 8) & 7], d = df[(cm >> 5) & 7];
        dkBlendStateSetFactors(&bs, s, d, const_alpha ? DkBlendFactor_One : s, const_alpha ? DkBlendFactor_Zero : d);
    }
    if (ST_CHANGED(bs, bs)) dkCmdBufBindBlendStates(g_cmd, 0, &bs, 1);
    DkColorWriteState cw;
    memset(&cw, 0, sizeof cw);
    dkColorWriteStateDefaults(&cw);
    dkColorWriteStateSetMask(&cw, 0, ((cm >> 3) & 1 ? DkColorMask_RGB : 0) | ((cm >> 4) & 1 ? DkColorMask_A : 0));
    if (ST_CHANGED(cw, cw)) dkCmdBufBindColorWriteState(g_cmd, &cw);

    DkRasterizerState rs;
    memset(&rs, 0, sizeof rs);
    dkRasterizerStateDefaults(&rs);
    uint32_t cull = (u->bp[0x00] >> 14) & 3;
    rs.cullMode = cull == 0 ? DkFace_None : cull == 1 ? DkFace_Back : cull == 2 ? DkFace_Front : DkFace_FrontAndBack;
    rs.frontFace = DkFrontFace_CW;  /* GX front faces are clockwise on screen (upper-left origin, no flip here) */
    if (ST_CHANGED(rs, rs)) dkCmdBufBindRasterizerState(g_cmd, &rs);
    g_st_valid = 1;
}

static void bind_textures(const GxCmd* c, const GxUniforms* u) {
    DkResHandle h[8];
    for (int m = 0; m < 8; m++) {
        int slot = c->tex[m] >= 0 ? (int)tex_slot(c->tex[m])->slot : 0;
        if (c->tex[m] < 0 || !tex_slot(c->tex[m])->live) slot = 0;
        uint32_t mode0 = u->bp[(m >= 4 ? 0xA0 : 0x80) + (m & 3)];
        uint32_t ws = mode0 & 3, wt = (mode0 >> 2) & 3;
        if (ws > 2) ws = 1;
        if (wt > 2) wt = 1;
        uint32_t samp = ws * 12 + wt * 4 + ((mode0 >> 4) & 1) * 2 + (((mode0 >> 5) & 7) >= 4 ? 1 : 0);
        h[m] = dkMakeTextureHandle((uint32_t)slot, samp);
    }
    dkCmdBufBindTextures(g_cmd, DkStage_Fragment, 0, h, 8);
}


/* ---- picture options: bloom, colour grading, FSR 1 (as render_vk.c / render_gl.c;
 * docs/notes/picture-options-port.md). The passes are screen quads with the Vulkan quad shaders built for
 * deko3d (QUAD_DK: parameters in uniform buffer 3, beside the GX shaders' 0-2). Images are made on first use
 * from the image pool, at fixed sizes (the largest EFB, the 1280x720 screen). After a pass the GX state is bound
 * again (gx_rebind). */
#define BL_W(l) ((uint32_t)(EFB_WIDTH * WIDE_K_MAX + 0.5f) / (4u << (l)) + 1)
#define BL_H(l) ((uint32_t)EFB_HEIGHT / (4u << (l)) + 1)
#define SAMP_NEAR 0  /* clamp, nearest (sampler index: wrap S * 12 + wrap T * 4 + mag * 2 + min) */
#define SAMP_LIN 3   /* clamp, linear */
typedef struct PpImg { DkImage image; DkImageView view; uint32_t off, size, slot, w, h; int live; } PpImg;
static PpImg g_pp_efb, g_pp_scene, g_pp_bl[4], g_pp_fsr;  /* g_pp_efb: a descriptor for sampling the EFB */
static uint8_t* g_fr_cpu;
static DkGpuAddr g_fr_gpu, g_fr_vb, g_fr_ib;
static uint32_t g_fr_vbytes;
static int g_post_logged, g_fsr_logged;
enum { PB_OPAQUE, PB_SCREEN };

static uint32_t desc_slot(void) {
    if (!g_nslot_free) rt_fatal("out of texture descriptors");
    return g_slot_free[--g_nslot_free];
}

/* a descriptor for an existing image (the EFB), or a new pool image of w x h */
static void pp_img(PpImg* p, uint32_t w, uint32_t h, int usage2d) {
    if (p->live) return;
    make_image(&p->image, &p->off, &p->size, w, h, DkImageFlags_UsageRender | (usage2d ? DkImageFlags_Usage2DEngine : 0),
               DkImageFormat_RGBA8_Unorm);
    dkImageViewDefaults(&p->view, &p->image);
    p->slot = desc_slot();
    dkImageDescriptorInitialize(&descriptors()[p->slot], &p->view, false, false);
    bar(DkBarrier_None, DkInvalidateFlags_Descriptors);
    p->w = w, p->h = h;
    p->live = 1;
}
static void pp_efb_desc(void) {
    if (g_pp_efb.live) return;
    g_pp_efb.view = g_efb_view;
    g_pp_efb.slot = desc_slot();
    g_pp_efb.w = EFB_W, g_pp_efb.h = EFB_H;
    dkImageDescriptorInitialize(&descriptors()[g_pp_efb.slot], &g_efb_view, false, false);
    bar(DkBarrier_None, DkInvalidateFlags_Descriptors);
    g_pp_efb.live = 1;
}

static int grade_wanted(void) {
    return g_settings.grade_bright != 0 || g_settings.grade_contrast != 100 || g_settings.grade_sat != 100 ||
           g_settings.grade_warm != 0;
}
static int post_wanted(void) { return g_settings.bloom > 0 || grade_wanted(); }

/* render into this image (no depth) */
static void pp_target(const DkImageView* v) {
    DkImageView const* t[1] = {v};
    dkCmdBufBindRenderTargets(g_cmd, t, 1, NULL);
}

/* two more vec4s for the next pp_quad's fragment shader (ext0, ext1: vk_bloom_grade.frag), then cleared */
static const float* g_pp_ext;
/* one screen quad with fragment shader fs into the bound target of w x h; rect in its pixels (vk_quad.vert) */
static void pp_quad(const DkShader* fs, const PpImg* tex, uint32_t samp, int blend, uint32_t mask, float w, float h,
                    const float px[4], const float uv[4], const float color[4], const float shape[4]) {
    if (g_stage_off + 256 > SLOT_STAGE) return;
    uint32_t so = SLOT_CMD + SLOT_VTX + SLOT_UBO + g_stage_off;
    float pc[28] = {px[0] / w * 2 - 1, px[1] / h * 2 - 1, px[2] / w * 2 - 1, px[3] / h * 2 - 1};
    uint32_t pcsize = g_pp_ext ? 28 * 4 : 20 * 4;
    if (g_pp_ext) memcpy(pc + 20, g_pp_ext, 32);
    g_pp_ext = NULL;
    static const float uv01[4] = {0, 0, 1, 1}, white[4] = {1, 1, 1, 1}, zero[4] = {0, 0, 0, 0}, ident[4] = {1, 0, 0, 1};
    memcpy(pc + 4, uv ? uv : uv01, 16);
    memcpy(pc + 8, color ? color : white, 16);
    memcpy(pc + 12, shape ? shape : zero, 16);
    memcpy(pc + 16, ident, 16);
    memcpy(g_fr_cpu + so, pc, pcsize);
    g_stage_off += 256;
    DkShader const* sh[2] = {&g_qvs, fs};
    dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, sh, 2);
    dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 3, g_fr_gpu + so, pcsize);
    dkCmdBufBindUniformBuffer(g_cmd, DkStage_Fragment, 3, g_fr_gpu + so, pcsize);
    dkCmdBufBindVtxAttribState(g_cmd, NULL, 0);
    dkCmdBufBindVtxBufferState(g_cmd, NULL, 0);
    DkViewport vp = {0, 0, w, h, 0.0f, 1.0f};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    DkScissor sc = {0, 0, (uint32_t)w, (uint32_t)h};
    dkCmdBufSetScissors(g_cmd, 0, &sc, 1);
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(g_cmd, &rs);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(g_cmd, &ds);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, blend == PB_SCREEN);
    dkCmdBufBindColorState(g_cmd, &cs);
    if (blend == PB_SCREEN) {  /* 1 - (1 - dst) * (1 - src) */
        DkBlendState bs;
        dkBlendStateDefaults(&bs);
        dkBlendStateSetOps(&bs, DkBlendOp_Add, DkBlendOp_Add);
        dkBlendStateSetFactors(&bs, DkBlendFactor_InvDstColor, DkBlendFactor_One, DkBlendFactor_Zero, DkBlendFactor_One);
        dkCmdBufBindBlendStates(g_cmd, 0, &bs, 1);
    }
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkColorWriteStateSetMask(&cw, 0, mask);
    dkCmdBufBindColorWriteState(g_cmd, &cw);
    dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 0, dkMakeTextureHandle(tex->slot, samp));
    dkCmdBufDraw(g_cmd, DkPrimitive_TriangleStrip, 4, 1, 0, 0);
    g_st_valid = 0;
}

/* the GX draw setup again after a pass (execute binds it once per frame) */
static void gx_rebind(const GxUniforms* cur) {
    DkImageView const* targets[1] = {&g_efb_view};
    dkCmdBufBindRenderTargets(g_cmd, targets, 1, &g_efb_depth_view);
    DkShader const* shaders[2] = {ab_vsh(), g_fsh_bound};
    dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, shaders, 2);
    static const uint32_t offs[10] = {
        offsetof(GxVertex, pos), offsetof(GxVertex, nrm), offsetof(GxVertex, col[0]), offsetof(GxVertex, col[1]),
        offsetof(GxVertex, tex[0]), offsetof(GxVertex, tex[2]), offsetof(GxVertex, tex[4]), offsetof(GxVertex, tex[6]),
        offsetof(GxVertex, tmtx[0]), offsetof(GxVertex, tmtx[4])};
    DkVtxAttribState attrs[10];
    for (int i = 0; i < 10; i++) {
        memset(&attrs[i], 0, sizeof attrs[i]);
        attrs[i].bufferId = 0;
        attrs[i].offset = offs[i];
        attrs[i].size = DkVtxAttribSize_4x32;
        attrs[i].type = DkVtxAttribType_Float;
    }
    dkCmdBufBindVtxAttribState(g_cmd, attrs, 10);
    DkVtxBufferState vb = {sizeof(GxVertex), 0};
    dkCmdBufBindVtxBufferState(g_cmd, &vb, 1);
    dkCmdBufBindVtxBuffer(g_cmd, 0, g_fr_vb, g_fr_vbytes ? g_fr_vbytes : 16);
    dkCmdBufBindIdxBuffer(g_cmd, DkIdxFormat_Uint32, g_fr_ib);
    g_st_valid = 0;
    if (cur) apply_state(cur);
}

/* one blur pass, src's top-left sw x sh into dst's dw x dh; dir in source texels (0: a linear copy) */
static void blur_pass(const PpImg* s, float sw, float sh, const PpImg* d, float dw, float dh, float dirx, float diry, float mode) {
    bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
    pp_target(&d->view);
    float px[4] = {0, 0, dw, dh}, uv[4] = {0, 0, sw / s->w, sh / s->h};
    float col[4] = {dirx / s->w, diry / s->h, 1, mode}, shape[4] = {0, 0, (sw - 0.5f) / s->w, (sh - 0.5f) / s->h};
    pp_quad(&g_q_blur, s, SAMP_LIN, PB_OPAQUE, DkColorMask_RGBA, dw, dh, px, uv, col, shape);
}

/* bloom and grading on the EFB, right after the last 3D draw before a picture (the HUD stays as drawn) */
static void post_process(int in_race, const GxUniforms* cur) {
    float bloom = in_race && !AB_NOBLOOM ? (float)g_settings.bloom / 100.0f : 0.0f;  /* menus with lit models went white (GL) */
    int grade = grade_wanted() && !AB_NOGRADE;
    if (bloom <= 0.0f && !grade) return;
    g_ab_posts++;
    pt_mark(PT_START);
    /* bloom and grading together: one pass draws both (vk_bloom_grade.frag) instead of the bloom mix + grading */
    int combined = bloom > 0.0f && grade && !AB_OLDPP;
    float bw[2] = {0, 0}, bh[2] = {0, 0};
    uint32_t uw = XSC(EFB_WIDTH), uh = SC(EFB_HEIGHT);  /* the EFB's used part */
    if (uw > (uint32_t)EFB_W) uw = EFB_W;
    if (uh > (uint32_t)EFB_H) uh = EFB_H;
    if (!g_post_logged) {
        g_post_logged = 1;
        rt_log("[deko3d] picture options: bloom %d%%, colour %+d/%d/%d/%+d", g_settings.bloom, g_settings.grade_bright,
               g_settings.grade_contrast, g_settings.grade_sat, g_settings.grade_warm);
    }
    pp_efb_desc();
    float px[4] = {0, 0, (float)uw, (float)uh};
    if (bloom > 0.0f) {
        for (int i = 0; i < 4; i++) pp_img(&g_pp_bl[i], BL_W(i >> 1), BL_H(i >> 1), 0);
        for (int l = 0; l < 2; l++) {  /* the same look at every internal resolution */
            bw[l] = floorf((float)EFB_WIDTH * g_k / (float)(4 << l));
            bh[l] = floorf((float)EFB_HEIGHT / (float)(4 << l));
        }
        /* bright pass: the EFB's used part into level 0 */
        bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
        pp_target(&g_pp_bl[0].view);
        float bpx[4] = {0, 0, bw[0], bh[0]}, uv[4] = {0, 0, (float)uw / EFB_W, (float)uh / EFB_H};
        float step[4] = {(float)uw / bw[0] / EFB_W / 4.0f, (float)uh / bh[0] / EFB_H / 4.0f, 0, 0};
        float clampuv[4] = {0, 0, ((float)uw - 0.5f) / EFB_W, ((float)uh - 0.5f) / EFB_H};
        pp_quad(&g_q_bright, &g_pp_efb, SAMP_LIN, PB_OPAQUE, DkColorMask_RGBA, bw[0], bh[0], bpx, uv, step, clampuv);
        pt_mark(PT_BRIGHT);
        for (int l = 0; l < 2; l++) {
            const PpImg* t = &g_pp_bl[2 * l];
            if (l) blur_pass(&g_pp_bl[0], bw[0], bh[0], t, bw[1], bh[1], 0, 0, AB_OLDPP ? 0.0f : 1.0f);  /* the 1/8 level from the 1/4 */
            if (!AB_OLDPP) {  /* both rounds in one (the 9-tap blur twice = their 17-tap convolution) */
                blur_pass(&t[0], bw[l], bh[l], &t[1], bw[l], bh[l], 1, 0, 2.0f);
                blur_pass(&t[1], bw[l], bh[l], &t[0], bw[l], bh[l], 0, 1, 2.0f);
                continue;
            }
            for (int k = 0; k < 2; k++) {  /* two rounds: wider glow */
                blur_pass(&t[0], bw[l], bh[l], &t[1], bw[l], bh[l], 1, 0, 0.0f);
                blur_pass(&t[1], bw[l], bh[l], &t[0], bw[l], bh[l], 0, 1, 0.0f);
            }
        }
        pt_mark(PT_BLUR);
    }
    if (bloom > 0.0f && !combined) {
        /* both levels onto the EFB with a screen blend, RGB only (the EFB alpha is kept) */
        bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
        pp_target(&g_efb_view);
        if (!AB_OLDPP) {  /* both levels in one pass (vk_bloom_mix.frag): the same picture */
            const PpImg* t0 = &g_pp_bl[0];
            const PpImg* t1 = &g_pp_bl[2];
            float luv[4] = {0, 0, bw[0] / t0->w, bh[0] / t0->h};
            float col[4] = {(bw[1] / t1->w) / luv[2], (bh[1] / t1->h) / luv[3], bloom * 0.72f, 0};
            float shape[4] = {(bw[1] - 0.5f) / t1->w, (bh[1] - 0.5f) / t1->h, (bw[0] - 0.5f) / t0->w, (bh[0] - 0.5f) / t0->h};
            dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 1, dkMakeTextureHandle(t1->slot, SAMP_LIN));
            pp_quad(&g_q_mix, t0, SAMP_LIN, PB_SCREEN, DkColorMask_RGB, EFB_W, EFB_H, px, luv, col, shape);
        } else for (int l = 0; l < 2; l++) {
            const PpImg* t = &g_pp_bl[2 * l];
            float luv[4] = {0, 0, bw[l] / t->w, bh[l] / t->h};
            float col[4] = {0, 0, bloom * 0.72f, 0}, shape[4] = {0, 0, (bw[l] - 0.5f) / t->w, (bh[l] - 0.5f) / t->h};
            pp_quad(&g_q_blur, t, SAMP_LIN, PB_SCREEN, DkColorMask_RGB, EFB_W, EFB_H, px, luv, col, shape);
        }
        pt_mark(PT_MIX);
    }
    if (grade) {  /* a copy of the picture, graded back into the EFB (with the bloom when combined) */
        pp_img(&g_pp_scene, EFB_W, EFB_H, 1);
        bar(DkBarrier_Fragments, 0);
        DkImageRect r = {0, 0, 0, uw, uh, 1};
        dkCmdBufCopyImage(g_cmd, &g_efb_view, &r, &g_pp_scene.view, &r, 0);
        bar(DkBarrier_Full, DkInvalidateFlags_Image);
        pp_target(&g_efb_view);
        float uv[4] = {0, 0, (float)uw / g_pp_scene.w, (float)uh / g_pp_scene.h};
        float g[4] = {exp2f((float)g_settings.grade_bright / 100.0f), (float)g_settings.grade_contrast / 100.0f,
                      (float)g_settings.grade_sat / 100.0f, (float)g_settings.grade_warm / 50.0f * 0.16f};
        if (combined) {
            const PpImg* t0 = &g_pp_bl[0];
            const PpImg* t1 = &g_pp_bl[2];
            float shape[4] = {(bw[0] / t0->w) / uv[2], (bh[0] / t0->h) / uv[3], (bw[1] / t1->w) / uv[2], (bh[1] / t1->h) / uv[3]};
            static float ext[8];
            ext[0] = (bw[0] - 0.5f) / t0->w, ext[1] = (bh[0] - 0.5f) / t0->h;
            ext[2] = (bw[1] - 0.5f) / t1->w, ext[3] = (bh[1] - 0.5f) / t1->h;
            ext[4] = bloom * 0.72f, ext[5] = ext[6] = ext[7] = 0;
            dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 1, dkMakeTextureHandle(t0->slot, SAMP_LIN));
            dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 2, dkMakeTextureHandle(t1->slot, SAMP_LIN));
            g_pp_ext = ext;
            pp_quad(&g_q_bgrade, &g_pp_scene, SAMP_NEAR, PB_OPAQUE, DkColorMask_RGB, EFB_W, EFB_H, px, uv, g, shape);
        } else
            pp_quad(&g_q_grade, &g_pp_scene, SAMP_NEAR, PB_OPAQUE, DkColorMask_RGB, EFB_W, EFB_H, px, uv, g, NULL);
        pt_mark(PT_GRADE);
    }
    bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
    gx_rebind(cur);
}

/* FSR 1 present: the EFB's src rect onto the screen image fv at dst. EASU into g_pp_fsr when the picture is
 * bigger than the source (else a linear stretch), then RCAS onto the screen. 0 when off (the caller blits). */
static int fsr_present(const DkImageRect* src, const DkImageView* fv, const DkImageRect* dst) {
    if (g_settings.sharpness <= 0 || AB_NOFSR || !src->width || !src->height || !dst->width || !dst->height) return 0;
    uint32_t sx = src->x, sy = src->y, sw = src->width, sh = src->height, dw = dst->width, dh = dst->height;
    if (!g_fsr_logged) {
        g_fsr_logged = 1;
        rt_log("[deko3d] FSR 1 on the presented picture: %ux%u -> %ux%u, sharpness %d%%", sw, sh, dw, dh, g_settings.sharpness);
    }
    pp_efb_desc();
    pp_img(&g_pp_fsr, FB_W, FB_H, 0);
    pt_mark(PT_FSR0);
    bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
    pp_target(&g_pp_fsr.view);
    float opx[4] = {0, 0, (float)dw, (float)dh};
    if (dw > sw || dh > sh) {
        float kx = (float)sw / (float)dw, ky = (float)sh / (float)dh;
        float con[4] = {kx, ky, 0.5f * kx - 0.5f, 0.5f * ky - 0.5f};
        float s[4] = {(float)sx, (float)sy, (float)(sx + sw - 1), (float)(sy + sh - 1)};
        pp_quad(&g_q_easu, &g_pp_efb, SAMP_NEAR, PB_OPAQUE, DkColorMask_RGBA, (float)FB_W, (float)FB_H, opx, opx, con, s);
    } else {  /* a smaller picture: a linear copy (vk_blur with no direction) */
        float uv[4] = {(float)sx / EFB_W, (float)sy / EFB_H, (float)(sx + sw) / EFB_W, (float)(sy + sh) / EFB_H};
        float col[4] = {0, 0, 1, AB_OLDPP ? 0.0f : 1.0f}, shape[4] = {0, 0, uv[2], uv[3]};
        pp_quad(&g_q_blur, &g_pp_efb, SAMP_LIN, PB_OPAQUE, DkColorMask_RGBA, (float)FB_W, (float)FB_H, opx, uv, col, shape);
    }
    pt_mark(PT_FSR_COPY);
    bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
    pp_target(fv);
    /* 100% = AMD's strongest; each 50% less halves it */
    float px[4] = {(float)dst->x, (float)dst->y, (float)(dst->x + dw), (float)(dst->y + dh)};
    float sharp[4] = {exp2f(-(float)(100 - g_settings.sharpness) / 50.0f), 0, 0, 0};
    float last[4] = {0, 0, (float)(dw - 1), (float)(dh - 1)};
    pp_quad(&g_q_rcas, &g_pp_fsr, SAMP_NEAR, PB_OPAQUE, DkColorMask_RGBA, (float)FB_W, (float)FB_H, px, opx, sharp, last);
    pt_mark(PT_FSR_END);
    return 1;
}

/* ---- EFB copies and presentation ---- */
static int g_fb_slot = -1;

/* Self-captures for hardware testing (off unless SR_SHOTS=1, e.g. in env.txt): presented frames
 * k_shots[] (or the frames listed in SR_SHOTS=a,b,c) are saved to the SD card as shot_<frame>.bmp;
 * each causes a visible hitch. */
static const unsigned k_shots[] = {300, 900, 1800, 3000, 4500, 6000, 7200};
static unsigned g_presented;
static DkMemBlock g_shot_mem;
static unsigned g_shot_pending;  /* frame number captured into g_shot_mem by this command list */

/* the menu's "Save screenshot", taken before the overlay is drawn: the frame that chose it still shows
 * the menu, the next one does not */
static int g_menu_shot;
static void shot_request(DkImageView const* fv) {
    unsigned n = ++g_presented;
    static int on = -1;
    static unsigned list[32], nlist;  /* SR_SHOTS=a,b,c: those frames instead of k_shots */
    if (on < 0) {
        const char* e = getenv("SR_SHOTS");
        on = e != NULL;
        for (const char* q = e; q && *q && nlist < 32; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
            if (atoi(q) > 1) list[nlist++] = (unsigned)atoi(q);
    }
    int want = g_menu_shot;
    g_menu_shot = 0;
    if (!on && !want) return;
    if (on && nlist)
        for (unsigned i = 0; i < nlist; i++) want |= list[i] == n;
    else if (on)
        for (size_t i = 0; i < sizeof k_shots / sizeof k_shots[0]; i++) want |= k_shots[i] == n;
    if (!want) return;
    if (!g_shot_mem) g_shot_mem = block(FB_W * FB_H * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    bar(DkBarrier_Full, 0);
    DkCopyBuf dst = {dkMemBlockGetGpuAddr(g_shot_mem), 0, 0};
    DkImageRect r = {0, 0, 0, FB_W, FB_H, 1};
    dkCmdBufCopyImageToBuffer(g_cmd, fv, &r, &dst, 0);
    g_shot_pending = n;
}

/* The capture is copied out of the uncached GPU buffer in one memcpy (byte reads there made a save
 * take 7-10 s), then converted and written to the SD card by a thread of its own: no hitch. */
typedef struct { uint8_t* px; unsigned frame; } Shot;

static int shot_thread(void* arg) {
    Shot* sh = arg;
    rt_thread_pin(2);
    rt_thread_lower_priority();
    uint64_t t0 = armGetSystemTick();
    char path[96];
    snprintf(path, sizeof path, "sdmc:/switch/sonicriders/shot_%u.bmp", sh->frame);
    FILE* f = fopen(path, "wb");
    if (f) {
        uint32_t row = FB_W * 3, size = 54 + row * FB_H;
        uint8_t h[54] = {'B', 'M'};
        memcpy(h + 2, &size, 4);
        h[10] = 54; h[14] = 40;
        int32_t w = FB_W, hh = FB_H;
        memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
        h[26] = 1; h[28] = 24;
        uint8_t* img = malloc((size_t)size);
        memcpy(img, h, 54);
        for (int y = FB_H - 1, oy = 0; y >= 0; y--, oy++) {  /* BMP rows go bottom-up */
            const uint8_t* s = sh->px + (size_t)y * FB_W * 4;
            uint8_t* line = img + 54 + (size_t)oy * row;
            for (int x = 0; x < FB_W; x++) { line[3 * x] = s[4 * x + 2]; line[3 * x + 1] = s[4 * x + 1]; line[3 * x + 2] = s[4 * x]; }
        }
        fwrite(img, 1, size, f);
        free(img);
        fclose(f);
        rt_log("[deko3d] saved %s in the background (%.0f ms)", path, armTicksToNs(armGetSystemTick() - t0) / 1e6);
    }
    free(sh->px);
    free(sh);
    return 0;
}

static void shot_write(void) {
    if (!g_shot_pending) return;
    Shot* sh = malloc(sizeof *sh);
    sh->px = malloc((size_t)FB_W * FB_H * 4);
    sh->frame = g_shot_pending;
    g_shot_pending = 0;
    memcpy(sh->px, dkMemBlockGetCpuAddr(g_shot_mem), (size_t)FB_W * FB_H * 4);
    rt_thread_start(shot_thread, sh);
}

static void efb_copy(const GxCmd* c, const GxUniforms* u) {
    g_st_valid = 0;  /* binds state of its own */
    uint32_t x = c->copy[0], y = c->copy[1], w = c->copy[2], h = c->copy[3];
    DkImageRect src = {XSC(x), SC(y), 0, XSC(x + w) - XSC(x), SC(h), 1};
    bar(DkBarrier_Primitives, 0);  /* everything drawn so far, before the 2D engine reads */
    if (c->copy[4]) {  /* to the XFB: into the swapchain image, letterboxed to 4:3 */
        if (g_fb_slot < 0) g_fb_slot = dkQueueAcquireImage(g_queue, g_swap);
        DkImageView fv;
        dkImageViewDefaults(&fv, &g_fb[g_fb_slot]);
        DkImageView const* targets[1] = {&fv};
        dkCmdBufBindRenderTargets(g_cmd, targets, 1, NULL);
        DkScissor full = {0, 0, FB_W, FB_H};
        dkCmdBufSetScissors(g_cmd, 0, &full, 1);
        DkColorWriteState cw;
        dkColorWriteStateDefaults(&cw);
        dkColorWriteStateSetMask(&cw, 0, DkColorMask_RGBA);
        dkCmdBufBindColorWriteState(g_cmd, &cw);
        float black[4] = {0, 0, 0, 1};
        dkCmdBufClearColor(g_cmd, 0, DkColorMask_RGBA, black);
        bar(DkBarrier_Fragments, 0);
        /* at the chosen aspect (settings.h), centred */
        float aspect = g_k > 1.0f ? 4.0f / 3.0f * g_k : settings_display_aspect();
        if (c->copy[11]) {  /* online: this player's part of a split screen (gx_render.c), same scale both ways */
            src.y = SC(y + (c->copy[11] >> 16));
            src.height = SC(c->copy[11] & 0xFFFF);
            aspect *= (float)h / (float)(c->copy[11] & 0xFFFF);
        }
        uint32_t dw = FB_W, dh = FB_H;
        if ((float)FB_W > FB_H * aspect) dw = (uint32_t)(FB_H * aspect + 0.5f);
        else dh = (uint32_t)(FB_W / aspect + 0.5f);
        DkImageRect dst = {(FB_W - dw) / 2, (FB_H - dh) / 2, 0, dw, dh, 1};
        if (fsr_present(&src, &fv, &dst)) {
            bar(DkBarrier_Fragments, DkInvalidateFlags_Image);
            gx_rebind(u);  /* the EFB as the target again, GX shaders and vertex state */
        } else {
            dkCmdBufBlitImage(g_cmd, &g_efb_view, &src, &fv, &dst, DkBlitFlag_FilterLinear, 0);
            DkImageView const* efb_targets[1] = {&g_efb_view};
            dkCmdBufBindRenderTargets(g_cmd, efb_targets, 1, &g_efb_depth_view);
        }
    } else if ((int)c->copy[8] >= 0) {  /* to a texture */
        int id = (int)c->copy[8];
        tex_prepare(id);
        DkTex* t = tex_slot(id);
        if (t->live) {
            DkImageView tv;
            dkImageViewDefaults(&tv, &t->image);
            DkImageRect dst = {0, 0, 0, t->w, t->h, 1};
            dkCmdBufBlitImage(g_cmd, &g_efb_view, &src, &tv, &dst, DkBlitFlag_FilterLinear, 0);
            uint32_t exec = c->copy[9];
            if ((((exec >> 4) & 7) | ((exec >> 3) & 1) << 3) == 4) {  /* RGB565 target: no alpha, it samples as 1 */
                bar(DkBarrier_Full, 0);
                DkImageView const* tt[1] = {&tv};
                dkCmdBufBindRenderTargets(g_cmd, tt, 1, NULL);
                DkScissor fs = {0, 0, t->w, t->h};
                dkCmdBufSetScissors(g_cmd, 0, &fs, 1);
                float one[4] = {0, 0, 0, 1};
                dkCmdBufClearColor(g_cmd, 0, DkColorMask_A, one);
                DkImageView const* efb_targets[1] = {&g_efb_view};
                dkCmdBufBindRenderTargets(g_cmd, efb_targets, 1, &g_efb_depth_view);
                if (u) apply_state(u);  /* the scissor (and the rest) of the draws that follow */
            }
            bar(DkBarrier_Primitives, DkInvalidateFlags_Image);
        }
    }
    if (c->copy[5]) {  /* clear the copied rectangle */
        uint32_t argb = c->copy[6], cm = u->bp[0x41];
        DkScissor sc = {XSC(x), SC(y), XSC(x + w) - XSC(x), SC(h)};
        dkCmdBufSetScissors(g_cmd, 0, &sc, 1);
        float col[4] = {((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, (argb >> 24) / 255.0f};
        uint32_t mask = ((cm >> 3) & 1 ? DkColorMask_RGB : 0) | ((cm >> 4) & 1 ? DkColorMask_A : 0);
        DkColorWriteState cw;
        dkColorWriteStateDefaults(&cw);
        dkColorWriteStateSetMask(&cw, 0, DkColorMask_RGBA);
        dkCmdBufBindColorWriteState(g_cmd, &cw);
        if (mask) dkCmdBufClearColor(g_cmd, 0, mask, col);
        if ((u->bp[0x40] >> 4) & 1) dkCmdBufClearDepthStencil(g_cmd, true, (float)c->copy[7] / 16777215.0f, 0, 0);
    }
}

/* the settings menu / FPS counter (overlay.h) over the presented picture */
static void draw_overlay(uint8_t* cpu, DkGpuAddr gpu) {
    g_st_valid = 0;
    unsigned version;
    int box[4];
    static int up_box[4];  /* what the overlay texture holds: content inside, transparent outside */
    const uint8_t* img = overlay_image(&version, box);
    if (!img) return;
    bar(DkBarrier_Full, DkInvalidateFlags_Image);  /* after the blit into the picture */
    DkImageView ov;
    dkImageViewDefaults(&ov, &g_ov_image);
    if (version != g_ov_version) {
        /* only the rows and columns that changed: the new content and what the old one covered */
        int u[4] = {box[0], box[1], box[2], box[3]};
        if (up_box[0] < up_box[2]) {
            if (u[0] >= u[2]) memcpy(u, up_box, sizeof u);
            else {
                u[0] = up_box[0] < u[0] ? up_box[0] : u[0];
                u[1] = up_box[1] < u[1] ? up_box[1] : u[1];
                u[2] = up_box[2] > u[2] ? up_box[2] : u[2];
                u[3] = up_box[3] > u[3] ? up_box[3] : u[3];
            }
        }
        uint32_t uw = u[2] > u[0] ? (uint32_t)(u[2] - u[0]) : 0, uh = u[3] > u[1] ? (uint32_t)(u[3] - u[1]) : 0;
        uint32_t bytes = uw * uh * 4;
        if (!bytes) {
            g_ov_version = version;
        } else if (g_stage_off + bytes <= SLOT_STAGE) {
            uint32_t so = SLOT_CMD + SLOT_VTX + SLOT_UBO + g_stage_off;
            for (uint32_t y = 0; y < uh; y++)
                memcpy(cpu + so + y * uw * 4, img + ((size_t)(u[1] + y) * OVERLAY_W + u[0]) * 4, uw * 4);
            DkCopyBuf src = {gpu + so, 0, 0};
            DkImageRect r = {(uint32_t)u[0], (uint32_t)u[1], 0, uw, uh, 1};
            dkCmdBufCopyBufferToImage(g_cmd, &src, &ov, &r, 0);
            bar(DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
            g_stage_off = (g_stage_off + bytes + 255) & ~255u;
            g_ov_version = version;
            memcpy(up_box, box, sizeof up_box);
        }
    }
    if (up_box[0] >= up_box[2]) return;  /* nothing visible: no full-screen blend */
    DkImageView fv;
    dkImageViewDefaults(&fv, &g_fb[g_fb_slot]);
    DkImageView const* targets[1] = {&fv};
    dkCmdBufBindRenderTargets(g_cmd, targets, 1, NULL);
    DkViewport vp = {0, 0, FB_W, FB_H, 0.0f, 1.0f};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    /* only the overlay's content is blended (the FPS counter alone: a small corner, not 1280x720) */
    uint32_t sx0 = (uint32_t)up_box[0] * FB_W / OVERLAY_W, sy0 = (uint32_t)up_box[1] * FB_H / OVERLAY_H;
    uint32_t sx1 = ((uint32_t)up_box[2] * FB_W + OVERLAY_W - 1) / OVERLAY_W, sy1 = ((uint32_t)up_box[3] * FB_H + OVERLAY_H - 1) / OVERLAY_H;
    if (sx1 > FB_W) sx1 = FB_W;
    if (sy1 > FB_H) sy1 = FB_H;
    DkScissor sc = {sx0, sy0, sx1 - sx0, sy1 - sy0};
    dkCmdBufSetScissors(g_cmd, 0, &sc, 1);
    DkShader const* shaders[2] = {&g_ovs, &g_ofs};
    dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindVtxAttribState(g_cmd, NULL, 0);
    dkCmdBufBindVtxBufferState(g_cmd, NULL, 0);
    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    rs.cullMode = DkFace_None;
    dkCmdBufBindRasterizerState(g_cmd, &rs);
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = false;
    ds.depthWriteEnable = false;
    dkCmdBufBindDepthStencilState(g_cmd, &ds);
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, true);
    dkCmdBufBindColorState(g_cmd, &cs);
    DkBlendState bs;
    dkBlendStateDefaults(&bs);
    dkBlendStateSetOps(&bs, DkBlendOp_Add, DkBlendOp_Add);
    dkBlendStateSetFactors(&bs, DkBlendFactor_One, DkBlendFactor_InvSrcAlpha, DkBlendFactor_Zero, DkBlendFactor_One);  /* premultiplied (menu_raster.cpp) */
    dkCmdBufBindBlendStates(g_cmd, 0, &bs, 1);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkCmdBufBindColorWriteState(g_cmd, &cw);
    dkCmdBufBindTexture(g_cmd, DkStage_Fragment, 0, dkMakeTextureHandle(OVERLAY_SLOT, 0));  /* clamp, nearest */
    dkCmdBufDraw(g_cmd, DkPrimitive_TriangleStrip, 4, 1, 0, 0);
}

/* wait until the GPU is done with slot `slot` (its frame from two frames ago), then free what that
 * frame retired */
static void slot_reclaim(unsigned slot) {
    if (g_slot_fenced[slot]) {
        uint64_t t = armGetSystemTick();
        dkFenceWait(&g_slot_fence[slot], -1);
        g_last_gpu = armGetSystemTick() - t;
        g_gpu_ticks += g_last_gpu;
        g_slot_fenced[slot] = 0;
    }
    for (uint32_t i = 0; i < g_nretired[slot]; i++) {
        tex_destroy(g_retired[slot][i]);
        gx_texture_free(g_retired[slot][i]);
    }
    g_nretired[slot] = 0;
}

static void execute(GxFrame* f) {
    g_st_valid = 0;  /* a new command list */
    uint64_t t_start = armGetSystemTick();
    g_last_upload = 0;
    g_last_gpu = 0;
    slot_reclaim(g_slot);
    /* this slot's previous frame is done: its GPU time, for the log */
    /* report k: start at [k * 4], end at [k * 4 + 2] (u64 value, u64 timestamp each) */
    volatile uint64_t* ts = (volatile uint64_t*)dkMemBlockGetCpuAddr(g_ts) + g_slot * 16;
    DkGpuAddr ts_gpu = dkMemBlockGetGpuAddr(g_ts) + g_slot * 128;
    int have = g_ts_valid[g_slot] && ts[3] >= ts[1];
    if (have) {
        g_gpu_frame_ns = dkTimestampToNs(ts[3] - ts[1]);
        double vs = (double)(ts[6] - ts[4]), fs = (double)(ts[10] - ts[8]), pr = (double)(ts[14] - ts[12]);
        g_cnt_sum[0] += vs;
        g_cnt_sum[1] += fs;
        g_cnt_sum[2] += pr;
        int m = g_ts_mode[g_slot];
        g_ab_ms[m] += g_gpu_frame_ns / 1e6;
        g_ab_vs[m] += vs;
        g_ab_fs[m] += fs;
        g_ab_pr[m] += pr;
        g_ab_n[m]++;
        {  /* the passes' marks: each one's time since the previous mark, by its label */
            volatile uint64_t* pt = (volatile uint64_t*)dkMemBlockGetCpuAddr(g_ts) + (1024 + g_slot * 1024) / 8;
            for (unsigned i = 1; i < g_pt_n[g_slot]; i++) {
                int lab = g_pt_lab[g_slot][i];
                if (lab == PT_START || lab == PT_FSR0 || pt[i * 2 + 1] < pt[i * 2 - 1]) continue;
                g_pt_ms[m][lab] += dkTimestampToNs(pt[i * 2 + 1] - pt[i * 2 - 1]) / 1e6;
            }
            g_pt_frames[m]++;
        }
        double ms = g_gpu_frame_ns / 1e6;
        g_gpu_ms_avg = g_gpu_ms_avg < 0 ? ms : g_gpu_ms_avg * 0.9 + ms * 0.1;
        g_gpu_ns_sum += g_gpu_frame_ns;
        if (g_gpu_frame_ns > g_gpu_ns_max) g_gpu_ns_max = g_gpu_frame_ns;
        g_gpu_n++;
    }
    g_pt_n[g_slot] = 0;
    g_s = settings_internal_scale();
    g_s_sum += g_s;
    settings_set_window_aspect((float)FB_W / (float)FB_H);  /* the fit mode: the screen's */
    g_k = f->wide_k > 1.0f ? (f->wide_k < WIDE_K_MAX ? f->wide_k : WIDE_K_MAX) : 1.0f;
    DkMemBlock mem = g_slots[g_slot];
    uint8_t* cpu = (uint8_t*)dkMemBlockGetCpuAddr(mem);
    DkGpuAddr gpu = dkMemBlockGetGpuAddr(mem);
    dkCmdBufClear(g_cmd);
    dkCmdBufAddMemory(g_cmd, mem, 0, SLOT_CMD);
    if (g_ab_on < 0) {
        g_ab_on = getenv("SR_GPU_AB") ? atoi(getenv("SR_GPU_AB")) : 0;
        if (g_ab_on < 0 || g_ab_on > 4) g_ab_on = 1;
        g_ab_count = g_ab_on == 2 || g_ab_on == 3 ? AB_MODES : 2;
        if (g_ab_on) rt_log("[gpu-ab] GPU cost test %d on: %d modes of %d frames each", g_ab_on, g_ab_count, AB_FRAMES);
    }
    if (g_ab_on) {
        static int fix = -2;  /* SR_GPU_AB_FIX=<mode>: that mode only (picture comparisons) */
        if (fix == -2) fix = getenv("SR_GPU_AB_FIX") ? atoi(getenv("SR_GPU_AB_FIX")) : -1;
        g_ab_mode = (int)((g_ab_frame++ / AB_FRAMES) % (unsigned)g_ab_count);
        if (fix >= 0 && fix < g_ab_count) g_ab_mode = fix;
        if (g_ab_frame % AB_LOG == 0) {
            for (int m = 0; m < g_ab_count; m++)
                if (g_ab_n[m])
                    rt_log("[gpu-ab] %-31s GPU %6.2f ms/frame, %6.0f vertex shader runs, %8.0f fragment shader runs, %6.0f primitives per frame",
                           g_ab_on == 4 ? k_ab_names4[m] : g_ab_on == 3 ? k_ab_names3[m] : g_ab_on == 2 ? k_ab_names2[m] : k_ab_names1[m], g_ab_ms[m] / g_ab_n[m], g_ab_vs[m] / g_ab_n[m], g_ab_fs[m] / g_ab_n[m], g_ab_pr[m] / g_ab_n[m]);
            memset(g_ab_ms, 0, sizeof g_ab_ms);
            memset(g_ab_vs, 0, sizeof g_ab_vs);
            memset(g_ab_fs, 0, sizeof g_ab_fs);
            memset(g_ab_pr, 0, sizeof g_ab_pr);
            memset(g_ab_n, 0, sizeof g_ab_n);
            rt_log("[gpu-ab] bloom/grading passes run %.2f times per frame", g_ab_posts / (double)AB_LOG);
            for (int m = 0; m < g_ab_count; m++) pt_log(m, g_ab_on == 4 ? k_ab_names4[m] : "");
            g_ab_posts = 0;
        }
    }
    g_ts_mode[g_slot] = g_ab_mode;
    if (g_ab_on == 3) dkCmdBufTiledCacheOp(g_cmd, AB_TILED ? DkTiledCacheOp_Enable : DkTiledCacheOp_Disable);
    for (int k = 0; k < 4; k++) dkCmdBufReportCounter(g_cmd, k_counters[k], ts_gpu + k * 32);
    g_vtx_off = g_stage_off = 0;

    /* vertices */
    uint32_t vbytes = f->nverts * (uint32_t)sizeof(GxVertex);
    if (vbytes > SLOT_VTX) {
        rt_log("[deko3d] frame has %u vertices, more than the buffer holds; truncated", f->nverts);
        vbytes = SLOT_VTX / sizeof(GxVertex) * sizeof(GxVertex);
    }
    memcpy(cpu + SLOT_CMD, f->verts, vbytes);
    /* the index lists, after the vertices (draws past what fits are dropped) */
    uint32_t ioff = (SLOT_CMD + vbytes + 255) & ~255u;
    uint32_t icap = (SLOT_CMD + SLOT_VTX - ioff) / 4, ncopy = f->nidx < icap ? f->nidx : icap;
    if (ncopy < f->nidx) rt_log("[deko3d] frame has %u indices, more than the buffer holds; truncated", f->nidx);
    memcpy(cpu + ioff, f->idx, (size_t)ncopy * 4);

    /* XF memory: one image per chunk table (state), bound as the vertex shader's uniform buffer 1 */
    uint32_t xoff = SLOT_CMD + SLOT_VTX + SLOT_UBO + SLOT_STAGE;
    uint32_t xbytes = f->nxchunks * GX_XFM_CHUNK * 4;
    if (xbytes > SLOT_XFM) xbytes = SLOT_XFM;
    if (g_ab_on == 1) memcpy(cpu + xoff, f->xchunks, xbytes);  /* the old shader's chunk pool */
    uint32_t xioff = xoff + SLOT_XFM, nximg = f->nxtabs < SLOT_XFI / XF_IMAGE_BYTES ? f->nxtabs : SLOT_XFI / XF_IMAGE_BYTES;
    if (nximg < f->nxtabs) {
        static int warned;
        if (!warned++) rt_log("[deko3d] a frame has %u XF memory states, more than the %u images that fit; the rest reuse the last",
                              f->nxtabs, nximg);
    }
    for (uint32_t t = 0; t < nximg; t++) {
        uint8_t* dst = cpu + xioff + (size_t)t * XF_IMAGE_BYTES;
        for (int k = 0; k < XF_IMAGE_CHUNKS; k++) {
            uint32_t pc = f->xtabs[t].xtab[k_xf_image_chunks[k]];
            if (pc < f->nxchunks) memcpy(dst + k * GX_XFM_CHUNK * 4, f->xchunks + (size_t)pc * GX_XFM_CHUNK, GX_XFM_CHUNK * 4);
            else memset(dst + k * GX_XFM_CHUNK * 4, 0, GX_XFM_CHUNK * 4);
        }
    }

    /* textures this frame uses: created and uploaded before any draw */
    for (uint32_t i = 0; i < f->ncmds; i++)
        if (f->cmds[i].type == GXC_DRAW)
            for (int m = 0; m < 8; m++)
                if (f->cmds[i].tex[m] >= 0) tex_prepare(f->cmds[i].tex[m]);
    bar(DkBarrier_Primitives, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);

    DkImageView const* targets[1] = {&g_efb_view};
    dkCmdBufBindRenderTargets(g_cmd, targets, 1, &g_efb_depth_view);
    DkShader const* shaders[2] = {ab_vsh(), &g_fsh};
    dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, shaders, 2);
    g_fsh_bound = &g_fsh;
    dkCmdBufBindImageDescriptorSet(g_cmd, dkMemBlockGetGpuAddr(g_desc), MAX_IMAGES);
    dkCmdBufBindSamplerDescriptorSet(g_cmd, dkMemBlockGetGpuAddr(g_desc) + MAX_IMAGES * sizeof(DkImageDescriptor), NUM_SAMPLERS);
    static const uint32_t offs[10] = {
        offsetof(GxVertex, pos), offsetof(GxVertex, nrm), offsetof(GxVertex, col[0]), offsetof(GxVertex, col[1]),
        offsetof(GxVertex, tex[0]), offsetof(GxVertex, tex[2]), offsetof(GxVertex, tex[4]), offsetof(GxVertex, tex[6]),
        offsetof(GxVertex, tmtx[0]), offsetof(GxVertex, tmtx[4])};
    DkVtxAttribState attrs[10];
    for (int i = 0; i < 10; i++) {
        memset(&attrs[i], 0, sizeof attrs[i]);
        attrs[i].bufferId = 0;
        attrs[i].offset = offs[i];
        attrs[i].size = DkVtxAttribSize_4x32;
        attrs[i].type = DkVtxAttribType_Float;
    }
    dkCmdBufBindVtxAttribState(g_cmd, attrs, 10);
    DkVtxBufferState vb = {sizeof(GxVertex), 0};
    dkCmdBufBindVtxBufferState(g_cmd, &vb, 1);
    dkCmdBufBindVtxBuffer(g_cmd, 0, gpu + SLOT_CMD, vbytes ? vbytes : 16);
    dkCmdBufBindIdxBuffer(g_cmd, DkIdxFormat_Uint32, gpu + ioff);
    if (AB_SSBO) dkCmdBufBindStorageBuffer(g_cmd, DkStage_Vertex, 0, gpu + xoff, xbytes ? xbytes : 256);
    g_fr_cpu = cpu, g_fr_gpu = gpu, g_fr_vb = gpu + SLOT_CMD, g_fr_ib = gpu + ioff, g_fr_vbytes = vbytes;  /* gx_rebind */

    /* the register blocks and chunk tables, uploaded once each at the uniform buffer alignment;
     * STATE commands bind them by index */
    const uint32_t ustride = ((uint32_t)sizeof(GxUniforms) + DK_UNIFORM_BUF_ALIGNMENT - 1) & ~(DK_UNIFORM_BUF_ALIGNMENT - 1);
    const uint32_t xstride = ((uint32_t)sizeof(GxXTab) + DK_UNIFORM_BUF_ALIGNMENT - 1) & ~(DK_UNIFORM_BUF_ALIGNMENT - 1);
    uint32_t uoff = SLOT_CMD + SLOT_VTX, xoff_ubo = uoff + f->nunis * ustride;
    int ubo_ok = f->nunis * ustride + f->nxtabs * xstride <= SLOT_UBO;
    if (ubo_ok) {
        for (uint32_t i = 0; i < f->nunis; i++) memcpy(cpu + uoff + i * ustride, &f->unis[i], sizeof(GxUniforms));
        for (uint32_t i = 0; i < f->nxtabs; i++) memcpy(cpu + xoff_ubo + i * xstride, &f->xtabs[i], sizeof(GxXTab));
    } else {
        static int warned;
        if (!warned++) rt_log("[deko3d] a frame's states (%u register blocks, %u chunk tables) overflow the uniform area: dropped",
                              f->nunis, f->nxtabs);
    }
    const GxUniforms* cur = NULL;
    uint32_t cur_regs = ~0u, cur_xtab = ~0u;
    /* bloom / grading (post_process): after the last 3D draw before each picture (XFB copy), as render_gl.c */
    static uint32_t* post_at;
    static uint32_t post_cap;
    uint32_t npost = 0, ipost = 0;
    if (post_wanted() && ubo_ok) {
        int64_t last3d = -1;
        uint32_t ureg = ~0u;
        for (uint32_t i = 0; i < f->ncmds; i++) {
            const GxCmd* c = &f->cmds[i];
            if (c->type == GXC_STATE) ureg = c->a;
            else if (c->type == GXC_DRAW && ureg < f->nunis && f->unis[ureg].xfr[0x26] == 0) last3d = i;
            else if (c->type == GXC_COPY && c->copy[4]) {
                if (last3d >= 0) {
                    if (npost == post_cap) post_at = realloc(post_at, (post_cap = post_cap ? post_cap * 2 : 8) * sizeof *post_at);
                    post_at[npost++] = (uint32_t)last3d;
                }
                last3d = -1;
            }
        }
    }
    for (uint32_t i = 0; i < f->ncmds; i++) {
        if (ipost < npost && i > post_at[ipost]) {
            ipost++;
            post_process(f->in_race, cur);
        }
        const GxCmd* c = &f->cmds[i];
        switch (c->type) {
        case GXC_STATE: {
            if (!ubo_ok) { cur = NULL; break; }
            if (c->b != cur_xtab) {
                cur_xtab = c->b;
                uint32_t t = c->b < nximg ? c->b : nximg - 1;
                if (nximg) dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 1, gpu + xioff + (DkGpuAddr)t * XF_IMAGE_BYTES, XF_IMAGE_BYTES);
                if (AB_SSBO)
                    dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 2, gpu + xoff_ubo + c->b * xstride, (uint32_t)sizeof(GxXTab));
            }
            if (c->a == cur_regs) break;
            cur_regs = c->a;
            cur = &f->unis[c->a];
            DkGpuAddr a = gpu + uoff + c->a * ustride;
            uint32_t usize = ((uint32_t)sizeof(GxUniforms) + 15) & ~15u;
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 0, a, usize);
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Fragment, 0, a, usize);
            apply_state(cur);
            {
                const DkShader* fsh = frag_shader_for(cur);
                if (fsh != g_fsh_bound) {
                    DkShader const* fs[1] = {fsh};
                    dkCmdBufBindShaders(g_cmd, DkStageFlag_Fragment, fs, 1);
                    g_fsh_bound = fsh;
                }
            }
            break;
        }
        case GXC_DRAW:
            if (!cur || c->a + c->b > ncopy || vbytes < f->nverts * (uint32_t)sizeof(GxVertex)) break;
            bind_textures(c, cur);
            dkCmdBufDrawIndexed(g_cmd, c->prim == GXP_TRIANGLES ? DkPrimitive_Triangles
                                       : c->prim == GXP_LINES   ? DkPrimitive_Lines
                                                                : DkPrimitive_Points,
                                c->b, 1, c->a, 0, 0);
            break;
        case GXC_COPY:
            if (cur) {
                efb_copy(c, cur);
                apply_state(cur);
            }
            break;
        case GXC_TEXBLIT: {  /* an EFB copy into the top left of a larger copy texture (gx_render.h) */
            int sid = (int)c->copy[0], did = (int)c->copy[1];
            tex_prepare(sid);
            tex_prepare(did);
            DkTex* s = tex_slot(sid);
            DkTex* d = tex_slot(did);
            if (!s->live || !d->live) break;
            /* each at its own scale (the resolution chosen when it was made) */
            GxTexture* sg = gx_texture_get(sid);
            GxTexture* dg = gx_texture_get(did);
            if (!sg || !dg || !sg->width || !dg->width || !sg->height || !dg->height) break;
            uint32_t sw = c->copy[2] * s->w / sg->width, sh = c->copy[3] * s->h / sg->height;
            uint32_t dw = c->copy[2] * d->w / dg->width, dh = c->copy[3] * d->h / dg->height;
            if (sw > s->w) sw = s->w;
            if (sh > s->h) sh = s->h;
            if (dw > d->w) dw = d->w;
            if (dh > d->h) dh = d->h;
            DkImageView sv, dv;
            dkImageViewDefaults(&sv, &s->image);
            dkImageViewDefaults(&dv, &d->image);
            DkImageRect sr = {0, 0, 0, sw, sh, 1}, dr = {0, 0, 0, dw, dh, 1};
            bar(DkBarrier_Primitives, 0);
            dkCmdBufBlitImage(g_cmd, &sv, &sr, &dv, &dr, sw == dw && sh == dh ? 0 : DkBlitFlag_FilterLinear, 0);
            bar(DkBarrier_Primitives, DkInvalidateFlags_Image);
            break;
        }
        default: break;
        }
    }
    if (g_fb_slot >= 0) {
        g_menu_shot |= overlay_take_screenshot_request();
        draw_overlay(cpu, gpu);
        /* screenshots show what the screen shows (the menu's own "Save screenshot" closes it first) */
        DkImageView fv;
        dkImageViewDefaults(&fv, &g_fb[g_fb_slot]);
        bar(DkBarrier_Fragments, 0);
        shot_request(&fv);
    }
    for (int k = 0; k < 4; k++) dkCmdBufReportCounter(g_cmd, k_counters[k], ts_gpu + k * 32 + 16);
    g_ts_valid[g_slot] = 1;
    dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(g_cmd));
    if (g_fb_slot >= 0) {
        dkQueuePresentImage(g_queue, g_swap, g_fb_slot);
        g_fb_slot = -1;
    }
    /* the GPU runs this frame while the next one is recorded into the other slot: the slot's fence
     * says when its memory and the textures retired with it are free again */
    dkQueueSignalFence(g_queue, &g_slot_fence[g_slot], true);
    g_slot_fenced[g_slot] = 1;
    dkQueueFlush(g_queue);
    if (g_shot_pending) {  /* debugging screenshots read the picture back now */
        dkQueueWaitIdle(g_queue);
        shot_write();
    }
    g_retired[g_slot] = realloc(g_retired[g_slot], (f->nretired ? f->nretired : 1) * sizeof(int));
    memcpy(g_retired[g_slot], f->retired, f->nretired * sizeof(int));
    g_nretired[g_slot] = f->nretired;
    g_slot ^= 1;
    /* render-thread cost per frame (recording, plus waiting for the GPU to free a slot) */
    uint64_t busy = armGetSystemTick() - t_start;
    g_busy_ticks += busy;
    if (armTicksToNs(busy) > 50000000ull)
        rt_log("[hitch] render thread: a frame took %.0f ms (%.0f ms waiting for the GPU), %u KB of texture uploads",
               armTicksToNs(busy) / 1e6, armTicksToNs(g_last_gpu) / 1e6, g_last_upload >> 10);
    if (++g_busy_frames == 120) {
        rt_log("[deko3d] %.2f ms/frame on the render thread (%.2f waiting for the GPU), %llu KB/frame texture uploads; "
               "specialized shaders for %llu of %llu states",
               armTicksToNs(g_busy_ticks) / 120 / 1e6, armTicksToNs(g_gpu_ticks) / 120 / 1e6,
               (unsigned long long)(g_upload_bytes / 120 / 1024), g_spec_hits, g_spec_hits + g_spec_misses);
        double n = g_gpu_n ? (double)g_gpu_n : 1.0;
        rt_log("[deko3d]   GPU %.2f ms/frame (longest %.2f) at %.1fx internal resolution; per frame %.0f vertex shader runs, "
               "%.0f fragment shader runs, %.0f primitives",
               g_gpu_ns_sum / n / 1e6, g_gpu_ns_max / 1e6, g_s_sum / 120, g_cnt_sum[0] / n, g_cnt_sum[1] / n, g_cnt_sum[2] / n);
        g_cnt_sum[0] = g_cnt_sum[1] = g_cnt_sum[2] = 0;
        if (!g_ab_on) pt_log(0, "");
        g_gpu_ns_sum = g_gpu_ns_max = 0;
        g_gpu_n = 0;
        g_s_sum = 0.0f;
        g_spec_hits = g_spec_misses = 0;
        g_busy_ticks = g_gpu_ticks = g_upload_bytes = 0;
        g_busy_frames = 0;
    }
}

/* ---- frame queue (as the GL backend) ---- */
static mtx_t g_lock;
static cnd_t g_cv;
static GxFrame g_frames[2];
static GxFrame* g_free[2];
static int g_nfree;
static GxFrame* g_pending;

/* watchdog diagnostics: where the game and render threads are in the frame handoff */
static volatile int g_dbg_take, g_dbg_submit, g_dbg_exec;
static volatile unsigned g_dbg_executed;
void render_dump(void) {
    rt_log("[render] game thread: %s%s; render thread: %s, %u frames executed, %d free, pending %d",
           g_dbg_take ? "waiting for a free frame " : "", g_dbg_submit ? "waiting to submit" : "",
           g_dbg_exec ? "executing" : "idle", g_dbg_executed, g_nfree, g_pending != NULL);
}

GxFrame* render_take_free_frame(void) {
    g_dbg_take = 1;
    mtx_lock(&g_lock);
    while (!g_nfree) cnd_wait(&g_cv, &g_lock);
    GxFrame* f = g_free[--g_nfree];
    mtx_unlock(&g_lock);
    g_dbg_take = 0;
    return f;
}

void render_submit(GxFrame* f) {
    g_dbg_submit = 1;
    mtx_lock(&g_lock);
    while (g_pending) cnd_wait(&g_cv, &g_lock);
    g_pending = f;
    cnd_broadcast(&g_cv);
    mtx_unlock(&g_lock);
    g_dbg_submit = 0;
}

int render_poll_quit(void) { return 0; }

static int render_thread(void* arg) {
    (void)arg;
    rt_thread_pin(1);
    dk_setup();
    for (;;) {
        mtx_lock(&g_lock);
        while (!g_pending) {
            mtx_unlock(&g_lock);
            input_pump();
            if (!appletMainLoop()) exit(0);
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
        input_pump();
        if (!appletMainLoop()) exit(0);
        g_dbg_exec = 1;
        execute(f);
        g_dbg_exec = 0;
        g_dbg_executed++;
        mtx_lock(&g_lock);
        g_pending = NULL;
        g_free[g_nfree++] = f;
        cnd_broadcast(&g_cv);
        mtx_unlock(&g_lock);
    }
    return 0;
}

void render_start(void) {
    mtx_init(&g_lock, mtx_plain);
    cnd_init(&g_cv);
    g_free[0] = &g_frames[0];
    g_free[1] = &g_frames[1];
    g_nfree = 2;
    input_init();
    rt_thread_start(render_thread, NULL);
}
