/* Probe V1b (docs/notes/vita-research.md): what does the Vita's GPU path cost for a GameCube-sized
 * frame? Thousands of small draws (a busy race frame: ~5,000-6,000 GX draws of ~8 vertices), each with
 * its own uniforms and vertex data, through one shader pair compiled at run time with the user's
 * libshacccg (ur0:data/libshacccg.suprx), as the port's specialized shaders would be.
 *
 * Measured per mode, over 120 frames (log: ux0:data/gxmbench/log.txt):
 *   CPU submit ms   BeginScene .. EndScene on this thread (uniforms, streams, draw calls)
 *   to idle ms      EndScene .. sceGxmFinish returning (the GPU finishing the scene after submission)
 * Modes: A = uniform + stream + draw per draw; B = A plus a fragment program switch every draw
 * (state churn, like GX's TEV changes). Also: the compile time of 50 shader variants (macros).
 *
 * Our own code and shaders only; no game data. Build: python tools/vita/gxmbench/build.py. */
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/power.h>
#include <psp2/shacccg.h>

#define W 960
#define H 544
#define STRIDE 960
#define NBUF 2
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#define DIR "ux0:data/gxmbench"

static FILE* g_log;
static void lg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (g_log) { vfprintf(g_log, fmt, ap); fputc('\n', g_log); fflush(g_log); }
    va_end(ap);
}
static void die(const char* what, int rc) {
    lg("FATAL: %s (0x%08X)", what, rc);
    if (g_log) fclose(g_log);
    sceKernelExitProcess(1);
    for (;;) {}
}
#define CHECK(call) do { int rc_ = (call); if (rc_ < 0) die(#call, rc_); } while (0)
static SceUInt64 now_us(void) { return sceKernelGetProcessTimeWide(); }

/* ---- GPU memory ---- */
static void* gpu_alloc(SceKernelMemBlockType type, unsigned size, unsigned attribs) {
    size = ALIGN(size, type == SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW ? 256 * 1024 : 4 * 1024);
    SceUID uid = sceKernelAllocMemBlock("gpu", type, size, NULL);
    if (uid < 0) die("sceKernelAllocMemBlock", uid);
    void* mem = NULL;
    CHECK(sceKernelGetMemBlockBase(uid, &mem));
    CHECK(sceGxmMapMemory(mem, size, (SceGxmMemoryAttribFlags)attribs));
    return mem;
}
static void* usse_alloc(int fragment, unsigned size, unsigned* offset) {
    size = ALIGN(size, 4096);
    SceUID uid = sceKernelAllocMemBlock("usse", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, NULL);
    if (uid < 0) die("usse alloc", uid);
    void* mem = NULL;
    CHECK(sceKernelGetMemBlockBase(uid, &mem));
    if (fragment) CHECK(sceGxmMapFragmentUsseMemory(mem, size, offset));
    else CHECK(sceGxmMapVertexUsseMemory(mem, size, offset));
    return mem;
}
static void* patcher_alloc(void* user, unsigned size) { (void)user; return malloc(size); }
static void patcher_free(void* user, void* mem) { (void)user; free(mem); }

typedef struct { void* address; } DisplayData;
static void display_cb(const void* data) {
    const DisplayData* d = data;
    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof fb);
    fb.size = sizeof fb;
    fb.base = d->address;
    fb.pitch = STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = W;
    fb.height = H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
}

/* ---- run-time shader compile (libshacccg) ---- */
static const char k_vs[] =
    "void main(float2 aPosition, uniform float4 uRect, uniform float4 uColor,\n"
    "          out float4 oPos : POSITION, out float4 oCol : COLOR)\n"
    "{\n"
    "    oPos = float4(aPosition * uRect.zw + uRect.xy, 0.5, 1.0);\n"
    "    oCol = uColor * K;\n"
    "}\n";
static const char k_fs[] =
    "float4 main(float4 vCol : COLOR) : COLOR\n"
    "{\n"
    "    return vCol * K;\n"
    "}\n";
