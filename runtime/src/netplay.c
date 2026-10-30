/* Online play, phase 2 (docs/ONLINE.md): the controller state the game sees on each frame, and a check
 * that two runs computed the same game. Everything here needs SR_DETCLOCK (turned on by these modes).
 *
 * At each game frame end, si.c (si_det_latch) asks netplay_latch for the next frame's controllers:
 *  - live: this machine's controllers (and SR_INPUT);
 *  - SR_RECORD=<file>: live, and written to the file (one record per frame);
 *  - SR_REPLAY=<file>: read back from a recording: the run reproduces the recorded one exactly;
 *  - SR_NET=host:<port> or SR_NET=join:<address>:<port>: delay-based lockstep with another copy of the
 *    game. Each machine brings one player (its first controller): the host is player 1 (port 1), the
 *    one that joins player 2. A machine sends its player's buttons for frame N + D (SR_NET_DELAY, set
 *    by the host, default 4) and steps frame N only once it has every player's buttons for N.
 *
 * Frames are counted in frame ends ("latches"), 1 = the first. (Game time, ppc_detclock_steps, also moves
 * on in idle ticks without a frame end; a recording keeps that step too, as a check.)
 *
 * State checksum: at each frame end, a checksum of one 1 MB block of guest RAM, block (frame mod 28), so
 * every 28 frames cover all of it at 1/28 of the cost. A recording carries it, so a replay checks
 * itself every frame; lockstep machines exchange it, and a mismatch ends the session with a "desync"
 * message on screen and in the log.
 *
 * Save data (docs/ONLINE.md): online, every machine uses the same generated save with everything unlocked.
 * The game boots on a fresh card made in memory (exi.c, fixed contents under SR_DETCLOCK; the player's card
 * is not touched), and at every frame end the unlock flags are set in the game's save data (online_unlock).
 * SR_ONLINE_SAVE=1 does the same without a session (with SR_RECORD; a replay of such a recording uses it). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "overlay.h"
#include "runtime.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#define closesocket close
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

extern const char g_build_version[];
uint32_t ram_block_hash(uint32_t blk);
void vi_det_keepalive(void);
void ppc_detclock_force(void);
uint32_t ppc_detclock_steps(void);

#define NBLOCKS (GUEST_RAM_SIZE >> 20)
#define MAXP 4
#define RING 256  /* frames kept for inputs and checksums (more than twice the largest delay) */

static const GcPad k_neutral = {0, 0x80, 0x80, 0x80, 0x80, 0, 0};
static int g_active;              /* any mode other than plain live input */
static int g_ports = 1;           /* controllers connected for the run (si.c) */
static uint32_t g_frame;          /* frame ends latched so far */
static int g_online_save;         /* the generated online save: a fresh card in memory, everything unlocked */
static uint32_t g_sum_next;       /* netplay_frame_start: the checksum for frame g_frame + 1 */

