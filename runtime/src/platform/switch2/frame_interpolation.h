/* PC matrix interpolation, reused from platform/host/render_gl.c.
 * CPU frame data stays immutable; only GPU upload copies are blended. */
#pragma once
/* ---- in-between pictures (setting pc_fps; reused PC implementation) ----
 * The game draws 60 pictures a second (its physics run at a fixed 60 steps). For other picture rates the
 * render thread keeps the last two game pictures A and B and draws B again with its matrices blended
 * from A's: at time t after B arrived, the picture shows A + (B - A) * t / (1/60 s), so it is one game
 * picture behind. Draws are paired by a key (primitive, index count, textures, the position matrices
 * its vertices use); the k-th draw with a key in B pairs with the k-th in A. Only the matrices a paired
 * draw uses are blended (position rows 0x000-0x0FF and their normal matrices), and the projection;
 * texture matrices (scrolling textures would sweep back) and lights stay B's. A matrix that turned
 * more than a little between A and B (a camera cut, a respawn) stays B's. */
typedef struct IpDraw { uint64_t key, pmask; uint32_t uni, xt, order; } IpDraw;
typedef struct IpInfo { IpDraw* d; uint32_t n, cap; } IpInfo;
static IpInfo g_ip_info[4];             /* per g_frames entry */
static uint32_t* g_ip_pairs;            /* (B pool word, A pool word) */
static uint32_t g_ip_npairs, g_ip_cap_pairs;
static uint32_t* g_ip_upairs;           /* (B state, A state) for the projection */
static uint32_t g_ip_nupairs, g_ip_cap_upairs;
static uint8_t* g_ip_mark;              /* B pool words / states already paired */
static size_t g_ip_cap_mark;
static uint32_t* g_ip_chunks;           /* B's XF memory chunk pool, blended */
static size_t g_ip_cap_chunks;
static const GxFrame* g_ip_a;           /* while drawing an in-between picture: A, and the blend factor */
static float g_ip_t;
static int g_ip_on;
static uint32_t g_ip_matched, g_ip_draws;  /* last pairing: draws paired / B's draws (log) */

static uint64_t ip_hash(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001B3ull; }

static void ip_frame_info(const GxFrame* f, IpInfo* in) {
    in->n = 0;
    uint32_t uni = 0, xt = 0;
    for (uint32_t i = 0; i < f->ncmds; i++) {
        const GxCmd* c = &f->cmds[i];
        if (c->type == GXC_STATE) { uni = c->a, xt = c->b; continue; }
        if (c->type != GXC_DRAW) continue;
        uint64_t pm = 0;
        for (uint32_t k = c->a; k < c->a + c->b && k < f->nidx; k++) {
            uint32_t vi = f->idx[k];
            if (vi >= f->nverts) continue;
            int m = (int)f->verts[vi].pos[3];
            if (m >= 0 && m < 64) pm |= 1ull << m;
        }
        uint64_t h = 0xCBF29CE484222325ull;
        h = ip_hash(h, c->prim);
        h = ip_hash(h, c->b);
        for (int t = 0; t < 8; t++) h = ip_hash(h, (uint32_t)c->tex[t]);
        h = ip_hash(h, pm);
        if (in->n == in->cap) in->d = realloc(in->d, (in->cap = in->cap ? in->cap * 2 : 1024) * sizeof *in->d);
        in->d[in->n] = (IpDraw){h, pm, uni, xt, in->n};
        in->n++;
    }
}

