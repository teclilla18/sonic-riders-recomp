/* Music packs: your own songs for each stage, picked when the game starts a stage song.
 *
 * A mod can carry a `music/` folder with one folder per stage:
 *     mods/<pack>/music/01 Metal City/<songs>.adx      (a number 1-16, a stage name, or both)
 *     mods/<pack>/music/01 Metal City/music.ini        (optional: shuffle = on|off, original = on|off,
 *                                                        <song file> = off|rare|normal|often)
 * The game has 8 stage song files (S1.ADX-S8.ADX); stages 9-16 reuse those of stages 1-8. mods.c makes each of
 * them a "music slot" on the virtual disc (as large as its largest song) and asks music_pick() which file to
 * serve when the game starts reading it, so every one of the 16 stages gets its own list:
 *   - player 1 holds a direction on the d-pad while the race loads: that song of the list (as Sonic Riders DX does:
 *     the same 4 buttons of the game's controller struct, first to fourth song; A held: the original song);
 *   - otherwise, with shuffle on (the default), a weighted random pick that never repeats the last song;
 *   - shuffle off: the first song (or the one music.ini marks `often`).
 * The stage is the race setup's stage number (REL bss +0x17928, 1-16). No songs are shipped: people add their own.
 * SR_MUSIC=0 turns it off. Supported now: ADX (the game's own format). */
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "recomp_table.h"
#include "runtime.h"

#define STAGES 16
/* race setup: stage number, 1-16 for the Grand Prix stages: REL bss +0x17928 (0x806129A8 in the original game, where
 * the module's bss follows its image) */
#define STAGE_BSS_OFF 0x17928u
#define PAD1_HELD 0x801AF9F8u    /* player 1's controller struct (0x801AF9F0) +0x8: held buttons */

static const char* const STAGE_NAMES[STAGES] = {
    "Metal City", "Splash Canyon", "Egg Factory", "Green Cave", "Sand Ruins", "Babylon Garden", "Digital Dimension",
    "Sega Carnival", "Night Chase", "Red Canyon", "Ice Factory", "White Cave", "Dark Desert", "Sky Road",
    "Babylon Guardian", "Sega Illusion"};

/* weight 0 off, 1 rare, 2 normal, 3 often; key "<mod>/<file>" names it in the player's music.ini */
typedef struct { char path[512]; char key[192]; uint64_t size; int weight; } Song;
typedef struct {
    Song* songs;
    int n, cap;
    int shuffle, original, orig_weight;  /* original: orig_weight > 0 (the game's own song in the shuffle) */
    int last;  /* index of the last song played, -1 none, -2 the original */
} Cat;
static Cat g_cat[STAGES];
static int g_off = -1;
static uint32_t g_rng = 0x9E3779B9u;

static int disabled(void) {
    if (g_off < 0) {
        const char* e = getenv("SR_MUSIC");
        g_off = e && *e == '0';
    }
    return g_off;
}

const char* music_stage_name(int stage) { return stage >= 1 && stage <= STAGES ? STAGE_NAMES[stage - 1] : "?"; }

/* "01 Metal City", "1", "metal-city", "Stage 09 Night Chase" -> 1-16, or 0 */
static int stage_of(const char* dir) {
    const char* p = dir;
    while (*p && !isdigit((unsigned char)*p)) p++;
    if (*p) {
        int v = atoi(p);
        if (v >= 1 && v <= STAGES) return v;
    }
    char want[64], have[64];
    size_t k = 0;
    for (const char* q = dir; *q && k + 1 < sizeof have; q++)
        if (isalpha((unsigned char)*q)) have[k++] = (char)tolower((unsigned char)*q);
    have[k] = 0;
    for (int s = 0; s < STAGES; s++) {
        k = 0;
        for (const char* q = STAGE_NAMES[s]; *q && k + 1 < sizeof want; q++)
            if (isalpha((unsigned char)*q)) want[k++] = (char)tolower((unsigned char)*q);
        want[k] = 0;
        if (k && strstr(have, want)) return s + 1;
    }
    return 0;
}

