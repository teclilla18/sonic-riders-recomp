"""Exercise the real native wait wrapper with a deterministic timeout/signal race.
No console/game data needed. Pass --cc <host C compiler>.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <time.h>
typedef int pthread_cond_t;
typedef int pthread_mutex_t;
typedef struct { _Atomic unsigned wait_kind, wait_timeouts; _Atomic uint64_t wait_since, wait_max_ns; } Worker;
static Worker worker, *g_self = &worker;
static int calls, mode, predicate;
static uint64_t platform_now_ns(void) { return 100000000ULL; }
static int switch2_stopping(void) { return 0; }
static void switch2_thread_checkpoint(void) { assert(0); }
static int pthread_mutex_unlock(pthread_mutex_t* m) { (void)m; return 0; }
static void worker_wait(unsigned k, const void* p) { (void)p; worker.wait_kind=k; }
static int fake_clock(int id, struct timespec* t) { (void)id; t->tv_sec=10; t->tv_nsec=0; return 0; }
#define clock_gettime fake_clock
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
static int __real_pthread_cond_wait(pthread_cond_t* c,pthread_mutex_t* m) { (void)c;(void)m;return 77; }
static int __real_pthread_cond_timedwait(pthread_cond_t* c,pthread_mutex_t* m,const struct timespec* t) {
    (void)c;(void)m;(void)t;calls++;
    if (!g_self) return 88;
    if (mode==0) {
        /* Deadline expires while the signaler changes the predicate under the
         * mutex, before this waiter finishes reacquiring it. No second signal. */
        predicate=1;
        return calls==1 ? ETIMEDOUT : EINVAL;
    }
    if (mode==1) return calls==1 ? ETIMEDOUT : 0;
    if (mode==2) return ETIMEDOUT;
    return EINVAL;
}
@FUNCTION@
int main(void) {
    pthread_cond_t c=0; pthread_mutex_t m=0;
    int rc=wait_cond(&c,&m,NULL);
    assert(predicate && rc==0 && calls==1 && worker.wait_kind==0);
    mode=1;calls=0;struct timespec later={11,0};
    assert(wait_cond(&c,&m,&later)==0 && calls==2);
    mode=2;calls=0;struct timespec final={10,1000000};
    assert(wait_cond(&c,&m,&final)==ETIMEDOUT && calls==1);
    mode=3;assert(wait_cond(&c,&m,NULL)==EINVAL);
    g_self=NULL;
    assert(wait_cond(&c,&m,NULL)==77 && wait_cond(&c,&m,&later)==88);
    return 0;
}
'''

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cc', required=True)
    a=ap.parse_args()
    source=(ROOT/'runtime/src/platform/switch2/platform_switch2.c').read_text()
    start=source.index('static int wait_cond(')
    end=source.index('\nint __wrap_pthread_cond_wait',start)
    with tempfile.TemporaryDirectory(prefix='sr-cond-') as temp:
        p=Path(temp); c=p/'wait.c'; exe=p/'wait-test.exe'
        c.write_text(HARNESS.replace('@FUNCTION@',source[start:end]))
        subprocess.run([a.cc,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('PASS timeout/signal race: indefinite predicate recheck; timed deadline/error/driver semantics retained')

if __name__=='__main__': main()
