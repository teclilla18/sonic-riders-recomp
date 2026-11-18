/* Bounded own-code log records; caller serializes with a short native mutex.
 * No file I/O or allocation is permitted while accessing this queue. */
#pragma once
#include <stdint.h>
#include <string.h>
#define LOG_QUEUE_COUNT 256u
#define LOG_RECORD_BYTES 1024u
typedef struct {
    char records[LOG_QUEUE_COUNT][LOG_RECORD_BYTES];
    uint64_t head, tail;
} Switch2LogQueue;
static inline int switch2_log_push(Switch2LogQueue* q, const char* text) {
    if (q->tail-q->head == LOG_QUEUE_COUNT) return 0;
    size_t n = strlen(text);
    if (n >= LOG_RECORD_BYTES) n = LOG_RECORD_BYTES-1;
    memcpy(q->records[q->tail % LOG_QUEUE_COUNT], text, n);
    q->records[q->tail++ % LOG_QUEUE_COUNT][n] = 0;
    return 1;
}
static inline int switch2_log_pop(Switch2LogQueue* q, char* text, uint64_t* seq) {
    if (q->head == q->tail) return 0;
    memcpy(text, q->records[q->head++ % LOG_QUEUE_COUNT], LOG_RECORD_BYTES);
    *seq = q->head;
    return 1;
}
