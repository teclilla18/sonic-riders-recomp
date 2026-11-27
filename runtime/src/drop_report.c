/* FPS drop reports: a player asked for "a log at the exact moment my fps drops" (Discord, 2026-11-27).
 *
 * gx_render.c hands every presented game frame's timing split here. During a race, when the last second averages
 * under 85 % of the game's frame rate (60, or 30 in its 30 fps mode) or one frame takes over 100 ms, the last five
 * seconds go to fps-drop-reports/fps-drop-<time>.txt next to the log: why it was saved, where the time went (in
 * plain words), the newest log lines and one line per frame. At most one report per 30 s and 10 per session; the
 * first 3 s of a race and the 3 s after a pause, the F1 menu or the app being in the background never count.
 * F1 > Bug reports shares the newest one (Android: share sheet; PC: opens the folder). Off: drop_reports=0. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "drop_report.h"
#include "runtime.h"
#include "settings.h"

extern const char g_build_version[];

enum { RING = 300, SETTLE = 180, MAX_REPORTS = 10 };
static DropFrame g_ring[RING];
static unsigned g_head, g_count, g_settled;
static double g_since_report = 1e9;  /* seconds of racing since the last report */
static int g_reports;
static char g_last[128];
static char g_gpu[160] = "unknown GPU";

void drop_report_set_gpu(const char* name) {
    if (name && name[0]) snprintf(g_gpu, sizeof g_gpu, "%s", name);
}
const char* drop_report_last(void) { return g_last; }
int drop_report_count(void) { return g_reports; }

static const DropFrame* nth_newest(unsigned i) { return &g_ring[(g_head + RING - 1 - i) % RING]; }

static void put_line(const char* text, void* user) { fprintf((FILE*)user, "  %s\n", text); }

static void write_report(const char* why, int target) {
    char name[128];
    time_t t = time(NULL);
    struct tm* lt = localtime(&t);
#ifdef _WIN32
    _mkdir(DROP_REPORT_DIR);
#else
    mkdir(DROP_REPORT_DIR, 0777);
#endif
    strftime(name, sizeof name, DROP_REPORT_DIR "/fps-drop-%Y%m%d-%H%M%S.txt", lt);
    FILE* f = fopen(name, "w");
    if (!f) {
        rt_log("[drop] cannot write %s", name);
        return;
    }
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", lt);
    fprintf(f, "Riders Rebuilt FPS drop report\nversion: %s\nsaved: %s\ndevice: %s\nwhy: %s\n", g_build_version, when,
            g_gpu, why);
    fprintf(f, "settings: framerate mode %d, picture rate %d, resolution %d, PC scale %dx, MSAA %d, bloom %d\n\n",
            g_settings.framerate, g_settings.pc_fps, g_settings.resolution, g_settings.pc_scale, g_settings.msaa,
            g_settings.bloom);

    /* the last second: average split and the biggest part */
    unsigned n = g_count < (unsigned)target ? g_count : (unsigned)target;
    double sum[7] = {0};
    unsigned draws = 0, newtex = 0;
    float worst = 0;
    for (unsigned i = 0; i < n; i++) {
        const DropFrame* d = nth_newest(i);
        sum[0] += d->total, sum[1] += d->game, sum[2] += d->draws, sum[3] += d->tex, sum[4] += d->wait;
        sum[5] += d->disc, sum[6] += d->idle;
        draws += d->ndraws, newtex += d->newtex;
        if (d->total > worst) worst = d->total;
    }
    static const char* part[7] = {
        "", "the game's own code (CPU)", "sending drawings to the GPU (CPU)",
        "loading new textures (texture packs, first sight of a model)",
        "waiting for the GPU (graphics too heavy, or new shaders being built)", "reading the disc image (storage)",
        "waiting for the next screen refresh (frame pacing)"};
    int big = 1;
    for (int i = 2; i < 6; i++)
        if (sum[i] > sum[big]) big = i;
    fprintf(f, "Last second: %.1f fps (target %d), slowest frame %.0f ms, %u draws per frame, %u new textures.\n",
            sum[0] > 0 ? n * 1000.0 / sum[0] : 0.0, target, worst, n ? draws / n : 0, newtex);
    fprintf(f, "Most of the time went to: %s.\n", part[big]);
    fprintf(f, "Average ms per frame: game code %.1f, GX draws %.1f, textures %.1f, waiting for GPU %.1f, disc %.1f, "
               "idle %.1f\n\n", n ? sum[1] / n : 0, n ? sum[2] / n : 0, n ? sum[3] / n : 0, n ? sum[4] / n : 0,
            n ? sum[5] / n : 0, n ? sum[6] / n : 0);

    fprintf(f, "Newest log lines:\n");
    rt_log_recent(put_line, f);

    fprintf(f, "\nLast %u frames, oldest first (ms):\n  frame  total   game  draws    tex   wait   disc   idle  draws# newtex\n",
            g_count);
    for (unsigned i = g_count; i-- > 0;) {
        const DropFrame* d = nth_newest(i);
        fprintf(f, "  %5u %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %6.1f %7u %6u\n", g_count - i, d->total, d->game,
                d->draws, d->tex, d->wait, d->disc, d->idle, d->ndraws, d->newtex);
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        rt_log("[drop] cannot write %s", name);
        return;
    }
    snprintf(g_last, sizeof g_last, "%s", name);
    g_reports++;
    rt_log("[drop] %s: saved %s", why, name);
}

void drop_report_frame(const DropFrame* f, int in_race, int paused) {
    if (!g_settings.drop_reports) return;
    if (!in_race || paused || f->total > 2000.0f) {  /* menus, F1, a suspended app: start over */
        g_count = g_settled = 0;
        return;
    }
    g_ring[g_head] = *f;
    g_head = (g_head + 1) % RING;
    if (g_count < RING) g_count++;
    g_since_report += f->total / 1000.0;
    if (++g_settled < SETTLE || g_since_report < 30.0 || g_reports >= MAX_REPORTS) return;

    int target = g_settings.framerate == FR_30 ? 30 : 60;
    unsigned n = g_count < (unsigned)target ? g_count : (unsigned)target;
    double ms = 0;
    for (unsigned i = 0; i < n; i++) ms += nth_newest(i)->total;
    double fps = ms > 0 ? n * 1000.0 / ms : target;
    char why[96];
    if (fps < target * 0.85)
        snprintf(why, sizeof why, "the frame rate fell to %.0f fps for a second", fps);
    else if (f->total > 100.0f)
        snprintf(why, sizeof why, "one frame took %.0f ms (a stutter)", f->total);
    else
        return;
    write_report(why, target);
    g_since_report = 0;
}
