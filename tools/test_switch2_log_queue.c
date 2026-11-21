/* Own-code logging queue checks; no console/game data. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/switch2/log_queue.h"
int main(void) {
    Switch2LogQueue* q = calloc(1, sizeof *q);
    char in[2048], out[LOG_RECORD_BYTES];
    uint64_t seq;
    assert(q && !switch2_log_pop(q, out, &seq));
    for (unsigned round = 0; round < 20; round++) {
        for (unsigned i = 0; i < LOG_QUEUE_COUNT; i++) {
            snprintf(in, sizeof in, "record %u", round*LOG_QUEUE_COUNT+i);
            assert(switch2_log_push(q, in));
        }
        assert(!switch2_log_push(q, "must drop instead of blocking or overwriting"));
        for (unsigned i = 0; i < LOG_QUEUE_COUNT; i++) {
            assert(switch2_log_pop(q, out, &seq));
            snprintf(in, sizeof in, "record %u", round*LOG_QUEUE_COUNT+i);
            assert(!strcmp(in, out) && seq == (uint64_t)round*LOG_QUEUE_COUNT+i+1);
        }
        assert(!switch2_log_pop(q, out, &seq));
    }
    memset(in, 'x', sizeof in-1); in[sizeof in-1]=0;
    assert(switch2_log_push(q, in) && switch2_log_pop(q, out, &seq));
    assert(strlen(out)==LOG_RECORD_BYTES-1 && q->head==q->tail);
    free(q);
    puts("PASS bounded log FIFO, overflow without overwrite, wraparound, barrier sequences and truncation");
    return 0;
}
