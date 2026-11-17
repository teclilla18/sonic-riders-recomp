/* SPDX-License-Identifier: MIT
 * Independent diagnostic: no game source, code, assets or data are linked.
 */
#include "diag.h"
#include "build_info.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DIR "sdmc:/switch/SR2Diag"
Test tests[TEST_COUNT] = {
    {"Application startup", PENDING, "not run"},
    {"System services / SD / RomFS", PENDING, "not run"},
    {"Timing / SDK counter", PENDING, "not run"},
    {"Worker / TLS / allowed cores", PENDING, "not run"},
    {"Controller buttons / sticks", PENDING, "not run"},
    {"Audio transport + heard tone", PENDING, "not run"},
    {"Guest mapping: stack SVC", PENDING, "not run"},
    {"Guest mapping: process-code SVC", PENDING, "not run"},
    {"Vulkan clear / presentation", PENDING, "not run"},
    {"Vulkan visible output", PENDING, "not run"},
    {"Explicit resource shutdown", PENDING, "checked when exiting"}
};
PadState pad;
bool quitting, cleanup_failed, persistent_log;
unsigned run_number;
static FILE *log_file;
static bool screen_live;
static bool log_fault_reported;
static unsigned log_sequence;
static int selected = T_SERVICES;
static const char *status_names[] = {"PENDING", "PASS", "FAIL", "SKIPPED"};

static void flush_log(void) {
    if (log_file) {
        bool ok = fflush(log_file) == 0 && !ferror(log_file);
        if (fsync(fileno(log_file)) != 0) ok = false;
        if (!ok) {
            persistent_log = false;
            if (!log_fault_reported) {
                fprintf(stderr, "SR2Diag: SD log flush failed, errno=%d; further probes disabled\n", errno);
                log_fault_reported = true;
            }
        }
    }
    fflush(stderr);
}

void diag_log(const char *fmt, ...) {
    if (!log_file) return;
    fprintf(log_file, "%06u ", ++log_sequence);
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fputc('\n', log_file);
    flush_log();
}

/* libounce has mounted SD and initialized default services before this hook. */
void userAppInit(void) {
    mkdir("sdmc:/switch", 0777);
    mkdir(DIR, 0777);
    char path[128];
    for (run_number = 1; run_number < 1000000; run_number++) {
        snprintf(path, sizeof path, DIR "/run-%06u.log", run_number);
        FILE *old = fopen(path, "rb");
        if (old) { fclose(old); continue; }
        log_file = fopen(path, "wb");
        break;
    }
    persistent_log = log_file != NULL;
    if (!persistent_log) return;
    setvbuf(log_file, NULL, _IONBF, 0);
    diag_log("BEGIN userAppInit; SR2Diag %s; SDK %s", DIAG_BUILD_ID, DIAG_SDK);
    FILE *latest = fopen(DIR "/latest.txt", "wb");
    if (latest) { fprintf(latest, "run-%06u.log\n", run_number); fflush(latest); fsync(fileno(latest)); fclose(latest); }
    snprintf(path, sizeof path, DIR "/driver-%06u.log", run_number);
    if (freopen(path, "w", stderr)) setvbuf(stderr, NULL, _IONBF, 0);
    else diag_log("WARN driver stderr log could not be opened; errno=%d", errno);
    diag_log("Default libounce startup reached; sm/applet/hid/time/fs are initialized");
    diag_log("BUILD %s", DIAG_COMPONENTS);
    diag_log("END userAppInit; log paths sdmc:/switch/SR2Diag/{run,driver}-%06u.log", run_number);
}

void userAppExit(void) {
    /* Return-to-hbmenu itself must be confirmed by the tester after this line. */
    diag_log("EXIT_HOOK reached; handing back to libounce; hbmenu return/relaunch UNCONFIRMED");
    if (log_file) { flush_log(); fclose(log_file); log_file = NULL; }
    fflush(stderr);
    fclose(stderr);
}

void result(int id, Status status, const char *note) {
    tests[id].status = status;
    tests[id].note = note;
    diag_log("RESULT %s %s: %s", status_names[status], tests[id].name, note);
}

uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

bool poll_input(void) {
    if (!appletMainLoop()) { quitting = true; return false; }
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) { quitting = true; return false; }
    return true;
}

bool screen_start(void) {
    if (screen_live) return true;
    diag_log("BEGIN consoleInit (native default window; potential failure boundary)");
    screen_live = consoleInit(NULL) != NULL;
    diag_log("END consoleInit success=%d", screen_live);
    return screen_live;
}

