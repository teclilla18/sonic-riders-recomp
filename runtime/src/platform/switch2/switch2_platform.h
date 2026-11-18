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
void switch2_log_start(void);
int switch2_log_enqueue(const char* text);
void switch2_log_flush(void);
void switch2_log_stop(void);
void switch2_audio_shutdown(void);
void render_main_loop(void);
#ifdef __cplusplus
}
#endif
