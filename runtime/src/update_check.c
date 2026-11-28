/* At start-up, on a helper thread: is a newer release on GitHub? If so, a banner says so for 20 s and
 * F1 > About shows it with a button to the download page (update_core.h does the fetch and compare).
 * Off with the setting update_check=0 (F1 > About), SR_NOUPDATECHECK=1, and in scripted test runs
 * (SR_INPUT, SR_PERFTEST) unless SR_UPDATECHECK=1. */
#include <stdio.h>
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

/* A release zip unpacked over an older install, without running the install again: the folder's VERSION
 * file (written by tools/make_release.py) names the new version, but this game is still the old build. */
static char g_unpacked[64];  /* that newer version, "" if none */
const char* update_unbuilt(void) { return g_unpacked[0] ? g_unpacked : NULL; }
static void check_unpacked(void) {
    FILE* f = fopen("VERSION", "rb");  /* the game runs in its install folder */
    if (!f) return;                      /* a git checkout: no VERSION file */
    size_t n = fread(g_unpacked, 1, sizeof g_unpacked - 1, f);
    fclose(f);
    g_unpacked[n] = 0;
    g_unpacked[strcspn(g_unpacked, "\r\n")] = 0;
    if (!g_unpacked[0] || !strcmp(g_unpacked, g_build_version)) {
        g_unpacked[0] = 0;
        return;
    }
    rt_log("[update] this folder holds %s but the game is %s: the install was not run after unpacking", g_unpacked,
           g_build_version);
}
const char* update_notes(void) { return g_latest_set ? g_notes : NULL; }

#ifdef _WIN32
static int unbuilt_thread(void* arg) {
    (void)arg;
    char msg[224];
    snprintf(msg, sizeof msg, "%.*s is unpacked here but not installed yet: quit and open SonicRidersLauncher.exe "
             "(FINISH UPDATE) to finish.", (int)strcspn(g_unpacked, " "), g_unpacked);  /* the tag, no hash */
    overlay_set_banner(msg);
    Sleep(30000);
    overlay_set_banner(NULL);
    return 0;
}

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
    if (g_unpacked[0]) return 0;  /* the "not installed yet" banner says what to do */
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
    check_unpacked();  /* always, even with update checks off: no internet needed */
    if (g_unpacked[0] && !getenv("SR_NET") && !getenv("SR_REPLAY") && !getenv("SR_RECORD") && !getenv("SR_PERFTEST"))
        rt_thread_start(unbuilt_thread, NULL);
    int forced = getenv("SR_UPDATECHECK") != NULL;  /* testing: check even in a scripted run */
    if (!g_settings.update_check || getenv("SR_NOUPDATECHECK") || getenv("RECOMPLAUNCHER") || (!forced && (getenv("SR_INPUT") || getenv("SR_PERFTEST")))) return;
    int v[3];
    if (!update_parse_version(g_build_version, v)) return;  /* a build without a release tag */
    rt_thread_start(update_thread, NULL);
#endif
}