void screen_stop(void) {
    if (screen_live) {
        diag_log("BEGIN consoleExit before releasing display buffers");
        consoleExit(NULL);
        screen_live = false;
        diag_log("END consoleExit");
    }
}

void redraw(const char *message) {
    if (!screen_live) return;
    printf("\x1b[2J\x1b[H");
    printf("SR2Diag 0.1.0 - native Switch 2 diagnostic\n");
    printf("Build %s / SDK %s / run %06u\n\n", DIAG_BUILD_ID, DIAG_SDK, run_number);
    for (int i = 0; i < TEST_COUNT; i++) {
        printf("%c %-8s %-34s\n", i == selected ? '>' : ' ', status_names[tests[i].status], tests[i].name);
        if (i == selected) printf("           %s\n", tests[i].note);
    }
    printf("\n%s\n", message);
    printf("\nSD logs: /switch/SR2Diag/run-%06u.log\n", run_number);
    printf("Driver:  /switch/SR2Diag/driver-%06u.log\n", run_number);
    if (!persistent_log) printf("FAIL: cannot open persistent SD log; probes disabled.\n");
    consoleUpdate(NULL);
}

Status confirm(const char *message) {
    diag_log("BEGIN user confirmation: %s", message);
    /* Discard held keys from the preceding stage. */
    for (int i = 0; i < 10 && poll_input(); i++) svcSleepThread(16000000);
    const uint64_t deadline = monotonic_ns() + 30000000000ULL;
    while (!quitting && poll_input()) {
        redraw(message);
        uint64_t down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Y) { diag_log("User confirmed YES"); return PASS; }
        if (down & HidNpadButton_B) { diag_log("User confirmed NO"); return FAIL; }
        if (down & HidNpadButton_X) return SKIPPED;
        if (monotonic_ns() >= deadline) break;
        svcSleepThread(16000000);
    }
    diag_log("Confirmation skipped or timed out");
    return SKIPPED;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    diag_log("BEGIN main; applet_type=%d own_process_handle=%08x", appletGetAppletType(), envGetOwnProcessHandle());
    diag_log("BEGIN padConfigureInput / padInitializeDefault");
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    diag_log("END pad initialization");
    if (!screen_start()) return 1;
    result(T_START, persistent_log ? PASS : FAIL, persistent_log ? "main reached; persistent SD log opened" : "SD logging failed; no probes allowed");
    while (!quitting && poll_input()) {
        uint64_t down = padGetButtonsDown(&pad);
        if ((down & HidNpadButton_Down) && selected < T_VISUAL) selected++;
        if ((down & HidNpadButton_Up) && selected > T_SERVICES) selected--;
        if (down & HidNpadButton_X) result(selected, SKIPPED, "skipped by tester");
        if ((down & HidNpadButton_A) && persistent_log && !cleanup_failed) {
            diag_log("BEGIN STAGE %d %s", selected, tests[selected].name);
            redraw("Running selected stage. + requests exit when calls return.");
            switch (selected) {
            case T_SERVICES: probe_services(); break;
            case T_TIMING: probe_timing(); break;
            case T_THREADS: probe_threads(); break;
            case T_INPUT: probe_input(); break;
            case T_AUDIO: probe_audio(); break;
            case T_STACK_MAP: probe_mapping(false); break;
            case T_CODE_MAP: probe_mapping(true); break;
            case T_VULKAN: probe_vulkan(); break;
            case T_VISUAL: result(T_VISUAL, SKIPPED, "run the Vulkan stage for visual confirmation"); break;
            default: break;
            }
            diag_log("END STAGE %d", selected);
            if (selected < T_VULKAN) selected++;
        }
        redraw(cleanup_failed ? "Resource cleanup failed. Exit and restart the homebrew session."
                             : "D-pad selects | A run | X skip | + exit (tests do not autorun)");
        svcSleepThread(16000000);
    }
    diag_log("BEGIN explicit shutdown");
    for (int i = 0; i < T_SHUTDOWN; i++)
        if (tests[i].status == PENDING) result(i, SKIPPED, "not selected before exit");
    redraw("Exiting. Confirm hbmenu appears, then relaunch and test another homebrew.");
    svcSleepThread(700000000);
    screen_stop();
    result(T_SHUTDOWN, cleanup_failed ? FAIL : PASS,
           cleanup_failed ? "resource cleanup failed; see log" : "owned resources released; hbmenu return still unconfirmed");
    diag_log("END main; resource cleanup complete=%d", !cleanup_failed);
    return cleanup_failed ? 1 : 0;
}