static int has_ext(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    if (n <= e) return 0;
    for (size_t i = 0; i < e; i++)
        if (tolower((unsigned char)name[n - e + i]) != ext[i]) return 0;
    return 1;
}

static int weight_word(const char* v) {
    if (!strncmp(v, "normal", 6) || !strncmp(v, "on", 2) || !strncmp(v, "yes", 3)) return 2;
    if (!strncmp(v, "off", 3) || !strncmp(v, "no", 2) || *v == '0') return 0;
    if (!strncmp(v, "rare", 4) || *v == '1') return 1;
    if (!strncmp(v, "often", 5) || *v == '3') return 3;
    return 2;
}

/* music.ini of one stage folder */
static void read_ini(const char* dir, Cat* c, int first_new) {
    char p[600], line[600];
    snprintf(p, sizeof p, "%s/music.ini", dir);
    FILE* f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char* s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#' || *s == ';' || *s == '[') continue;
        char* eq = strchr(s, '=');
        if (!eq) continue;
        char* ke = eq;
        while (ke > s && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
        *ke = 0;
        char* v = eq + 1;
        while (*v == ' ' || *v == '\t') v++;
        v[strcspn(v, "\r\n")] = 0;
        for (char* q = v; *q; q++) *q = (char)tolower((unsigned char)*q);
        if (!strcmp(s, "shuffle")) c->shuffle = weight_word(v) != 0;
        else if (!strcmp(s, "original")) { c->orig_weight = weight_word(v); c->original = c->orig_weight > 0; }
        else
            for (int i = first_new; i < c->n; i++) {
                const char* b = strrchr(c->songs[i].path, '/');
                if (b && !strcmp(b + 1, s)) c->songs[i].weight = weight_word(v);
            }
    }
    fclose(f);
}

static int cmp_song(const void* a, const void* b) { return strcmp(((const Song*)a)->path, ((const Song*)b)->path); }