static int ip_cmp(const void* a, const void* b) {
    const IpDraw *x = a, *y = b;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

static float ip_f(uint32_t w) { float f; memcpy(&f, &w, 4); return f; }

/* one matrix (n words from XF memory word w0) of a paired draw: its pool words, unless it jumped */
static void ip_pair_words(const GxFrame* a, const GxFrame* b, uint32_t xa, uint32_t xb, uint32_t w0, int n, int rot_check) {
    uint32_t sa[12], sb[12];
    if (xa >= a->nxtabs || xb >= b->nxtabs) return;
    for (int k = 0; k < n; k++) {
        uint32_t w = w0 + (uint32_t)k, ch = w / GX_XFM_CHUNK, o = w % GX_XFM_CHUNK;
        if (ch >= GX_XFM_CHUNKS) return;
        uint32_t pa = a->xtabs[xa].xtab[ch], pb = b->xtabs[xb].xtab[ch];
        if (pa >= a->nxchunks || pb >= b->nxchunks) return;
        sa[k] = pa * GX_XFM_CHUNK + o, sb[k] = pb * GX_XFM_CHUNK + o;
        float fa = ip_f(a->xchunks[sa[k]]), fb = ip_f(b->xchunks[sb[k]]);
        if (!isfinite(fa) || !isfinite(fb)) return;
        /* the 3x3 part of a position matrix (not the translation column 3/7/11) */
        if (rot_check && (k & 3) != 3 && fabsf(fa - fb) > 0.35f * fmaxf(1.0f, fmaxf(fabsf(fa), fabsf(fb)))) return;
    }
    for (int k = 0; k < n; k++) {
        if (g_ip_mark[sb[k]]) continue;
        g_ip_mark[sb[k]] = 1;
        if (g_ip_npairs + 2 > g_ip_cap_pairs)
            g_ip_pairs = realloc(g_ip_pairs, (size_t)(g_ip_cap_pairs = g_ip_cap_pairs ? g_ip_cap_pairs * 2 : 65536) * 4);
        g_ip_pairs[g_ip_npairs++] = sb[k];
        g_ip_pairs[g_ip_npairs++] = sa[k];
    }
}

/* pair B's draws with A's (once per new game picture) */
static void ip_match(const GxFrame* a, const IpInfo* ia, const GxFrame* b, const IpInfo* ib) {
    g_ip_npairs = g_ip_nupairs = 0;
    g_ip_matched = 0, g_ip_draws = ib->n;
    size_t need = (size_t)b->nxchunks * GX_XFM_CHUNK + b->nunis + 1;
    if (need > g_ip_cap_mark) g_ip_mark = realloc(g_ip_mark, g_ip_cap_mark = need * 2);
    memset(g_ip_mark, 0, need);
    uint8_t* umark = g_ip_mark + (size_t)b->nxchunks * GX_XFM_CHUNK;
    /* sorted copies (key, then order) */
    static IpDraw *sa, *sb;
    static uint32_t cap_a, cap_b;
    if (ia->n > cap_a) sa = realloc(sa, (cap_a = ia->n) * sizeof *sa);
    if (ib->n > cap_b) sb = realloc(sb, (cap_b = ib->n) * sizeof *sb);
    if (ia->n) memcpy(sa, ia->d, ia->n * sizeof *sa);
    if (ib->n) memcpy(sb, ib->d, ib->n * sizeof *sb);
    qsort(sa, ia->n, sizeof *sa, ip_cmp);
    qsort(sb, ib->n, sizeof *sb, ip_cmp);
    uint32_t i = 0, j = 0;
    while (i < ia->n && j < ib->n) {
        if (sa[i].key < sb[j].key) { i++; continue; }
        if (sa[i].key > sb[j].key) { j++; continue; }
        const IpDraw *da = &sa[i++], *db = &sb[j++];
        g_ip_matched++;
        for (uint64_t pm = db->pmask; pm; pm &= pm - 1) {
            int m = __builtin_ctzll(pm);
            ip_pair_words(a, b, da->xt, db->xt, (uint32_t)m * 4, 12, 1);
            ip_pair_words(a, b, da->xt, db->xt, 0x400u + (uint32_t)(m & 31) * 3, 9, 0);
        }
        if (db->uni < b->nunis && da->uni < a->nunis && !umark[db->uni]) {
            umark[db->uni] = 1;
            const uint32_t *ra = a->unis[da->uni].xfr, *rb = b->unis[db->uni].xfr;
            if (ra[0x26] == rb[0x26]) {
                if (g_ip_nupairs + 2 > g_ip_cap_upairs)
                    g_ip_upairs = realloc(g_ip_upairs, (size_t)(g_ip_cap_upairs = g_ip_cap_upairs ? g_ip_cap_upairs * 2 : 4096) * 4);
                g_ip_upairs[g_ip_nupairs++] = db->uni;
                g_ip_upairs[g_ip_nupairs++] = da->uni;
            }
        }
    }
}

static uint32_t ip_lerp(uint32_t a, uint32_t b, float t) {
    float fa = ip_f(a), fb = ip_f(b), r = fa + (fb - fa) * t;
    uint32_t w;
    memcpy(&w, &r, 4);
    return w;
}

/* B's chunk pool with the paired words blended (execute uploads this instead) */
static const uint32_t* ip_chunks(const GxFrame* b) {
    size_t n = (size_t)b->nxchunks * GX_XFM_CHUNK;
    if (n > g_ip_cap_chunks) g_ip_chunks = realloc(g_ip_chunks, (g_ip_cap_chunks = n) * 4);
    if (n) memcpy(g_ip_chunks, b->xchunks, n * 4);
    for (uint32_t k = 0; k < g_ip_npairs; k += 2)
        g_ip_chunks[g_ip_pairs[k]] = ip_lerp(g_ip_a->xchunks[g_ip_pairs[k + 1]], b->xchunks[g_ip_pairs[k]], g_ip_t);
    return g_ip_chunks;
}

/* the paired states' projection (XF 0x1020-0x1025), blended in the staged copy */
static void ip_states(uint8_t* staging, size_t stride) {
    for (uint32_t k = 0; k < g_ip_nupairs; k += 2) {
        GxUniforms* u = (GxUniforms*)(staging + stride * g_ip_upairs[k]);
        const uint32_t* ra = g_ip_a->unis[g_ip_upairs[k + 1]].xfr;
        for (int r = 0x20; r < 0x26; r++) {
            if (!isfinite(ip_f(ra[r])) || !isfinite(ip_f(u->xfr[r]))) continue;
            u->xfr[r] = ip_lerp(ra[r], u->xfr[r], g_ip_t);
        }
    }
}
