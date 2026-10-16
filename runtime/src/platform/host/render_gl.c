/* Desktop backend: SDL2 window + OpenGL 3.3 core on a render thread, executing the frames built by
 * gx_render.c. The EFB is an FBO at EFB_SCALE x the native 640x528, stored top row first (the GX
 * y axis is flipped in the vertex shader), so EFB copies and texture memory share one orientation.
 *
 * SR_HEADLESS=1: no window (frames are recycled at once). SR_SCREENSHOT=N: save the Nth presented
 * frame as build/shot_N.png. */
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "gx.h"
#include "gx_render.h"
#include "gx_shaders.h"
#include "runtime.h"

#define EFB_SCALE 2
#define EFB_W (EFB_WIDTH * EFB_SCALE)
#define EFB_H (EFB_HEIGHT * EFB_SCALE)

/* ---- GL functions beyond 1.1 ---- */
#define GLFUNCS(X) \
    X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) X(PFNGLBUFFERDATAPROC, glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData) X(PFNGLBINDBUFFERRANGEPROC, glBindBufferRange) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex) X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer) X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate) X(PFNGLBLENDEQUATIONPROC, glBlendEquation)
#define DECL(t, n) static t p_##n;
GLFUNCS(DECL)
#undef DECL

static SDL_Window* g_win;
static GLuint g_prog, g_vao, g_vbo, g_ubo, g_fbo, g_efb_color, g_efb_depth, g_copy_fbo;
static GLint g_ubo_align = 256;
static GLuint* g_gltex;
static uint32_t* g_gltex_gen;
static int g_gltex_cap;

static mtx_t g_lock;
static cnd_t g_cv;
static GxFrame g_frames[2];
static GxFrame* g_free[2];
static int g_nfree;
static GxFrame* g_pending;
static volatile int g_quit;
static int g_headless;
static int g_shot_at;
static unsigned g_presented;

static GLuint compile(GLenum type, const char* src) {
    const char* parts[2] = {"#version 330 core\n", src};
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 2, parts, NULL);
    p_glCompileShader(s);
    GLint ok;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetShaderInfoLog(s, sizeof log, NULL, log);
        rt_fatal("shader compile error:\n%s", log);
    }
    return s;
}

static const char* const k_attrs[10] = {"a_pos", "a_nrm", "a_col0", "a_col1", "a_tex01", "a_tex23", "a_tex45", "a_tex67", "a_tm0", "a_tm1"};

