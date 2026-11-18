/* CPU-only tests of the actual native interpolation/scheduler, with a simulated
 * clock, producer and GPU slots. No game data or console services required.
 * clang -std=c11 -Iruntime/src tools/test_switch2_render_rate.c -o <output> */
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gx_render.h"
#include "settings.h"

typedef int mtx_t;
typedef int cnd_t;
static uint64_t clock_ns = 1000000000ull;
static unsigned sleeps, executed, blended, producer_frames, retired_count;
static unsigned seen[10000];
static int g_lock, g_cv, g_quit, g_swap_bad, g_present_mode;
static GxFrame g_frames[4], *g_free[4], *g_pending;
static int g_nfree;
static unsigned g_slot, g_presented, g_frames_done;
static int *g_retired[2];
static uint32_t g_nretired[2];
volatile int g_render_counts_fps;
Settings g_settings;
static void mtx_lock(mtx_t* x) { (void)x; }
static void mtx_unlock(mtx_t* x) { (void)x; }
static void cnd_broadcast(cnd_t* x) { (void)x; }
static int cnd_timedwait(cnd_t* x, mtx_t* y, const struct timespec* t) {
    (void)x; (void)y; (void)t; clock_ns += 100000; return 0;
}
static uint64_t platform_now_ns(void) { return clock_ns; }
static void platform_sleep_ns(uint64_t n) { sleeps++; clock_ns += n; }
static void rt_log(const char* fmt, ...) { (void)fmt; }
static void rt_fatal(const char* fmt, ...) { (void)fmt; abort(); }
static void overlay_set_fps(float v) { assert(isfinite(v) && v >= 0); }
static void pump(void) {}
static void native_display_rate(int target) { (void)target; }
typedef int VkPresentModeKHR;
enum {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_FIFO_KHR};
#include "platform/switch2/render_present.h"
#include "platform/switch2/frame_interpolation.h"
static void reclaim(unsigned slot) {
    for (uint32_t i = 0; i < g_nretired[slot]; i++) {
        unsigned id = (unsigned)g_retired[slot][i];
        assert(id < sizeof seen / sizeof seen[0]);
        assert(!seen[id]++); /* no duplicate/double-free, including skipped frames */
        retired_count++;
    }
    g_nretired[slot] = 0;
}
static void execute(GxFrame* f) {
    assert(f >= g_frames && f < g_frames+4); /* owns a pool frame */
    reclaim(g_slot); /* previous use of this GPU slot completed */
    if (g_ip_on) { assert(g_ip_a && g_ip_t >= 0 && g_ip_t < 1); blended++; }
    executed++; g_presented++; g_frames_done++;
    clock_ns += 100000; /* 0.1 ms fake GPU/CPU recording cost */
    g_slot ^= 1;
}
#include "platform/switch2/render_rate.h"

static uint32_t word(float f) { uint32_t w; memcpy(&w, &f, 4); return w; }
static void pair_test(void) {
    GxCmd cmd[2] = {{.type=GXC_STATE}, {.type=GXC_DRAW, .b=1}};
    GxVertex v = {0};
    uint32_t idx = 0, a[26*64] = {0}, b[26*64] = {0};
    GxXTab xt;
    for (int i = 0; i < 26; i++) xt.xtab[i] = (uint32_t)i;
    GxUniforms ua = {0}, ub = {0};
    GxFrame fa = {.cmds=cmd,.ncmds=2,.verts=&v,.nverts=1,.idx=&idx,.nidx=1,
        .unis=&ua,.nunis=1,.xchunks=a,.nxchunks=26,.xtabs=&xt,.nxtabs=1};
    GxFrame fb = fa; fb.unis=&ub; fb.xchunks=b;
    a[3]=word(10); b[3]=word(30); /* position translation */
    a[30]=word(10); b[30]=word(30); /* unpaired texture matrix stays B */
    ua.xfr[0x20]=word(2); ub.xfr[0x20]=word(6);
    IpInfo ia={0}, ib={0};
    ip_frame_info(&fa,&ia); ip_frame_info(&fb,&ib); ip_match(&fa,&ia,&fb,&ib);
    assert(g_ip_matched == 1);
    g_ip_a=&fa; g_ip_t=0.5f;
    const uint32_t* c=ip_chunks(&fb);
    assert(ip_f(c[3])==20 && ip_f(c[30])==30);
    assert(ip_f(b[3])==30); /* source immutable */
    GxUniforms staged=ub; ip_states((uint8_t*)&staged,sizeof staged);
    assert(ip_f(staged.xfr[0x20])==4 && ip_f(ub.xfr[0x20])==6);
    a[0]=word(1); b[0]=word(-1); /* camera cut rejects whole position matrix */
    ip_match(&fa,&ia,&fb,&ib); c=ip_chunks(&fb); assert(ip_f(c[3])==30);
    a[0]=word(NAN); ip_match(&fa,&ia,&fb,&ib); c=ip_chunks(&fb); assert(isfinite(ip_f(c[0])));
    free(ia.d); free(ib.d); g_ip_a=NULL;
    puts("PASS PC pairing, staged matrices/projection, source immutability, cut/NaN guards");
}

