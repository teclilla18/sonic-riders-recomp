/* Reuse the existing audout resampler; only this target adds explicit cleanup. */
#include <stdlib.h>
#include <switch.h>
#include "runtime.h"
static Result tracked_append(AudioOutBuffer*);
static Result tracked_release(AudioOutBuffer**, u32*);
#define audoutAppendAudioOutBuffer tracked_append
#define audoutGetReleasedAudioOutBuffer tracked_release
#define platform_audio_push native_audio_push
#include "../switch/audio_switch.c"
#undef audoutAppendAudioOutBuffer
#undef audoutGetReleasedAudioOutBuffer
#undef platform_audio_push
#include "switch2_platform.h"

static uint64_t g_appends, g_released, g_drops, g_errors, g_input_frames, g_max_gap, g_last_push, g_report;
void switch2_audio_drop(void) { g_drops++; }
static Result tracked_append(AudioOutBuffer* b) {
    Result rc = audoutAppendAudioOutBuffer(b);
    if (R_SUCCEEDED(rc)) g_appends++;
    else { g_errors++; rt_log("[audio2] append failed rc=%X size=%llu", rc, (unsigned long long)b->data_size); }
    return rc;
}
static Result tracked_release(AudioOutBuffer** b, u32* n) {
    Result rc = audoutGetReleasedAudioOutBuffer(b, n);
    if (R_SUCCEEDED(rc)) g_released += *n;
    else { if (!g_errors++) rt_log("[audio2] release failed rc=%X", rc); }
    return rc;
}
void platform_audio_push(const int16_t* lr, unsigned frames) {
    uint64_t now = platform_now_ns();
    if (g_last_push && now - g_last_push > g_max_gap) g_max_gap = now - g_last_push;
    g_last_push = now; g_input_frames += frames;
    native_audio_push(lr, frames);
    if (now - g_report > 2000000000ULL) {
        u32 queued = 0; u64 played = 0;
        Result qr = audoutGetAudioOutBufferCount(&queued), pr = audoutGetAudioOutPlayedSampleCount(&played);
        rt_log("[audio2] rate=%u channels=%u format=%d input_frames=%llu append=%llu released=%llu dropped_samples=%llu errors=%llu free=%d queued=%u(%X) played=%llu(%X) max_input_gap_ms=%.2f",
               audoutGetSampleRate(), audoutGetChannelCount(), audoutGetPcmFormat(), (unsigned long long)g_input_frames,
               (unsigned long long)g_appends, (unsigned long long)g_released, (unsigned long long)g_drops,
               (unsigned long long)g_errors, g_nfree, queued, qr, (unsigned long long)played, pr, g_max_gap / 1e6);
        g_report = now; g_max_gap = 0;
    }
}

void switch2_audio_shutdown(void) {
    switch2_checkpoint("BEGIN_AUDIO_SHUTDOWN");
    if (serviceIsActive(audoutGetServiceSession())) {
        Result stopped = audoutStopAudioOut();
        bool flushed = false;
        Result flush = audoutFlushAudioOutBuffers(&flushed);
        rt_log("[switch2] audio stop=0x%X flush=0x%X released=%d", stopped, flush, flushed);
        audoutExit();
    }
    for (unsigned i = 0; i < NBUF; i++) { free(g_bufs[i].buffer); g_bufs[i].buffer = NULL; }
    g_state = 0; g_fill = NULL; g_fill_idx = -1; g_nfree = 0;
    switch2_checkpoint("AUDIO_SHUTDOWN_COMPLETE");
}