static void pack(const GcPad* p, uint8_t* b) {
    b[0] = (uint8_t)p->buttons; b[1] = (uint8_t)(p->buttons >> 8);
    b[2] = p->stick_x; b[3] = p->stick_y; b[4] = p->c_x; b[5] = p->c_y; b[6] = p->trig_l; b[7] = p->trig_r;
}
static void unpack(const uint8_t* b, GcPad* p) {
    p->buttons = (uint16_t)(b[0] | b[1] << 8);
    p->stick_x = b[2]; p->stick_y = b[3]; p->c_x = b[4]; p->c_y = b[5]; p->trig_l = b[6]; p->trig_r = b[7];
}
static void put32(uint8_t* b, uint32_t v) { b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t* b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

/* ------------------------------------------------------------------ recording and replay */
/* File: a 64-byte header ("SRINPUT1", u32 version 1, u32 ports, u32 flags (1: an online session,
 * 2: the generated online save),
 * u32 reserved, the build version, 40 bytes), then per frame: u32 game step, u32 state checksum,
 * ports x 8 bytes (buttons u16, stick x/y, C x/y, L/R). Little-endian. */
#define REC_HEADER 64
static FILE* g_rec;
static FILE* g_rep;
static int g_rep_ended;
static unsigned g_rep_checked, g_rep_bad;
static uint32_t g_rep_first_bad;

static void rec_open(const char* path, int flags) {
    g_rec = fopen(path, "wb");
    if (!g_rec) { rt_log("[replay] SR_RECORD: cannot create %s", path); return; }
    uint8_t h[REC_HEADER] = {0};
    memcpy(h, "SRINPUT1", 8);
    put32(h + 8, 1);
    put32(h + 12, (uint32_t)g_ports);
    put32(h + 16, (uint32_t)flags);
    snprintf((char*)h + 24, 40, "%s", g_build_version);
    fwrite(h, 1, sizeof h, g_rec);
    rt_log("[replay] recording the controllers (%d port%s) to %s", g_ports, g_ports > 1 ? "s" : "", path);
}

static void rec_close(void) {
    if (g_rec) { fclose(g_rec); g_rec = NULL; }
    if (g_rep && !g_rep_ended)
        rt_log("[replay] stopped at frame %u: %u checksums compared, %u differed", g_frame, g_rep_checked, g_rep_bad);
}

static int rep_open(const char* path) {
    g_rep = fopen(path, "rb");
    uint8_t h[REC_HEADER];
    if (!g_rep || fread(h, 1, sizeof h, g_rep) != sizeof h || memcmp(h, "SRINPUT1", 8) || get32(h + 8) != 1) {
        rt_log("[replay] SR_REPLAY: %s is not a recording", path);
        if (g_rep) fclose(g_rep);
        g_rep = NULL;
        return 0;
    }
    g_ports = (int)get32(h + 12);
    if (g_ports < 1 || g_ports > MAXP) g_ports = 1;
    char build[41];
    memcpy(build, h + 24, 40);
    build[40] = 0;
    if (get32(h + 16) & 2) g_online_save = 1;  /* recorded on the generated online save: replayed on it too */
    rt_log("[replay] replaying %s (%d port%s%s%s, recorded with %s)", path, g_ports, g_ports > 1 ? "s" : "",
           get32(h + 16) & 1 ? ", an online session" : "", g_online_save ? ", the online save" : "", build);
    if (strncmp(build, g_build_version, 39)) rt_log("[replay] note: recorded with another build (%s); it may not match", build);
    return 1;
}

/* the recorded controllers for this frame; 0 after the end of the recording */
static int rep_read(GcPad* pads, uint32_t* sum) {
    if (g_rep_ended) return 0;
    uint8_t r[8 + MAXP * 8];
    size_t len = 8 + (size_t)g_ports * 8;
    if (fread(r, 1, len, g_rep) != len) {
        g_rep_ended = 1;
        if (g_rep_bad)
            rt_log("[replay] end of the recording at frame %u: %u checksums compared, %u DIFFERED (first at frame %u)",
                   g_frame, g_rep_checked, g_rep_bad, g_rep_first_bad);
        else
            rt_log("[replay] end of the recording at frame %u: all %u checksums matched", g_frame, g_rep_checked);
        overlay_set_banner(g_rep_bad ? "Replay finished: it did NOT match the recording (see the log)." : NULL);
        return 0;
    }
    uint32_t step = get32(r);
    if (step != ppc_detclock_steps()) {
        static int logged;
        if (logged++ < 5) rt_log("[replay] frame %u: recorded at game step %u, replayed at %u", g_frame + 1, step, ppc_detclock_steps());
    }
    *sum = get32(r + 4);
    for (int p = 0; p < g_ports; p++) unpack(r + 8 + p * 8, &pads[p]);
    return 1;
}

/* ------------------------------------------------------------------ lockstep */
enum { M_HELLO = 1, M_WELCOME = 2, M_REJECT = 3, M_INPUT = 4, M_BYE = 5 };
#define PROTO 2  /* 2: no save check (everyone uses the generated online save) */
static sock_t g_sock = SOCK_BAD;
static struct sockaddr_in g_peer;
static int g_net;                 /* 1: a session is on */
static int g_net_ended;           /* the session ended (desync, left, lost): the game goes on alone */
static int g_me, g_players = 2, g_delay = 4;
static int g_is_host;
static uint8_t g_in[MAXP][RING][8];
static uint32_t g_in_f[MAXP][RING];   /* which frame each slot holds (0: none) */
static uint32_t g_recv_upto;          /* the peer's inputs are here for every frame up to this */
static uint32_t g_peer_ack;           /* the peer has all of ours up to this */
static uint32_t g_local_upto;         /* our own inputs are decided up to this frame */
static uint32_t g_sum_upto;           /* our checksums are known up to this frame */
static uint32_t g_sum_mine[RING], g_sum_mine_f[RING], g_sum_peer[RING], g_sum_peer_f[RING];
static uint32_t g_sum_done_f[RING];  /* compared already (checksums are sent several times) */
static unsigned g_sums_compared;
static uint64_t g_last_send_ns, g_last_recv_ns;

enum { BYE_LEFT = 0, BYE_DESYNC = 1, BYE_LOST = 2 };
static void net_end(int why, uint32_t frame, const char* log_text, const char* banner) {
    if (g_net_ended) return;
    g_net_ended = 1;
    rt_log("[net] %s", log_text);
    rt_log("[net] session ended at frame %u (%u checksums compared); the game goes on without the other player",
           g_frame, g_sums_compared);
    overlay_set_banner(banner);
    uint8_t b[12] = {'S', 'R', M_BYE, (uint8_t)g_me, (uint8_t)why};  /* tell the other machine why */
    put32(b + 8, frame);
    for (int i = 0; i < 3 && g_sock != SOCK_BAD; i++) sendto(g_sock, (const char*)b, sizeof b, 0, (struct sockaddr*)&g_peer, sizeof g_peer);
}

static void sum_check(uint32_t f) {
    int s = (int)(f % RING);
    if (g_sum_mine_f[s] != f || g_sum_peer_f[s] != f || g_sum_done_f[s] == f) return;
    g_sum_done_f[s] = f;
    g_sums_compared++;
    if (g_sum_mine[s] == g_sum_peer[s]) return;
    char t[200], b[200];
    snprintf(t, sizeof t, "DESYNC at frame %u: memory block %u differs (this machine %08X, the other %08X)",
             f, f % NBLOCKS, g_sum_mine[s], g_sum_peer[s]);
    snprintf(b, sizeof b, "Desync: the two games no longer match (frame %u).\nThe online session has ended.", f);
    net_end(BYE_DESYNC, f, t, b);
}

static void net_send_input(void) {
    uint8_t m[16 + 64 * 8 + 8 * 8];
    uint32_t from = g_peer_ack + 1;
    if (from < (uint32_t)g_delay + 1) from = (uint32_t)g_delay + 1;  /* the first D frames are neutral for all */
    uint32_t n = g_local_upto >= from ? g_local_upto - from + 1 : 0;
    if (n > 64) n = 64;
    m[0] = 'S'; m[1] = 'R'; m[2] = M_INPUT; m[3] = (uint8_t)g_me;
    put32(m + 4, g_recv_upto);
    put32(m + 8, from);
    m[12] = (uint8_t)n;
    size_t len = 14;
    for (uint32_t i = 0; i < n; i++, len += 8) memcpy(m + len, g_in[g_me][(from + i) % RING], 8);
    int ns = 0;
    for (uint32_t f = g_sum_upto >= 8 ? g_sum_upto - 7 : 1; f <= g_sum_upto; f++) {  /* our last 8 checksums */
        int s = (int)(f % RING);
        if (g_sum_mine_f[s] != f) continue;
        put32(m + len, f); put32(m + len + 4, g_sum_mine[s]);
        len += 8; ns++;
    }
    m[13] = (uint8_t)ns;
    sendto(g_sock, (const char*)m, (int)len, 0, (struct sockaddr*)&g_peer, sizeof g_peer);
    g_last_send_ns = platform_now_ns();
}

static void net_send_welcome(void) {
    uint8_t m[8] = {'S', 'R', M_WELCOME, 1, (uint8_t)g_delay, (uint8_t)g_players, 0, 0};
    sendto(g_sock, (const char*)m, 8, 0, (struct sockaddr*)&g_peer, sizeof g_peer);
}

static void net_handle(const uint8_t* m, int len, const struct sockaddr_in* from) {
    if (len < 4 || m[0] != 'S' || m[1] != 'R') return;
    if (from->sin_addr.s_addr != g_peer.sin_addr.s_addr || from->sin_port != g_peer.sin_port) return;  /* someone else */
    g_last_recv_ns = platform_now_ns();
    switch (m[2]) {
    case M_HELLO:  /* the joiner did not get our welcome yet */
        if (g_is_host) net_send_welcome();
        break;
    case M_BYE:
        if (len >= 12 && m[4] == BYE_DESYNC) {
            char t[160], b[200];
            snprintf(t, sizeof t, "DESYNC at frame %u (found by the other machine)", get32(m + 8));
            snprintf(b, sizeof b, "Desync: the two games no longer match (frame %u).\nThe online session has ended.", get32(m + 8));
            net_end(BYE_DESYNC, get32(m + 8), t, b);
        } else {
            net_end(BYE_LEFT, g_frame, "the other player left", "The other player left the online session.");
        }
        break;
    case M_INPUT: {
        if (len < 14) return;
        int p = m[3];
        if (p < 0 || p >= g_players || p == g_me) return;
        uint32_t ack = get32(m + 4), f0 = get32(m + 8), n = m[12], ns = m[13];
        if (len < (int)(14 + n * 8 + ns * 8)) return;
        if (ack > g_peer_ack) g_peer_ack = ack;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t f = f0 + i;
            if (f <= g_recv_upto || f > g_recv_upto + RING / 2) continue;
            memcpy(g_in[p][f % RING], m + 14 + i * 8, 8);
            g_in_f[p][f % RING] = f;
        }
        while (g_in_f[p][(g_recv_upto + 1) % RING] == g_recv_upto + 1) g_recv_upto++;
        const uint8_t* q = m + 14 + n * 8;
        for (uint32_t i = 0; i < ns; i++, q += 8) {
            uint32_t f = get32(q);
            if (!f || f + RING / 2 < g_frame || f > g_frame + RING / 2) continue;
            g_sum_peer[f % RING] = get32(q + 4);
            g_sum_peer_f[f % RING] = f;
            sum_check(f);
        }
        break;
    }
    default: break;
    }
}

