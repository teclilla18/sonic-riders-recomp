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
#include <string.h>
#include <switch.h>
#include <threads.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_shaders_dksh.h"  /* generated: tools/embed_shaders.py dksh */
#include "overlay.h"
#include "runtime.h"
#include "settings.h"

void input_init(void);
void input_pump(void);

#define EFB_SCALE 2  /* the largest internal resolution; g_s is this frame's (1, 1.5 or 2) */
static float g_s = EFB_SCALE;
#define SC(v) ((uint32_t)((float)(v) * g_s + 0.5f))
#define EFB_W (EFB_WIDTH * EFB_SCALE)
#define EFB_H (EFB_HEIGHT * EFB_SCALE)
#define FB_W 1280
#define FB_H 720
#define MAX_IMAGES 4096
#define NUM_SAMPLERS 36

#define SLOT_CMD   (4u << 20)
#define SLOT_VTX   (48u << 20)
#define SLOT_UBO   (24u << 20)  /* ~14000 states (1792 bytes each) */
#define SLOT_STAGE (32u << 20)
#define SLOT_XFM   (8u << 20)   /* the frame's XF memory chunk pool (storage buffer) */
#define SLOT_SIZE  (SLOT_CMD + SLOT_VTX + SLOT_UBO + SLOT_STAGE + SLOT_XFM)
#define IMAGE_POOL (384u << 20)

