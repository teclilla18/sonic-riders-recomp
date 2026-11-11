/* Android milestone 1 probe (docs/ANDROID_PLAN.md): a small SDL2 app with no game code that reports what
 * the port needs to know about a phone, then quits by itself (about 20 s):
 *   - system: Android version, SoC, CPU clusters (capacity, max clock), whether thread affinity and a
 *     raised audio priority are allowed, performance hint and thermal APIs;
 *   - audio: a 440 Hz tone for 2 s through SDL at the game's 32 kHz (driver, granted rate/buffer);
 *   - input: controllers present, every button/touch event while it runs;
 *   - GLES 3.2: renderer, version, the extensions the GX shader needs (dual-source blending, framebuffer
 *     fetch, ...), whether a dual-source blend shader compiles; a screenshot (probe_gles.bmp);
 *   - Vulkan: version, driver, the features/extensions/limits a backend would use;
 *   - draw cost: 7,000 small draws a frame, each with its own uniform range, a pipeline/blend change
 *     every 8 draws (like a busy race view), in GLES and in Vulkan: CPU ms to issue a frame.
 * Output: logcat tag "srprobe" and probe.log in the app's files folder
 * (/sdcard/Android/data/<package>/files/). Launch arguments (SonicRidersActivity "args"):
 * PROBE_DRAWS=<n> (default 7000), PROBE_FRAMES=<n> measured frames per API (default 180). */
#define _GNU_SOURCE  /* cpu_set_t, sched_getcpu */
#include <SDL.h>
#include <SDL_vulkan.h>
#include <GLES3/gl32.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/system_properties.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

static const uint32_t k_vs_spv[] =
#include "probe_vert.inc"
    ;
static const uint32_t k_fs_spv[] =
#include "probe_frag.inc"
    ;

static FILE* g_log;
static void plog(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, "srprobe", buf);
    if (g_log) {
        fprintf(g_log, "%s\n", buf);
        fflush(g_log);
    }
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int cmp_d(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}
static double median(double* v, int n) {
    qsort(v, n, sizeof *v, cmp_d);
    return n ? v[n / 2] : 0;
}

static int g_draws = 7000, g_frames = 180;

/* ---------------------------------------------------------------- system */
static long read_long(const char* path) {
    FILE* f = fopen(path, "r");
    long v = -1;
    if (f) {
        if (fscanf(f, "%ld", &v) != 1) v = -1;
        fclose(f);
    }
    return v;
}

static void probe_system(void) {
    char v[PROP_VALUE_MAX];
    static const char* props[] = {"ro.build.version.release", "ro.build.version.sdk", "ro.board.platform",
                                  "ro.soc.model", "ro.hardware", "ro.product.cpu.abi"};
    for (unsigned i = 0; i < sizeof props / sizeof *props; i++) {
        v[0] = 0;
        __system_property_get(props[i], v);
        plog("[sys] %s = %s", props[i], v);
    }
    plog("[sys] page size %ld, %ld CPUs online", sysconf(_SC_PAGESIZE), sysconf(_SC_NPROCESSORS_ONLN));
    int n = (int)sysconf(_SC_NPROCESSORS_CONF);
    long best = 0;
    for (int c = 0; c < n && c < 32; c++) {
        char p[128];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpu_capacity", c);
        long cap = read_long(p);
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        long mhz = read_long(p) / 1000;
        plog("[cpu] cpu%d capacity %ld max %ld MHz", c, cap, mhz);
        if (mhz > best) best = mhz;
    }
    /* affinity: the fastest cores only (what the game thread would get) */
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c = 0; c < n && c < 32; c++) {
        char p[128];
        snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
        if (read_long(p) / 1000 == best) CPU_SET(c, &set);
    }
    int r = sched_setaffinity(0, sizeof set, &set);
    plog("[thread] affinity to the fastest cluster: %s (now on cpu%d)", r == 0 ? "allowed" : strerror(errno), sched_getcpu());
    CPU_ZERO(&set);
    for (int c = 0; c < n && c < 32; c++) CPU_SET(c, &set);
    sched_setaffinity(0, sizeof set, &set);
    /* priorities: THREAD_PRIORITY_AUDIO (-16), THREAD_PRIORITY_URGENT_AUDIO (-19) */
    int before = getpriority(PRIO_PROCESS, 0);
    for (int want = -16; want >= -19; want -= 3) {
        int ok = setpriority(PRIO_PROCESS, 0, want) == 0;
        plog("[thread] nice %d: %s", want, ok ? "allowed" : strerror(errno));
    }
    setpriority(PRIO_PROCESS, 0, before);
    void* a = dlopen("libandroid.so", RTLD_NOW);
    plog("[api] APerformanceHint_getManager: %s", a && dlsym(a, "APerformanceHint_getManager") ? "present" : "missing");
    plog("[api] AThermal_getThermalHeadroom: %s", a && dlsym(a, "AThermal_getThermalHeadroom") ? "present" : "missing");
    plog("[api] ANativeWindow_setFrameRate: %s", a && dlsym(a, "ANativeWindow_setFrameRate") ? "present" : "missing");
    if (a) {
        typedef void* (*AcquireFn)(void);
        typedef float (*HeadroomFn)(void*, int);
        AcquireFn acq = (AcquireFn)dlsym(a, "AThermal_acquireManager");
        HeadroomFn hr = (HeadroomFn)dlsym(a, "AThermal_getThermalHeadroom");
        if (acq && hr) {
            void* m = acq();
            if (m) plog("[api] thermal headroom now %.2f (1.0 = throttling)", hr(m, 0));
        }
    }
}