static void feed(void) {
    assert(!g_pending && g_nfree > 0);
    GxFrame* f=g_free[--g_nfree];
    assert(f != g_ip_fa && f != g_ip_fb);
    f->retired=realloc(f->retired,sizeof(int));
    f->retired[0]=(int)++producer_frames; f->nretired=1;
    g_pending=f;
}
static void run_rate(int target) {
    unsigned before=executed, before_sleeps=sleeps, before_produced=producer_frames;
    uint64_t end=clock_ns+2000000000ull, next=clock_ns;
    while (clock_ns<end) {
        if (clock_ns>=next) { feed(); next+=IP_GAME_NS; }
        ip_round(target);
        clock_ns+=100000;
    }
    unsigned draws=executed-before, produced=producer_frames-before_produced;
    assert(produced >= 119 && produced <= 121); /* render cap never paces simulation */
    if (target>0) assert(draws <= (unsigned)target*2+1 && draws >= (unsigned)target*18/10);
    if (target==-1) { assert(draws>480); assert(sleeps==before_sleeps); }
    if (!target) assert(draws >= 119 && draws <= 122);
    printf("PASS live target %d: %u renders, %u guest frames in 2 s\n",target,draws,produced);
}
int main(void) {
    VkPresentModeKHR modes[]={VK_PRESENT_MODE_MAILBOX_KHR,VK_PRESENT_MODE_IMMEDIATE_KHR,VK_PRESENT_MODE_FIFO_KHR};
    assert(native_present_mode(modes,3,0,1,60)==VK_PRESENT_MODE_FIFO_KHR);
    assert(native_present_mode(modes,3,120,1,120)==VK_PRESENT_MODE_FIFO_KHR);
    assert(native_present_mode(modes,3,144,1,120)==VK_PRESENT_MODE_IMMEDIATE_KHR);
    assert(native_present_mode(modes,3,-1,1,120)==VK_PRESENT_MODE_IMMEDIATE_KHR);
    assert(native_present_mode(modes,1,-1,1,60)==VK_PRESENT_MODE_MAILBOX_KHR);
    assert(native_present_mode(modes+2,1,-1,1,60)==VK_PRESENT_MODE_FIFO_KHR);
    puts("PASS presentation/VSync choice, refresh match, Unlimited, mailbox/FIFO fallback");
    pair_test();
    for (int i=0;i<4;i++) g_free[g_nfree++]=&g_frames[i];
    int rates[]={0,120,144,180,240,-1,45,0,120,-1,0};
    for (unsigned i=0;i<sizeof rates/sizeof rates[0];i++) run_rate(rates[i]);
    assert(blended>1000);
    assert(g_swap_bad); /* presentation policy refreshed on live limit changes */
    g_settings.framerate=FR_30; g_settings.pc_fps=-1; assert(ip_target()==0);
    g_settings.framerate=FR_AUTO; assert(ip_target()==-1);
    g_quit=1; feed(); ip_round(-1); ip_release();
    assert(!g_pending && g_nfree==4 && !g_ip_fa && !g_ip_fb);
    reclaim(0); reclaim(1); assert(retired_count==producer_frames);
    puts("PASS held/free ownership, 30 fps override, quit, every retirement exactly once");
    return 0;
}
