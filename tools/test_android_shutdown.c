/* Actual shutdown/prewarm source + audio_sdl.c, with concurrent checked SDL/Vulkan test doubles. */
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#ifndef _WIN32
#include <pthread.h>
#endif
#include <vulkan/vulkan.h>
#include <SDL.h>

#define H(t, n) ((t)(uintptr_t)(n))
struct TestSDLThread {
#ifdef _WIN32
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int (*fn)(void*);
    void* arg;
};
static void pause_ms(void) { struct timespec t = {0, 1000000}; thrd_sleep(&t, NULL); }
#ifdef _WIN32
static DWORD WINAPI trampoline(LPVOID p) {
#else
static void* trampoline(void* p) {
#endif
    SDL_Thread* t = p; t->fn(t->arg); return 0;
}
static SDL_Thread* start(int (*fn)(void*), void* arg) {
    SDL_Thread* t = calloc(1, sizeof *t); t->fn = fn; t->arg = arg;
#ifdef _WIN32
    t->thread = CreateThread(NULL, 0, trampoline, t, 0, NULL); assert(t->thread);
#else
    assert(!pthread_create(&t->thread, NULL, trampoline, t));
#endif
    return t;
}
void SDL_WaitThread(SDL_Thread* t, int* status) {
    (void)status;
#ifdef _WIN32
    assert(WaitForSingleObject(t->thread, INFINITE) == WAIT_OBJECT_0); CloseHandle(t->thread);
#else
    assert(!pthread_join(t->thread, NULL));
#endif
    free(t);
}

static mtx_t g_lock, g_pw_lock;
static cnd_t g_cv;
static int g_stopping;
static VkDevice g_dev = H(VkDevice, 1);
static VkInstance g_inst = H(VkInstance, 2);
static VkSurfaceKHR g_surf = H(VkSurfaceKHR, 3);
static SDL_Window* g_win = H(SDL_Window*, 4);
static VkFormat g_swap_fmt = VK_FORMAT_R8G8B8A8_UNORM;
enum { TGT_SWAP = 2 };
typedef struct { uint64_t key; VkPipeline p; } PipeEntry;
static PipeEntry* g_pw_done;
static uint32_t g_pw_ndone, g_pw_cap;
static uint64_t* g_pw_keys;
static uint32_t g_pw_nkeys;
static _Atomic int g_pw_running, g_pw_cancel;
static SDL_Thread* g_pw_thread;
static atomic_int compiler_entered, compiler_active, release_compiler;
static atomic_int audio_entered, audio_active, release_audio, overlay_joined;
static unsigned compiled, pipes_taken, cache_saved, device_destroyed, instance_destroyed, window_destroyed;
static unsigned audio_opens, audio_closes, audio_calls, sdl_quit;
static int audio_closed;
static uint32_t sdl_init = SDL_INIT_AUDIO;

void rt_log(const char* fmt, ...) { (void)fmt; }
static void rt_thread_lower_priority(void) {}
static uint64_t platform_now_ns(void) { return 0; }
static VkPipeline pipe_make(uint64_t key) {
    (void)key;
    atomic_store(&compiler_active, 1); atomic_store(&compiler_entered, 1);
    while (!atomic_load(&release_compiler)) pause_ms();
    assert(g_dev && !device_destroyed);
    atomic_store(&compiler_active, 0); compiled++;
    return H(VkPipeline, 5);
}
static VkPipeline pipe_find(uint64_t key) { (void)key; return VK_NULL_HANDLE; }
static void pipe_put(uint64_t key, VkPipeline p) {
    (void)key; (void)p; assert(!g_pw_thread && !g_pw_running && !compiler_active); pipes_taken++;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice d, VkPipeline p, const VkAllocationCallbacks* a) {
    (void)d; (void)p; (void)a; assert(!compiler_active);
}
static void overlay_shutdown(void) { assert(!g_pw_thread && !g_pw_running); atomic_store(&overlay_joined, 1); }
void platform_audio_shutdown(void);
void platform_audio_push(const int16_t*, unsigned);
void platform_audio_pause(int);
static void pcache_save(int force) { assert(force && pipes_taken == 1 && audio_closed); cache_saved++; }
static void device_teardown(void) {
    assert(g_stopping && !compiler_active && !g_pw_thread && !g_pw_running);
    assert(overlay_joined && audio_closed && cache_saved == 1);
    g_dev = VK_NULL_HANDLE; device_destroyed++;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySurfaceKHR(VkInstance i, VkSurfaceKHR s, const VkAllocationCallbacks* a) {
    (void)i; (void)s; (void)a; assert(!g_dev && device_destroyed == 1);
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance i, const VkAllocationCallbacks* a) {
    (void)i; (void)a; assert(!g_dev && !g_surf); instance_destroyed++;
}
void SDL_DestroyWindow(SDL_Window* w) { assert(w && !g_inst && instance_destroyed == 1); window_destroyed++; }
void SDL_Quit(void) { assert(!g_win && window_destroyed == 1 && audio_closed); sdl_init = 0; sdl_quit++; }

uint32_t SDL_WasInit(uint32_t f) { audio_calls++; return sdl_init & f; }
SDL_AudioDeviceID SDL_OpenAudioDevice(const char* name, int capture, const SDL_AudioSpec* want, SDL_AudioSpec* have, int changes) {
    (void)name; (void)capture; (void)changes; assert(!audio_closed); *have = *want; audio_opens++; return 7;
}
void SDL_CloseAudioDevice(SDL_AudioDeviceID d) { assert(d == 7 && !audio_active); audio_closed = 1; audio_closes++; }
void SDL_PauseAudioDevice(SDL_AudioDeviceID d, int on) { (void)on; assert(d == 7 && !audio_closed); audio_calls++; }
void SDL_ClearQueuedAudio(SDL_AudioDeviceID d) { assert(d == 7 && !audio_closed); audio_calls++; }
uint32_t SDL_GetQueuedAudioSize(SDL_AudioDeviceID d) { assert(d == 7 && !audio_closed); audio_calls++; return 0; }
int SDL_QueueAudio(SDL_AudioDeviceID d, const void* data, uint32_t bytes) {
    (void)data; (void)bytes; assert(d == 7 && !audio_closed);
    atomic_store(&audio_active, 1); atomic_store(&audio_entered, 1);
    while (!atomic_load(&release_audio)) pause_ms();
    assert(!audio_closed); atomic_store(&audio_active, 0); audio_calls++; return 0;
}
const char* SDL_GetError(void) { return "mock"; }

#include "shutdown_under_test.h"

static int audio_producer(void* unused) { (void)unused; int16_t lr[4] = {0}; platform_audio_push(lr, 2); return 0; }
static int release_inflight(void* unused) {
    (void)unused;
    while (!atomic_load(&g_pw_cancel)) pause_ms();
    assert(g_stopping && g_dev && !device_destroyed && compiler_active);
    atomic_store(&release_compiler, 1);
    while (!atomic_load(&overlay_joined)) pause_ms();
    assert(audio_active && !audio_closed && !audio_closes);
    atomic_store(&release_audio, 1);
    return 0;
}
int main(void) {
    mtx_init(&g_lock, mtx_plain); mtx_init(&g_pw_lock, mtx_plain); cnd_init(&g_cv);
    SDL_Thread* audio = start(audio_producer, NULL);
    while (!atomic_load(&audio_entered)) pause_ms();
    g_pw_nkeys = 3; g_pw_keys = calloc(g_pw_nkeys, sizeof *g_pw_keys); g_pw_running = 1;
    g_pw_thread = start(prewarm_thread, NULL);
    while (!atomic_load(&compiler_entered)) pause_ms();
    SDL_Thread* release = start(release_inflight, NULL);
    renderer_shutdown();
    SDL_WaitThread(audio, NULL); SDL_WaitThread(release, NULL);
    assert(compiled == 1 && pipes_taken == 1 && !g_pw_ndone); /* remaining compiler jobs cancelled */
    assert(device_destroyed == 1 && instance_destroyed == 1 && sdl_quit == 1);
    assert(audio_opens == 1 && audio_closes == 1 && !sdl_init);
    unsigned calls = audio_calls;
    for (int i = 0; i < 100; i++) { platform_audio_push(NULL, 0); platform_audio_pause(i & 1); }
    platform_audio_shutdown(); renderer_shutdown();
    assert(audio_calls == calls && audio_closes == 1 && sdl_quit == 1);
    free(g_pw_done);
    puts("PASS: in-flight compiler/audio quiescence, cancellation+real join, quit ordering, no SDL calls after close, idempotence");
    return 0;
}