/* ---------------------------------------------------------------- audio */
static SDL_AudioDeviceID g_audio;
static void probe_audio(void) {
    SDL_AudioSpec want = {0}, have;
    want.freq = 32000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 512;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_audio) {
        plog("[audio] no device: %s", SDL_GetError());
        return;
    }
    plog("[audio] driver %s, %d Hz, %d channels, buffer %d frames", SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples);
    static int16_t tone[32000 * 2 * 2];
    for (int i = 0; i < 32000 * 2; i++) {
        float t = (float)i / 32000.0f, env = t < 0.05f ? t / 0.05f : t > 1.9f ? (2.0f - t) / 0.1f : 1.0f;
        int16_t s = (int16_t)(SDL_sinf(t * 440.0f * 6.2831853f) * 6000.0f * env);
        tone[i * 2] = tone[i * 2 + 1] = s;
    }
    SDL_QueueAudio(g_audio, tone, sizeof tone);
    SDL_PauseAudioDevice(g_audio, 0);
}

/* ---------------------------------------------------------------- events */
static int pump(void) {
    SDL_Event e;
    int quit = 0;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: quit = 1; break;
        case SDL_CONTROLLERDEVICEADDED: {
            SDL_GameController* c = SDL_GameControllerOpen(e.cdevice.which);
            plog("[input] controller added: %s", c ? SDL_GameControllerName(c) : SDL_GetError());
            if (c && SDL_GameControllerHasRumble(c)) plog("[input]   it can rumble");
            break;
        }
        case SDL_CONTROLLERBUTTONDOWN:
            plog("[input] button %s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
            break;
        case SDL_FINGERDOWN: plog("[input] touch at %.2f, %.2f", e.tfinger.x, e.tfinger.y); break;
        case SDL_KEYDOWN: plog("[input] key %s", SDL_GetKeyName(e.key.keysym.sym)); break;
        case SDL_APP_WILLENTERBACKGROUND: plog("[life] going to the background"); break;
        case SDL_APP_DIDENTERFOREGROUND: plog("[life] back in the foreground"); break;
        }
    }
    return quit;
}

