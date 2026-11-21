#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int online_boot_args(int argc, char** argv);
void online_boot_wait(void); /* main thread, before devices/netplay init; renders lobby without guest frames */
int online_lobby_visible(void);
void online_lobby_finish(const char* spec, const char* name, const char* peer); /* render thread */
void online_present_blank(void); /* main thread only, before the guest starts */
void online_loading_set(int loading);
int online_loading(void);
int platform_restart_online(void);
extern const char* g_launch_path;
#ifdef __cplusplus
}
#endif