/* receives what is there, waiting up to `ms` for the first packet */
static void net_poll(int ms) {
    for (;;) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(g_sock, &rs);
        struct timeval tv = {0, ms * 1000};
        if (select((int)g_sock + 1, &rs, NULL, NULL, &tv) <= 0) return;
        uint8_t m[1500];
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = (int)recvfrom(g_sock, (char*)m, sizeof m, 0, (struct sockaddr*)&from, &fl);
        if (n <= 0) return;
        net_handle(m, n, &from);
        ms = 0;
    }
}

/* this frame's controllers in a session: ours (decided D frames ago) and the other player's */
static void net_frame(GcPad* pads) {
    if (g_net_ended) {  /* alone: our controller as player g_me, the other one neutral */
        input_get_pad(0, &pads[g_me]);
        return;
    }
    uint32_t f = g_frame + 1;
    GcPad local;
    input_get_pad(0, &local);
    g_local_upto = f + (uint32_t)g_delay;
    pack(&local, g_in[g_me][g_local_upto % RING]);
    g_in_f[g_me][g_local_upto % RING] = g_local_upto;
    net_send_input();
    static double timeout_s = -1;
    if (timeout_s < 0) timeout_s = getenv("SR_NET_TIMEOUT") ? atof(getenv("SR_NET_TIMEOUT")) : 10.0;
    uint64_t t0 = platform_now_ns();
    int waited = 0;
    static uint64_t wait_total_ns, waits;
    while (f > (uint32_t)g_delay && g_recv_upto < f && !g_net_ended) {
        net_poll(2);
        vi_det_keepalive();  /* the game is held on purpose: no backstops */
        uint64_t now = platform_now_ns();
        if (now - g_last_send_ns > 15000000ull) net_send_input();  /* resend (UDP loses packets) */
        if (!waited && now - t0 > 1000000000ull) {
            waited = 1;
            rt_log("[net] frame %u: waiting for the other player", f);
            overlay_set_banner("Waiting for the other player...");
        }
        if ((double)(now - g_last_recv_ns) > timeout_s * 1e9)
            net_end(BYE_LOST, f, "connection lost: nothing from the other player for the timeout (SR_NET_TIMEOUT)",
                    "Connection lost: the other player stopped answering.\nThe online session has ended.");
    }
    net_poll(0);
    if (waited && !g_net_ended) overlay_set_banner(NULL);
    wait_total_ns += platform_now_ns() - t0;
    if (++waits % 600 == 0 && !g_net_ended) {
        rt_log("[net] frame %u: waited %.2f ms per frame for the other player (last 600 frames); %u checksums compared, all equal",
               f, (double)wait_total_ns / 600 / 1e6, g_sums_compared);
        wait_total_ns = 0;
    }
    for (int p = 0; p < g_players; p++) {
        if (g_net_ended && p != g_me) continue;
        if (f <= (uint32_t)g_delay) pads[p] = k_neutral;
        else unpack(g_in[p][f % RING], &pads[p]);
    }
}

