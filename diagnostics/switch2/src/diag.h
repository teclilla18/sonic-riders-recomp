/* SPDX-License-Identifier: MIT */
#pragma once
#include <switch.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum { PENDING, PASS, FAIL, SKIPPED } Status;
typedef struct { const char *name; Status status; const char *note; } Test;
enum { T_START, T_SERVICES, T_TIMING, T_THREADS, T_INPUT, T_AUDIO,
       T_STACK_MAP, T_CODE_MAP, T_VULKAN, T_VISUAL, T_SHUTDOWN, TEST_COUNT };
extern Test tests[TEST_COUNT];
extern PadState pad;
extern bool quitting, cleanup_failed;
extern unsigned run_number;
extern bool persistent_log;
void diag_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void result(int id, Status status, const char *note);
bool poll_input(void);
bool screen_start(void);
void screen_stop(void);
void redraw(const char *message);
Status confirm(const char *message);
uint64_t monotonic_ns(void);
void probe_services(void);
void probe_timing(void);
void probe_threads(void);
void probe_input(void);
void probe_audio(void);
void probe_mapping(bool code);
void probe_vulkan(void);