static SceShaccCgSourceFile g_src;
static SceShaccCgSourceFile* open_cb(const char* name, const SceShaccCgSourceLocation* loc, const SceShaccCgCompileOptions* o,
                                     const char** err) {
    (void)name; (void)loc; (void)o; (void)err;
    return &g_src;
}
static SceGxmProgram* compile(const char* text, int fragment, const char* k) {
    g_src.fileName = fragment ? "f.cg" : "v.cg";
    g_src.text = text;
    g_src.size = strlen(text);
    SceShaccCgCallbackList cb;
    sceShaccCgInitializeCallbackList(&cb, SCE_SHACCCG_TRIVIAL);
    cb.openFile = open_cb;
    SceShaccCgCompileOptions o;
    sceShaccCgInitializeCompileOptions(&o);
    o.mainSourceFile = g_src.fileName;
    o.targetProfile = fragment ? SCE_SHACCCG_PROFILE_FP : SCE_SHACCCG_PROFILE_VP;
    o.entryFunctionName = "main";
    const char* defs[1] = {k};
    o.macroDefinitionCount = 1;
    o.macroDefinitions = defs;
    o.locale = SCE_SHACCCG_ENGLISH;
    o.useFx = 1;
    o.optimizationLevel = 3;
    o.useFastmath = 1;
    o.useFastint = 1;
    const SceShaccCgCompileOutput* out = sceShaccCgCompileProgram(&o, &cb, 0);
    if (!out || !out->programData) {
        if (out)
            for (int i = 0; i < out->diagnosticCount; i++) lg("shader: %s", out->diagnostics[i].message);
        die("sceShaccCgCompileProgram", out ? -2 : -1);
    }
    SceGxmProgram* p = malloc(out->programSize);
    memcpy(p, out->programData, out->programSize);
    sceShaccCgDestroyCompileOutput(out);
    CHECK(sceGxmProgramCheck(p));
    return p;
}