static int net_resolve(const char* host, int port, struct sockaddr_in* out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &res) || !res) return 0;
    memcpy(out, res->ai_addr, sizeof *out);
    out->sin_port = htons((uint16_t)port);
    freeaddrinfo(res);
    return 1;
}

static void net_fail(const char* why) {
    rt_log("[net] %s", why);
    overlay_set_banner(why);
    rt_fatal("online session: %s", why);
}

static void net_bye_at_exit(void) {  /* the game closed: the other machine need not wait for the timeout */
    if (g_net && !g_net_ended) net_end(BYE_LEFT, g_frame, "this player left (the game closed)", NULL);
}

/* connects to the other machine before the game starts (main thread) */
static void net_connect(const char* spec) {
#ifdef _WIN32
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
#endif
#ifdef __SWITCH__
    socketInitializeDefault();
#endif
    char host[128] = "";
    int port = 0;
    uint32_t bind_ip = INADDR_ANY;
    if (!strncmp(spec, "host:", 5)) {  /* host:<port>, or host:<address>:<port> to listen on one address only */
        g_is_host = 1;
        const char* c = strrchr(spec + 5, ':');
        port = atoi(c ? c + 1 : spec + 5);
        if (c) {
            char a[64];
            snprintf(a, sizeof a, "%.*s", (int)(c - spec - 5), spec + 5);
            struct in_addr ia;
            if (inet_pton(AF_INET, a, &ia) == 1) bind_ip = ntohl(ia.s_addr);
        }
    } else if (!strncmp(spec, "join:", 5)) {
        const char* c = strrchr(spec + 5, ':');
        if (c) { snprintf(host, sizeof host, "%.*s", (int)(c - spec - 5), spec + 5); port = atoi(c + 1); }
    }
    if (port <= 0 || port > 65535 || (!g_is_host && !host[0])) net_fail("SR_NET must be host:<port> or join:<address>:<port>");
    if (!g_is_host) {
        if (!net_resolve(host, port, &g_peer)) net_fail("cannot find the host's address");
        if ((ntohl(g_peer.sin_addr.s_addr) >> 24) == 127) bind_ip = INADDR_LOOPBACK;  /* two copies on one machine */
    }
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == SOCK_BAD) net_fail("cannot open a network socket");
    struct sockaddr_in me;
    memset(&me, 0, sizeof me);
    me.sin_family = AF_INET;
    me.sin_addr.s_addr = htonl(bind_ip);
    me.sin_port = htons(g_is_host ? (uint16_t)port : 0);