/* ---------------------------------------------------------------- GLES */
static const char* g_glsl_version = "#version 320 es\n";  /* matches the context that was created */
static GLuint compile(GLenum type, const char* src, int quiet) {
    GLuint s = glCreateShader(type);
    const char* parts[2] = {g_glsl_version, src};
    glShaderSource(s, 2, parts, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char msg[512] = "";
        glGetShaderInfoLog(s, sizeof msg, NULL, msg);
        if (!quiet) plog("[gles] shader error: %s", msg);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static const char* k_gl_vs =
    "layout(std140) uniform U { vec4 off; vec4 col; };\n"
    "out vec4 v_col;\n"
    "void main() { vec2 p = vec2(gl_VertexID == 1 ? 0.02 : 0.0, gl_VertexID == 2 ? 0.02 : 0.0);\n"
    "  gl_Position = vec4(p + off.xy, 0.0, 1.0); v_col = col; }\n";
static const char* k_gl_fs =
    "precision highp float;\nin vec4 v_col;\nlayout(location = 0) out vec4 o;\n"
    "void main() { o = v_col; }\n";
static const char* k_gl_fs_dual =
    "#extension GL_EXT_blend_func_extended : require\nprecision highp float;\nin vec4 v_col;\n"
    "layout(location = 0, index = 0) out vec4 o;\nlayout(location = 0, index = 1) out vec4 o1;\n"
    "void main() { o = v_col; o1 = vec4(0.5); }\n";

static void save_bmp(int w, int h, const char* name) {
    uint8_t* px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < h; y++) memcpy((uint8_t*)s->pixels + (size_t)y * s->pitch, px + (size_t)(h - 1 - y) * w * 4, (size_t)w * 4);
    char path[512];
    snprintf(path, sizeof path, "%s/%s", SDL_AndroidGetExternalStoragePath(), name);
    plog("[gles] screenshot %s: %s", name, SDL_SaveBMP(s, path) == 0 ? "saved" : SDL_GetError());
    SDL_FreeSurface(s);
    free(px);
}

static double g_gl_issue = -1, g_gl_frame = -1, g_vk_issue = -1, g_vk_frame = -1;

static void probe_gles(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_Window* w = SDL_CreateWindow("probe", 0, 0, 0, 0, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!w) {
        plog("[gles] window: %s", SDL_GetError());
        return;
    }
    /* ES 3.2 (the port's target), else what the device has: the emulator stops below 3.2 */
    static const char* versions[3] = {"#version 320 es\n", "#version 310 es\n", "#version 300 es\n"};
    SDL_GLContext ctx = NULL;
    for (int minor = 2; minor >= 0 && !ctx; minor--) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
        ctx = SDL_GL_CreateContext(w);
        if (!ctx) plog("[gles] no ES 3.%d context: %s", minor, SDL_GetError());
        else g_glsl_version = versions[2 - minor];
    }
    if (!ctx) {
        SDL_DestroyWindow(w);
        return;
    }
    SDL_GL_SetSwapInterval(1);
    int ww, wh;
    SDL_GL_GetDrawableSize(w, &ww, &wh);
    plog("[gles] %s / %s / %s, drawable %dx%d", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION), ww, wh);
    static const char* exts[] = {"GL_EXT_blend_func_extended", "GL_EXT_shader_framebuffer_fetch",
                                 "GL_ARM_shader_framebuffer_fetch", "GL_EXT_texture_buffer", "GL_EXT_buffer_storage",
                                 "GL_EXT_clip_control", "GL_EXT_depth_clamp", "GL_EXT_disjoint_timer_query",
                                 "GL_KHR_texture_compression_astc_ldr", "GL_OES_get_program_binary"};
    GLint next = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &next);
    for (unsigned i = 0; i < sizeof exts / sizeof *exts; i++) {
        int have = 0;
        for (GLint k = 0; k < next && !have; k++) have = !strcmp((const char*)glGetStringi(GL_EXTENSIONS, k), exts[i]);
        plog("[gles] %s: %s", exts[i], have ? "yes" : "no");
    }
    GLint ua = 0, ubs = 0, nbin = 0;
    glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &ua);
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &ubs);
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &nbin);
    plog("[gles] uniform offset alignment %d, max block %d, program binary formats %d", ua, ubs, nbin);
    GLuint dual = compile(GL_FRAGMENT_SHADER, k_gl_fs_dual, 1);
    plog("[gles] dual-source blend shader compiles: %s", dual ? "yes" : "no");
    if (dual) glDeleteShader(dual);

    GLuint vs = compile(GL_VERTEX_SHADER, k_gl_vs, 0), fs = compile(GL_FRAGMENT_SHADER, k_gl_fs, 0);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUniformBlockBinding(prog, glGetUniformBlockIndex(prog, "U"), 0);
    GLuint vao, ubo;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    GLint stride = ua > 32 ? ua : 32;
    size_t bytes = (size_t)stride * g_draws;
    uint8_t* data = calloc(1, bytes);
    for (int i = 0; i < g_draws; i++) {
        float* f = (float*)(data + (size_t)i * stride);
        f[0] = -0.95f + 1.9f * (float)(i % 100) / 100.0f;
        f[1] = -0.95f + 1.9f * (float)(i / 100 % 70) / 70.0f;
        f[4] = (float)(i % 7) / 7.0f; f[5] = (float)(i % 13) / 13.0f; f[6] = 0.6f; f[7] = 1.0f;
    }
    glGenBuffers(1, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, ubo);
    glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_DRAW);
    glUseProgram(prog);
    double* issue = calloc(g_frames, sizeof(double));
    double* frame = calloc(g_frames, sizeof(double));
    int m = 0;
    double last = now_ms();
    for (int fr = 0; fr < g_frames + 30; fr++) {
        if (pump()) break;
        double t0 = now_ms();
        glBindBuffer(GL_UNIFORM_BUFFER, ubo);
        glBufferData(GL_UNIFORM_BUFFER, (GLsizeiptr)bytes, NULL, GL_STREAM_DRAW);  /* orphan, as a renderer would per frame */
        glBufferSubData(GL_UNIFORM_BUFFER, 0, (GLsizeiptr)bytes, data);
        glViewport(0, 0, ww, wh);
        glClearColor(0.05f, 0.1f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (int i = 0; i < g_draws; i++) {
            if ((i & 7) == 0) {  /* a state change every 8 draws */
                if (i & 8) {
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                } else {
                    glDisable(GL_BLEND);
                }
            }
            glBindBufferRange(GL_UNIFORM_BUFFER, 0, ubo, (GLintptr)i * stride, 32);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        double t1 = now_ms();
        if (fr == g_frames + 29) save_bmp(ww, wh, "probe_gles.bmp");
        SDL_GL_SwapWindow(w);
        double t2 = now_ms();
        if (fr >= 30) {
            issue[m] = t1 - t0;
            frame[m] = t2 - last;
            m++;
        }
        last = t2;
    }
    g_gl_issue = median(issue, m);
    g_gl_frame = median(frame, m);
    plog("[bench] GLES: %d draws, CPU issue median %.2f ms, frame median %.2f ms (%d frames)", g_draws, g_gl_issue, g_gl_frame, m);
    free(issue);
    free(frame);
    free(data);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(w);
}

/* ---------------------------------------------------------------- Vulkan */
#define VKCHECK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { plog("[vk] %s failed: %d", #x, (int)r_); goto out; } } while (0)

static int has_ext(const VkExtensionProperties* e, uint32_t n, const char* name) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(e[i].extensionName, name)) return 1;
    return 0;
}

