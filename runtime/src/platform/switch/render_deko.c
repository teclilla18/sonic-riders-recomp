/* Switch backend: deko3d, executing the frames built by gx_render.c on a render thread. Mirrors the
 * GL backend (platform/host/render_gl.c); the shaders are the same GLSL, compiled ahead of time with
 * uam (tools/embed_shaders.py). The EFB is an image at EFB_SCALE x 640x528, stored top row first
 * like GX memory: with deko3d 0.5's upper-left origin, clip-space +y is the top row, so the shader
 * does not flip y (CLIP_Y) as it does for GL, and front faces keep the GX winding (CCW).
 *
 * Memory: a code block for the shaders; two frame slots (command memory, vertices, uniforms,
 * texture staging), used alternately; an image pool with a first-fit allocator; descriptor tables
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
#include "runtime.h"

void input_init(void);
void input_pump(void);

#define EFB_SCALE 2
#define EFB_W (EFB_WIDTH * EFB_SCALE)
#define EFB_H (EFB_HEIGHT * EFB_SCALE)
#define FB_W 1280
#define FB_H 720
#define MAX_IMAGES 4096
#define NUM_SAMPLERS 36

#define SLOT_CMD   (4u << 20)
#define SLOT_VTX   (48u << 20)
#define SLOT_UBO   (16u << 20)
#define SLOT_STAGE (32u << 20)
#define SLOT_SIZE  (SLOT_CMD + SLOT_VTX + SLOT_UBO + SLOT_STAGE)
#define IMAGE_POOL (384u << 20)

static DkDevice g_dev;
static DkQueue g_queue;
static DkCmdBuf g_cmd;
static DkMemBlock g_code, g_slots[2], g_images, g_desc;
static DkShader g_vsh, g_fsh;
static DkSwapchain g_swap;
static DkImage g_fb[2], g_efb, g_efb_depth;
static DkImageView g_efb_view, g_efb_depth_view;
static unsigned g_slot;
static uint32_t g_vtx_off, g_ubo_off, g_stage_off;
static uint64_t g_busy_ticks, g_upload_bytes;  /* for the [deko3d] timing line */
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
    uint32_t w = gt->is_copy ? gt->width * EFB_SCALE : gt->width, h = gt->is_copy ? gt->height * EFB_SCALE : gt->height;
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

    g_code = block(sizeof k_gx_vert_dksh + sizeof k_gx_frag_dksh + 2 * DK_SHADER_CODE_ALIGNMENT,
                   DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code);
    uint32_t off = 0;
    shader_load(&g_vsh, k_gx_vert_dksh, sizeof k_gx_vert_dksh, &off);
    shader_load(&g_fsh, k_gx_frag_dksh, sizeof k_gx_frag_dksh, &off);

    for (int i = 0; i < 2; i++) g_slots[i] = block(SLOT_SIZE, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    g_images = block(IMAGE_POOL, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image);
    g_free_list = malloc(sizeof *g_free_list);
    g_free_list->off = 0; g_free_list->size = IMAGE_POOL; g_free_list->next = NULL;
    g_desc = block(MAX_IMAGES * sizeof(DkImageDescriptor) + NUM_SAMPLERS * sizeof(DkSamplerDescriptor),
                   DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached);
    for (int i = 0; i < MAX_IMAGES - 1; i++) g_slot_free[g_nslot_free++] = (uint32_t)(MAX_IMAGES - 1 - i);  /* slot 0: dummy */

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
    DkViewport vp = {(xo - ax - (float)sox) * EFB_SCALE, (yo - ay - (float)soy) * EFB_SCALE, 2 * ax * EFB_SCALE,
                     2 * ay * EFB_SCALE, 0.0f, 1.0f};
    dkCmdBufSetViewports(g_cmd, 0, &vp, 1);
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    DkScissor sc = {(uint32_t)x0 * EFB_SCALE, (uint32_t)y0 * EFB_SCALE, (uint32_t)(x1 > x0 ? x1 - x0 : 0) * EFB_SCALE,
                    (uint32_t)(y1 > y0 ? y1 - y0 : 0) * EFB_SCALE};
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
    rs.frontFace = DkFrontFace_CCW;  /* the opposite of the GL backend, whose shader flips y */
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

/* Self-captures for hardware testing: presented frames k_shots[] are saved to the SD card as
 * shot_<frame>.bmp, so a test run shows what was drawn without a capture card. */
static const unsigned k_shots[] = {300, 900, 1800, 3000, 4500};
static unsigned g_presented;
static DkMemBlock g_shot_mem;
static unsigned g_shot_pending;  /* frame number captured into g_shot_mem by this command list */

static void shot_request(DkImageView const* fv) {
    unsigned n = ++g_presented;
    int want = 0;
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
    rt_log("[deko3d] saved %s", path);
}

static void efb_copy(const GxCmd* c, const GxUniforms* u) {
    uint32_t x = c->copy[0], y = c->copy[1], w = c->copy[2], h = c->copy[3];
    DkImageRect src = {x * EFB_SCALE, y * EFB_SCALE, 0, w * EFB_SCALE, h * EFB_SCALE, 1};
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
        uint32_t dw = FB_H * 4 / 3;
        DkImageRect dst = {(FB_W - dw) / 2, 0, 0, dw, FB_H, 1};
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
        DkScissor sc = {x * EFB_SCALE, y * EFB_SCALE, w * EFB_SCALE, h * EFB_SCALE};
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

static void execute(GxFrame* f) {
    uint64_t t_start = armGetSystemTick();
    DkMemBlock mem = g_slots[g_slot];
    uint8_t* cpu = (uint8_t*)dkMemBlockGetCpuAddr(mem);
    DkGpuAddr gpu = dkMemBlockGetGpuAddr(mem);
    dkCmdBufClear(g_cmd);
    dkCmdBufAddMemory(g_cmd, mem, 0, SLOT_CMD);
    g_vtx_off = g_ubo_off = g_stage_off = 0;

    /* vertices */
    uint32_t vbytes = f->nverts * (uint32_t)sizeof(GxVertex);
    if (vbytes > SLOT_VTX) {
        rt_log("[deko3d] frame has %u vertices, more than the buffer holds; truncated", f->nverts);
        vbytes = SLOT_VTX / sizeof(GxVertex) * sizeof(GxVertex);
    }
    memcpy(cpu + SLOT_CMD, f->verts, vbytes);

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

    const GxUniforms* cur = NULL;
    uint32_t ubo_size = ((uint32_t)sizeof(GxUniforms) + 15) & ~15u;
    for (uint32_t i = 0; i < f->ncmds; i++) {
        const GxCmd* c = &f->cmds[i];
        switch (c->type) {
        case GXC_STATE: {
            cur = &f->unis[c->a];
            if (g_ubo_off + ubo_size > SLOT_UBO) { cur = NULL; break; }
            memcpy(cpu + SLOT_CMD + SLOT_VTX + g_ubo_off, cur, sizeof(GxUniforms));
            DkGpuAddr a = gpu + SLOT_CMD + SLOT_VTX + g_ubo_off;
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Vertex, 0, a, ubo_size);
            dkCmdBufBindUniformBuffer(g_cmd, DkStage_Fragment, 0, a, ubo_size);
            g_ubo_off = (g_ubo_off + ubo_size + DK_UNIFORM_BUF_ALIGNMENT - 1) & ~(DK_UNIFORM_BUF_ALIGNMENT - 1);
            apply_state(cur);
            break;
        }
        case GXC_DRAW:
            if (!cur || (c->a + c->b) * sizeof(GxVertex) > vbytes) break;
            bind_textures(c, cur);
            dkCmdBufDraw(g_cmd, c->prim == GXP_TRIANGLES ? DkPrimitive_Triangles : c->prim == GXP_LINES ? DkPrimitive_Lines
                                                                                                         : DkPrimitive_Points,
                         c->b, 1, c->a, 0);
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
    dkQueueSubmitCommands(g_queue, dkCmdBufFinishList(g_cmd));
    if (g_fb_slot >= 0) {
        dkQueuePresentImage(g_queue, g_swap, g_fb_slot);
        g_fb_slot = -1;
    }
    dkQueueFlush(g_queue);
    dkQueueWaitIdle(g_queue);
    shot_write();
    for (uint32_t i = 0; i < f->nretired; i++) {
        tex_destroy(f->retired[i]);
        gx_texture_free(f->retired[i]);
    }
    g_slot ^= 1;
    /* render-thread cost per frame (build + GPU, as execute waits for the GPU) */
    g_busy_ticks += armGetSystemTick() - t_start;
    if (++g_busy_frames == 120) {
        rt_log("[deko3d] %.2f ms/frame on the render thread, %llu KB/frame texture uploads",
               armTicksToNs(g_busy_ticks) / 120 / 1e6, (unsigned long long)(g_upload_bytes / 120 / 1024));
        g_busy_ticks = g_upload_bytes = 0;
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
        execute(f);
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