/* mods.c, for each mod that is on: <mods dir>/<mod>/music */
void music_scan(const char* music_dir, const char* mod_name) {
    if (disabled()) return;
    DIR* d = opendir(music_dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char sub[512];
        snprintf(sub, sizeof sub, "%s/%s", music_dir, e->d_name);
        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        int stage = stage_of(e->d_name);
        if (!stage) {
            rt_log("[music] %s: folder \"%s\" is not a stage name or number 1-16; skipped", mod_name, e->d_name);
            continue;
        }
        Cat* c = &g_cat[stage - 1];
        if (!c->n && !c->cap) { c->shuffle = 1; c->original = 0; c->orig_weight = 0; c->last = -1; }
        int first = c->n;
        DIR* sd = opendir(sub);
        if (!sd) continue;
        struct dirent* f;
        while ((f = readdir(sd))) {
            if (f->d_name[0] == '.' || !has_ext(f->d_name, ".adx")) continue;
            char p[600];
            snprintf(p, sizeof p, "%s/%s", sub, f->d_name);
            if (stat(p, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0x40) continue;
            if (c->n == c->cap) {
                c->cap = c->cap * 2 + 8;
                c->songs = realloc(c->songs, (size_t)c->cap * sizeof *c->songs);
                if (!c->songs) rt_fatal("out of memory (music)");
            }
            Song* s = &c->songs[c->n++];
            snprintf(s->path, sizeof s->path, "%s", p);
            snprintf(s->key, sizeof s->key, "%s/%s", mod_name, f->d_name);
            s->size = (uint64_t)st.st_size;
            s->weight = 2;
        }
        closedir(sd);
        qsort(c->songs + first, (size_t)(c->n - first), sizeof *c->songs, cmp_song);
        read_ini(sub, c, first);
        if (c->n > first)
            rt_log("[music] %s: %s, %d song%s%s", mod_name, STAGE_NAMES[stage - 1], c->n - first, c->n - first == 1 ? "" : "s",
                   c->shuffle ? " (shuffle)" : "");
    }
    closedir(d);
}

/* slot k (1-8: S<k>.ADX) is used by stages k and k+8 */
int music_slot_used(int k) { return k >= 1 && k <= 8 && (g_cat[k - 1].n || g_cat[k + 7].n); }
uint64_t music_slot_max(int k) {
    uint64_t m = 0;
    for (int h = 0; h < 2; h++) {
        const Cat* c = &g_cat[k - 1 + 8 * h];
        for (int i = 0; i < c->n; i++)
            if (c->songs[i].size > m) m = c->songs[i].size;
    }
    return m;
}

static uint32_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* ---- the player's own choices (the in-game Music page): music.ini next to settings.ini ----
 *   [Metal City]
 *   shuffle = on
 *   original = off            (the game's own song: off | rare | normal | often)
 *   dx-music/1 (DX 00).adx = often
 * They are read after the packs (so they win over a pack's music.ini) and written on every change. */
const char* platform_settings_path(void);
static int g_user_loaded;
static void user_path(char* out, size_t n) {
    const char* sp = platform_settings_path();
    const char* slash = strrchr(sp, '/');
    snprintf(out, n, "%.*smusic.ini", slash ? (int)(slash - sp + 1) : 0, sp);
}
static void user_load(void) {
    if (g_user_loaded) return;
    g_user_loaded = 1;
    char p[512], line[700];
    user_path(p, sizeof p);
    FILE* f = fopen(p, "r");
    if (!f) return;
    Cat* c = NULL;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char* e = strchr(line, ']');
            if (e) *e = 0;
            int st = stage_of(line + 1);
            c = st ? &g_cat[st - 1] : NULL;
            continue;
        }
        char* eq = strstr(line, " = ");
        if (!c || !eq) continue;
        *eq = 0;
        const char* v = eq + 3;
        if (!strcmp(line, "shuffle")) c->shuffle = weight_word(v) != 0;
        else if (!strcmp(line, "original")) { c->orig_weight = weight_word(v); c->original = c->orig_weight > 0; }
        else
            for (int i = 0; i < c->n; i++)
                if (!strcmp(c->songs[i].key, line)) c->songs[i].weight = weight_word(v);
    }
    fclose(f);
}
static const char* const WORDS[4] = {"off", "rare", "normal", "often"};
static void user_save(void) {
    char p[512];
    user_path(p, sizeof p);
    FILE* f = fopen(p, "w");
    if (!f) { rt_log("[music] cannot write %s", p); return; }
    fprintf(f, "# Your music choices (in-game settings menu > Music). Packs live in mods/<pack>/music/<stage>/.\n");
    for (int s = 0; s < STAGES; s++) {
        const Cat* c = &g_cat[s];
        if (!c->n) continue;
        fprintf(f, "\n[%s]\nshuffle = %s\noriginal = %s\n", STAGE_NAMES[s], c->shuffle ? "on" : "off", WORDS[c->orig_weight & 3]);
        for (int i = 0; i < c->n; i++) fprintf(f, "%s = %s\n", c->songs[i].key, WORDS[c->songs[i].weight & 3]);
    }
    fclose(f);
}

/* for the Music page (menu.cpp); stage 1-16, song 0..n-1 */
int music_songs(int stage) { user_load(); return stage >= 1 && stage <= STAGES ? g_cat[stage - 1].n : 0; }
const char* music_song_file(int stage, int i) {
    const char* b = strrchr(g_cat[stage - 1].songs[i].path, '/');
    return b ? b + 1 : g_cat[stage - 1].songs[i].path;
}
const char* music_song_pack(int stage, int i) { return g_cat[stage - 1].songs[i].key; }
int music_song_weight(int stage, int i) { return g_cat[stage - 1].songs[i].weight; }
void music_set_song_weight(int stage, int i, int w) { g_cat[stage - 1].songs[i].weight = w & 3; user_save(); }
int music_shuffle(int stage) { return g_cat[stage - 1].shuffle; }
void music_set_shuffle(int stage, int on) { g_cat[stage - 1].shuffle = !!on; user_save(); }
int music_original(int stage) { return g_cat[stage - 1].orig_weight; }
void music_set_original(int stage, int w) {
    g_cat[stage - 1].orig_weight = w & 3;
    g_cat[stage - 1].original = g_cat[stage - 1].orig_weight > 0;
    user_save();
}

/* which file to serve for slot k now: a song's path and size, or NULL for the slot's own file */
const char* music_pick(int k, uint64_t* size) {
    static int seeded;
    if (!seeded) { seeded = 1; g_rng ^= (uint32_t)platform_now_ns(); }
    user_load();
    uint32_t st = mem_r32(g_rel_bss + STAGE_BSS_OFF);
    int stage = (st == (uint32_t)k || st == (uint32_t)k + 8) ? (int)st : k;  /* another mode using S<k>: stage k's list */
    Cat* c = &g_cat[stage - 1];
    if (!c->n) {
        rt_log("[music] %s (stage %u): no songs added, original music", STAGE_NAMES[stage - 1], st);
        return NULL;
    }
    uint32_t held = mem_r32(PAD1_HELD);
    int pick = -3;
    const char* how = "";
    /* DX's buttons: the highest of 0x80/0x40/0x20/0x10 picks the first to fourth song, 0x01 the original */
    uint32_t b = held & 0xF1u;
    if (b) {
        int bit = 31 - __builtin_clz(b);  /* 7..4 or 0 */
        if (bit >= 4) {
            int want = 7 - bit;
            if (want < c->n) { pick = want; how = "chosen with the d-pad"; }
        } else { pick = -2; how = "chosen (original)"; }
    }
    if (pick == -3 && !c->shuffle) {
        pick = 0;
        for (int i = 0; i < c->n; i++)
            if (c->songs[i].weight == 3) { pick = i; break; }
        how = "shuffle off";
    }
    if (pick == -3) {
        uint32_t tot = 0;
        int alive = (c->original && c->orig_weight) ? 1 : 0;
        for (int i = 0; i < c->n; i++) alive += c->songs[i].weight > 0;
        for (int pass = 0; pass < 2 && pick == -3; pass++) {  /* pass 0 skips the last song, pass 1 allows it */
            tot = 0;
            for (int i = -1; i < c->n; i++) {
                int w = i < 0 ? (c->original ? c->orig_weight : 0) : c->songs[i].weight;
                int id = i < 0 ? -2 : i;
                if (pass == 0 && alive > 1 && id == c->last) w = 0;
                tot += (uint32_t)w;
            }
            if (!tot) continue;
            uint32_t r = rnd() % tot;
            for (int i = -1; i < c->n; i++) {
                int w = i < 0 ? (c->original ? c->orig_weight : 0) : c->songs[i].weight;
                int id = i < 0 ? -2 : i;
                if (pass == 0 && alive > 1 && id == c->last) w = 0;
                if (r < (uint32_t)w) { pick = id; break; }
                r -= (uint32_t)w;
            }
        }
        if (pick == -3) pick = 0;
        how = "shuffle";
    }
    c->last = pick;
    if (pick == -2) {
        rt_log("[music] %s (stage %u): original song (%s; buttons %04x)", STAGE_NAMES[stage - 1], st, how, held & 0xFFFFu);
        return NULL;
    }
    const char* name = strrchr(c->songs[pick].path, '/');
    rt_log("[music] %s (stage %u): song %d of %d, %s (%s; buttons %04x)", STAGE_NAMES[stage - 1], st, pick + 1, c->n,
           name ? name + 1 : c->songs[pick].path, how, held & 0xFFFFu);
    *size = c->songs[pick].size;
    return c->songs[pick].path;
}
