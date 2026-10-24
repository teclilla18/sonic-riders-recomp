/* Data mods: [gear.<Name>] fields, [memory] writes and [gecko] codes in each enabled mod's mod.ini
 * (docs/MODDING.md). The mod list and order come from mods.c.
 *
 * Gear records follow the community's layout (Sewer56.SonicRiders ExtremeGear, written for the PC
 * version): the GameCube table (GAME_GEAR_TABLE, _Main.rel data) has the same 0x1D0-byte records,
 * checked against the file (level blocks at 0x68/0x84/0xA0: max air 100000, boost cost 25000, boost
 * speed 0.926 = 200 km/h for the Default gear). Speeds in km/h are stored divided by 216.
 *
 * Applied once when the game has linked _Main.rel (gear table, [memory]) and every presented frame
 * ([gecko], as a Gecko code handler does). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_addrs.h"
#include "runtime.h"

int mods_count(void);
int mods_enabled(int i);
const char* mods_folder(int i);
const char* mods_base_dir(void);

static int ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static const char* const k_gears[41] = {
    "Default", "HighBooster", "AutoSlider", "PowerfulGear", "Fastest", "TurboStar", "SpeedBalancer", "BlueStarII",
    "Access", "Beginner", "Accelerator", "TrapGear", "LightBoard", "SlideBooster", "Legend", "MagicCarpet", "AirBroom",
    "Hovercraft", "ChaosEmerald", "Faster", "Gambler", "PowerGear", "OpaOpa", "TheCrazy", "Berserker", "ERider",
    "AirTank", "HeavyBike", "Destroyer", "Omnipotence", "CoverS", "CoverF", "CoverP", "HangOn", "SuperHangOn",
    "Darkness", "Grinder", "AdvantageS", "AdvantageF", "AdvantageP", "Cannonball"};

/* type: 'f' float, 'k' float given in km/h (stored / 216), 'i' int32, 'u' uint32 (hex allowed),
 * 'b' byte, 's' signed byte */
typedef struct { const char* key; uint16_t off; char type; } Field;
static const Field k_fields[] = {
    {"who_can_select", 0x00, 'u'}, {"gear_type", 0x04, 'b'}, {"gear_model", 0x05, 'b'},
    {"acceleration", 0x0C, 'f'}, {"additive_speed", 0x10, 'f'}, {"additive_speed_kmh", 0x10, 'k'},
    {"offroad_speed", 0x14, 'f'}, {"speed_handling_multiplier", 0x18, 'f'}, {"weight", 0x1C, 'f'},
    {"extra_types", 0x20, 'u'}, {"turn_speed_loss", 0x24, 'f'}, {"turn_low_speed_multiplier", 0x28, 'f'},
    {"turn_acceleration", 0x2C, 'f'}, {"turn_max_radius", 0x30, 'f'}, {"drift_max_turn_radius", 0x34, 'f'},
    {"drift_momentum", 0x38, 'f'}, {"drift_min_radius", 0x3C, 'f'}, {"drift_acceleration", 0x40, 'f'},
    {"drift_boost_frames", 0x50, 'i'}, {"air_gain_trick", 0x54, 'f'}, {"air_gain_shortcut", 0x58, 'f'},
    {"air_gain_autorotate", 0x5C, 'f'}, {"special_flags", 0x60, 'u'}, {"jump_air_multiplier", 0x64, 'f'},
    {"stat_dash", 0xBC, 's'}, {"stat_limit", 0xBD, 's'}, {"stat_power", 0xBE, 's'}, {"stat_cornering", 0xBF, 's'},
};
/* per level: level1_ / level2_ / level3_ + key, blocks of 0x1C at 0x68, 0x84, 0xA0 */
static const Field k_level_fields[] = {
    {"max_air", 0x00, 'i'}, {"passive_air_drain", 0x04, 'i'}, {"drift_air_cost", 0x08, 'i'}, {"boost_cost", 0x0C, 'i'},
    {"tornado_cost", 0x10, 'i'}, {"drift_dash_speed", 0x14, 'f'}, {"drift_dash_speed_kmh", 0x14, 'k'},
    {"boost_speed", 0x18, 'f'}, {"boost_speed_kmh", 0x18, 'k'},
};

