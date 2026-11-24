/* Synthetic complete GX frames test the actual renderer pass, without a disc or GPU. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "local_hud_under_test.inc"

uint8_t* ppc_mem_base_var;
Settings g_settings;
int settings_wide_k_fixed(void) { return 0; }
void rt_log(const char* fmt, ...) { (void)fmt; }
void rt_fatal(const char* fmt, ...) { (void)fmt; abort(); }
uint32_t mmio_read(uint32_t ea, int size) { (void)ea; (void)size; assert(0); return 0; }
void mmio_write(uint32_t ea, uint32_t v, int size) { (void)ea; (void)v; (void)size; assert(0); }

static GxUniforms view(HudView v, int ortho) {
    GxUniforms u = {0};
    u.bp[0x59] = 171 | 171 << 10;
    u.bp[0x20] = 0x123456; u.bp[0x21] = 0x654321;
    u.xfr[0x1A] = ubits(v.hx * 320); u.xfr[0x1B] = ubits(-v.hy * 240);
    u.xfr[0x1D] = ubits(342 + (v.cx + 1) * 320);
    u.xfr[0x1E] = ubits(342 + (1 - v.cy) * 240);
    u.xfr[0x20] = u.xfr[0x22] = ubits(1);
    u.xfr[0x26] = (uint32_t)ortho; u.xfr[GX_XR_WIDE_FLAGS] = GX_WIDE_HUD_43;
    return u;
}
static void command(GxFrame* f, GxCmd c) {
    f->cmds = grow(f->cmds, &f->cap_cmds, f->ncmds + 1, sizeof *f->cmds);
    f->cmds[f->ncmds++] = c;
}
static void rect(GxFrame* f, float x0, float x1, float y0, float y1, int tex) {
    uint32_t v = f->nverts, ix = f->nidx;
    f->verts = grow(f->verts, &f->cap_verts, v + 4, sizeof *f->verts);
    f->idx = grow(f->idx, &f->cap_idx, ix + 6, sizeof *f->idx);
    for (int j = 0; j < 4; j++) {
        f->verts[v + j] = (GxVertex){0};
        f->verts[v + j].pos[0] = j & 1 ? x1 : x0;
        f->verts[v + j].pos[1] = j & 2 ? y1 : y0;
        f->verts[v + j].nrm[3] = GX_ANCHOR_RIGHT; /* deliberately wrong old global anchor */
    }
    const uint32_t indices[6] = {0,1,2,2,1,3};
    for (int j = 0; j < 6; j++) f->idx[ix + j] = v + indices[j];
    f->nverts += 4; f->nidx += 6;
    GxCmd d = {.type = GXC_DRAW, .a = ix, .b = 6}; d.tex[0] = tex; command(f, d);
}
static void dispose(GxFrame* f) {
    free(f->cmds); free(f->unis); free(f->idx); free(f->verts); free(f->xtabs); free(f->xchunks);
}
static void timer_overlap(float k, int tall) {
    GxFrame f = {.wide_k = k};
    f.unis = calloc(8, sizeof *f.unis); f.cap_unis = 8; f.nunis = 3;
    for (int j = 0; j < 2; j++) f.unis[j] = view(hud_player_view(j, 0), 0);
    f.unis[2] = view((HudView){0,0,1,1}, 1);
    f.xtabs = calloc(1, sizeof *f.xtabs); f.nxtabs = f.cap_xtabs = 1;
    f.xchunks = calloc(GX_XFM_CHUNK, sizeof *f.xchunks); f.nxchunks = f.cap_xchunks = 1;
    f.xchunks[0] = f.xchunks[5] = ubits(1);
    command(&f, (GxCmd){.type = GXC_STATE, .a = 2});
    rect(&f, -.35f, .35f, .7667f, .8208f, 10); /* shared backing */
    rect(&f, -.25f, .25f, .6542f, .7667f, 10); /* shared clock digits */
    rect(&f, -.8656f, -.5437f, .6542f, .8292f, 10);
    rect(&f, -.5344f, -.3406f, .7625f, .8208f, 10); /* actual 3px overlap */
    if (tall) rect(&f, -.863f, -.716f, .108f, .608f, 10);
    rect(&f, -.8656f, -.5437f, -.3458f, -.1708f, 10);
    rect(&f, -.5344f, -.3406f, -.2375f, -.1792f, 10);
    local_hud_frame(&f, -1);
    int state = -1, draw = 0;
    for (uint32_t i = 0; i < f.ncmds; i++) {
        const GxCmd* c = &f.cmds[i];
        if (c->type == GXC_STATE) state = (int)c->a;
        if (c->type != GXC_DRAW) continue;
        float move = fbits(f.unis[state].xfr[0x21]);
        if (fabsf(move - (draw < 2 ? 0 : -(k - 1))) >= .0001f)
            fprintf(stderr, "timer overlap k=%f tall=%d draw=%d move=%f\n", k, tall, draw, move);
        assert(fabsf(move - (draw < 2 ? 0 : -(k - 1))) < .0001f);
        draw++;
    }
    dispose(&f);
}
static void check(float k, int players) {
    int quad = players >= 3;
    GxFrame f = {.wide_k = k};
    f.unis = calloc(8, sizeof *f.unis); f.cap_unis = 8; f.nunis = (uint32_t)players + 1;
    for (int j = 0; j < players; j++) f.unis[j] = view(hud_player_view(j, quad), 0);
    f.unis[players] = view((HudView){0,0,1,1}, 1);
    f.xtabs = calloc(1, sizeof *f.xtabs); f.nxtabs = f.cap_xtabs = 1;
    f.xchunks = calloc(GX_XFM_CHUNK, sizeof *f.xchunks); f.nxchunks = f.cap_xchunks = 1;
    f.xchunks[0] = f.xchunks[5] = ubits(1);
    GxUniforms perspective = f.unis[0], original = f.unis[players];
    command(&f, (GxCmd){.type = GXC_STATE, .a = (uint32_t)players});
    rect(&f, -.35f, .35f, .7667f, .8208f, 10); /* shared timer backing */
    rect(&f, -.25f, -.20f, .6542f, .7667f, 10); /* timer digit must not join P1 */
    rect(&f, -.866f, -.603f, .654f, .829f, 10);
    rect(&f, -.600f, -.406f, .767f, .825f, 10);
    rect(&f, -.863f, -.716f, .108f, .608f, 10);
    rect(&f, -.854f, -.734f, .037f, .104f, 10);
    rect(&f, -.700f, -.656f, .046f, .104f, 10); /* gap in lap digits */
    rect(&f, -.672f, -.328f, .137f, .154f, 10);
    rect(&f, -.850f, -.819f, .246f, .408f, 10); /* separate draw-order block, same gauge */
    if (quad) {
        rect(&f, .716f, .863f, .108f, .608f, 10);
        rect(&f, .603f, .866f, .654f, .829f, 10);
        rect(&f, .406f, .600f, .767f, .825f, 10);
        rect(&f, .350f, .650f, .350f, .500f, 10); /* centred P2 notification in same atlas */
        rect(&f, 0, 1, -1, 0, 33); /* silhouette/whole-view effect */
    }
    rect(&f, -.2031f, .1969f, -.5583f, .5083f, 26);
    rect(&f, -.10f, -.07f, .10f, .14f, 10); /* shared map marker uses HUD atlas */
    if (quad) {
        /* Actual trick score/header overlap the old gauge by six source pixels and reuse its atlas,
         * but are drawn after the shared map. Keep this compact run centred in P2's view. */
        rect(&f, .6406f, .7344f, .3625f, .4042f, 10);
        rect(&f, .5500f, .6062f, .2958f, .3708f, 10);
        rect(&f, .6781f, .7344f, .2958f, .3708f, 10);
    }
    GxCmd copy = {.type = GXC_COPY}; copy.copy[0] = 3; copy.copy[1] = 240; copy.copy[4] = 1;
    command(&f, copy);
    uint32_t before = f.ncmds;
    local_hud_frame(&f, -1);
    assert(!memcmp(&perspective, &f.unis[0], sizeof perspective));
    assert(!memcmp(&original, &f.unis[players], sizeof original));
    assert(!memcmp(&copy, &f.cmds[f.ncmds - 1], sizeof copy));
    if (k == 1) { assert(f.ncmds == before); dispose(&f); return; }
    int state = -1, draw = 0;
    for (uint32_t i = 0; i < f.ncmds; i++) {
        const GxCmd* c = &f.cmds[i];
        if (c->type == GXC_STATE) state = (int)c->a;
        if (c->type != GXC_DRAW) continue;
        float move = fbits(f.unis[state].xfr[0x21]);
        float expected = draw >= 2 && draw <= 8 ? -(k - 1) :
                         quad && draw >= 9 && draw <= 11 ? k - 1 :
                         quad && (draw == 12 || draw >= 16) ? 0.5f * (k - 1) : 0;
        assert(fabsf(move - expected) < .0001f);
        assert(!memcmp(f.unis[state].bp, original.bp, sizeof original.bp));
        if (draw >= 2 && draw <= 8) {
            float left = (f.verts[f.idx[c->a]].pos[0] + move) / k;
            float right = (f.verts[f.idx[c->a + 1]].pos[0] + move) / k;
            assert(left >= -1 && right <= (quad ? 0 : 1));
            float old_width = f.verts[f.idx[c->a + 1]].pos[0] - f.verts[f.idx[c->a]].pos[0];
            assert(fabsf((right - left) * k - old_width) < .0001f);
            assert(f.verts[f.idx[c->a]].nrm[3] == GX_ANCHOR_MIDDLE);
        }
        if (quad && draw == 13) assert(f.verts[f.idx[c->a]].nrm[3] == GX_ANCHOR_STRETCH);
        draw++;
    }
    dispose(&f);
}
static void check_own_quad_hud(void) {
    HudView full = {0, 0, 1, 1};
    for (int p = 0; p < 4; p++) {
        HudDraw draws[] = {
            {.source = full, .owner = -1, .textured = 1, .b = {-0.08f, 0.08f, -0.65f, 0.52f}},
            {.source = full, .owner = 0, .textured = 1, .b = {-0.04f, 0.01f, 0.28f, 0.34f}},
            {.source = full, .owner = -1, .textured = 1, .b = {-0.29f, 0.29f, 0.82f, 0.93f}},
            {.source = full, .owner = -1, .b = {-0.002f, 0.002f, -0.5f, 0.5f}},
        };
        float wide = 1.75f;
        quad_shared_hud(draws, 4, p, wide);
        HudView vp = hud_player_view(p, 1);
        assert(draws[0].scale > 0 && draws[0].scale < 0.5f);
        assert(fabsf(draws[0].move / wide - (vp.cx + 0.39f)) < .0001f);
        assert(draws[0].move_y + draws[0].scale * draws[0].b[2] > vp.cy);
        assert(draws[0].move_y + draws[0].scale * draws[0].b[3] < vp.cy + vp.hy);
        assert(fabsf(draws[1].move - draws[0].move) < .0001f);
        assert(fabsf(draws[1].move_y - draws[0].move_y) < .0001f);
        assert(draws[2].scale == 0.5f && fabsf(draws[2].move / wide - vp.cx) < .0001f);
        assert(draws[3].scale == 0 && draws[3].move == 0 && draws[3].move_y == 0);
    }
}
int main(void) {
    ppc_mem_base_var = calloc(1, 32 * 1024 * 1024); assert(ppc_mem_base_var);
    st32(GAME_TOP_MODE, GAME_TOP_MODE_MULTIPLAYER); g_settings.screen = SCREEN_FIT;
    const float ratios[] = {1, 4.0f/3, 1.2f, 1.75f, 8.0f/3};
    for (int p = 2; p <= 4; p++) for (unsigned k = 0; k < sizeof ratios/sizeof ratios[0]; k++) check(ratios[k], p);
    for (unsigned k = 0; k < sizeof ratios/sizeof ratios[0]; k++) {
        timer_overlap(ratios[k], 0); timer_overlap(ratios[k], 1);
    }
    check_own_quad_hud();
    puts("local_hud: complete frames, 2/3/4P x five aspects, shared timer/map, centred notification, silhouette, group spacing, clipping and state isolation PASS");
    free(ppc_mem_base_var);
}
