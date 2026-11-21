#include "online_boot.h"
#include "gx_render.h"
#include "runtime.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
const char* g_launch_path;
static _Atomic int lobby, loading;
static char net_spec[96], player[64], other[64];
int online_boot_args(int argc, char** argv) {
    g_launch_path=argc>0?argv[0]:"";
    int n=1, wanted=getenv("SR_ONLINE") && atoi(getenv("SR_ONLINE"));
#ifdef SR_ONLINE_DEFAULT
    wanted=1; /* dedicated online application, including loaders without argv support */
#endif
#ifdef __SWITCH__
    if (strstr(g_launch_path,"online")) wanted=1;
#endif
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"online") || !strcmp(argv[i],"--online")) wanted=1;
        else argv[n++]=argv[i];
    }
    if (wanted && !getenv("SR_NET")) atomic_store(&lobby,1);
    return n;
}
int online_lobby_visible(void) { return atomic_load(&lobby)==1; }
void online_lobby_finish(const char* spec,const char* name,const char* peer) {
    snprintf(net_spec,sizeof net_spec,"%s",spec?spec:"");
    snprintf(player,sizeof player,"%s",name?name:"");
    snprintf(other,sizeof other,"%s",peer?peer:"");
    atomic_store(&lobby,spec?2:3);
}
void online_loading_set(int on) {
    static int allowed=-1; /* SR_ONLINE_LOADING=0: no black loading screen (Switch 2 device-loss test) */
    if (allowed<0) { const char* e=getenv("SR_ONLINE_LOADING"); allowed=!(e && !strcmp(e,"0")); }
    atomic_store(&loading,on && allowed);
}
int online_loading(void) { return atomic_load(&loading); }
void online_present_blank(void) {
#if defined(__SWITCH2__) || defined(SR_IOS)
    /* direct SR_NET: netplay waits on the main thread before render_main_loop starts; no frame can be shown */
    extern _Atomic int g_render_loop_running;
    if (!atomic_load(&g_render_loop_running)) { platform_sleep_ns(16000000); return; }
#endif
    GxFrame* f=render_take_free_frame();
    f->nverts=f->nidx=f->nunis=f->nretired=f->nxchunks=f->nxtabs=0;
    f->wide_k=1.0f; f->in_race=0;
    if (!f->cap_cmds) { f->cmds=malloc(sizeof *f->cmds); f->cap_cmds=1; }
    f->ncmds=1;
    memset(f->cmds,0,sizeof *f->cmds);
    f->cmds[0].type=GXC_PRESENT; /* presentation without guest GX state */
    f->cmds[0].copy[2]=640; f->cmds[0].copy[3]=528; f->cmds[0].copy[4]=1;
    render_submit(f);
    platform_sleep_ns(16000000);
}
void online_boot_wait(void) {
    if (!online_lobby_visible()) return;
    rt_log("[lobby] Online LAN lobby opened before game frame 1");
    while (online_lobby_visible()) online_present_blank();
    if (atomic_load(&lobby)==2) {
#ifdef _WIN32
        _putenv_s("SR_NET",net_spec); _putenv_s("SR_NET_NAME",player); _putenv_s("SR_NET_PEER",other);
#else
        setenv("SR_NET",net_spec,1); setenv("SR_NET_NAME",player,1); setenv("SR_NET_PEER",other,1);
#endif
        online_loading_set(1);
        rt_log("[lobby] starting %s as \"%s\", peer \"%s\"",net_spec,player,other);
    }
    atomic_store(&lobby,0);
}
