/* Reuse the existing audout resampler; only this target adds explicit cleanup. */
#include <stdlib.h>
#include "../switch/audio_switch.c"
#include "switch2_platform.h"

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
