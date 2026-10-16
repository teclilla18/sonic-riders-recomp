/* External Interface (0xCC006800): three channels of chip-selected serial devices, modelled at the
 * register level so the game's own EXI and CARD libraries (mount, FAT, directory, files) run
 * recompiled. Devices:
 *   channel 0, device 0: memory card in slot A (a raw card image file)
 *   channel 0, device 1: the IPL chip: SRAM (64 bytes) and the real-time clock (ROM reads give zeros)
 *   everything else: absent
 * Transfers complete at once; transfer-complete and card interrupts go through os_raise_interrupt.
 * Command set and register layout: YAGCD "EXI", Dolphin's EXI_DeviceMemoryCard / EXI_DeviceIPL. */
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <threads.h>
#include <time.h>

#include "runtime.h"

#define OS_INTERRUPT_EXI_0_EXI 9   /* +3 per channel: EXI, TC, EXT */

/* CSR bits */
#define CSR_EXIINTMASK 0x0001
#define CSR_EXIINT     0x0002
#define CSR_TCINTMASK  0x0004
#define CSR_TCINT      0x0008
#define CSR_EXTINTMASK 0x0400
#define CSR_EXTINT     0x0800
#define CSR_EXT        0x1000
#define CSR_INTS       (CSR_EXIINT | CSR_TCINT | CSR_EXTINT)

typedef struct Device Device;
struct Device {
    void (*select)(Device* d);              /* chip select asserted: a new command starts */
    void (*write)(Device* d, uint8_t b);    /* one byte from the CPU */
    uint8_t (*read)(Device* d);             /* one byte to the CPU */
    int chan;
};

static struct { uint32_t csr, mar, len, cr, data; Device* dev; } g_ch[3];

/* ---------------------------------------------------------------- memory card */
static uint32_t rtc_now_for_card(void);
typedef struct Ipl Ipl;

#define CARD_MBITS 4                          /* 59 blocks */
#define CARD_SIZE (CARD_MBITS * 0x20000u)     /* 512 KiB */
typedef struct {
    Device base;
    uint8_t* mem;
    char path[260];
    uint8_t cmd, status, irq_enable;
    uint32_t pos, addr, rd;
    uint8_t buf[128];
    _Atomic int dirty;         /* changed since the file was last written */
    _Atomic uint64_t touched;  /* rt_ticks() of the last change */
    mtx_t lock;                /* mem vs. the writer thread's snapshot */
} Card;
static struct Ipl g_ipl;

/* The card file is written in the background, once the game has not changed the card for 0.5 s
 * (and at exit): a save is ~1250 erase/program commands, and rewriting the whole image after each
 * one took long enough on the Switch's SD card for the CARD library's timeout alarms to fire. */
static _Atomic unsigned g_card_file_writes;
static _Atomic unsigned g_card_erases, g_card_programs;
unsigned exi_card_file_writes(void) { return atomic_load(&g_card_file_writes); }

static void card_write_file(Card* c) {
    atomic_fetch_add(&g_card_file_writes, 1);
    static uint8_t snap[CARD_SIZE];
    mtx_lock(&c->lock);
    atomic_store(&c->dirty, 0);
    memcpy(snap, c->mem, CARD_SIZE);
    mtx_unlock(&c->lock);
    FILE* f = fopen(c->path, "wb");
    if (!f) { rt_log("[card] cannot write %s", c->path); return; }
    size_t n = fwrite(snap, 1, CARD_SIZE, f);
    if (fclose(f) || n != CARD_SIZE) rt_log("[card] write error on %s", c->path);
    else rt_log("[card] saved the card file (%u sector erases, %u page writes since the last save)",
                atomic_exchange(&g_card_erases, 0), atomic_exchange(&g_card_programs, 0));
}

static void card_save(Card* c) {  /* called with mem changed */
    atomic_store(&c->touched, rt_ticks());
    atomic_store(&c->dirty, 1);
}

static Card g_card_a;

