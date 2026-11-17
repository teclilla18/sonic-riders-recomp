/* Native Switch 2 integration; independent of the original Switch backend. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int switch2_stopping(void);
void switch2_thread_checkpoint(void);
void switch2_stop_workers(void);
void switch2_checkpoint(const char* message);
void switch2_shutdown(void);
void switch2_audio_shutdown(void);
void render_main_loop(void);
#ifdef __cplusplus
}
#endif
