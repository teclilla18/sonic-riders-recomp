/* Desktop audio output: SDL's queued audio at the game's 32 kHz. The queue is kept short (the AI
 * clock in dsp.c paces the game); when it grows past ~120 ms (the device runs slow), it is cut back.
 * SR_NOAUDIO=1 disables the device. */
#include <SDL.h>
#include <stdlib.h>
#include <threads.h>

#include "runtime.h"

static SDL_AudioDeviceID g_dev;
static int g_tried;
static int g_closed;
static once_flag g_lock_once = ONCE_FLAG_INIT;
static mtx_t g_audio_lock;
static void audio_lock_init(void) { mtx_init(&g_audio_lock, mtx_plain); }

/* The AX producer can still be finishing a mix when the render thread quits. Never let it
 * queue/open/pause a device while SDL closes it or after SDL_Quit has released audio state. */
void platform_audio_shutdown(void) {
    call_once(&g_lock_once, audio_lock_init);
    mtx_lock(&g_audio_lock);
    g_closed = 1;
    SDL_AudioDeviceID dev = g_dev;
    g_dev = 0;
    mtx_unlock(&g_audio_lock);
    if (dev) SDL_CloseAudioDevice(dev);
}

void platform_audio_push(const int16_t* lr, unsigned frames) {
    call_once(&g_lock_once, audio_lock_init);
    mtx_lock(&g_audio_lock);
    if (g_closed) { mtx_unlock(&g_audio_lock); return; }
    if (!g_tried) {
        g_tried = 1;
        if (getenv("SR_NOAUDIO") || getenv("SR_HEADLESS") || !SDL_WasInit(SDL_INIT_AUDIO)) {
            mtx_unlock(&g_audio_lock); return;
        }
        SDL_AudioSpec want = {0}, have;
        want.freq = 32000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!g_dev) {
            rt_log("[audio] no audio device: %s", SDL_GetError());
            mtx_unlock(&g_audio_lock);
            return;
        }
        SDL_PauseAudioDevice(g_dev, 0);
        rt_log("[audio] SDL output at %d Hz", have.freq);
    }
    if (!g_dev) { mtx_unlock(&g_audio_lock); return; }
    if (SDL_GetQueuedAudioSize(g_dev) > 32000 * 4 * 120 / 1000) SDL_ClearQueuedAudio(g_dev);
    SDL_QueueAudio(g_dev, lr, frames * 4);
    mtx_unlock(&g_audio_lock);
}

void platform_audio_pause(int on) {
    call_once(&g_lock_once, audio_lock_init);
    mtx_lock(&g_audio_lock);
    if (!g_dev) { mtx_unlock(&g_audio_lock); return; }
    if (on) SDL_ClearQueuedAudio(g_dev);
    SDL_PauseAudioDevice(g_dev, on);
    mtx_unlock(&g_audio_lock);
}
