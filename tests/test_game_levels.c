/* Exercise the actual runtime with synthetic guest RAM; no game data required. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/src/game_levels.c"

uint8_t* ppc_mem_base_var;
volatile int g_ring_cap = 999;
void rt_log(const char* fmt, ...) { (void)fmt; }
uint32_t mmio_read(uint32_t ea, int size) { (void)ea; (void)size; assert(0); return 0; }
void mmio_write(uint32_t ea, uint32_t v, int size) { (void)ea; (void)v; (void)size; assert(0); }
static uint32_t player(int i) { return GAME_PLAYERS + (uint32_t)i * GAME_PLAYER_SIZE; }
static void stats(uint32_t p, uint32_t block, float top, float drift, float boost) {
    st32(p + block + OFF_TOP, f32_as_u32(top / 216.0f));
    st32(p + block + OFF_DRIFT, f32_as_u32(drift / 216.0f));
    st32(p + block + OFF_BOOST, f32_as_u32(boost / 216.0f));
}
static void expect(uint32_t p, uint32_t block, uint32_t off, float shown) {
    assert(fabsf(u32_as_f32(ld32(p + block + off)) * 216.0f - shown) < 0.001f);
}
int main(void) {
    ppc_mem_base_var = calloc(1, 32 * 1024 * 1024);
    assert(ppc_mem_base_var);
    st32(GAME_TOP_MODE, GAME_TOP_MODE_RACE);
    uint32_t p = player(0), q = player(1);
    st8(p + GAME_RACER_CHARACTER, GAME_CHAR_SUPER_SONIC);
    st8(p + GAME_RACER_GEAR, GAME_GEAR_CHAOS_EMERALD);
    st8(p + 0x1034, 5); /* shared ability mask is deliberately not a gear identifier */
    stats(p, 0x8DC, 182, 100, 250); stats(p, BLK3, 200, 100, 250);
    st32(p + 0x984, 12345); st32(p + 0x988, 67890);
    assert(lv_super_air_cap(p, 220000, 110000, 110000) == 220000);
    assert(lv_super_air_cap(p, 1200000, 110000, 110000) == 1098900);
    assert(lv_super_air_cap(p, UINT32_MAX, 110000, 0) == 0);
    assert(lv_super_air_cap(q, 220000, 110000, 110000) == 110000);
    const uint32_t rings[] = {0, 99, 100, 199, 200, 299, 300, 999, 300, 299, 199, 0};
    for (unsigned t = 0; t < sizeof rings / sizeof rings[0]; t++) {
        int e = rings[t] >= 300 ? 2 : rings[t] >= 200 ? 1 : 0;
        st32(p + 0xB98, rings[t]);
        uint8_t before[GAME_PLAYER_SIZE];
        for (unsigned b = 0; b < sizeof before; b++) before[b] = ld8(p + b);
        for (int repeat = 0; repeat < 100; repeat++) lv_racer_update(p);
        for (unsigned b = 0; b < sizeof before; b++) {
            int speed_field = 0;
            for (int f = 0; f < 3; f++)
                if (b >= 0x8DC + k_off[f] && b < 0x8DC + k_off[f] + 4) speed_field = 1;
            if (!speed_field) assert(ld8(p + b) == before[b]);
        }
        const float target[3][3] = {{195,110,270},{220,125,300},{240,140,330}};
        expect(p, 0x8DC, OFF_TOP, target[e][0]);
        expect(p, 0x8DC, OFF_DRIFT, target[e][1]);
        expect(p, 0x8DC, OFF_BOOST, target[e][2]);
        expect(p, BLK3, OFF_TOP, 200);
        assert(ld8(p + 0x102E) == 0 && ld32(p + 0xB98) == rings[t]);
        assert(ld32(p + 0x984) == 12345 && ld32(p + 0x988) == 67890);
        if (e) {
            assert(lv_hud_cell(p, 0x1234, 1) != 0x1234);
            assert(lv_hud_cell(p, 0x1234, 0) != 0x1234);
        }
        else assert(lv_hud_cell(p, 0x1234, 1) == 0x1234);
    }
    st32(p + 0xB98, 300); lv_racer_update(p);
    assert(lv_super_player_update(p) == 1 && lv_super_player_update(p) == 0);
    assert(ld8(p + 0x102E) == 0 && ld32(p + 0x988) == 67890);
    /* A partial stat rebuild uses the new boost baseline without compounding the other fields. */
    st32(p + 0x8DC + OFF_BOOST, f32_as_u32(280.0f / 216.0f)); lv_racer_update(p);
    expect(p, 0x8DC, OFF_TOP, 240); expect(p, 0x8DC, OFF_BOOST, 369.6f);
    g_ring_cap = 100; lv_racers_frame();
    assert(lv_super_air_cap(p, 220000, 110000, 110000) == 110000);
    expect(p, 0x8DC, OFF_TOP, 182); expect(p, 0x8DC, OFF_BOOST, 280);
    assert(!lv_super_player_update(p));
    /* Base-tier writes also need ownership: toggle, partial mods and constructor/reset restore originals. */
    g_ring_cap = 999; st32(p + 0xB98, 0); lv_racer_update(p);
    expect(p, 0x8DC, OFF_TOP, 195); expect(p, 0x8DC, OFF_BOOST, 302.4f);
    assert(!lv_super_player_update(p)); /* Base balance itself never queues level-up feedback */
    st32(p + 0x8DC + OFF_DRIFT, f32_as_u32(120.0f / 216.0f));
    for (int repeat = 0; repeat < 100; repeat++) lv_racer_update(p);
    expect(p, 0x8DC, OFF_TOP, 195); expect(p, 0x8DC, OFF_DRIFT, 132);
    lv_racer_reset(p); expect(p, 0x8DC, OFF_TOP, 182); expect(p, 0x8DC, OFF_DRIFT, 120);
    lv_racer_update(p); g_ring_cap = 100; lv_racers_frame();
    expect(p, 0x8DC, OFF_TOP, 182); expect(p, 0x8DC, OFF_DRIFT, 120);
    /* Standard Blue Star retains the original Lv.3 baseline and feedback mechanism. */
    g_ring_cap = 999; stats(q, BLK3, 180, 100, 250); st8(q + 0x102E, 2); st32(q + 0xB98, 300);
    lv_racer_update(q); lv_racer_update(p);
    expect(q, BLK3, OFF_TOP, 215); expect(q, BLK3, OFF_BOOST, 300);
    expect(p, 0x8DC, OFF_TOP, 195); /* still below 200 rings */
    lv_player_update(q); assert(ld8(q + 0x102E) == 1); st8(q + 0x102E, 2);
    /* A gear/character change restores only our still-owned fields. */
    st8(p + GAME_RACER_GEAR, 0); st8(p + GAME_RACER_CHARACTER, 0); lv_racer_update(p);
    expect(p, 0x8DC, OFF_TOP, 182); expect(p, 0x8DC, OFF_BOOST, 280);
    lv_race_reset(); expect(q, BLK3, OFF_TOP, 180);
    st32(q + 0xB98, 300); lv_racer_update(q);
    lv_racer_reset(q); /* new gear's unscaled top is exactly the old gear's raised top */
    st8(q + GAME_RACER_GEAR, 1); stats(q, BLK3, 215, 100, 250); lv_racer_update(q);
    expect(q, BLK3, OFF_TOP, 215 * k_top[2]);
    lv_race_reset(); expect(q, BLK3, OFF_TOP, 215);
    /* All eight racers have independent ownership; a race reset precedes identical rebuilds. */
    for (int i = 0; i < 8; i++) {
        uint32_t a = player(i); stats(a, BLK3, 180 + i, 100, 250);
        st8(a + 0x102E, 2); st32(a + 0xB98, i & 1 ? 300 : 200); lv_racer_update(a);
    }
    lv_race_reset();
    for (int i = 0; i < 8; i++) expect(player(i), BLK3, OFF_TOP, (float)(180 + i));
    assert(racer_index(GAME_PLAYERS + 1) == -1 && racer_index(GAME_PLAYERS + 8 * GAME_PLAYER_SIZE) == -1);
    puts("game_levels: thresholds, baseline ownership, feedback/resource isolation, toggle, gear change, race reset and eight racers PASS");
    free(ppc_mem_base_var);
}