static void gl_setup(void) {
#define LOAD(t, n) if (!(p_##n = (t)SDL_GL_GetProcAddress(#n))) rt_fatal("missing GL function " #n);
    GLFUNCS(LOAD)
#undef LOAD
    rt_log("[gl] %s / %s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint vs = compile(GL_VERTEX_SHADER, k_gx_vertex_shader), fs = compile(GL_FRAGMENT_SHADER, k_gx_fragment_shader);
    g_prog = p_glCreateProgram();
    p_glAttachShader(g_prog, vs);
    p_glAttachShader(g_prog, fs);
    for (int i = 0; i < 10; i++) p_glBindAttribLocation(g_prog, (GLuint)i, k_attrs[i]);
    p_glLinkProgram(g_prog);
    GLint ok;
    p_glGetProgramiv(g_prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        p_glGetProgramInfoLog(g_prog, sizeof log, NULL, log);
        rt_fatal("shader link error:\n%s", log);
    }
    p_glUseProgram(g_prog);
    p_glUniformBlockBinding(g_prog, p_glGetUniformBlockIndex(g_prog, "GX"), 0);
    for (int i = 0; i < 8; i++) {
        char n[16];
        snprintf(n, sizeof n, "u_tex%d", i);
        p_glUniform1i(p_glGetUniformLocation(g_prog, n), i);
    }
    glGetIntegerv(0x8A34 /* GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT */, &g_ubo_align);
    p_glGenVertexArrays(1, &g_vao);
    p_glBindVertexArray(g_vao);
    p_glGenBuffers(1, &g_vbo);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    const struct { int n; size_t off; } layout[10] = {
        {4, offsetof(GxVertex, pos)}, {4, offsetof(GxVertex, nrm)}, {4, offsetof(GxVertex, col[0])},
        {4, offsetof(GxVertex, col[1])}, {4, offsetof(GxVertex, tex[0])}, {4, offsetof(GxVertex, tex[2])},
        {4, offsetof(GxVertex, tex[4])}, {4, offsetof(GxVertex, tex[6])}, {4, offsetof(GxVertex, tmtx[0])},
        {4, offsetof(GxVertex, tmtx[4])}};
    for (int i = 0; i < 10; i++) {
        p_glEnableVertexAttribArray((GLuint)i);
        p_glVertexAttribPointer((GLuint)i, layout[i].n, GL_FLOAT, GL_FALSE, sizeof(GxVertex), (void*)layout[i].off);
    }
    p_glGenBuffers(1, &g_ubo);
    /* the EFB */
    glGenTextures(1, &g_efb_color);
    glBindTexture(GL_TEXTURE_2D, g_efb_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W, EFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenTextures(1, &g_efb_depth);
    glBindTexture(GL_TEXTURE_2D, g_efb_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, EFB_W, EFB_H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    p_glGenFramebuffers(1, &g_fbo);
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_efb_color, 0);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_efb_depth, 0);
    if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) rt_fatal("EFB framebuffer incomplete");
    p_glGenFramebuffers(1, &g_copy_fbo);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* ---- textures ---- */
static GLuint gl_texture(int id) {
    GxTexture* t = gx_texture_get(id);
    if (!t) return 0;
    if (id >= g_gltex_cap) {
        int n = g_gltex_cap ? g_gltex_cap : 1024;
        while (n <= id) n *= 2;
        g_gltex = realloc(g_gltex, n * sizeof *g_gltex);
        g_gltex_gen = realloc(g_gltex_gen, n * sizeof *g_gltex_gen);
        memset(g_gltex + g_gltex_cap, 0, (n - g_gltex_cap) * sizeof *g_gltex);
        memset(g_gltex_gen + g_gltex_cap, 0xFF, (n - g_gltex_cap) * sizeof *g_gltex_gen);
        g_gltex_cap = n;
    }
    if (!g_gltex[id]) {
        glGenTextures(1, &g_gltex[id]);
        glBindTexture(GL_TEXTURE_2D, g_gltex[id]);
        if (t->is_copy)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)(t->width * EFB_SCALE), (GLsizei)(t->height * EFB_SCALE), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        g_gltex_gen[id] = t->is_copy ? t->gen : 0xFFFFFFFFu;
    }
    if (!t->is_copy && g_gltex_gen[id] != t->gen) {
        glBindTexture(GL_TEXTURE_2D, g_gltex[id]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)t->width, (GLsizei)t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->rgba);
        g_gltex_gen[id] = t->gen;
    }
    return g_gltex[id];
}

static GLint wrap_mode(uint32_t w) { return w == 0 ? GL_CLAMP_TO_EDGE : w == 2 ? GL_MIRRORED_REPEAT : GL_REPEAT; }

static void bind_textures(const GxCmd* c, const GxUniforms* u) {
    for (int m = 0; m < 8; m++) {
        p_glActiveTexture(GL_TEXTURE0 + m);
        GLuint t = c->tex[m] >= 0 ? gl_texture(c->tex[m]) : 0;
        glBindTexture(GL_TEXTURE_2D, t);
        if (!t) continue;
        uint32_t mode0 = u->bp[(m >= 4 ? 0xA0 : 0x80) + (m & 3)];
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode(mode0 & 3));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode((mode0 >> 2) & 3));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (mode0 >> 4) & 1 ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, ((mode0 >> 5) & 7) >= 4 ? GL_LINEAR : GL_NEAREST);
    }
    p_glActiveTexture(GL_TEXTURE0);
}

/* ---- pipeline state from BP/XF ---- */
static float f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static int g_debug_frame;

