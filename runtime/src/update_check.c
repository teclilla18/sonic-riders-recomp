/* At start-up, on a helper thread: is a newer release on GitHub? If so, a banner says so for 20 s and
 * F1 > About shows it with a button to the download page (update_core.h does the fetch and compare).
 * Off with the setting update_check=0 (F1 > About), SR_NOUPDATECHECK=1, and in scripted test runs
 * (SR_INPUT, SR_PERFTEST) unless SR_UPDATECHECK=1. */
#include <stdlib.h>
#include <string.h>
#include "overlay.h"
#include "runtime.h"
#include "settings.h"
#include "update_core.h"

extern const char g_build_version[];

static char g_latest[32];          /* the newer tag, "" if none (or not known yet) */
static char* g_notes;              /* its release notes (plain text) */
static volatile int g_latest_set;  /* g_latest and g_notes are complete */

const char* update_available(void) { return g_latest_set ? g_latest : NULL; }
const char* update_notes(void) { return g_latest_set ? g_notes : NULL; }

#ifdef _WIN32
static int update_thread(void* arg) {
    (void)arg;
    UpdateInfo info;
    if (update_fetch_info(&info, NULL)) {
        rt_log("[update] could not reach GitHub (no internet?); skipped");
        return 0;
    }
    const char* tag = info.tag;
    if (!update_is_newer(tag, g_build_version)) {
        rt_log("[update] up to date (newest release %s)", tag);
        free(info.notes);
        return 0;
    }
    strcpy(g_latest, tag);
    g_notes = info.notes;
    g_latest_set = 1;
    rt_log("[update] %s is available (this is %s)", tag, g_build_version);
    if (getenv("SR_NET") || getenv("SR_REPLAY") || getenv("SR_RECORD")) return 0;  /* the banner is theirs */
    char msg[192];
    snprintf(msg, sizeof msg, "Update available: %s. See what's new in F1 > About; the launcher installs it.", tag);
    overlay_set_banner(msg);
    Sleep(20000);
    overlay_set_banner(NULL);
    return 0;
}
#endif

void update_check_start(void) {
#ifdef _WIN32
    int forced = getenv("SR_UPDATECHECK") != NULL;  /* testing: check even in a scripted run */
    if (!g_settings.update_check || getenv("SR_NOUPDATECHECK") || (!forced && (getenv("SR_INPUT") || getenv("SR_PERFTEST")))) return;
    int v[3];
    if (!update_parse_version(g_build_version, v)) return;  /* a build without a release tag */
    rt_thread_start(update_thread, NULL);
#endif
}