static void probe_vulkan(void) {
    SDL_Window* w = SDL_CreateWindow("probe", 0, 0, 0, 0, SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN);
    VkInstance inst = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkSurfaceKHR surf = VK_NULL_HANDLE;
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    VkExtensionProperties* de = NULL;
    double *issue = NULL, *frame = NULL;
    if (!w) {
        plog("[vk] window: %s", SDL_GetError());
        return;
    }
    uint32_t iv = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&iv);
    plog("[vk] instance version %u.%u.%u", VK_VERSION_MAJOR(iv), VK_VERSION_MINOR(iv), VK_VERSION_PATCH(iv));
    unsigned nie = 0;
    SDL_Vulkan_GetInstanceExtensions(w, &nie, NULL);
    const char* ie[8];
    if (nie > 8) nie = 8;
    SDL_Vulkan_GetInstanceExtensions(w, &nie, ie);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "srprobe", 1, "srprobe", 1, VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, nie, ie};
    VKCHECK(vkCreateInstance(&ici, NULL, &inst));
    uint32_t npd = 1;
    VkPhysicalDevice pd;
    VkResult er = vkEnumeratePhysicalDevices(inst, &npd, &pd);
    if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || npd == 0) {
        plog("[vk] no physical device");
        goto out;
    }
    VkPhysicalDeviceProperties pp;
    vkGetPhysicalDeviceProperties(pd, &pp);
    plog("[vk] %s, API %u.%u.%u, driver 0x%08X, vendor 0x%04X", pp.deviceName, VK_VERSION_MAJOR(pp.apiVersion),
         VK_VERSION_MINOR(pp.apiVersion), VK_VERSION_PATCH(pp.apiVersion), pp.driverVersion, pp.vendorID);
    VkPhysicalDeviceFeatures pf;
    vkGetPhysicalDeviceFeatures(pd, &pf);
    plog("[vk] features: dualSrcBlend %d, logicOp %d, depthClamp %d, depthBiasClamp %d, samplerAnisotropy %d, "
         "textureCompressionBC %d, shaderInt16 %d, wideLines %d, largePoints %d", pf.dualSrcBlend, pf.logicOp, pf.depthClamp,
         pf.depthBiasClamp, pf.samplerAnisotropy, pf.textureCompressionBC, pf.shaderInt16, pf.wideLines, pf.largePoints);
    plog("[vk] limits: maxUniformBufferRange %u, minUniformBufferOffsetAlignment %u, maxBoundDescriptorSets %u, "
         "maxPushConstantsSize %u, timestampPeriod %.2f ns, timestamps in graphics %d", pp.limits.maxUniformBufferRange,
         (unsigned)pp.limits.minUniformBufferOffsetAlignment, pp.limits.maxBoundDescriptorSets, pp.limits.maxPushConstantsSize,
         pp.limits.timestampPeriod, pp.limits.timestampComputeAndGraphics);
    uint32_t nde = 0;
    vkEnumerateDeviceExtensionProperties(pd, NULL, &nde, NULL);
    de = calloc(nde ? nde : 1, sizeof *de);
    vkEnumerateDeviceExtensionProperties(pd, NULL, &nde, de);
    static const char* want[] = {"VK_EXT_extended_dynamic_state", "VK_EXT_extended_dynamic_state2",
                                 "VK_EXT_extended_dynamic_state3", "VK_EXT_rasterization_order_attachment_access",
                                 "VK_ARM_rasterization_order_attachment_access", "VK_KHR_push_descriptor",
                                 "VK_KHR_dynamic_rendering", "VK_KHR_timeline_semaphore", "VK_EXT_custom_border_color",
                                 "VK_EXT_depth_clip_control", "VK_EXT_index_type_uint8", "VK_KHR_pipeline_library",
                                 "VK_EXT_graphics_pipeline_library", "VK_KHR_present_wait", "VK_GOOGLE_display_timing",
                                 "VK_EXT_pipeline_creation_cache_control", "VK_KHR_shader_float16_int8"};
    for (unsigned i = 0; i < sizeof want / sizeof *want; i++) plog("[vk] %s: %s", want[i], has_ext(de, nde, want[i]) ? "yes" : "no");

    if (!SDL_Vulkan_CreateSurface(w, inst, &surf)) {
        plog("[vk] surface: %s", SDL_GetError());
        goto out;
    }
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
    VkQueueFamilyProperties qf[16];
    if (nq > 16) nq = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qf);
    uint32_t qfi = UINT32_MAX;
    for (uint32_t i = 0; i < nq && qfi == UINT32_MAX; i++) {
        VkBool32 pres = 0;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surf, &pres);
        if ((qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) qfi = i;
    }
    if (qfi == UINT32_MAX) {
        plog("[vk] no graphics+present queue");
        goto out;
    }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qfi, 1, &prio};
    const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, NULL, 0, 1, &qci, 0, NULL, 1, dext, NULL};
    VKCHECK(vkCreateDevice(pd, &dci, NULL, &dev));
    VkQueue q;
    vkGetDeviceQueue(dev, qfi, 0, &q);

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surf, &caps);
    uint32_t nf = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, NULL);
    VkSurfaceFormatKHR fmts[32];
    if (nf > 32) nf = 32;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surf, &nf, fmts);
    VkSurfaceFormatKHR fmt = fmts[0];
    for (uint32_t i = 0; i < nf; i++)
        if (fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) fmt = fmts[i];
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == UINT32_MAX) {
        int dw, dh;
        SDL_Vulkan_GetDrawableSize(w, &dw, &dh);
        ext.width = (uint32_t)dw;
        ext.height = (uint32_t)dh;
    }
    VkCompositeAlphaFlagBitsKHR ca = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & ca)) ca = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    uint32_t nimg = caps.minImageCount + 1;
    if (caps.maxImageCount && nimg > caps.maxImageCount) nimg = caps.maxImageCount;
    plog("[vk] surface %ux%u, format %d, transform 0x%X, %u images", ext.width, ext.height, fmt.format, caps.currentTransform, nimg);
    VkSwapchainCreateInfoKHR sci = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, surf, nimg, fmt.format, fmt.colorSpace, ext, 1,
                                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, caps.currentTransform, ca,
                                    VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, VK_NULL_HANDLE};
    VKCHECK(vkCreateSwapchainKHR(dev, &sci, NULL, &sc));
    VkImage imgs[8];
    if (nimg > 8) nimg = 8;
    vkGetSwapchainImagesKHR(dev, sc, &nimg, imgs);
    VkAttachmentDescription att = {0, fmt.format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
                                   VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference aref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub = {0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, NULL, 1, &aref, NULL, NULL, 0, NULL};
    VkSubpassDependency dep = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
    VkRenderPassCreateInfo rpci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, NULL, 0, 1, &att, 1, &sub, 1, &dep};
    VkRenderPass rp;
    VKCHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp));
    VkImageView views[8];
    VkFramebuffer fbs[8];
    for (uint32_t i = 0; i < nimg; i++) {
        VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, NULL, 0, imgs[i], VK_IMAGE_VIEW_TYPE_2D, fmt.format,
                                     {0, 0, 0, 0}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VKCHECK(vkCreateImageView(dev, &vci, NULL, &views[i]));
        VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, NULL, 0, rp, 1, &views[i], ext.width, ext.height, 1};
        VKCHECK(vkCreateFramebuffer(dev, &fci, NULL, &fbs[i]));
    }
    /* one dynamic uniform buffer binding; the draw's range is picked with a dynamic offset */
    VkDescriptorSetLayoutBinding b = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo dlci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, NULL, 0, 1, &b};
    VkDescriptorSetLayout dsl;
    VKCHECK(vkCreateDescriptorSetLayout(dev, &dlci, NULL, &dsl));
    VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, NULL, 0, 1, &dsl, 0, NULL};
    VkPipelineLayout pl;
    VKCHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
    VkShaderModuleCreateInfo smv = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof k_vs_spv, k_vs_spv};
    VkShaderModuleCreateInfo smf = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, NULL, 0, sizeof k_fs_spv, k_fs_spv};
    VkShaderModule mv, mf;
    VKCHECK(vkCreateShaderModule(dev, &smv, NULL, &mv));
    VKCHECK(vkCreateShaderModule(dev, &smf, NULL, &mf));
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, mv, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, mf, "main", NULL}};
    VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, NULL, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, NULL, 0, 1, NULL, 1, NULL};
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, NULL, 0, VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState cb[2] = {{0}, {0}};
    cb[0].colorWriteMask = cb[1].colorWriteMask = 0xF;
    cb[1].blendEnable = VK_TRUE;
    cb[1].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb[1].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb[1].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb[1].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, NULL, 0, 2, dyn};
    VkPipeline pipes[2];
    for (int i = 0; i < 2; i++) {
        VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, NULL, 0, VK_FALSE, 0, 1, &cb[i]};
        VkGraphicsPipelineCreateInfo gci = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, NULL, 0, 2, st, &vi, &ia, NULL, &vp, &rs, &ms, NULL,
                                            &cbs, &ds, pl, rp, 0, VK_NULL_HANDLE, -1};
        VKCHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, NULL, &pipes[i]));
    }
    /* uniforms: one host-visible buffer per frame in flight */
    VkDeviceSize stride = pp.limits.minUniformBufferOffsetAlignment > 32 ? pp.limits.minUniformBufferOffsetAlignment : 32;
    VkDeviceSize bytes = stride * (VkDeviceSize)g_draws;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    VkBuffer ub[2];
    VkDeviceMemory um[2];
    uint8_t* umap[2];
    VkCommandPool pool;
    VkCommandPoolCreateInfo cpci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfi};
    VKCHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
    VkCommandBuffer cmd[2];
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2};
    VKCHECK(vkAllocateCommandBuffers(dev, &cai, cmd));
    VkDescriptorPoolSize dps = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 2};
    VkDescriptorPoolCreateInfo dpci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, NULL, 0, 2, 1, &dps};
    VkDescriptorPool dpool;
    VKCHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
    VkDescriptorSet sets[2];
    VkDescriptorSetLayout dsls[2] = {dsl, dsl};
    VkDescriptorSetAllocateInfo dsai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, NULL, dpool, 2, dsls};
    VKCHECK(vkAllocateDescriptorSets(dev, &dsai, sets));
    VkFence fences[2];
    VkSemaphore acq[2], done[2];
    for (int i = 0; i < 2; i++) {
        VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, bytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE};
        VKCHECK(vkCreateBuffer(dev, &bci, NULL, &ub[i]));
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(dev, ub[i], &mr);
        uint32_t mt = 0;
        for (uint32_t k = 0; k < mp.memoryTypeCount; k++) {
            VkMemoryPropertyFlags need = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((mr.memoryTypeBits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & need) == need) {
                mt = k;
                break;
            }
        }
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, mt};
        VKCHECK(vkAllocateMemory(dev, &mai, NULL, &um[i]));
        VKCHECK(vkBindBufferMemory(dev, ub[i], um[i], 0));
        VKCHECK(vkMapMemory(dev, um[i], 0, bytes, 0, (void**)&umap[i]));
        VkDescriptorBufferInfo dbi = {ub[i], 0, 32};
        VkWriteDescriptorSet wds = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, sets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, NULL, &dbi, NULL};
        vkUpdateDescriptorSets(dev, 1, &wds, 0, NULL);
        VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT};
        VKCHECK(vkCreateFence(dev, &fci, NULL, &fences[i]));
        VkSemaphoreCreateInfo sci2 = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VKCHECK(vkCreateSemaphore(dev, &sci2, NULL, &acq[i]));
        VKCHECK(vkCreateSemaphore(dev, &sci2, NULL, &done[i]));
    }
    issue = calloc(g_frames, sizeof(double));
    frame = calloc(g_frames, sizeof(double));
    int m = 0;
    double last = now_ms();
    for (int fr = 0; fr < g_frames + 30; fr++) {
        if (pump()) break;
        int s = fr & 1;
        vkWaitForFences(dev, 1, &fences[s], VK_TRUE, UINT64_MAX);
        uint32_t ii;
        VkResult ar = vkAcquireNextImageKHR(dev, sc, UINT64_MAX, acq[s], VK_NULL_HANDLE, &ii);
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
            plog("[vk] acquire: %d", (int)ar);
            break;
        }
        vkResetFences(dev, 1, &fences[s]);
        double t0 = now_ms();
        for (int i = 0; i < g_draws; i++) {
            float* f = (float*)(umap[s] + (VkDeviceSize)i * stride);
            f[0] = -0.95f + 1.9f * (float)(i % 100) / 100.0f;
            f[1] = -0.95f + 1.9f * (float)(i / 100 % 70) / 70.0f;
            f[4] = (float)(i % 7) / 7.0f; f[5] = 0.6f; f[6] = (float)(i % 13) / 13.0f; f[7] = 1.0f;
        }
        vkResetCommandBuffer(cmd[s], 0);
        VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        vkBeginCommandBuffer(cmd[s], &bi);
        VkClearValue clr = {{{0.3f, 0.1f, 0.05f, 1.0f}}};
        VkRenderPassBeginInfo rbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, NULL, rp, fbs[ii], {{0, 0}, ext}, 1, &clr};
        vkCmdBeginRenderPass(cmd[s], &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport v = {0, 0, (float)ext.width, (float)ext.height, 0, 1};
        VkRect2D sr = {{0, 0}, ext};
        vkCmdSetViewport(cmd[s], 0, 1, &v);
        vkCmdSetScissor(cmd[s], 0, 1, &sr);
        for (int i = 0; i < g_draws; i++) {
            if ((i & 7) == 0) vkCmdBindPipeline(cmd[s], VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[(i >> 3) & 1]);
            uint32_t off = (uint32_t)(i * stride);
            vkCmdBindDescriptorSets(cmd[s], VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &sets[s], 1, &off);
            vkCmdDraw(cmd[s], 3, 1, 0, 0);
        }
        vkCmdEndRenderPass(cmd[s]);
        vkEndCommandBuffer(cmd[s]);
        VkPipelineStageFlags wst = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &acq[s], &wst, 1, &cmd[s], 1, &done[s]};
        vkQueueSubmit(q, 1, &si, fences[s]);
        double t1 = now_ms();
        VkPresentInfoKHR pi = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &done[s], 1, &sc, &ii, NULL};
        vkQueuePresentKHR(q, &pi);
        double t2 = now_ms();
        if (fr >= 30) {
            issue[m] = t1 - t0;
            frame[m] = t2 - last;
            m++;
        }
        last = t2;
    }
    vkDeviceWaitIdle(dev);
    g_vk_issue = median(issue, m);
    g_vk_frame = median(frame, m);
    plog("[bench] Vulkan: %d draws, CPU issue median %.2f ms (incl. uniform writes), frame median %.2f ms (%d frames)", g_draws, g_vk_issue,
         g_vk_frame, m);