static int card_writer(void* arg) {
    Card* c = arg;
    rt_thread_pin(2);  /* off the game thread's core 0 (see di.c's di_timer) */
    for (;;) {
        struct timespec ts = {0, 100000000};
        thrd_sleep(&ts, NULL);
        if (atomic_load(&c->dirty) && rt_ticks() - atomic_load(&c->touched) > 40500000ull / 2) card_write_file(c);
    }
    return 0;
}

static void card_flush_at_exit(void) {
    if (atomic_load(&g_card_a.dirty)) card_write_file(&g_card_a);
}

static void card_raise(Card* c) {
    if (c->irq_enable) {
        g_ch[c->base.chan].csr |= CSR_EXIINT;
        if (g_ch[c->base.chan].csr & CSR_EXIINTMASK) os_raise_interrupt(OS_INTERRUPT_EXI_0_EXI + 3 * c->base.chan);
    }
}

static uint32_t card_addr(const uint8_t* b) {  /* the 4 address bytes of read/program */
    return ((uint32_t)(b[0] & 0x7F) << 17) | ((uint32_t)b[1] << 9) | ((uint32_t)(b[2] & 3) << 7) | (b[3] & 0x7F);
}

static void card_select(Device* d) {
    Card* c = (Card*)d;
    if (c->cmd == 0xF1) g_card_erases++;  /* counted for the log line of the next card file write */
    else if (c->cmd == 0xF2) g_card_programs++;
    /* the end of a page program happens when the CPU deselects: commit it */
    if (c->cmd == 0xF2 && c->pos > 5) {
        uint32_t n = c->pos - 5 > 128 ? 128 : c->pos - 5;
        uint32_t page = c->addr & (CARD_SIZE - 1) & ~0x7Fu;
        mtx_lock(&c->lock);
        for (uint32_t i = 0; i < n; i++) c->mem[(page + ((c->addr + i) & 0x7F)) & (CARD_SIZE - 1)] = c->buf[i];
        mtx_unlock(&c->lock);
        card_save(c);
        card_raise(c);
    }
    c->cmd = 0;
    c->pos = 0;
    c->rd = 0;
}

static void card_write(Device* d, uint8_t b) {
    Card* c = (Card*)d;
    if (c->pos == 0) { c->cmd = b; c->pos = 1; c->rd = 0; return; }
    static uint8_t a[4];
    switch (c->cmd) {
    case 0x52:  /* read array: 4 address bytes, then latency bytes are ignored */
        if (c->pos <= 4) a[c->pos - 1] = b;
        if (c->pos == 4) c->addr = card_addr(a);
        break;
    case 0xF1:  /* sector erase: 2 address bytes */
        if (c->pos <= 2) a[c->pos - 1] = b;
        if (c->pos == 2) {
            uint32_t sector = (((uint32_t)(a[0] & 0x7F) << 17) | ((uint32_t)a[1] << 9)) & (CARD_SIZE - 1) & ~0x1FFFu;
            mtx_lock(&c->lock);
            memset(c->mem + sector, 0xFF, 0x2000);
            mtx_unlock(&c->lock);
            card_save(c);
            card_raise(c);
        }
        break;
    case 0xF2:  /* page program: 4 address bytes, then up to 128 data bytes */
        if (c->pos <= 4) a[c->pos - 1] = b;
        if (c->pos == 4) c->addr = card_addr(a);
        if (c->pos > 4 && c->pos - 5 < 128) c->buf[c->pos - 5] = b;
        break;
    case 0x81: c->irq_enable = b & 1; break;            /* interrupt enable */
    case 0x89: c->status &= ~0x18u; break;              /* clear status: error bits */
    default: break;
    }
    c->pos++;
}

static uint8_t card_read(Device* d) {
    Card* c = (Card*)d;
    uint8_t v = 0;
    switch (c->cmd) {
    case 0x00: {  /* ID: 4 bytes, the size in Mbits */
        static const uint8_t id[4] = {0, 0, 0, CARD_MBITS};
        v = c->rd < 4 ? id[c->rd] : 0;
        break;
    }
    case 0x83: v = c->status; break;                    /* status: ready + unlocked */
    case 0x52: v = c->mem[(c->addr + c->rd) & (CARD_SIZE - 1)]; break;
    case 0x85: v = c->rd == 0 ? 0xC2 : 0x21; break;     /* flash ID (Macronix) */
    default: v = 0; break;
    }
    c->rd++;
    return v;
}