#ifdef _WIN32
    {   /* a packet to a port nobody listens on yet (the host not started) must not make recvfrom fail */
        BOOL off = FALSE;
        DWORD got;
        WSAIoctl(g_sock, (DWORD)_WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof off, NULL, 0, &got, NULL, NULL);
    }
#endif
    if (bind(g_sock, (struct sockaddr*)&me, sizeof me)) net_fail("cannot use that network port (another program has it?)");
    if (g_is_host) {
        g_me = 0;
        g_delay = getenv("SR_NET_DELAY") ? atoi(getenv("SR_NET_DELAY")) : 4;
        if (g_delay < 1) g_delay = 1;
        if (g_delay > 60) g_delay = 60;
        rt_log("[net] hosting on UDP port %d (input delay %d frames): waiting for a player to join", port, g_delay);
        overlay_set_banner("Online: waiting for a player to join...");
        for (;;) {
            uint8_t m[1500];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            int n = (int)recvfrom(g_sock, (char*)m, sizeof m, 0, (struct sockaddr*)&from, &fl);
            if (n < 72 || m[0] != 'S' || m[1] != 'R' || m[2] != M_HELLO) continue;
            char why[160] = "";
            char build[65];
            memcpy(build, m + 8, 64);
            build[64] = 0;
            if (m[3] != PROTO) snprintf(why, sizeof why, "a different online protocol version");
            else if (strncmp(build, g_build_version, 63)) snprintf(why, sizeof why, "a different game build (%.60s, the host has %.60s)", build, g_build_version);
            if (why[0]) {
                char ip[64];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
                rt_log("[net] refused a player from %s: %s", ip, why);
                uint8_t r[4 + 160] = {'S', 'R', M_REJECT, 0};
                memcpy(r + 4, why, sizeof why);
                sendto(g_sock, (const char*)r, sizeof r, 0, (struct sockaddr*)&from, fl);
                continue;
            }
            g_peer = from;
            break;
        }
        net_send_welcome();
        char ip[64];
        inet_ntop(AF_INET, &g_peer.sin_addr, ip, sizeof ip);
        rt_log("[net] player 2 joined from %s:%u", ip, ntohs(g_peer.sin_port));
    } else {
        rt_log("[net] joining %s:%d", host, port);
        overlay_set_banner("Online: joining...");
        uint8_t hello[72] = {'S', 'R', M_HELLO, PROTO};
        snprintf((char*)hello + 8, 64, "%s", g_build_version);
        uint64_t t0 = platform_now_ns(), last = 0;
        for (;;) {
            uint64_t now = platform_now_ns();
            if (now - last > 250000000ull) {
                sendto(g_sock, (const char*)hello, sizeof hello, 0, (struct sockaddr*)&g_peer, sizeof g_peer);
                last = now;
            }
            if (now - t0 > 60000000000ull) net_fail("the host did not answer within 60 s");
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(g_sock, &rs);
            struct timeval tv = {0, 50000};
            if (select((int)g_sock + 1, &rs, NULL, NULL, &tv) <= 0) continue;
            uint8_t m[1500];
            struct sockaddr_in from;
            socklen_t fl = sizeof from;
            int n = (int)recvfrom(g_sock, (char*)m, sizeof m, 0, (struct sockaddr*)&from, &fl);
            if (n < 4 || m[0] != 'S' || m[1] != 'R') continue;
            if (m[2] == M_REJECT) {
                char why[200];
                snprintf(why, sizeof why, "the host refused: %.160s", n > 4 ? (const char*)m + 4 : "");
                net_fail(why);
            }
            if (m[2] == M_WELCOME && n >= 8) {
                g_me = m[3];
                g_delay = m[4];
                g_players = m[5];
                break;
            }
        }
        rt_log("[net] joined as player %d of %d (input delay %d frames)", g_me + 1, g_players, g_delay);
    }
    if (g_players != 2 || g_me >= g_players) net_fail("only two-player sessions are supported so far");
    g_ports = g_players;
    g_net = 1;
    g_recv_upto = g_peer_ack = (uint32_t)g_delay;  /* the first D frames: neutral for everyone, never sent */
    g_last_recv_ns = platform_now_ns();
    atexit(net_bye_at_exit);
    overlay_set_banner(NULL);
}