out:
    /* the process ends right after: the driver objects go with it */
    free(issue);
    free(frame);
    free(de);
    if (dev) vkDeviceWaitIdle(dev);
    SDL_DestroyWindow(w);
}

int main(int argc, char** argv) {
    /* KEY=VALUE launch arguments become environment variables (SonicRidersActivity "args") */
    for (int i = 1; i < argc; i++) {
        char* eq = strchr(argv[i], '=');
        if (eq) {
            *eq = 0;
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        }
    }
    if (getenv("PROBE_DRAWS")) g_draws = atoi(getenv("PROBE_DRAWS"));
    if (getenv("PROBE_FRAMES")) g_frames = atoi(getenv("PROBE_FRAMES"));
    if (g_draws < 1) g_draws = 1;
    if (g_frames < 1) g_frames = 1;
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "srprobe", "SDL_Init: %s", SDL_GetError());
        return 1;
    }
    char path[512];
    snprintf(path, sizeof path, "%s/probe.log", SDL_AndroidGetExternalStoragePath());
    g_log = fopen(path, "w");
    SDL_version v;
    SDL_GetVersion(&v);
    plog("[probe] Android milestone 1 probe, SDL %d.%d.%d, %d draws x %d frames per API", v.major, v.minor, v.patch, g_draws, g_frames);
    plog("[probe] files folder: %s", SDL_AndroidGetExternalStoragePath());
    probe_system();
    probe_audio();
    pump();
    probe_gles();
    probe_vulkan();
    if (g_gl_issue >= 0 && g_vk_issue >= 0)
        plog("[bench] summary: GLES %.2f ms vs Vulkan %.2f ms CPU per frame of %d draws (Vulkan %.1fx)", g_gl_issue, g_vk_issue, g_draws,
             g_vk_issue > 0 ? g_gl_issue / g_vk_issue : 0.0);
    plog("[probe] done");
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    if (g_log) fclose(g_log);
    SDL_Quit();
    return 0;
}