/* CARD checksum (__CARDCheckSum): sum and sum of complements of the big-endian 16-bit words;
 * 0xFFFF is stored as 0 */
static void card_checksum(const uint8_t* p, uint32_t len, uint8_t* out) {
    uint16_t c1 = 0, c2 = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2) {
        uint16_t w = (uint16_t)(p[i] << 8 | p[i + 1]);
        c1 += w;
        c2 += (uint16_t)~w;
    }
    if (c1 == 0xFFFF) c1 = 0;
    if (c2 == 0xFFFF) c2 = 0;
    out[0] = c1 >> 8; out[1] = (uint8_t)c1; out[2] = c2 >> 8; out[3] = (uint8_t)c2;
}
static void put16be(uint8_t* p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put32be(uint8_t* p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

/* A freshly formatted card, exactly as the game's __CARDFormatRegionAsync writes one: real cards
 * come formatted, and an unformatted (all 0xFF) card is reported as damaged, with no format offer.
 * The serial must match the console's SRAM flash ID (all zeros here) through the CARD LCG seeded
 * with the format time; VerifyID checks it. */
static void card_format(uint8_t* m, const uint8_t* sram) {
    memset(m, 0xFF, CARD_SIZE);
    uint8_t* id = m;
    uint64_t t = (uint64_t)rtc_now_for_card() * 40500000ull;  /* OSTime: timebase ticks since 2000 */
    uint64_t r = t;
    for (int i = 0; i < 12; i++) {
        r = (r * 0x41C64E6Dull + 0x3039) >> 16;
        id[i] = (uint8_t)(sram[0x14 + i] + (uint8_t)r);  /* SRAMEX flash ID of slot A, +LCG */
        r = ((r * 0x41C64E6Dull + 0x3039) >> 16) & 0x7FFF;
    }
    put32be(id + 0x0C, (uint32_t)(t >> 32));
    put32be(id + 0x10, (uint32_t)t);
    memcpy(id + 0x14, sram + 0x0C, 4);                         /* SRAM counter bias */
    put32be(id + 0x18, sram[0x12]);                             /* SRAM language */
    put32be(id + 0x1C, 0);                                      /* VI DTV status */
    put16be(id + 0x20, 0);                                      /* device id */
    put16be(id + 0x22, CARD_MBITS);
    put16be(id + 0x24, 0);                                      /* encoding: ANSI */
    card_checksum(id, 0x1FC, id + 0x1FC);
    for (int k = 0; k < 2; k++) {                               /* directories */
        uint8_t* d = m + (1 + k) * 0x2000;
        put16be(d + 0x1FFA, (uint16_t)k);
        card_checksum(d, 0x1FFC, d + 0x1FFC);
    }
    for (int k = 0; k < 2; k++) {                               /* block allocation tables */
        uint8_t* b = m + (3 + k) * 0x2000;
        memset(b, 0, 0x2000);
        put16be(b + 4, (uint16_t)k);
        put16be(b + 6, (uint16_t)(CARD_SIZE / 0x2000 - 5));    /* free blocks */
        put16be(b + 8, 4);                                      /* last allocated */
        card_checksum(b + 4, 0x1FFC, b);
    }
}

static void card_init(Card* c, int chan, const char* path, const uint8_t* sram) {
    c->base.select = card_select;
    c->base.write = card_write;
    c->base.read = card_read;
    c->base.chan = chan;
    c->status = 0x41;  /* ready | unlocked: the SDK skips the unlock sequence */
    c->mem = malloc(CARD_SIZE);
    mtx_init(&c->lock, mtx_plain);
    snprintf(c->path, sizeof c->path, "%s", path);
    FILE* f = fopen(path, "rb");
    if (f && fread(c->mem, 1, CARD_SIZE, f) == CARD_SIZE) {
        rt_log("[card] slot A: %s", path);
    } else {
        card_format(c->mem, sram);
        card_write_file(c);
        rt_log("[card] slot A: new formatted card %s (59 blocks)", path);
    }
    if (f) fclose(f);
    atexit(card_flush_at_exit);
    rt_thread_start(card_writer, c);
}

/* ---------------------------------------------------------------- IPL: SRAM + RTC */
struct Ipl {
    Device base;
    uint8_t sram[64];
    uint32_t cmd, pos, rd, region, off, write;
};

static void sram_checksum(uint8_t* s) {
    uint16_t c1 = 0, c2 = 0;
    for (int i = 0x0C; i < 0x14; i += 2) {
        uint16_t w = (uint16_t)(s[i] << 8 | s[i + 1]);
        c1 += w;
        c2 += (uint16_t)~w;
    }
    s[0] = c1 >> 8; s[1] = (uint8_t)c1; s[2] = c2 >> 8; s[3] = (uint8_t)c2;
}

/* days since 1970-01-01 of a civil date (proleptic Gregorian; H. Hinnant's days_from_civil) */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static uint32_t rtc_now(void) {  /* seconds since 2000-01-01 00:00 local time (no mktime: it failed on the Switch) */
    int y, mo, d, h, mi, s;
    platform_local_time(&y, &mo, &d, &h, &mi, &s);
    int64_t secs = (days_from_civil(y, mo, d) - days_from_civil(2000, 1, 1)) * 86400 + h * 3600 + mi * 60 + s;
    return secs > 0 ? (uint32_t)secs : 0;
}

static uint32_t rtc_now_for_card(void) { return rtc_now(); }

static void ipl_select(Device* d) { Ipl* p = (Ipl*)d; p->pos = 0; p->rd = 0; p->cmd = 0; }

static void ipl_write(Device* d, uint8_t b) {
    Ipl* p = (Ipl*)d;
    if (p->pos < 4) {
        p->cmd = p->cmd << 8 | b;
        if (++p->pos == 4) {
            p->write = p->cmd >> 31;
            uint32_t a = (p->cmd & 0x7FFFFFFF) >> 6;
            p->region = a == 0x800000 ? 1 : (a >= 0x800004 && a < 0x800004 + 64) ? 2 : 0;
            p->off = p->region == 2 ? a - 0x800004 : 0;
        }
        return;
    }
    if (p->write && p->region == 2 && p->off + (p->pos - 4) < 64) {
        p->sram[p->off + (p->pos - 4)] = b;
        sram_checksum(p->sram);
    }
    p->pos++;
}

static uint8_t ipl_read(Device* d) {
    Ipl* p = (Ipl*)d;
    uint8_t v = 0;
    if (p->region == 1) { uint32_t t = rtc_now(); v = (uint8_t)(t >> (24 - 8 * (p->rd & 3))); }
    else if (p->region == 2 && p->off + p->rd < 64) v = p->sram[p->off + p->rd];
    p->rd++;
    return v;
}

static void ipl_init(void) {
    g_ipl.base.select = ipl_select;
    g_ipl.base.write = ipl_write;
    g_ipl.base.read = ipl_read;
    memset(g_ipl.sram, 0, sizeof g_ipl.sram);
    g_ipl.sram[0x11] = 0x00;  /* ntd: NTSC */
    g_ipl.sram[0x12] = 0x00;  /* language: English */
    g_ipl.sram[0x13] = 0x2C;  /* flags: stereo sound, setup done */
    /* OSSramEx (at 0x14): flashID[2][12] stays zero; flashIDCheckSum[i] = ~sum(flashID[i]) = 0xFF.
     * CARD mount checks it for an already-unlocked card (DoMount) */
    g_ipl.sram[0x14 + 0x26] = 0xFF;
    g_ipl.sram[0x14 + 0x27] = 0xFF;
    sram_checksum(g_ipl.sram);
}

/* ---------------------------------------------------------------- registers */
static Device* selected(int ch) {
    uint32_t cs = (g_ch[ch].csr >> 7) & 7;
    if (ch == 0 && cs == 1) return &g_card_a.base;
    if (ch == 0 && cs == 2) return &g_ipl.base;
    return NULL;
}

static void transfer(int ch) {
    uint32_t cr = g_ch[ch].cr;
    Device* d = selected(ch);
    int rw = (cr >> 2) & 3;  /* 0 read, 1 write, 2 read/write */
    if (cr & 2) {            /* DMA */
        uint32_t addr = g_ch[ch].mar & 0x03FFFFE0, len = g_ch[ch].len;
        for (uint32_t i = 0; i < len && addr + i < GUEST_RAM_SIZE; i++) {
            if (rw == 1) { if (d) d->write(d, *mem_phys(addr + i)); }
            else *mem_phys(addr + i) = d ? d->read(d) : 0;
        }
        g_ch[ch].len = 0;
    } else {                 /* immediate: up to 4 bytes through DATA */
        int n = (int)((cr >> 4) & 3) + 1;
        uint32_t out = g_ch[ch].data, in = 0;
        for (int i = 0; i < n; i++) {
            uint8_t b = (uint8_t)(out >> (24 - 8 * i)), r = 0;
            if (d) {
                if (rw != 0) d->write(d, b);
                if (rw != 1) r = d->read(d);
            }
            in |= (uint32_t)r << (24 - 8 * i);
        }
        if (rw != 1) g_ch[ch].data = in;
    }
    g_ch[ch].cr &= ~1u;
    g_ch[ch].csr |= CSR_TCINT;
    if (g_ch[ch].csr & CSR_TCINTMASK) os_raise_interrupt(OS_INTERRUPT_EXI_0_EXI + 3 * ch + 1);
}

uint32_t exi_read(uint32_t ea, int size) {
    (void)size;
    uint32_t off = (ea & 0xFF) - 0x00, ch = off / 0x14, r = (off % 0x14) >> 2;
    if (ch > 2) return 0;
    switch (r) {
    case 0: {
        uint32_t v = g_ch[ch].csr & ~CSR_EXT;
        if (ch == 0) v |= CSR_EXT;  /* slot A has a card */
        return v;
    }
    case 1: return g_ch[ch].mar;
    case 2: return g_ch[ch].len;
    case 3: return g_ch[ch].cr;
    default: return g_ch[ch].data;
    }
}

void exi_write(uint32_t ea, uint32_t v, int size) {
    (void)size;
    uint32_t off = (ea & 0xFF), ch = off / 0x14, r = (off % 0x14) >> 2;
    if (ch > 2) return;
    switch (r) {
    case 0: {
        uint32_t old_cs = (g_ch[ch].csr >> 7) & 7;
        g_ch[ch].csr = (g_ch[ch].csr & CSR_INTS & ~v) | (v & ~CSR_INTS & ~CSR_EXT);
        uint32_t cs = (g_ch[ch].csr >> 7) & 7;
        if (cs != old_cs) {
            Device* d = selected((int)ch);
            if (old_cs && ch == 0 && old_cs == 1) g_card_a.base.select(&g_card_a.base);  /* deselect commits */
            if (d) d->select(d);
        }
        /* pending interrupt status still enabled: raise again */
        if ((g_ch[ch].csr & CSR_EXIINT) && (g_ch[ch].csr & CSR_EXIINTMASK)) os_raise_interrupt(OS_INTERRUPT_EXI_0_EXI + 3 * (int)ch);
        break;
    }
    case 1: g_ch[ch].mar = v; break;
    case 2: g_ch[ch].len = v; break;
    case 3: g_ch[ch].cr = v; if (v & 1) transfer((int)ch); break;
    default: g_ch[ch].data = v; break;
    }
}

void exi_init(const char* card_path) {
    ipl_init();
    card_init(&g_card_a, 0, card_path, g_ipl.sram);
}