/* ------------------------------------------------------------------ the frame */
void netplay_init(void) {  /* main(), before the devices: the modes decide SR_DETCLOCK and the ports */
    const char* rec = getenv("SR_RECORD");
    const char* rep = getenv("SR_REPLAY");
    const char* net = getenv("SR_NET");
    const char* os = getenv("SR_ONLINE_SAVE");
    if (net || (os && atoi(os))) g_online_save = 1;
    if (!rec && !rep && !net && !g_online_save) return;
    if (rep && net) rt_fatal("SR_REPLAY and SR_NET cannot be used together");
    ppc_detclock_force();
    g_active = 1;
    const char* n = getenv("SR_PADS");
    g_ports = n && atoi(n) > 1 ? (atoi(n) > MAXP ? MAXP : atoi(n)) : 1;
    if (rep && !rep_open(rep)) rt_fatal("SR_REPLAY: cannot replay %s", rep);
    atexit(rec_close);
}

void netplay_connect(void) {  /* main(), after the memory card is loaded, before the game runs */
    const char* net = getenv("SR_NET");
    if (net) net_connect(net);
    const char* rec = getenv("SR_RECORD");
    if (rec) rec_open(rec, g_net | g_online_save << 1);
}

int netplay_ports(void) { return g_ports; }
int netplay_online_save(void) { return g_online_save; }  /* main(): a fresh card in memory */

/* gx_render.c: whose view to show full screen in a split-screen race (0-based), or -1 for the whole
 * picture. Online: this machine's player. SR_VIEW=<1-2> forces one (testing, also offline). Presentation
 * only: the game still draws every view, so all machines keep computing the same game. */