static DkDevice g_dev;
static DkQueue g_queue;
static DkCmdBuf g_cmd;
static DkMemBlock g_code, g_slots[2], g_images, g_desc;
static DkShader g_vsh, g_fsh, g_fsh_ind, g_ovs, g_ofs;  /* the GX ubershader (+ indirect variant); the overlay */
static int g_fsh_bound_ind;  /* the variant bound in this frame's command list (-1: none yet) */
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
    uint32_t w = gt->is_copy ? SC(gt->width) : gt->width, h = gt->is_copy ? SC(gt->height) : gt->height;
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
                       sizeof k_overlay_frag_dksh + 5 * DK_SHADER_CODE_ALIGNMENT,
                   DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    uint32_t off = 0;
    shader_load(&g_vsh, k_gx_vert_dksh, sizeof k_gx_vert_dksh, &off);
    shader_load(&g_fsh, k_gx_frag_dksh, sizeof k_gx_frag_dksh, &off);
    shader_load(&g_fsh_ind, k_gx_frag_ind_dksh, sizeof k_gx_frag_ind_dksh, &off);
    shader_load(&g_ovs, k_overlay_vert_dksh, sizeof k_overlay_vert_dksh, &off);
    shader_load(&g_ofs, k_overlay_frag_dksh, sizeof k_overlay_frag_dksh, &off);

    for (int i = 0; i < 2; i++) g_slots[i] = block(SLOT_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
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

static void apply_state(const GxUniforms* u) {
    float xs = f32(u->xfr[0x1A]), ys = f32(u->xfr[0x1B]), xo = f32(u->xfr[0x1D]), yo = f32(u->xfr[0x1E]);
    int sox = (int)(u->bp[0x59] & 0x3FF) * 2, soy = (int)((u->bp[0x59] >> 10) & 0x3FF) * 2;
    float ax = xs < 0 ? -xs : xs, ay = ys < 0 ? -ys : ys;
    DkViewport vp = {(xo - ax - (float)sox) * g_s, (yo - ay - (float)soy) * g_s, 2 * ax * g_s, 2 * ay * g_s, 0.0f, 1.0f};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    DkScissor sc = {SC(x0), SC(y0), SC(x1 > x0 ? x1 - x0 : 0), SC(y1 > y0 ? y1 - y0 : 0)};
    dkCmdBufSetScissors(g_cmd, 0, &sc, 1);

    static const DkCompareOp zf[8] = {DkCompareOp_Never, DkCompareOp_Less, DkCompareOp_Equal, DkCompareOp_Lequal,
                                      DkCompareOp_Greater, DkCompareOp_NotEqual, DkCompareOp_Gequal, DkCompareOp_Always};
    uint32_t z = u->bp[0x40];
    DkDepthStencilState ds;
    dkDepthStencilStateDefaults(&ds);
    ds.depthTestEnable = z & 1;
    ds.depthWriteEnable = (z >> 4) & 1;
    ds.depthCompareOp = zf[(z >> 1) & 7];
    dkCmdBufBindDepthStencilState(g_cmd, &ds);

    uint32_t cm = u->bp[0x41];
    static const DkBlendFactor sf[8] = {DkBlendFactor_Zero, DkBlendFactor_One, DkBlendFactor_DstColor, DkBlendFactor_InvDstColor,
                                        DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_DstAlpha, DkBlendFactor_InvDstAlpha};
    static const DkBlendFactor df[8] = {DkBlendFactor_Zero, DkBlendFactor_One, DkBlendFactor_SrcColor, DkBlendFactor_InvSrcColor,
                                        DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_DstAlpha, DkBlendFactor_InvDstAlpha};
    DkColorState cs;
    dkColorStateDefaults(&cs);
    dkColorStateSetBlendEnable(&cs, 0, cm & 1);
    dkCmdBufBindColorState(g_cmd, &cs);
    DkBlendState bs;
    dkBlendStateDefaults(&bs);
    if ((cm >> 11) & 1) {
        dkBlendStateSetOps(&bs, DkBlendOp_RevSub, DkBlendOp_RevSub);
        dkBlendStateSetFactors(&bs, DkBlendFactor_One, DkBlendFactor_One, DkBlendFactor_One, DkBlendFactor_One);
    } else {
        dkBlendStateSetOps(&bs, DkBlendOp_Add, DkBlendOp_Add);
        DkBlendFactor s = sf[(cm >> 8) & 7], d = df[(cm >> 5) & 7];
        dkBlendStateSetFactors(&bs, s, d, s, d);
    }
    dkCmdBufBindBlendStates(g_cmd, 0, &bs, 1);
    DkColorWriteState cw;
    dkColorWriteStateDefaults(&cw);
    dkColorWriteStateSetMask(&cw, 0, ((cm >> 3) & 1 ? DkColorMask_RGB : 0) | ((cm >> 4) & 1 ? DkColorMask_A : 0));
    dkCmdBufBindColorWriteState(g_cmd, &cw);

    DkRasterizerState rs;
    dkRasterizerStateDefaults(&rs);
    uint32_t cull = (u->bp[0x00] >> 14) & 3;
    rs.cullMode = cull == 0 ? DkFace_None : cull == 1 ? DkFace_Back : cull == 2 ? DkFace_Front : DkFace_FrontAndBack;
    rs.frontFace = DkFrontFace_CW;  /* GX front faces are clockwise on screen (upper-left origin, no flip here) */
    dkCmdBufBindRasterizerState(g_cmd, &rs);
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

/* ---- EFB copies and presentation ---- */
static int g_fb_slot = -1;

/* Self-captures for hardware testing (off unless SR_SHOTS=1, e.g. in env.txt): presented frames
 * k_shots[] are saved to the SD card as shot_<frame>.bmp; each causes a visible hitch. */
static const unsigned k_shots[] = {300, 900, 1800, 3000, 4500, 6000, 7200};
static unsigned g_presented;
static DkMemBlock g_shot_mem;
static unsigned g_shot_pending;  /* frame number captured into g_shot_mem by this command list */

static void shot_request(DkImageView const* fv) {
    unsigned n = ++g_presented;
    static int on = -1;
    if (on < 0) on = getenv("SR_SHOTS") != NULL;
    int want = overlay_take_screenshot_request();  /* the menu's "Save screenshot" */
    if (!on && !want) return;
    if (on)
        for (size_t i = 0; i < sizeof k_shots / sizeof k_shots[0]; i++) want |= k_shots[i] == n;
    if (!want) return;
    if (!g_shot_mem) g_shot_mem = block(FB_W * FB_H * 4, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    dkCmdBufBarrier(g_cmd, DkBarrier_Full, 0);
    DkCopyBuf dst = {dkMemBlockGetGpuAddr(g_shot_mem), 0, 0};
    DkImageRect r = {0, 0, 0, FB_W, FB_H, 1};
    dkCmdBufCopyImageToBuffer(g_cmd, fv, &r, &dst, 0);
    g_shot_pending = n;
}

static void shot_write(void) {
    if (!g_shot_pending) return;
    uint64_t t0 = armGetSystemTick();
    const uint8_t* px = dkMemBlockGetCpuAddr(g_shot_mem);
    char path[96];
    snprintf(path, sizeof path, "sdmc:/switch/sonicriders/shot_%u.bmp", g_shot_pending);
    g_shot_pending = 0;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    uint32_t row = FB_W * 3, size = 54 + row * FB_H;
    uint8_t h[54] = {'B', 'M'};
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    int32_t w = FB_W, hh = FB_H;
    memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    uint8_t* line = malloc(row);
    for (int y = FB_H - 1; y >= 0; y--) {  /* BMP rows go bottom-up */
        const uint8_t* s = px + (size_t)y * FB_W * 4;
        for (int x = 0; x < FB_W; x++) { line[3 * x] = s[4 * x + 2]; line[3 * x + 1] = s[4 * x + 1]; line[3 * x + 2] = s[4 * x]; }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    rt_log("[deko3d] saved %s (%.0f ms: a [hitch] at this frame is this screenshot)", path,
           armTicksToNs(armGetSystemTick() - t0) / 1e6);
}

static void efb_copy(const GxCmd* c, const GxUniforms* u) {
    uint32_t x = c->copy[0], y = c->copy[1], w = c->copy[2], h = c->copy[3];
    DkImageRect src = {SC(x), SC(y), 0, SC(w), SC(h), 1};
    dkCmdBufBarrier(g_cmd, DkBarrier_Primitives, 0);  /* everything drawn so far, before the 2D engine reads */
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
        dkCmdBufBarrier(g_cmd, DkBarrier_Fragments, 0);
        /* at the chosen aspect (settings.h), centred */
        float aspect = settings_display_aspect();
        uint32_t dw = FB_W, dh = FB_H;
        if ((float)FB_W > FB_H * aspect) dw = (uint32_t)(FB_H * aspect + 0.5f);
        else dh = (uint32_t)(FB_W / aspect + 0.5f);
        DkImageRect dst = {(FB_W - dw) / 2, (FB_H - dh) / 2, 0, dw, dh, 1};
        dkCmdBufBlitImage(g_cmd, &g_efb_view, &src, &fv, &dst, DkBlitFlag_FilterLinear, 0);
        shot_request(&fv);
        DkImageView const* efb_targets[1] = {&g_efb_view};
        dkCmdBufBindRenderTargets(g_cmd, efb_targets, 1, &g_efb_depth_view);
    } else if ((int)c->copy[8] >= 0) {  /* to a texture */
        int id = (int)c->copy[8];
        tex_prepare(id);
        DkTex* t = tex_slot(id);
        if (t->live) {
            DkImageView tv;
            dkImageViewDefaults(&tv, &t->image);
            DkImageRect dst = {0, 0, 0, t->w, t->h, 1};
            dkCmdBufBlitImage(g_cmd, &g_efb_view, &src, &tv, &dst, DkBlitFlag_FilterLinear, 0);
            dkCmdBufBarrier(g_cmd, DkBarrier_Primitives, DkInvalidateFlags_Image);
        }
    }
    if (c->copy[5]) {  /* clear the copied rectangle */
        uint32_t argb = c->copy[6], cm = u->bp[0x41];
        DkScissor sc = {SC(x), SC(y), SC(w), SC(h)};
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
    unsigned version;
    const uint8_t* img = overlay_image(&version);
    if (!img) return;
    dkCmdBufBarrier(g_cmd, DkBarrier_Full, DkInvalidateFlags_Image);  /* after the blit into the picture */
    DkImageView ov;
    dkImageViewDefaults(&ov, &g_ov_image);
    uint32_t bytes = OVERLAY_W * OVERLAY_H * 4;
    if (version != g_ov_version && g_stage_off + bytes <= SLOT_STAGE) {
        uint32_t so = SLOT_CMD + SLOT_VTX + SLOT_UBO + g_stage_off;
        memcpy(cpu + so, img, bytes);
        DkCopyBuf src = {gpu + so, 0, 0};
        DkImageRect r = {0, 0, 0, OVERLAY_W, OVERLAY_H, 1};
        dkCmdBufCopyBufferToImage(g_cmd, &src, &ov, &r, 0);
        dkCmdBufBarrier(g_cmd, DkBarrier_Full, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
        g_stage_off = (g_stage_off + bytes + 255) & ~255u;
        g_ov_version = version;
    }
    DkImageView fv;
    dkImageViewDefaults(&fv, &g_fb[g_fb_slot]);
    DkImageView const* targets[1] = {&fv};
    dkCmdBufBindRenderTargets(g_cmd, targets, 1, NULL);
    DkViewport vp = {0, 0, FB_W, FB_H, 0.0f, 1.0f};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    DkScissor sc = {0, 0, FB_W, FB_H};
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
    dkBlendStateSetFactors(&bs, DkBlendFactor_SrcAlpha, DkBlendFactor_InvSrcAlpha, DkBlendFactor_Zero, DkBlendFactor_One);
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
    uint64_t t_start = armGetSystemTick();
    g_s = settings_internal_scale();
    g_last_upload = 0;
    g_last_gpu = 0;
    slot_reclaim(g_slot);
    DkMemBlock mem = g_slots[g_slot];
    uint8_t* cpu = (uint8_t*)dkMemBlockGetCpuAddr(mem);
    DkGpuAddr gpu = dkMemBlockGetGpuAddr(mem);
    dkCmdBufClear(g_cmd);
    dkCmdBufAddMemory(g_cmd, mem, 0, SLOT_CMD);
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

    /* the XF memory chunk pool, read by the vertex shader through the states' chunk tables */
    uint32_t xoff = SLOT_CMD + SLOT_VTX + SLOT_UBO + SLOT_STAGE;
    uint32_t xbytes = f->nxchunks * GX_XFM_CHUNK * 4;
    if (xbytes > SLOT_XFM) {
        rt_log("[deko3d] frame has %u XF memory chunks, more than the pool holds; truncated", f->nxchunks);
        xbytes = SLOT_XFM;
    }
    memcpy(cpu + xoff, f->xchunks, xbytes);

    /* textures this frame uses: created and uploaded before any draw */
    for (uint32_t i = 0; i < f->ncmds; i++)
        if (f->cmds[i].type == GXC_DRAW)
            for (int m = 0; m < 8; m++)
                if (f->cmds[i].tex[m] >= 0) tex_prepare(f->cmds[i].tex[m]);
    dkCmdBufBarrier(g_cmd, DkBarrier_Primitives, DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);

    DkImageView const* targets[1] = {&g_efb_view};
    dkCmdBufBindRenderTargets(g_cmd, targets, 1, &g_efb_depth_view);
    DkShader const* shaders[2] = {&g_vsh, &g_fsh};
    dkCmdBufBindShaders(g_cmd, DkStageFlag_GraphicsMask, shaders, 2);
    g_fsh_bound_ind = 0;
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
    dkCmdBufBindStorageBuffer(g_cmd, DkStage_Vertex, 0, gpu + xoff, xbytes ? xbytes : 256);

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
    uint32_t cur_regs = ~0u;
    for (uint32_t i = 0; i < f->ncmds; i++) {
        const GxCmd* c = &f->cmds[i];
        switch (c->type) {
        case GXC_STATE: {
            if (!ubo_ok) { cur = NULL; break; }
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 1, gpu + xoff_ubo + c->b * xstride, (uint32_t)sizeof(GxXTab));
            if (c->a == cur_regs) break;
            cur_regs = c->a;
            cur = &f->unis[c->a];
            DkGpuAddr a = gpu + uoff + c->a * ustride;
            uint32_t usize = ((uint32_t)sizeof(GxUniforms) + 15) & ~15u;
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 0, a, usize);
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Fragment, 0, a, usize);
            apply_state(cur);
            /* the fragment ubershader with indirect texturing only for draws that use it */
            int ind = ((cur->bp[0x00] >> 16) & 7) != 0;
            if (ind != g_fsh_bound_ind) {
                DkShader const* fs[1] = {ind ? &g_fsh_ind : &g_fsh};
                dkCmdBufBindShaders(g_cmd, DkStageFlag_Fragment, fs, 1);
                g_fsh_bound_ind = ind;
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
        default: break;
        }
    }
    if (g_fb_slot >= 0) draw_overlay(cpu, gpu);
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
        rt_log("[deko3d] %.2f ms/frame on the render thread (%.2f waiting for the GPU), %llu KB/frame texture uploads",
               armTicksToNs(g_busy_ticks) / 120 / 1e6, armTicksToNs(g_gpu_ticks) / 120 / 1e6,
               (unsigned long long)(g_upload_bytes / 120 / 1024));
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
