/* File mods: a virtual disc built at startup from the disc image and the mods folder.
 *
 *   <mods dir>/mods.ini             load order, one mod folder per line, top to bottom: a later mod
 *                                   wins when two replace the same file; "#" in front turns one off.
 *                                   Mod folders not listed yet are added at the end (on).
 *   <mods dir>/<Mod>/mod.ini        [mod] name, author, version, description (shown in the log)
 *   <mods dir>/<Mod>/files/<path>   replaces the disc file <path> (any size), or adds a new one
 *
 * The disc's file system table (FST) is rebuilt: replaced and added files get offsets past the end of
 * the image (from MODS_BASE) and disc_read serves those ranges from the mod files. The game's own
 * DVD code then reads them like any other file. Paths compare without regard to case, as the game's
 * DVDConvertPathToEntrynum does; new files may only go into folders the disc already has.
 * main.dol (outside the FST) and _Main.rel cannot be replaced: their code is translated ahead of time.
 * Docs: docs/MODDING.md. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "runtime.h"

#define MODS_BASE 0x60000000ull  /* past the 1.46 GB image; FST offsets are 32-bit */
#define MAX_MODS 64

typedef struct { uint64_t off, size; char path[512]; int logged; } VFile;
static VFile* g_vf;
static size_t g_nvf, g_capvf;
static uint64_t g_vend = MODS_BASE;
static FILE* g_open;
static size_t g_open_i = (size_t)-1;

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void wbe32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

static int ieq(const char* a, const char* b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

/* ---- the replacement list: disc path (lower case, '/'-separated) -> host file ---- */
typedef struct { char disc[256]; char host[512]; uint64_t size; int mod; } Repl;
static Repl* g_repl;
static size_t g_nrepl, g_caprepl;

static void add_repl(const char* disc, const char* host, uint64_t size, int mod) {
    for (size_t i = 0; i < g_nrepl; i++)
        if (ieq(g_repl[i].disc, disc)) {  /* a later mod wins */
            snprintf(g_repl[i].host, sizeof g_repl[i].host, "%s", host);
            g_repl[i].size = size;
            g_repl[i].mod = mod;
            return;
        }
    if (g_nrepl == g_caprepl) {
        g_caprepl = g_caprepl * 2 + 64;
        g_repl = realloc(g_repl, g_caprepl * sizeof *g_repl);
        if (!g_repl) rt_fatal("out of memory (mods)");
    }
    Repl* r = &g_repl[g_nrepl++];
    snprintf(r->disc, sizeof r->disc, "%s", disc);
    snprintf(r->host, sizeof r->host, "%s", host);
    r->size = size;
    r->mod = mod;
}

static void scan_files(const char* host_dir, const char* disc_dir, int mod) {
    DIR* d = opendir(host_dir);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char host[512], disc[256];
        snprintf(host, sizeof host, "%s/%s", host_dir, e->d_name);
        snprintf(disc, sizeof disc, "%s%s%s", disc_dir, *disc_dir ? "/" : "", e->d_name);
        struct stat st;
        if (stat(host, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) scan_files(host, disc, mod);
        else add_repl(disc, host, (uint64_t)st.st_size, mod);
    }
    closedir(d);
}

/* ---- the mods folder: mods.ini order, each mod's mod.ini and files ---- */
static char g_names[MAX_MODS][128], g_titles[MAX_MODS][128];
static int g_on[MAX_MODS], g_nmods;
static char g_base[256];

static void write_order(void) {
    char path[512];
    snprintf(path, sizeof path, "%s/mods.ini", g_base);
    FILE* w = fopen(path, "w");
    if (!w) return;
    fprintf(w, "# Sonic Riders mods, applied top to bottom: when two mods replace the same file, the\n"
               "# lower one wins. Put a # in front of a mod to turn it off. Changes apply on the next start.\n");
    for (int i = 0; i < g_nmods; i++) fprintf(w, "%s%s\n", g_on[i] ? "" : "# ", g_names[i]);
    fclose(w);
}

/* the settings menu's Mods page (overlay.c): the list as mods.ini has it; changes apply on restart */
int mods_count(void) { return g_nmods; }
const char* mods_title(int i) { return i >= 0 && i < g_nmods ? (g_titles[i][0] ? g_titles[i] : g_names[i]) : ""; }
int mods_enabled(int i) { return i >= 0 && i < g_nmods && g_on[i]; }
void mods_set_enabled(int i, int on) {
    if (i < 0 || i >= g_nmods || g_on[i] == !!on) return;
    g_on[i] = !!on;
    write_order();
    rt_log("[mods] %s turned %s in the settings menu (applies on the next start)", g_names[i], on ? "on" : "off");
}

static void ini_value(const char* path, const char* key, char* out, size_t n) {
    out[0] = 0;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[512];
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (strncmp(p, key, kl) != 0) continue;
        p += kl;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        p[strcspn(p, "\r\n")] = 0;
        snprintf(out, n, "%s", p);
        break;
    }
    fclose(f);
}