static void apply_state(const GxUniforms* u) {
    /* viewport (XF) and scissor (BP), relative to the scissor offset */
    float xs = f32(u->xfr[0x1A]), ys = f32(u->xfr[0x1B]), xo = f32(u->xfr[0x1D]), yo = f32(u->xfr[0x1E]);
    int sox = (int)(u->bp[0x59] & 0x3FF) * 2, soy = (int)((u->bp[0x59] >> 10) & 0x3FF) * 2;
    if (g_debug_frame)
        rt_log("[gxdbg] viewport scale %.1f %.1f off %.1f %.1f | scissor %08X %08X off %d %d | proj %u", xs, ys, xo, yo,
               u->bp[0x20], u->bp[0x21], sox, soy, u->xfr[0x26]);
    float vx = xo - (xs < 0 ? -xs : xs) - (float)sox, vy = yo - (ys < 0 ? -ys : ys) - (float)soy;
    glViewport((GLint)(vx * EFB_SCALE), (GLint)(vy * EFB_SCALE), (GLsizei)(2 * (xs < 0 ? -xs : xs) * EFB_SCALE),
               (GLsizei)(2 * (ys < 0 ? -ys : ys) * EFB_SCALE));
    int x0 = (int)((u->bp[0x20] >> 12) & 0x7FF) - sox, y0 = (int)(u->bp[0x20] & 0x7FF) - soy;
    int x1 = (int)((u->bp[0x21] >> 12) & 0x7FF) - sox + 1, y1 = (int)(u->bp[0x21] & 0x7FF) - soy + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0 * EFB_SCALE, y0 * EFB_SCALE, (x1 > x0 ? x1 - x0 : 0) * EFB_SCALE, (y1 > y0 ? y1 - y0 : 0) * EFB_SCALE);
    /* depth */
    uint32_t z = u->bp[0x40];
    static const GLenum zf[8] = {GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    if (z & 1) { glEnable(GL_DEPTH_TEST); glDepthFunc(zf[(z >> 1) & 7]); } else glDisable(GL_DEPTH_TEST);
    glDepthMask((z >> 4) & 1 ? GL_TRUE : GL_FALSE);
    /* blending and write masks */
    uint32_t cm = u->bp[0x41];
    static const GLenum sf[8] = {GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                 GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    static const GLenum df[8] = {GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                 GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA};
    if (cm & 1) {
        glEnable(GL_BLEND);
        if ((cm >> 11) & 1) { p_glBlendEquation(0x800B /* GL_FUNC_REVERSE_SUBTRACT */); glBlendFunc(GL_ONE, GL_ONE); }
        else { p_glBlendEquation(0x8006 /* GL_FUNC_ADD */); glBlendFunc(sf[(cm >> 8) & 7], df[(cm >> 5) & 7]); }
    } else {
        glDisable(GL_BLEND);
    }
    GLboolean cu = (cm >> 3) & 1, au = (cm >> 4) & 1;
    glColorMask(cu, cu, cu, au);
    /* culling: the y flip in the vertex shader reverses the winding */
    uint32_t cull = (u->bp[0x00] >> 14) & 3;
    if (cull == 0) glDisable(GL_CULL_FACE);
    else { glEnable(GL_CULL_FACE); glFrontFace(GL_CW); glCullFace(cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK); }
}

/* ---- EFB copies and presentation ---- */
static void save_png(const char* path, int w, int h, const uint8_t* rgba);

static void efb_copy(const GxCmd* c, const GxUniforms* u) {
    int x = (int)c->copy[0], y = (int)c->copy[1], w = (int)c->copy[2], h = (int)c->copy[3];
    if (g_debug_frame)
        rt_log("[gxdbg] copy %d,%d %dx%d xfb %u clear %u dest %08X exec %06X", x, y, w, h, c->copy[4], c->copy[5], c->copy[10],
               c->copy[9]);
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_fbo);
    glDisable(GL_SCISSOR_TEST);
    if (c->copy[4]) {  /* to the XFB: show it */
        if (!g_headless) {
            int ww, wh;
            SDL_GetWindowSize(g_win, &ww, &wh);
            p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glColorMask(1, 1, 1, 1);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            /* the EFB is stored top row first: flip while blitting to the window */
            p_glBlitFramebuffer(x * EFB_SCALE, y * EFB_SCALE, (x + w) * EFB_SCALE, (y + h) * EFB_SCALE, 0, wh, ww, 0,
                                GL_COLOR_BUFFER_BIT, GL_LINEAR);
            g_presented++;
            if (g_shot_at && g_presented == (unsigned)g_shot_at) {
                uint8_t* px = malloc((size_t)ww * wh * 4);
                p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px);
                char path[64];
                snprintf(path, sizeof path, "build/shot_%d.png", g_shot_at);
                save_png(path, ww, wh, px);
                free(px);
                rt_log("[gl] screenshot %s", path);
            }
            SDL_GL_SwapWindow(g_win);
        }
    } else if ((int)c->copy[8] >= 0) {  /* to a texture: render-to-texture */
        GxTexture* t = gx_texture_get((int)c->copy[8]);
        GLuint tex = gl_texture((int)c->copy[8]);
        if (t && tex) {
            p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_copy_fbo);
            p_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
            glColorMask(1, 1, 1, 1);
            p_glBlitFramebuffer(x * EFB_SCALE, y * EFB_SCALE, (x + w) * EFB_SCALE, (y + h) * EFB_SCALE, 0, 0,
                                (GLint)(t->width * EFB_SCALE), (GLint)(t->height * EFB_SCALE), GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    if (c->copy[5]) {  /* clear the copied rectangle */
        uint32_t argb = c->copy[6];
        uint32_t cm = u->bp[0x41];
        glEnable(GL_SCISSOR_TEST);
        glScissor(x * EFB_SCALE, y * EFB_SCALE, w * EFB_SCALE, h * EFB_SCALE);
        glColorMask((cm >> 3) & 1, (cm >> 3) & 1, (cm >> 3) & 1, (cm >> 4) & 1);
        glDepthMask((u->bp[0x40] >> 4) & 1 ? GL_TRUE : GL_FALSE);
        glClearColor(((argb >> 16) & 255) / 255.0f, ((argb >> 8) & 255) / 255.0f, (argb & 255) / 255.0f, (argb >> 24) / 255.0f);
        glClearDepth((double)c->copy[7] / 16777215.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
}

static void execute(GxFrame* f) {
    p_glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
    p_glUseProgram(g_prog);
    p_glBindVertexArray(g_vao);
    p_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)f->nverts * sizeof(GxVertex), f->verts, GL_STREAM_DRAW);
    size_t stride = (sizeof(GxUniforms) + (size_t)g_ubo_align - 1) / (size_t)g_ubo_align * (size_t)g_ubo_align;
    p_glBindBuffer(GL_UNIFORM_BUFFER, g_ubo);
    p_glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)(stride * (f->nunis ? f->nunis : 1)), NULL, GL_STREAM_DRAW);
    for (uint32_t i = 0; i < f->nunis; i++)
        p_glBufferSubData(GL_UNIFORM_BUFFER, (GLintptr)(stride * i), sizeof(GxUniforms), &f->unis[i]);
    const GxUniforms* cur = NULL;
    for (uint32_t i = 0; i < f->ncmds; i++) {
        const GxCmd* c = &f->cmds[i];
        switch (c->type) {
        case GXC_STATE:
            cur = &f->unis[c->a];
            p_glBindBufferRange(GL_UNIFORM_BUFFER, 0, g_ubo, (GLintptr)(stride * c->a), sizeof(GxUniforms));
            apply_state(cur);
            break;
        case GXC_DRAW:
            if (!cur) break;
            bind_textures(c, cur);
            glDrawArrays(c->prim == GXP_TRIANGLES ? GL_TRIANGLES : c->prim == GXP_LINES ? GL_LINES : GL_POINTS,
                         (GLint)c->a, (GLsizei)c->b);
            break;
        case GXC_COPY:
            if (cur) efb_copy(c, cur);
            if (cur) apply_state(cur);
            break;
        default: break;
        }
    }
    for (uint32_t i = 0; i < f->nretired; i++) {
        int id = f->retired[i];
        if (id < g_gltex_cap && g_gltex[id]) { glDeleteTextures(1, &g_gltex[id]); g_gltex[id] = 0; }
        gx_texture_free(id);
    }
}