int main(void) {
    sceIoMkdir(DIR, 0777);
    g_log = fopen(DIR "/log.txt", "w");
    lg("gxmbench (probe V1b) started");
    scePowerSetArmClockFrequency(444);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    lg("clocks: CPU %d, bus %d, GPU %d, xbar %d MHz", scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
       scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());

    int mod = sceKernelLoadStartModule("ur0:data/libshacccg.suprx", 0, NULL, 0, NULL, NULL);
    if (mod < 0) die("loading ur0:data/libshacccg.suprx", mod);
    sceShaccCgSetDefaultAllocator(malloc, free);
    lg("libshacccg loaded: %s", sceShaccCgGetVersionString());

    /* the compile cost of specialized shaders: 50 variants (25 vertex + 25 fragment) */
    {
        SceUInt64 t0 = now_us();
        char k[32];
        for (int i = 0; i < 25; i++) {
            snprintf(k, sizeof k, "K=%d.0/%d.0", i + 1, i + 1);
            free(compile(k_vs, 0, k));
            free(compile(k_fs, 1, k));
        }
        lg("compile: 50 shaders in %.1f ms (%.2f ms each)", (now_us() - t0) / 1000.0, (now_us() - t0) / 50000.0);
    }

    SceGxmInitializeParams ip;
    memset(&ip, 0, sizeof ip);
    ip.displayQueueMaxPendingCount = NBUF - 1;
    ip.displayQueueCallback = display_cb;
    ip.displayQueueCallbackDataSize = sizeof(DisplayData);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    CHECK(sceGxmInitialize(&ip));

    unsigned fusse_off;
    SceGxmContextParams cp;
    memset(&cp, 0, sizeof cp);
    cp.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    /* a bigger vertex ring: 6,000+ default uniform buffers per scene */
    unsigned vring = 4 * SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, vring, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.vertexRingBufferMemSize = vring;
    cp.fragmentRingBufferMem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferMem = usse_alloc(1, SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, &fusse_off);
    cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = fusse_off;
    SceGxmContext* ctx;
    CHECK(sceGxmCreateContext(&cp, &ctx));

    SceGxmRenderTargetParams rp;
    memset(&rp, 0, sizeof rp);
    rp.width = W;
    rp.height = H;
    rp.scenesPerFrame = 1;
    rp.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    rp.driverMemBlock = -1;
    SceGxmRenderTarget* rt;
    CHECK(sceGxmCreateRenderTarget(&rp, &rt));

    void* fb[NBUF];
    SceGxmColorSurface surf[NBUF];
    SceGxmSyncObject* sync[NBUF];
    for (int i = 0; i < NBUF; i++) {
        fb[i] = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 4 * STRIDE * H, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
        memset(fb[i], 0, 4 * STRIDE * H);
        CHECK(sceGxmColorSurfaceInit(&surf[i], SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                                     SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, W, H, STRIDE, fb[i]));
        CHECK(sceGxmSyncObjectCreate(&sync[i]));
    }
    unsigned dw = ALIGN(W, SCE_GXM_TILE_SIZEX), dh = ALIGN(H, SCE_GXM_TILE_SIZEY);
    void* depth = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4 * dw * dh, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
    SceGxmDepthStencilSurface ds;
    CHECK(sceGxmDepthStencilSurfaceInit(&ds, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, dw, depth, NULL));

    unsigned vo, fo;
    SceGxmShaderPatcherParams pp;
    memset(&pp, 0, sizeof pp);
    pp.hostAllocCallback = patcher_alloc;
    pp.hostFreeCallback = patcher_free;
    pp.bufferMem = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 64 * 1024, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE);
    pp.bufferMemSize = 64 * 1024;
    pp.vertexUsseMem = usse_alloc(0, 64 * 1024, &vo);
    pp.vertexUsseMemSize = 64 * 1024;
    pp.vertexUsseOffset = vo;
    pp.fragmentUsseMem = usse_alloc(1, 64 * 1024, &fo);
    pp.fragmentUsseMemSize = 64 * 1024;
    pp.fragmentUsseOffset = fo;
    SceGxmShaderPatcher* patcher;
    CHECK(sceGxmShaderPatcherCreate(&pp, &patcher));

    SceGxmProgram* vgxp = compile(k_vs, 0, "K=1.0");
    SceGxmProgram* fgxp = compile(k_fs, 1, "K=1.0");
    SceGxmProgram* fgxp2 = compile(k_fs, 1, "K=0.75");
    SceGxmShaderPatcherId vid, fid, fid2;
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher, vgxp, &vid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher, fgxp, &fid));
    CHECK(sceGxmShaderPatcherRegisterProgram(patcher, fgxp2, &fid2));
    const SceGxmProgramParameter* pa = sceGxmProgramFindParameterByName(vgxp, "aPosition");
    const SceGxmProgramParameter* pr = sceGxmProgramFindParameterByName(vgxp, "uRect");
    const SceGxmProgramParameter* pc = sceGxmProgramFindParameterByName(vgxp, "uColor");
    if (!pa || !pr || !pc) die("shader parameters not found", 0);
    SceGxmVertexAttribute attr;
    memset(&attr, 0, sizeof attr);
    attr.streamIndex = 0;
    attr.offset = 0;
    attr.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attr.componentCount = 2;
    attr.regIndex = sceGxmProgramParameterGetResourceIndex(pa);
    SceGxmVertexStream stream;
    stream.stride = 8;
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    SceGxmVertexProgram* vprog;
    CHECK(sceGxmShaderPatcherCreateVertexProgram(patcher, vid, &attr, 1, &stream, 1, &vprog));
    SceGxmFragmentProgram *fprog, *fprog2;
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fid, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, NULL, vgxp, &fprog));
    SceGxmBlendInfo blend;
    memset(&blend, 0, sizeof blend);
    blend.colorMask = SCE_GXM_COLOR_MASK_ALL;
    blend.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    blend.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    blend.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
    blend.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaSrc = SCE_GXM_BLEND_FACTOR_ONE;
    blend.alphaDst = SCE_GXM_BLEND_FACTOR_ZERO;
    CHECK(sceGxmShaderPatcherCreateFragmentProgram(patcher, fid2, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE, &blend, vgxp, &fprog2));

    /* each draw has its own 8 vertices (two quads' worth as a strip of 6 triangles' corners) */
    enum { DRAWS = 7000, VPD = 8 };
    float* verts = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, DRAWS * VPD * 8, SCE_GXM_MEMORY_ATTRIB_READ);
    for (int d = 0; d < DRAWS; d++)
        for (int v = 0; v < VPD; v++) {
            verts[(d * VPD + v) * 2 + 0] = (float)(v & 1);
            verts[(d * VPD + v) * 2 + 1] = (float)((v >> 1) & 1);
        }
    uint16_t* idx = gpu_alloc(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 4096, SCE_GXM_MEMORY_ATTRIB_READ);
    static const uint16_t k_idx[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    memcpy(idx, k_idx, sizeof k_idx);

    lg("setup done; draws per frame %d, %d vertices each", DRAWS, VPD);
    int back = 0, front = NBUF - 1;
    for (int mode = 0; mode < 2; mode++) {
        double sub = 0, idle = 0, total = 0, sub_max = 0;
        enum { FRAMES = 120, WARM = 10 };
        for (int f = 0; f < FRAMES + WARM; f++) {
            SceUInt64 t0 = now_us();
            CHECK(sceGxmBeginScene(ctx, 0, rt, NULL, NULL, sync[back], &surf[back], &ds));
            sceGxmSetVertexProgram(ctx, vprog);
            sceGxmSetFragmentProgram(ctx, fprog);
            for (int d = 0; d < DRAWS; d++) {
                if (mode == 1) sceGxmSetFragmentProgram(ctx, (d & 1) ? fprog2 : fprog);
                void* ub;
                sceGxmReserveVertexDefaultUniformBuffer(ctx, &ub);
                float x = -1.0f + 2.0f * (float)((d * 37 + f * 3) % 120) / 120.0f, y = -1.0f + 2.0f * (float)((d * 53) % 68) / 68.0f;
                float rect[4] = {x, y, 0.03f, 0.05f};
                float col[4] = {(float)(d & 7) / 7.0f, (float)((d >> 3) & 7) / 7.0f, mode ? 1.0f : 0.3f, 0.8f};
                sceGxmSetUniformDataF(ub, pr, 0, 4, rect);
                sceGxmSetUniformDataF(ub, pc, 0, 4, col);
                sceGxmSetVertexStream(ctx, 0, verts + d * VPD * 2);
                sceGxmDraw(ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, idx, 12);
            }
            CHECK(sceGxmEndScene(ctx, NULL, NULL));
            SceUInt64 t1 = now_us();
            sceGxmFinish(ctx);
            SceUInt64 t2 = now_us();
            sceGxmPadHeartbeat(&surf[back], sync[back]);
            DisplayData dd = {fb[back]};
            CHECK(sceGxmDisplayQueueAddEntry(sync[front], sync[back], &dd));
            front = back;
            back = (back + 1) % NBUF;
            if (f >= WARM) {
                double s = (t1 - t0) / 1000.0;
                sub += s;
                if (s > sub_max) sub_max = s;
                idle += (t2 - t1) / 1000.0;
                total += (t2 - t0) / 1000.0;
            }
        }
        lg("mode %c (%s): CPU submit %.2f ms (max %.2f), then GPU to idle %.2f ms; BeginScene..Finish %.2f ms per frame",
           'A' + mode, mode ? "fragment program switch every draw" : "uniforms + stream + draw", sub / FRAMES, sub_max,
           idle / FRAMES, total / FRAMES);
    }
    sceGxmDisplayQueueFinish();
    lg("done");
    fclose(g_log);
    g_log = NULL;
    sceKernelExitProcess(0);
    return 0;
}