typedef struct { uint32_t addr; uint8_t size; uint32_t value; } Poke;
static Poke* g_pokes;
static size_t g_npokes, g_cappokes;
typedef struct { uint32_t a, b; } Gecko;
static Gecko* g_gecko;
static size_t g_ngecko, g_capgecko;

/* original GameCube addresses inside _Main.rel (text/data from 803FBD00, then its bss) move to where this
 * port keeps the module; one offset covers both (819FF380 - 805FB080 = 81800000 - 803FBD00) */
static uint32_t relocate(uint32_t a) {
    const uint32_t lo = 0x803FBD00u, delta = 0x81800000u - 0x803FBD00u;
    uint32_t bss = ld32(0x81800000u + 0x20);  /* the module header's bss size */
    if (a >= lo && a < 0x805FB080u + bss) return a + delta;
    return a;
}

static void add_poke(uint32_t addr, uint8_t size, uint32_t value) {
    if (g_npokes == g_cappokes) {
        g_cappokes = g_cappokes * 2 + 32;
        g_pokes = realloc(g_pokes, g_cappokes * sizeof *g_pokes);
        if (!g_pokes) rt_fatal("out of memory (mods)");
    }
    g_pokes[g_npokes++] = (Poke){addr, size, value};
}

static uint32_t parse_value(const Field* f, const char* v, int* ok) {
    char* end;
    *ok = 1;
    if (f->type == 'f' || f->type == 'k') {
        float x = strtof(v, &end);
        if (end == v) { *ok = 0; return 0; }
        if (f->type == 'k') x /= 216.0f;
        uint32_t u;
        memcpy(&u, &x, 4);
        return u;
    }
    long long x = strtoll(v, &end, 0);
    if (end == v) *ok = 0;
    return (uint32_t)x;
}

static int gear_index(const char* name) {
    char* end;
    long n = strtol(name, &end, 10);
    if (end != name && !*end && n >= 0 && n < 41) return (int)n;
    for (int i = 0; i < 41; i++)
        if (ieq(k_gears[i], name)) return i;
    return -1;
}

static void data_entry(const char* mod, const char* section, const char* key, const char* val) {
    if (!strncmp(section, "gear.", 5) || !strncmp(section, "Gear.", 5)) {
        int g = gear_index(section + 5);
        if (g < 0) { rt_log("[mods] %s: unknown gear \"%s\"", mod, section + 5); return; }
        const Field* f = NULL;
        uint32_t base = GAME_GEAR_TABLE + (uint32_t)g * 0x1D0;
        for (size_t i = 0; i < sizeof k_fields / sizeof k_fields[0] && !f; i++)
            if (ieq(k_fields[i].key, key)) f = &k_fields[i];
        if (!f && !strncmp(key, "level", 5) && key[5] >= '1' && key[5] <= '3' && key[6] == '_') {
            for (size_t i = 0; i < sizeof k_level_fields / sizeof k_level_fields[0] && !f; i++)
                if (ieq(k_level_fields[i].key, key + 7)) f = &k_level_fields[i];
            if (f) base += 0x68 + (uint32_t)(key[5] - '1') * 0x1C;
        }
        if (!f) { rt_log("[mods] %s: [%s] unknown field \"%s\"", mod, section, key); return; }
        int ok;
        uint32_t v = parse_value(f, val, &ok);
        if (!ok) { rt_log("[mods] %s: [%s] %s: \"%s\" is not a number", mod, section, key, val); return; }
        add_poke(base + f->off, f->type == 'b' || f->type == 's' ? 1 : 4, v);
        return;
    }
    if (ieq(section, "memory")) {  /* ADDRESS = VALUE in hex; 2, 4 or 8 digits: byte, halfword, word */
        const char* v = val;
        if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
        size_t digits = strspn(v, "0123456789abcdefABCDEF");
        if (digits != 2 && digits != 4 && digits != 8) {
            rt_log("[mods] %s: [memory] %s: write the value as 2, 4 or 8 hex digits", mod, key);
            return;
        }
        add_poke((uint32_t)strtoul(key, NULL, 16), (uint8_t)(digits / 2), (uint32_t)strtoul(v, NULL, 16));
        return;
    }
    rt_log("[mods] %s: unknown section [%s]", mod, section);
}

