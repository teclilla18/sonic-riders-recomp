/* Desktop audio output: SDL's queued audio at the game's 32 kHz. The queue is kept short (the AI
 * clock in dsp.c paces the game); when it grows past ~120 ms (the device runs slow), it is cut back.
 * SR_NOAUDIO=1 disables the device. */
#include <SDL.h>
#include <stdlib.h>

#include "runtime.h"

static SDL_AudioDeviceID g_dev;
static int g_tried;

void platform_audio_push(const int16_t* lr, unsigned frames) {
    if (!g_tried) {
        g_tried = 1;
        if (getenv("SR_NOAUDIO") || getenv("SR_HEADLESS") || !SDL_WasInit(SDL_INIT_AUDIO)) return;
        SDL_AudioSpec want = {0}, have;
        want.freq = 32000;
        want.format = AUDIO_S16SYS;
        want.channels = 2;
        want.samples = 512;
        g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!g_dev) {
            rt_log("[audio] no audio device: %s", SDL_GetError());
            return;
        }
        SDL_PauseAudioDevice(g_dev, 0);
        rt_log("[audio] SDL output at %d Hz", have.freq);
    }
    if (!g_dev) return;
    if (SDL_GetQueuedAudioSize(g_dev) > 32000 * 4 * 120 / 1000) SDL_ClearQueuedAudio(g_dev);
    SDL_QueueAudio(g_dev, lr, frames * 4);
}