/* ---- frame queue ---- */
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

static int render_thread(void* arg) {
    (void)arg;
    if (!g_headless) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        g_win = SDL_CreateWindow("Sonic Riders (native)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
        if (!g_win) rt_fatal("SDL_CreateWindow: %s", SDL_GetError());
        if (!SDL_GL_CreateContext(g_win)) rt_fatal("SDL_GL_CreateContext: %s", SDL_GetError());
        SDL_GL_SetSwapInterval(0);
        gl_setup();
    }
    for (;;) {
        mtx_lock(&g_lock);
        while (!g_pending) {
            mtx_unlock(&g_lock);
            if (!g_headless) {
                SDL_Event e;
                while (SDL_PollEvent(&e))
                    if (e.type == SDL_QUIT) g_quit = 1;
            }
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
        if (g_headless) {
            for (uint32_t i = 0; i < f->nretired; i++) gx_texture_free(f->retired[i]);
        } else {
            const char* dbg = getenv("SR_GXDEBUG");
            g_debug_frame = dbg && (unsigned)atoi(dbg) == g_presented + 1;
            execute(f);
            g_debug_frame = 0;
        }
        mtx_lock(&g_lock);
        g_pending = NULL;
        g_free[g_nfree++] = f;
        cnd_broadcast(&g_cv);
        mtx_unlock(&g_lock);
        if (g_quit) {
            rt_log("[gl] window closed");
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
    const char* h = getenv("SR_HEADLESS");
    g_headless = h && *h == '1';
    const char* s = getenv("SR_SCREENSHOT");
    g_shot_at = s ? atoi(s) : 0;
    if (!g_headless && SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0)
        rt_fatal("SDL_Init: %s", SDL_GetError());
    thrd_t t;
    if (thrd_create(&t, render_thread, NULL) != thrd_success) rt_fatal("cannot start the render thread");
    thrd_detach(t);
}

/* ---- minimal PNG writer (stored deflate blocks), for screenshots ---- */
static uint32_t crc_table[256];
static uint32_t crc32_of(const uint8_t* p, size_t n, uint32_t c) {
    if (!crc_table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    c = ~c;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 255] ^ (c >> 8);
    return ~c;
}
static void be32w(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }
static void chunk(FILE* f, const char* type, const uint8_t* data, uint32_t n) {
    uint8_t h[8];
    be32w(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    fwrite(data, 1, n, f);
    uint32_t c = crc32_of((const uint8_t*)type, 4, 0);
    c = crc32_of(data, n, c);
    be32w(h, c);
    fwrite(h, 1, 4, f);
}
static void save_png(const char* path, int w, int h, const uint8_t* rgba) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    be32w(ihdr, (uint32_t)w);
    be32w(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    size_t raw_n = (size_t)h * (1 + (size_t)w * 4);
    uint8_t* raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {  /* GL rows are bottom first */
        raw[(size_t)y * (1 + w * 4)] = 0;
        memcpy(raw + (size_t)y * (1 + w * 4) + 1, rgba + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    }
    size_t nblocks = (raw_n + 65534) / 65535;
    size_t z_n = 2 + raw_n + nblocks * 5 + 4;
    uint8_t* z = malloc(z_n);
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_n; i += 65535) {
        size_t n = raw_n - i < 65535 ? raw_n - i : 65535;
        z[o++] = i + n >= raw_n ? 1 : 0;
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8); z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + i, n);
        o += n;
    }
    for (size_t i = 0; i < raw_n; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32w(z + o, b << 16 | a);
    o += 4;
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}