static void gecko_line(const char* mod, const char* line) {
    unsigned a, b;
    if (sscanf(line, "%x %x", &a, &b) != 2) return;
    unsigned type = (a >> 24) & 0xFE;
    if (type != 0x00 && type != 0x02 && type != 0x04) {
        rt_log("[mods] %s: Gecko code %08X %08X not supported (only 00/02/04 writes)", mod, a, b);
        return;
    }
    if (g_ngecko == g_capgecko) {
        g_capgecko = g_capgecko * 2 + 32;
        g_gecko = realloc(g_gecko, g_capgecko * sizeof *g_gecko);
        if (!g_gecko) rt_fatal("out of memory (mods)");
    }
    g_gecko[g_ngecko++] = (Gecko){a, b};
}

/* [gear.*], [memory] and [gecko] of every enabled mod, in load order (a later mod wins) */
static void read_data_mods(void) {
    for (int m = 0; m < mods_count(); m++) {
        if (!mods_enabled(m)) continue;
        const char* mod = mods_folder(m);
        char path[512];
        snprintf(path, sizeof path, "%s/%s/mod.ini", mods_base_dir(), mod);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        char line[512], section[128] = "";
        size_t p0 = g_npokes, g0 = g_ngecko;
        while (fgets(line, sizeof line, f)) {
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            p[strcspn(p, "\r\n;")] = 0;  /* ";" starts a comment */
            if (!*p || *p == '#') continue;
            if (*p == '[') {
                char* e = strchr(p, ']');
                if (e) *e = 0;
                snprintf(section, sizeof section, "%s", p + 1);
                continue;
            }
            if (ieq(section, "mod")) continue;
            if (ieq(section, "gecko")) { gecko_line(mod, p); continue; }
            char* eq = strchr(p, '=');
            if (!eq) continue;
            *eq = 0;
            char *k = p, *v = eq + 1;
            for (char* t = eq - 1; t >= k && (*t == ' ' || *t == '\t'); t--) *t = 0;
            while (*v == ' ' || *v == '\t') v++;
            size_t vl = strlen(v);
            while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) v[--vl] = 0;
            data_entry(mod, section, k, v);
        }
        fclose(f);
        if (g_npokes > p0 || g_ngecko > g0)
            rt_log("[mods] %s: %zu data changes, %zu Gecko codes", mod, g_npokes - p0, g_ngecko - g0);
    }
}

static void poke(uint32_t addr, int size, uint32_t v) {
    addr = relocate(addr);
    if ((addr & 0xC0000000u) != 0x80000000u) return;
    if (size == 1) st8(addr, (uint8_t)v);
    else if (size == 2) st16(addr, (uint16_t)v);
    else st32(addr, v);
}

/* game_mods.c: _Main.rel is linked (its data and bss are in place) */
void mods_apply_link(void) {
    static int done;
    if (done++ || !mods_count()) return;
    read_data_mods();
    for (size_t i = 0; i < g_npokes; i++) poke(g_pokes[i].addr, g_pokes[i].size, g_pokes[i].value);
    if (g_npokes) rt_log("[mods] %zu data changes applied", g_npokes);
}

/* game_mods.c, every presented frame: the Gecko writes (00 byte / 02 halfword with a repeat count,
 * 04 word; codes 01/03/05 add 0x01000000 to the address) */
void mods_apply_frame(void) {
    for (size_t i = 0; i < g_ngecko; i++) {
        uint32_t a = g_gecko[i].a, b = g_gecko[i].b;
        uint32_t addr = 0x80000000u | (a & 0x01FFFFFFu);
        switch ((a >> 24) & 0xFE) {
        case 0x00: for (uint32_t k = 0; k <= (b >> 16); k++) poke(addr + k, 1, b & 0xFF); break;
        case 0x02: for (uint32_t k = 0; k <= (b >> 16); k++) poke(addr + 2 * k, 2, b & 0xFFFF); break;
        default: poke(addr, 4, b); break;
        }
    }
}
