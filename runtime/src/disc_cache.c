/* Host-side read-ahead for deterministic DVD reads. Guest DMA and completion points are untouched.
 * A second read-only FILE belongs to the worker, so its seeks cannot move the demand reader.
 * Two 256 KiB windows, bounded to the same FST file; replacement mods always use disc_read. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#define CACHE_BYTES (256u<<10)
enum { EMPTY, READING, READY };
typedef struct { uint64_t off; size_t len, got; int state; unsigned char* bytes; } Window;
static Window windows[2];
static FILE* ahead;
static char* disc_path;
static mtx_t lock;
static cnd_t wake;
static once_flag once=ONCE_FLAG_INIT;
static int enabled;
static unsigned hits, reads;
void disc_cache_path(const char* path) { disc_path=strdup(path); }
static uint32_t be32(const unsigned char* p) { return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static uint64_t file_end(uint64_t off) {
    uint32_t size; const unsigned char* fst=disc_fst(&size);
    if (!fst || size<12) return 0;
    uint32_t count=be32(fst+8);
    if (count>size/12) return 0;
    for (uint32_t i=1;i<count;i++) {
        const unsigned char* p=fst+i*12;
        if (p[0]) continue;
        uint64_t start=be32(p+4), end=start+be32(p+8);
        if (off>=start && off<end) return end;
    }
    return 0;
}
static size_t read_at(FILE* f,uint64_t off,void* dst,size_t n) {
#ifdef _WIN32
    if (_fseeki64(f,(long long)off,SEEK_SET)) return 0;
#else
    if (fseeko(f,(off_t)off,SEEK_SET)) return 0;
#endif
    return fread(dst,1,n,f);
}
static int worker(void* unused) {
    (void)unused;
    rt_thread_pin(2); rt_thread_lower_priority(); /* core 3 is not in the original Switch application's allowed mask */
    mtx_lock(&lock);
    for (;;) {
        int k=-1;
        for (int i=0;i<2;i++) if (windows[i].state==READING) { k=i; break; }
        if (k<0) { cnd_wait(&wake,&lock); continue; }
        Window* w=&windows[k]; uint64_t off=w->off; size_t len=w->len;
        mtx_unlock(&lock);
        size_t got=read_at(ahead,off,w->bytes,len);
        mtx_lock(&lock);
        w->got=got; w->state=READY;
    }
    return 0;
}
static void init(void) {
    const char* e=getenv("SR_DISC_READAHEAD");
    if ((e && !atoi(e)) || !disc_path) return;
    ahead=fopen(disc_path,"rb"); if (!ahead) return;
    for (int i=0;i<2;i++) windows[i].bytes=malloc(CACHE_BYTES);
    if (!windows[0].bytes || !windows[1].bytes) {
        free(windows[0].bytes); free(windows[1].bytes); fclose(ahead); ahead=NULL; return;
    }
    mtx_init(&lock,mtx_plain); cnd_init(&wake);
    enabled=1; rt_thread_start(worker,NULL);
    rt_log("[disc-cache] deterministic DVD read-ahead: two 256 KiB windows, read-only worker");
}
uint64_t mods_base(void);
size_t disc_read_cached(uint64_t off,void* dst,size_t n) {
    if (off>=mods_base() || n>CACHE_BYTES || !n) return disc_read(off,dst,n);
    call_once(&once,init);
    if (!enabled) return disc_read(off,dst,n);
    int hit=0;
    mtx_lock(&lock);
    for (int i=0;i<2;i++) {
        Window* w=&windows[i];
        if (w->state==READY && off>=w->off && off-w->off<=w->got && n<=w->got-(size_t)(off-w->off)) {
            memcpy(dst,w->bytes+(size_t)(off-w->off),n); hit=1; hits++; break;
        }
    }
    mtx_unlock(&lock);
    size_t got=hit?n:disc_read(off,dst,n);
    if (hit && hits==1) rt_log("[disc-cache] first DVD read served from memory");
    /* Prefetch only bytes from this same file, after the demand read. A miss keeps the old synchronous
     * path; it never waits for a speculative read or changes the game's virtual completion time. */
    uint64_t next=off+got, end=file_end(off);
    if (got==n && next<end) {
        mtx_lock(&lock);
        int covered=0, target=-1;
        for (int i=0;i<2;i++) {
            Window* w=&windows[i];
            if (w->state!=EMPTY && next>=w->off && next-w->off<w->len) covered=1;
            if (w->state!=READING && (target<0 || w->state==EMPTY || w->off<windows[target].off)) target=i;
        }
        if (!covered && target>=0) {
            Window* w=&windows[target]; w->off=next;
            w->len=end-next<CACHE_BYTES?(size_t)(end-next):CACHE_BYTES;
            w->got=0; w->state=READING; cnd_signal(&wake);
        }
        mtx_unlock(&lock);
    }
    if (++reads%600==0) rt_log("[disc-cache] %u/%u reads served from memory",hits,reads);
    return got;
}
