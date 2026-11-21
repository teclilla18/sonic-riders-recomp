/* Synthetic data only: test speculative seeks, interleaved streams, boundaries and short reads
 * against an independent byte oracle, while the real read-ahead worker runs. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "runtime.h"
#ifdef _WIN32
#include <windows.h>
#endif
void disc_cache_path(const char*);
size_t disc_read_cached(uint64_t,void*,size_t);
static FILE* raw;
static uint8_t fst[36], expected[2<<20];
static unsigned cache_hits;
static void putbe(uint8_t* p,uint32_t x) { p[0]=x>>24; p[1]=x>>16; p[2]=x>>8; p[3]=x; }
const uint8_t* disc_fst(uint32_t* n) { *n=sizeof fst; return fst; }
uint64_t mods_base(void) { return 0x100000000ull; }
size_t disc_read(uint64_t off,void* dst,size_t n) {
    if (off>=mods_base()) { memset(dst,0xC7,n); return n; }
    fseek(raw,(long)off,SEEK_SET); return fread(dst,1,n,raw);
}
#ifdef _WIN32
static int (*work_fn)(void*);
static DWORD WINAPI run_worker(void* arg) { return (DWORD)work_fn(arg); }
void rt_thread_start(int (*fn)(void*),void* arg) {
    work_fn=fn; HANDLE t=CreateThread(NULL,0,run_worker,arg,0,NULL); assert(t); CloseHandle(t);
}
#else
void rt_thread_start(int (*fn)(void*),void* arg) { thrd_t t; assert(thrd_create(&t,fn,arg)==thrd_success); thrd_detach(t); }
#endif
void rt_thread_pin(int core) { (void)core; }
void rt_thread_lower_priority(void) {}
void rt_log(const char* fmt,...) {
    va_list ap; va_start(ap,fmt);
    if (strstr(fmt,"reads served")) cache_hits=va_arg(ap,unsigned);
    va_end(ap);
}
static void check(uint64_t off,size_t n) {
    uint8_t got[300000]; assert(n<=sizeof got);
    size_t count=disc_read_cached(off,got,n);
    size_t want=off>=sizeof expected?0:sizeof expected-(size_t)off;
    if (want>n) want=n;
    assert(count==want);
    assert(!memcmp(got,expected+(off<sizeof expected?(size_t)off:0),want));
}
int main(int argc,char** argv) {
    assert(argc==2);
    for (size_t i=0;i<sizeof expected;i++) expected[i]=(uint8_t)((i*37)^(i>>9));
    FILE* out=fopen(argv[1],"wb"); assert(out); assert(fwrite(expected,1,sizeof expected,out)==sizeof expected); fclose(out);
    raw=fopen(argv[1],"rb"); assert(raw);
    fst[0]=1; putbe(fst+8,3);
    putbe(fst+16,0x1000); putbe(fst+20,0xB0000);
    putbe(fst+28,0x100000); putbe(fst+32,0x70000);
    disc_cache_path(argv[1]);
    struct timespec pause={0,1000000};
    for (unsigned i=0;i<1200;i++) {
        uint64_t off=(i&1?0x100000u:0x1000u)+(i/2%10)*57000u;
        check(off,57000);
        thrd_sleep(&pause,NULL);
    }
    check(0xAFFE0,0x4000); /* cross a file boundary */
    check(sizeof expected-100,300); /* short physical read */
    check(sizeof expected+100,300); /* EOF */
    check(0x1000,270000); /* larger than a cache window */
    uint8_t mod[32]; assert(disc_read_cached(mods_base(),mod,sizeof mod)==sizeof mod);
    for (unsigned i=0;i<sizeof mod;i++) assert(mod[i]==0xC7);
    assert(cache_hits>0);
    printf("PASS: interleaved streams, boundaries, EOF, large reads, mods; %u cache hits\n",cache_hits);
    return 0;
}