int netplay_view_player(void) {
    static int forced = -2;
    if (forced == -2) forced = getenv("SR_VIEW") ? atoi(getenv("SR_VIEW")) - 1 : -1;
    if (forced >= 0) return forced;
    return g_net ? g_me : -1;
}

/* The unlock flags in the game's save data (one struct at 81AAD000, saved in SONICRIDERS_GAMEDATA_01 from
 * file offset 0x1460; NOTES.md 2026-10-30 "unlock flags"). A flag is read as "== 1" (characters, tracks)
 * or by its bit 0 (gear), so setting them again each frame is harmless, and it also covers the game
 * re-initialising its save data (the NEW GAME / create-save path on a fresh card). */
#define SAVE_DATA 0x81AAD000u
static void online_unlock(void) {
    uint8_t* s = mem_phys(SAVE_DATA & 0x3FFFFFFF);
    s[0x002] = 1;                       /* a course gated separately in the mode menus */
    memset(s + 0x004, 1, 16);           /* the 16 tracks (8 track slots x 2 courses) */
    memset(s + 0x3A0, 1, 17);           /* the characters 0-16 */
    static const uint8_t gear[][2] = {{1, 24}, {40, 49}, {70, 75}};  /* the gear table's flag indices */
    for (unsigned r = 0; r < sizeof gear / sizeof gear[0]; r++)
        for (unsigned i = gear[r][0]; i <= gear[r][1]; i++) s[0x348 + i] |= 1;
}

/* hle_frame_end, at its start (where SR_RAMHASH looks): the next frame's state checksum */
void netplay_frame_start(void) {
    if (!g_active) return;
    if (g_online_save) online_unlock();  /* before the checksum: the same on every machine */
    {   /* SR_NET_TEST_DESYNC=<frame> (testing the desync check): change one byte of this machine's game
         * memory then, in the DOL's code (only the recompiled copy runs, so the game is unaffected) */
        static int at = -1;
        if (at < 0) at = getenv("SR_NET_TEST_DESYNC") ? atoi(getenv("SR_NET_TEST_DESYNC")) : 0;
        if (at && g_frame + 1 == (uint32_t)at) {
            mem_phys(0x10000)[0] ^= 0x5A;
            rt_log("[net] SR_NET_TEST_DESYNC: changed a byte of game memory at frame %u", g_frame + 1);
        }
    }
    g_sum_next = ram_block_hash((g_frame + 1) % NBLOCKS);
}

/* si.c, at each frame end under SR_DETCLOCK: the controllers for the next frame */
void netplay_latch(GcPad* pads) {
    for (int ch = 0; ch < MAXP; ch++) pads[ch] = k_neutral;
    if (!g_active) {
        for (int ch = 0; ch < MAXP; ch++) input_get_pad(ch, &pads[ch]);
        return;
    }
    uint32_t f = g_frame + 1, sum = g_sum_next;
    if (g_rep) {
        uint32_t rsum;
        if (rep_read(pads, &rsum)) {
            g_rep_checked++;
            if (rsum != sum) {
                if (!g_rep_bad++) {
                    g_rep_first_bad = f;
                    rt_log("[replay] frame %u DIFFERS from the recording: memory block %u (recorded %08X, now %08X)",
                           f, f % NBLOCKS, rsum, sum);
                    overlay_set_banner("The replay no longer matches the recording (see the log).");
                }
            } else if (f % 600 == 0) {
                rt_log("[replay] frame %u: %u checksums compared, %u differed", f, g_rep_checked, g_rep_bad);
            }
        }
    } else if (g_net) {
        g_sum_mine[f % RING] = sum;
        g_sum_mine_f[f % RING] = f;
        g_sum_upto = f;  /* net_frame sends it */
        sum_check(f);
        net_frame(pads);
    } else {
        for (int ch = 0; ch < g_ports; ch++) input_get_pad(ch, &pads[ch]);
    }
    if (g_rec) {
        uint8_t r[8 + MAXP * 8];
        put32(r, ppc_detclock_steps());
        put32(r + 4, sum);
        for (int p = 0; p < g_ports; p++) pack(&pads[p], r + 8 + p * 8);
        fwrite(r, 1, 8 + (size_t)g_ports * 8, g_rec);
        if (f % 60 == 0) fflush(g_rec);
    }
    g_frame = f;
}