static int is_mod_dir(const char* base, const char* name) {
    char p[512];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s/files", base, name);
    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) return 1;
    snprintf(p, sizeof p, "%s/%s/mod.ini", base, name);
    return stat(p, &st) == 0;
}

static void read_order(const char* base) {
    char path[512];
    snprintf(path, sizeof path, "%s/mods.ini", base);
    FILE* f = fopen(path, "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f) && g_nmods < MAX_MODS) {
            line[strcspn(line, "\r\n")] = 0;
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            int on = 1;
            if (*p == '#') {  /* "# Name": off, if Name is a mod folder (otherwise a comment) */
                on = 0;
                p++;
                while (*p == ' ' || *p == '\t') p++;
            }
            size_t l = strlen(p);
            while (l && (p[l - 1] == ' ' || p[l - 1] == '\t')) p[--l] = 0;
            if (!*p || !is_mod_dir(base, p)) continue;
            int dup = 0;
            for (int i = 0; i < g_nmods; i++) dup |= ieq(g_names[i], p);
            if (dup) continue;
            snprintf(g_names[g_nmods], sizeof g_names[0], "%s", p);
            g_on[g_nmods++] = on;
        }
        fclose(f);
    }
    /* folders not listed yet: appended, on, and written back so the list shows them */
    int added = 0;
    DIR* d = opendir(base);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) && g_nmods < MAX_MODS) {
            if (e->d_name[0] == '.' || !is_mod_dir(base, e->d_name)) continue;
            int known = 0;
            for (int i = 0; i < g_nmods; i++) known |= ieq(g_names[i], e->d_name);
            if (known) continue;
            snprintf(g_names[g_nmods], sizeof g_names[0], "%s", e->d_name);
            g_on[g_nmods++] = 1;
            added++;
        }
        closedir(d);
    }
    if (added || !f) write_order();
}

/* ---- the rebuilt FST ---- */
typedef struct { const char* name; int is_dir; uint32_t parent, next, off, size; } Node;

static Node* g_nodes;
static size_t g_nnodes, g_capnodes;
static size_t push_node(Node n) {
    if (g_nnodes == g_capnodes) {
        g_capnodes = g_capnodes * 2 + 2048;
        g_nodes = realloc(g_nodes, g_capnodes * sizeof *g_nodes);
        if (!g_nodes) rt_fatal("out of memory (mods)");
    }
    g_nodes[g_nnodes] = n;
    return g_nnodes++;
}

static uint64_t vfile(const Repl* r) {
    if (g_nvf == g_capvf) {
        g_capvf = g_capvf * 2 + 64;
        g_vf = realloc(g_vf, g_capvf * sizeof *g_vf);
        if (!g_vf) rt_fatal("out of memory (mods)");
    }
    VFile* v = &g_vf[g_nvf++];
    v->off = g_vend;
    v->size = r->size;
    v->logged = 0;
    snprintf(v->path, sizeof v->path, "%s", r->host);
    g_vend = (g_vend + r->size + 0x7FFF) & ~0x7FFFull;  /* 32 KB aligned, as disc files are */
    return v->off;
}

/* copy the directory `dir` (FST index) of the original table into g_nodes, applying replacements; new
 * files whose folder is `prefix` go in at its end */
static uint32_t g_replaced, g_added;
static void copy_dir(const uint8_t* fst, const char* names, uint32_t first, uint32_t stop, const char* prefix,
                     size_t parent, int* used) {
    uint32_t i = first;
    while (i < stop) {
        const uint8_t* e = fst + i * 12;
        const char* nm = names + (be32(e) & 0xFFFFFF);
        char path[256];
        snprintf(path, sizeof path, "%s%s%s", prefix, *prefix ? "/" : "", nm);
        if (e[0]) {
            size_t me = push_node((Node){nm, 1, (uint32_t)parent, 0, 0, 0});
            copy_dir(fst, names, i + 1, be32(e + 8), path, me, used);
            g_nodes[me].next = (uint32_t)g_nnodes;
            i = be32(e + 8);
            continue;
        }
        Node n = {nm, 0, 0, 0, be32(e + 4), be32(e + 8)};
        for (size_t r = 0; r < g_nrepl; r++) {
            if (used[r] || !ieq(g_repl[r].disc, path)) continue;
            used[r] = 1;
            if (ieq(nm, "_Main.rel")) {
                rt_log("[mods] %s: _Main.rel cannot be replaced (its code is translated ahead of time); skipped",
                       g_names[g_repl[r].mod]);
                break;
            }
            n.off = (uint32_t)vfile(&g_repl[r]);
            n.size = (uint32_t)g_repl[r].size;
            g_replaced++;
            break;
        }
        push_node(n);
        i++;
    }
    /* files the mods add to this folder */
    size_t pl = strlen(prefix);
    for (size_t r = 0; r < g_nrepl; r++) {
        if (used[r]) continue;
        const char* d = g_repl[r].disc;
        const char* slash = strrchr(d, '/');
        size_t dl = slash ? (size_t)(slash - d) : 0;
        if (dl != pl) continue;
        char dir[256];
        snprintf(dir, sizeof dir, "%.*s", (int)dl, d);
        if (!ieq(dir, prefix)) continue;
        used[r] = 1;
        Node n = {slash ? slash + 1 : d, 0, 0, 0, 0, (uint32_t)g_repl[r].size};
        n.off = (uint32_t)vfile(&g_repl[r]);
        push_node(n);
        g_added++;
    }
}

