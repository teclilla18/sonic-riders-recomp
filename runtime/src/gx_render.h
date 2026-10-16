/* Renderer interface: the guest side (gx_render.c, on whichever thread writes the GX FIFO) turns the
 * GX command stream into per-frame command lists; a backend (render_gl.c on the desktop, deko3d on the
 * Switch) executes them on its own render thread. Shading is done by one "ubershader" pair that reads
 * the raw XF/BP register state (GxUniforms), so the backend stays thin and the GLSL is shared. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EFB_WIDTH 640
#define EFB_HEIGHT 528

/* one decoded vertex: raw attributes as floats (the XF stage runs in the vertex shader) */
typedef struct GxVertex {
    float pos[4];      /* xyz, w = position/normal matrix index (row address / 4... see gx_render.c) */
    float nrm[4];      /* xyz, w unused */
    float col[2][4];   /* color0, color1 (RGBA 0..1); white when the vertex has none */
    float tex[8][2];   /* texture coordinates 0..7 */
    float tmtx[8];     /* texture matrix index per coordinate */
} GxVertex;

/* XF memory 0x000-0x67F (matrices, lights) as chunks of 64 words: each frame keeps a pool of chunk
 * copies (GxFrame.xchunks); a state names the pool entry of each chunk (GxUniforms.xtab), so a state
 * change copies only the chunks the game rewrote. */
#define GX_XFM_CHUNK 64
#define GX_XFM_CHUNKS 26

/* the register state the shaders read per state, std140-compatible (one uniform buffer) */
typedef struct GxUniforms {
    uint32_t bp[256];         /* BP registers */
    uint32_t xfr[128];        /* XF registers 0x1000-0x107F */
    uint32_t konst[8];        /* TEV konstant colors (raw BP values) */
    uint32_t xtab[28];        /* pool index of XF memory chunk i (GX_XFM_CHUNKS used) */
} GxUniforms;

enum { GXC_STATE, GXC_DRAW, GXC_COPY, GXC_PRESENT };
enum { GXP_TRIANGLES, GXP_LINES, GXP_POINTS };

typedef struct GxCmd {
    uint8_t type;
    uint8_t prim;             /* GXC_DRAW: GXP_* */
    uint16_t pad;
    uint32_t a, b;            /* DRAW: first vertex, vertex count. STATE: uniforms index */
    int32_t tex[8];           /* DRAW: texture ids per texmap (-1: none) */
    uint32_t copy[12];        /* COPY: see gx_render.c efb_copy */
} GxCmd;

/* a decoded texture, created on the guest side, uploaded by the backend */
typedef struct GxTexture {
    int id;
    uint32_t width, height;
    uint8_t* rgba;            /* width*height*4, owned by the cache; NULL for EFB-copy targets */
    int is_copy;              /* render target filled by an EFB copy */
    uint32_t gen;             /* bumped when the pixels change (backend re-uploads) */
} GxTexture;

typedef struct GxFrame {
    GxCmd* cmds; uint32_t ncmds, cap_cmds;
    GxVertex* verts; uint32_t nverts, cap_verts;
    GxUniforms* unis; uint32_t nunis, cap_unis;
    uint32_t* xchunks; uint32_t nxchunks, cap_xchunks;  /* XF memory chunk copies (GX_XFM_CHUNK words) */
    int* retired; uint32_t nretired, cap_retired;  /* texture ids to free after this frame */
} GxFrame;

/* backend (render_gl.c / render_deko.c) */
void render_start(void);                      /* creates the window and the render thread */
void render_submit(GxFrame* f);               /* hands a finished frame over; blocks while the
                                                 previous one is still being drawn */
GxFrame* render_take_free_frame(void);        /* an empty frame to fill */
GxTexture* gx_texture_get(int id);            /* backend side lookup */
void gx_texture_free(int id);                 /* backend: a retired texture is no longer in use */
int render_poll_quit(void);

#ifdef __cplusplus
}
#endif