/* called by disc_open with the disc's FST: replaces it with the modded one when mods are active */
void mods_build_fst(uint8_t** fst_io, uint32_t* size_io) {
    const char* base = platform_mods_dir();
    struct stat st;
    if (!base || stat(base, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    snprintf(g_base, sizeof g_base, "%s", base);
    read_order(base);
    for (int m = 0; m < g_nmods; m++) {
        char p[512], name[128], author[128], version[64];
        snprintf(p, sizeof p, "%s/%s/mod.ini", base, g_names[m]);
        ini_value(p, "name", name, sizeof name);
        ini_value(p, "author", author, sizeof author);
        ini_value(p, "version", version, sizeof version);
        snprintf(g_titles[m], sizeof g_titles[m], "%s", name);
        size_t before = g_nrepl;
        if (g_on[m]) {
            snprintf(p, sizeof p, "%s/%s/files", base, g_names[m]);
            scan_files(p, "", m);
        }
        rt_log("[mods] %s %s: \"%s\"%s%s%s%s, %zu files", g_on[m] ? "on " : "off", g_names[m], *name ? name : g_names[m],
               *version ? " v" : "", version, *author ? " by " : "", author, g_nrepl - before);
    }
    if (!g_nrepl) return;
    const uint8_t* fst = *fst_io;
    uint32_t n = be32(fst + 8);
    const char* names = (const char*)fst + n * 12;
    int* used = calloc(g_nrepl, sizeof *used);
    push_node((Node){"", 1, 0, 0, 0, 0});
    copy_dir(fst, names, 1, n, "", 0, used);
    g_nodes[0].next = (uint32_t)g_nnodes;
    for (size_t r = 0; r < g_nrepl; r++)
        if (!used[r]) rt_log("[mods] %s: %s not added (its folder is not on the disc)", g_names[g_repl[r].mod], g_repl[r].disc);
    free(used);
    /* encode: entries, then the name table */
    size_t names_len = 0;
    for (size_t i = 1; i < g_nnodes; i++) names_len += strlen(g_nodes[i].name) + 1;
    uint32_t size = (uint32_t)(g_nnodes * 12 + names_len);
    uint8_t* out = calloc(1, size + 32);
    size_t np = 0;
    char* nt = (char*)out + g_nnodes * 12;
    for (size_t i = 0; i < g_nnodes; i++) {
        uint8_t* e = out + i * 12;
        const Node* nd = &g_nodes[i];
        uint32_t name_off = 0;
        if (i) {
            name_off = (uint32_t)np;
            size_t l = strlen(nd->name) + 1;
            memcpy(nt + np, nd->name, l);
            np += l;
        }
        wbe32(e, (nd->is_dir ? 0x01000000u : 0) | name_off);
        wbe32(e + 4, nd->is_dir ? nd->parent : nd->off);
        wbe32(e + 8, nd->is_dir ? nd->next : nd->size);
    }
    *fst_io = out;
    *size_io = size;
    rt_log("[mods] virtual disc: %u files replaced, %u added (%llu MB from mods)", g_replaced, g_added,
           (unsigned long long)((g_vend - MODS_BASE) >> 20));
}

uint64_t mods_base(void) { return g_nvf ? MODS_BASE : ~0ull; }

/* disc reads at or past MODS_BASE: from the mod files (zeros between and past them) */
size_t mods_read(uint64_t off, void* dst, size_t n) {
    uint8_t* out = dst;
    memset(out, 0, n);
    for (size_t i = 0; i < g_nvf; i++) {
        const VFile* v = &g_vf[i];
        if (off + n <= v->off || off >= v->off + v->size) continue;
        uint64_t from = off > v->off ? off - v->off : 0;
        uint64_t to = off + n < v->off + v->size ? off + n - v->off : v->size;
        if (!g_vf[i].logged) {  /* the first read of each mod file goes to the log (a mod's files really used?) */
            g_vf[i].logged = 1;
            rt_log("[mods] the game reads %s", v->path);
        }
        if (g_open_i != i) {
            if (g_open) fclose(g_open);
            g_open = fopen(v->path, "rb");
            g_open_i = i;
            if (!g_open) rt_log("[mods] cannot open %s", v->path);
        }
        if (!g_open) continue;
#ifdef _WIN32
        _fseeki64(g_open, (long long)from, SEEK_SET);
#else
        fseeko(g_open, (off_t)from, SEEK_SET);
#endif
        size_t got = fread(out + (v->off + from - off), 1, (size_t)(to - from), g_open);
        (void)got;
    }
    return n;
}
