/* Internet play helpers: STUN (RFC 5389 binding requests on the game's own socket), UPnP IGD port mapping on a
 * worker thread, invite codes, and the lobby -> netplay socket handoff. No server of ours is involved. */
#include "inet.h"
#include "runtime.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define BAD INVALID_SOCKET
#define WOULD_BLOCK(e) ((e) == WSAEWOULDBLOCK || (e) == WSAEINPROGRESS)
static int last_error(void) { return WSAGetLastError(); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Sock;
#define BAD (-1)
#define closesocket close
#define WOULD_BLOCK(e) ((e) == EINPROGRESS || (e) == EWOULDBLOCK || (e) == EAGAIN)
static int last_error(void) { return errno; }
#endif

enum { OWNER_NONE, OWNER_LOBBY, OWNER_NETPLAY };
static _Atomic intptr_t g_sock = (intptr_t)-1;
static _Atomic int g_owner;
static struct sockaddr_in g_peer;
static InetInfo info;
static uint64_t opened_ns;

/* ---------------------------------------------------------------- small helpers */
static uint64_t rng_state;
static uint64_t rnd64(void) { /* splitmix64; ids only need to be hard to guess for an off-path sender */
    if (!rng_state) rng_state = platform_now_ns() ^ ((uint64_t)(uintptr_t)&rng_state << 17) ^ 0x9E3779B97F4A7C15ull;
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static void nonblocking(Sock s) {
#ifdef _WIN32
    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
}
static struct sockaddr_in addr4(uint32_t ip, int port) {
    struct sockaddr_in a; memset(&a, 0, sizeof a);
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(ip); a.sin_port = htons((uint16_t)port);
    return a;
}
static void ip_text(uint32_t ip, char* out, int cap) { snprintf(out, cap, "%u.%u.%u.%u", ip >> 24, ip >> 16 & 255, ip >> 8 & 255, ip & 255); }
/* Logs end up in public bug reports: public addresses are written as a.b.x.x */
void inet_mask(uint32_t ip, char* out, int cap) { snprintf(out, cap, "%u.%u.x.x", ip >> 24, ip >> 16 & 255); }
static int private_ip(uint32_t ip) {
    return (ip >> 24) == 10 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 || (ip >> 22) == (100u << 2 | 1) /* 100.64/10 */ ||
           (ip >> 24) == 127 || (ip >> 16) == 0xA9FE || (ip >> 24) == 0;
}
int inet_public_ip(uint32_t ip) { return ip && !private_ip(ip) && (ip >> 28) < 14 /* not multicast/reserved */; }

/* ---------------------------------------------------------------- codes */
void inet_code_encode(uint32_t ip, int port, char out[20]) {
    if (port == INET_PORT) snprintf(out, 20, "%010llu", (unsigned long long)ip);
    else snprintf(out, 20, "%015llu", (unsigned long long)ip * 100000ull + (unsigned long long)port);
}
const char* inet_code_decode(const char* code, uint32_t* ip, int* port) {
    char d[24]; int n = 0;
    for (const char* p = code; *p; p++) {
        if (isdigit((unsigned char)*p)) { if (n < 20) d[n++] = *p; else return "That code is too long."; }
        else if (*p != ' ' && *p != '-' && *p != '+') return "A code has only numbers.";
    }
    d[n] = 0;
    if (n != 10 && n != 15) return "A code has 10 or 15 numbers. Check it with your friend.";
    unsigned long long v = strtoull(d, NULL, 10);
    unsigned long long a = n == 10 ? v : v / 100000ull;
    int pt = n == 10 ? INET_PORT : (int)(v % 100000ull);
    if (a > 0xFFFFFFFFull || pt < 1 || pt > 65535) return "That code is not valid. Check it with your friend.";
    const char* test = getenv("SR_INET_TEST_IP"); /* two copies on one PC: loopback codes are allowed */
    if (!test && !inet_public_ip((uint32_t)a)) return "That code is not valid. Check it with your friend.";
    *ip = (uint32_t)a; *port = pt;
    return NULL;
}
void inet_code_pretty(const char* c, char* out, int cap) {
    if (strlen(c) == 10) snprintf(out, cap, "%.4s-%.3s-%.3s", c, c + 4, c + 7);
    else if (strlen(c) == 15) snprintf(out, cap, "%.4s-%.3s-%.3s - %.5s", c, c + 4, c + 7, c + 10);
    else snprintf(out, cap, "%s", c);
}

/* ---------------------------------------------------------------- worker context (DNS for STUN, UPnP) */
typedef struct {
    _Atomic int refs, cancel;
    int local_port;
    struct sockaddr_in stun[2];
    _Atomic int stun_resolved[2];   /* 1 = stun[i] usable, -1 = lookup failed */
    _Atomic int upnp;               /* INET_UPNP_* */
    uint32_t ext_ip; int ext_port;  /* written before upnp is stored */
    /* UPnP worker only */
    char host[64], path[256], service[128]; int hport; uint32_t lan_ip; int lease;
} Ctx;
static Ctx* cur;
static _Atomic int upnp_live; /* UPnP workers still running (app exit waits a little for their cleanup) */
static void unref(Ctx* c) { if (atomic_fetch_sub(&c->refs, 1) == 1) free(c); }

static int resolve(const char* name, int port, struct sockaddr_in* out) {
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof hints); hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(name, NULL, &hints, &res) || !res) return 0;
    memcpy(out, res->ai_addr, sizeof *out); out->sin_port = htons((uint16_t)port);
    freeaddrinfo(res);
    return 1;
}
static int dns_thread(void* p) {
    Ctx* c = p;
    static const char* names[2] = {"stun.l.google.com", "stun.cloudflare.com"};
    static const int ports[2] = {19302, 3478};
    for (int i = 0; i < 2 && !atomic_load(&c->cancel); i++)
        atomic_store(&c->stun_resolved[i], resolve(names[i], ports[i], &c->stun[i]) ? 1 : -1);
    unref(c);
    return 0;
}

/* Waits for a socket in slices so a cancel (leaving the lobby, closing the app) is noticed quickly. */
static int wait_io(Ctx* c, Sock s, int write, int ms) {
    while (ms > 0 && !atomic_load(&c->cancel)) {
        fd_set fs; FD_ZERO(&fs); FD_SET(s, &fs);
        struct timeval tv = {0, (ms < 200 ? ms : 200) * 1000};
        int r = select((int)s + 1, write ? NULL : &fs, write ? &fs : NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0) return 0;
        ms -= 200;
    }
    return 0;
}
/* One HTTP exchange with the router (Connection: close). Returns the response length, 0 on failure. */
static int http(Ctx* c, const char* req, char* resp, int cap) {
    uint32_t ip; struct in_addr ia;
    if (inet_pton(AF_INET, c->host, &ia) == 1) ip = ntohl(ia.s_addr);
    else { struct sockaddr_in a; if (!resolve(c->host, c->hport, &a)) return 0; ip = ntohl(a.sin_addr.s_addr); }
    Sock s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == BAD) return 0;
    nonblocking(s);
    struct sockaddr_in to = addr4(ip, c->hport);
    int n = 0;
    if (connect(s, (struct sockaddr*)&to, sizeof to) && !WOULD_BLOCK(last_error())) goto out;
    if (!wait_io(c, s, 1, 2000)) goto out;
    { int err = 0; socklen_t el = sizeof err; getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &el); if (err) goto out; }
    { struct sockaddr_in me; socklen_t ml = sizeof me; if (!getsockname(s, (struct sockaddr*)&me, &ml)) c->lan_ip = ntohl(me.sin_addr.s_addr); }
    for (int sent = 0, len = (int)strlen(req); sent < len;) {
        if (!wait_io(c, s, 1, 2000)) goto out;
        int k = (int)send(s, req + sent, len - sent, 0);
        if (k <= 0) goto out;
        sent += k;
    }
    while (n < cap - 1 && wait_io(c, s, 0, 2000)) {
        int k = (int)recv(s, resp + n, cap - 1 - n, 0);
        if (k <= 0) break;
        n += k;
    }
out:
    closesocket(s);
    resp[n] = 0;
    return n;
}
static int status_ok(const char* r) { const char* sp = strchr(r, ' '); return sp && !strncmp(sp + 1, "200", 3); }
/* The text between <tag> and </tag> (namespace prefixes on the tag are allowed), after position `from`. */
static const char* xml_value(const char* from, const char* tag, char* out, int cap) {
    char open[64]; snprintf(open, sizeof open, "%s>", tag);
    for (const char* p = from; (p = strstr(p, open)); p++) {
        if (p == from || (p[-1] != '<' && p[-1] != ':')) continue;
        const char* v = p + strlen(open), *e = strchr(v, '<');
        if (!e) return NULL;
        int k = (int)(e - v); if (k >= cap) k = cap - 1;
        memcpy(out, v, (size_t)k); out[k] = 0;
        while (k && isspace((unsigned char)out[k - 1])) out[--k] = 0;
        return e;
    }
    return NULL;
}
static int soap(Ctx* c, const char* action, const char* args, char* resp, int cap) {
    char body[1400], req[2200];
    snprintf(body, sizeof body,
             "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
             "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s>"
             "</s:Body></s:Envelope>", action, c->service, args, action);
    snprintf(req, sizeof req,
             "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
             "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
             c->path, c->host, c->hport, c->service, action, (int)strlen(body), body);
    return http(c, req, resp, cap) && status_ok(resp);
}
static int split_url(const char* url, char* host, int hcap, int* port, char* path, int pcap) {
    if (strncmp(url, "http://", 7)) return 0;
    const char* h = url + 7, *e = h;
    while (*e && *e != ':' && *e != '/') e++;
    int k = (int)(e - h); if (!k || k >= hcap) return 0;
    memcpy(host, h, (size_t)k); host[k] = 0;
    *port = 80;
    if (*e == ':') { *port = atoi(e + 1); while (*e && *e != '/') e++; }
    snprintf(path, pcap, "%s", *e ? e : "/");
    return *port > 0 && *port < 65536;
}
static int ssdp_find(Ctx* c, char* location, int cap) {
    Sock s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == BAD) return 0;
    nonblocking(s);
    struct sockaddr_in to = addr4(0xEFFFFFFAu, 1900); /* 239.255.255.250 */
    static const char* targets[2] = {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                                     "urn:schemas-upnp-org:device:InternetGatewayDevice:2"};
    for (int i = 0; i < 2; i++) {
        char m[300];
        int n = snprintf(m, sizeof m, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n"
                                      "MX: 2\r\nST: %s\r\n\r\n", targets[i]);
        sendto(s, m, n, 0, (struct sockaddr*)&to, sizeof to);
    }
    int found = 0;
    uint64_t end = platform_now_ns() + 3000000000ull;
    for (uint64_t now; !found && (now = platform_now_ns()) < end && wait_io(c, s, 0, (int)((end - now) / 1000000));) {
        char r[1500];
        int n = (int)recvfrom(s, r, sizeof r - 1, 0, NULL, NULL);
        if (n <= 0) continue;
        r[n] = 0;
        for (char* line = r; line && *line; line = strstr(line, "\n") ? strstr(line, "\n") + 1 : NULL) {
            if (strncmp(line, "LOCATION:", 9) && strncmp(line, "Location:", 9) && strncmp(line, "location:", 9)) continue;
            const char* v = line + 9; while (*v == ' ') v++;
            int k = 0; while (v[k] && v[k] != '\r' && v[k] != '\n' && k < cap - 1) { location[k] = v[k]; k++; }
            location[k] = 0; found = k > 0; break;
        }
    }
    closesocket(s);
    return found;
}
static int add_mapping(Ctx* c, int ext, char* resp, int cap, int* error) {
    char args[700], lan[20]; ip_text(c->lan_ip, lan, sizeof lan);
    snprintf(args, sizeof args,
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>"
             "<NewInternalPort>%d</NewInternalPort><NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>Sonic Riders online</NewPortMappingDescription>"
             "<NewLeaseDuration>%d</NewLeaseDuration>", ext, c->local_port, lan, c->lease);
    if (soap(c, "AddPortMapping", args, resp, cap)) return 1;
    char e[16] = ""; xml_value(resp, "errorCode", e, sizeof e);
    *error = atoi(e);
    return 0;
}
static void delete_mapping(Ctx* c) {
    char args[300], resp[2048];
    snprintf(args, sizeof args, "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", c->ext_port);
    atomic_store(&c->cancel, 0); /* the delete itself must not be cut short; it has its own 2 s timeouts */
    int ok = soap(c, "DeletePortMapping", args, resp, sizeof resp);
    rt_log("[inet] UPnP: removed the mapping for external UDP %d%s", c->ext_port, ok ? "" : " (the router did not confirm)");
}
static int upnp_map(Ctx* c, char* buf, int cap) {
    char location[300];
    if (!ssdp_find(c, location, sizeof location)) { rt_log("[inet] UPnP: no router answered"); return INET_UPNP_NONE; }
    if (!split_url(location, c->host, sizeof c->host, &c->hport, c->path, sizeof c->path)) return INET_UPNP_NONE;
    char req[600];
    snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", c->path, c->host, c->hport);
    if (!http(c, req, buf, cap) || !status_ok(buf)) { rt_log("[inet] UPnP: the router's description could not be read"); return INET_UPNP_NONE; }
    const char* p = buf; char type[128] = "", control[256] = "";
    while ((p = xml_value(p, "serviceType", type, sizeof type))) {
        if (strstr(type, "WANIPConnection:") || strstr(type, "WANPPPConnection:")) { xml_value(p, "controlURL", control, sizeof control); break; }
    }
    if (!control[0]) { rt_log("[inet] UPnP: the router offers no port mapping"); return INET_UPNP_NONE; }
    snprintf(c->service, sizeof c->service, "%s", type);
    if (!strncmp(control, "http://", 7)) { char h[64]; int hp; if (split_url(control, h, sizeof h, &hp, c->path, sizeof c->path)) { snprintf(c->host, sizeof c->host, "%s", h); c->hport = hp; } }
    else snprintf(c->path, sizeof c->path, "%s%s", control[0] == '/' ? "" : "/", control);
    char ext[32] = "";
    if (soap(c, "GetExternalIPAddress", "", buf, cap)) xml_value(buf, "NewExternalIPAddress", ext, sizeof ext);
    struct in_addr ia; uint32_t eip = inet_pton(AF_INET, ext, &ia) == 1 ? ntohl(ia.s_addr) : 0;
    char em[20]; inet_mask(eip, em, sizeof em);
    rt_log("[inet] UPnP: router %s:%d, external address %s%s", c->host, c->hport, eip ? em : "(none)",
           eip && !inet_public_ip(eip) ? " (private: a second router or the carrier's)" : "");
    if (!c->lan_ip) return INET_UPNP_NONE;
    if (eip && !inet_public_ip(eip)) { c->ext_ip = eip; return INET_UPNP_DOUBLE_NAT; }
    static const int tries[3] = {0, INET_PORT_ALT, INET_PORT_ALT + 10};
    c->lease = 7200;
    for (int i = 0; i < 3 && !atomic_load(&c->cancel); i++) {
        int port = i ? tries[i] : c->local_port, error = 0;
        if (i && port == c->local_port) continue;
        int ok = add_mapping(c, port, buf, cap, &error);
        if (!ok && error == 725) { c->lease = 0; ok = add_mapping(c, port, buf, cap, &error); } /* permanent leases only */
        if (ok) {
            c->ext_ip = eip; c->ext_port = port;
            rt_log("[inet] UPnP: external UDP %d -> this device's port %d (lease %s)", port, c->local_port, c->lease ? "2 h" : "permanent");
            return INET_UPNP_MAPPED;
        }
        rt_log("[inet] UPnP: mapping external UDP %d refused (error %d)", port, error);
    }
    return INET_UPNP_NONE;
}
static int upnp_thread(void* p) {
    Ctx* c = p;
    int cap = 65536;
    char* buf = malloc((size_t)cap);
    int r = buf ? upnp_map(c, buf, cap) : INET_UPNP_NONE;
    free(buf);
    atomic_store(&c->upnp, r);
    if (r == INET_UPNP_MAPPED) {
        uint64_t since = platform_now_ns();
        while (!atomic_load(&c->cancel)) {
            platform_sleep_ns(250000000ull);
            if (c->lease && platform_now_ns() - since > 1800000000000ull) { /* renew every 30 min while open */
                char buf[2048]; int error = 0;
                if (!add_mapping(c, c->ext_port, buf, sizeof buf, &error)) rt_log("[inet] UPnP: renewing the mapping failed (error %d)", error);
                since = platform_now_ns();
            }
        }
        delete_mapping(c);
    }
    atomic_fetch_sub(&upnp_live, 1);
    unref(c);
    return 0;
}
static void inet_atexit(void) {
    if (cur) atomic_store(&cur->cancel, 1);
    for (int i = 0; i < 60 && atomic_load(&upnp_live) > 0; i++) platform_sleep_ns(25000000ull); /* <= 1.5 s */
}

static void finish_code(void);
/* ---------------------------------------------------------------- STUN on the shared socket */
static uint8_t stun_tid[2][12];
static int stun_tries[2], stun_answered[2];
static uint64_t stun_last[2];
static void stun_send(int i) {
    uint8_t m[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    memcpy(m + 8, stun_tid[i], 12);
    inet_send(m, 20, &cur->stun[i]);
}
static int stun_handle(const uint8_t* b, int n, const struct sockaddr_in* from) {
    if (n < 20 || b[0] != 0x01 || b[1] != 0x01) return 0; /* Binding Success Response only */
    if (b[4] != 0x21 || b[5] != 0x12 || b[6] != 0xA4 || b[7] != 0x42) return 0;
    int len = b[2] << 8 | b[3];
    if (len != n - 20 || (len & 3)) return 1;
    int i;
    for (i = 0; i < 2; i++)
        if (!stun_answered[i] && !memcmp(b + 8, stun_tid[i], 12) && cur && atomic_load(&cur->stun_resolved[i]) == 1 &&
            from->sin_addr.s_addr == cur->stun[i].sin_addr.s_addr && from->sin_port == cur->stun[i].sin_port) break;
    if (i == 2) return 1;
    uint32_t ip = 0; int port = 0;
    for (int pos = 20; pos + 4 <= n;) {
        int type = b[pos] << 8 | b[pos + 1], alen = b[pos + 2] << 8 | b[pos + 3];
        if (pos + 4 + alen > n) break;
        const uint8_t* a = b + pos + 4;
        if ((type == 0x0020 || (type == 0x0001 && !ip)) && alen >= 8 && a[1] == 0x01) {
            port = a[2] << 8 | a[3];
            ip = (uint32_t)a[4] << 24 | (uint32_t)a[5] << 16 | (uint32_t)a[6] << 8 | a[7];
            if (type == 0x0020) { port ^= 0x2112; ip ^= 0x2112A442u; }
        }
        pos += 4 + ((alen + 3) & ~3);
    }
    if (!ip || !port) return 1;
    stun_answered[i] = 1;
    if (info.ready && i == 0) { /* a keepalive answer */
        if (ip != info.stun_ip || port != info.stun_port) {
            char m[20]; inet_mask(ip, m, sizeof m);
            rt_log("[inet] the router/carrier moved this device to %s:%d (was port %d)", m, port, info.stun_port);
            info.stun_ip = ip; info.stun_port = port;
            char old[20]; snprintf(old, sizeof old, "%s", info.code);
            finish_code();
            if (strcmp(old, info.code)) info.changes++;
        }
        return 1;
    }
    if (i == 0 || !info.stun_ip) { info.stun_ip = ip; info.stun_port = port; }
    if (i == 1) info.stun_port2 = port;
    char t[20]; inet_mask(ip, t, sizeof t);
    rt_log("[inet] STUN server %d: this network is %s:%d from outside", i + 1, t, port);
    return 1;
}

/* ---------------------------------------------------------------- the socket */
static Sock sock(void) { return (Sock)atomic_load(&g_sock); }
int inet_open(void) {
    if (sock() != BAD) return 1;
    memset(&info, 0, sizeof info);
    memset(stun_answered, 0, sizeof stun_answered); memset(stun_tries, 0, sizeof stun_tries);
    if (atomic_load(&upnp_live) > 0) { /* a previous worker still removes its mapping */
        for (int i = 0; i < 40 && atomic_load(&upnp_live) > 0; i++) platform_sleep_ns(25000000ull);
    }
    Sock s = BAD;
    static const int ports[2] = {INET_PORT, INET_PORT_ALT};
    for (int i = 0; i < 2 && s == BAD; i++) {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == BAD) return 0;
        nonblocking(s);
#ifdef _WIN32
        BOOL off = FALSE; DWORD got; /* an ICMP "port unreachable" must not make recvfrom fail */
        WSAIoctl(s, (DWORD)_WSAIOW(IOC_VENDOR, 12), &off, sizeof off, NULL, 0, &got, NULL, NULL);
#endif
        struct sockaddr_in a = addr4(INADDR_ANY, ports[i]);
        if (bind(s, (struct sockaddr*)&a, sizeof a)) { closesocket(s); s = BAD; continue; }
        info.local_port = ports[i];
    }
    if (s == BAD) return 0;
    atomic_store(&g_sock, (intptr_t)s);
    atomic_store(&g_owner, OWNER_LOBBY);
    info.open = 1;
    opened_ns = platform_now_ns();
    for (int i = 0; i < 2; i++) for (int k = 0; k < 12; k++) stun_tid[i][k] = (uint8_t)rnd64();
    Ctx* c = calloc(1, sizeof *c);
    if (c) {
        c->local_port = info.local_port;
        atomic_store(&c->refs, 3); /* this file, the DNS thread, the UPnP thread */
        atomic_fetch_add(&upnp_live, 1);
        cur = c;
        rt_thread_start(dns_thread, c);
        rt_thread_start(upnp_thread, c);
    }
    static int registered;
    if (!registered) { registered = 1; rt_atexit(inet_atexit); }
    rt_log("[inet] internet socket on UDP %d; looking for this network's public address", info.local_port);
    return 1;
}
static void finish_code(void) {
    const char* test = getenv("SR_INET_TEST_IP");
    uint32_t sip = info.stun_ip; int sport = info.stun_port;
    info.pub_ip = 0; info.pub_port = 0;
    if (info.upnp == INET_UPNP_MAPPED && info.upnp_ext_ip && (!sip || info.upnp_ext_ip == sip)) {
        info.pub_ip = info.upnp_ext_ip; info.pub_port = info.upnp_ext_port;
    } else if (sip) { info.pub_ip = sip; info.pub_port = sport; }
    if (test) { struct in_addr ia; if (inet_pton(AF_INET, test, &ia) == 1) { info.pub_ip = ntohl(ia.s_addr); info.pub_port = info.local_port; } }
    if (info.pub_ip) inet_code_encode(info.pub_ip, info.pub_port, info.code);
    info.note[0] = 0;
    if (!info.pub_ip) snprintf(info.note, sizeof info.note, "Could not find your internet address. Check this device is online, then try again.");
    else if (info.upnp == INET_UPNP_MAPPED) snprintf(info.note, sizeof info.note, "Your router opened the game's port automatically.");
    else if (info.upnp == INET_UPNP_DOUBLE_NAT) snprintf(info.note, sizeof info.note, "Your network has two routers (or a mobile network). If your friend can't join, let them host.");
    else if (info.stun_port2 && info.stun_port2 != info.stun_port) snprintf(info.note, sizeof info.note, "Your network changes ports for each connection (strict). If it doesn't connect, let your friend host, and both add each other's codes.");
    else snprintf(info.note, sizeof info.note, "Your router didn't open the port by itself. If your friend can't join, add their code too.");
    char t[20]; inet_mask(info.pub_ip, t, sizeof t);
    rt_log("[inet] code ready: %s (%s:%d; STUN %d/%d, UPnP %d)%s", info.code[0] ? "yes" : "no", info.pub_ip ? t : "-", info.pub_port,
           info.stun_port, info.stun_port2, info.upnp, test ? " [SR_INET_TEST_IP]" : "");
}
static uint64_t refresh_last;
/* Carrier NATs forget an idle mapping within ~30-60 s and then hand out a new port, which makes a shown code
 * stale (seen 2026-11-21: a phone on a hotspot moved from one port to another while hosting). A binding request
 * every 10 s keeps the mapping alive; if the address still changes, the code is updated and the lobby says so. */
static void refresh(uint64_t now) {
    if (!cur || atomic_load(&cur->stun_resolved[0]) != 1 || now - refresh_last < 10000000000ull) return;
    refresh_last = now;
    stun_answered[0] = 0;
    for (int k = 0; k < 12; k++) stun_tid[0][k] = (uint8_t)rnd64();
    stun_send(0);
}
const InetInfo* inet_info(void) {
    if (info.open && info.ready && atomic_load(&g_owner) == OWNER_LOBBY) { refresh(platform_now_ns()); return &info; }
    if (!info.open || info.ready || !cur) return &info;
    uint64_t now = platform_now_ns(), age = now - opened_ns;
    for (int i = 0; i < 2; i++)
        if (!stun_answered[i] && atomic_load(&cur->stun_resolved[i]) == 1 && stun_tries[i] < 6 && now - stun_last[i] > 500000000ull) {
            stun_send(i); stun_tries[i]++; stun_last[i] = now;
        }
    int up = atomic_load(&cur->upnp);
    if (up != INET_UPNP_PENDING && info.upnp == INET_UPNP_PENDING) {
        info.upnp = up; info.upnp_ext_ip = cur->ext_ip; info.upnp_ext_port = cur->ext_port;
    }
    int stun_done = (stun_answered[0] && (stun_answered[1] || age > 3000000000ull)) || age > 4000000000ull ||
                    (atomic_load(&cur->stun_resolved[0]) == -1 && atomic_load(&cur->stun_resolved[1]) == -1);
    int upnp_done = info.upnp != INET_UPNP_PENDING || age > 6000000000ull;
    if (stun_done && upnp_done) {
        if (info.upnp == INET_UPNP_PENDING) { info.upnp = INET_UPNP_NONE; rt_log("[inet] UPnP: no answer within 6 s"); }
        info.ready = 1;
        refresh_last = now;
        finish_code();
    }
    return &info;
}
int inet_recv(unsigned char* b, int cap, void* from_v) {
    struct sockaddr_in* from = from_v;
    for (int budget = 0; budget < 16; budget++) {
        Sock s = sock();
        if (s == BAD || atomic_load(&g_owner) != OWNER_LOBBY) return -1;
        socklen_t fl = sizeof *from;
        int n = (int)recvfrom(s, (char*)b, cap, 0, (struct sockaddr*)from, &fl);
        if (n < 0) return -1;
        if (n >= 20 && b[0] == 0x01 && stun_handle(b, n, from)) continue;
        return n;
    }
    return -1;
}
void inet_send(const void* b, int n, const void* to) {
    Sock s = sock();
    if (s != BAD) sendto(s, (const char*)b, n, 0, (const struct sockaddr*)to, sizeof(struct sockaddr_in));
}
void inet_close(void) {
    intptr_t s = atomic_exchange(&g_sock, (intptr_t)-1);
    if ((Sock)s != BAD) { closesocket((Sock)s); rt_log("[inet] internet socket closed"); }
    atomic_store(&g_owner, OWNER_NONE);
    if (cur) { atomic_store(&cur->cancel, 1); unref(cur); cur = NULL; }
    info.open = info.ready = 0;
}
void inet_handoff(const void* peer) {
    memcpy(&g_peer, peer, sizeof g_peer);
    atomic_store(&g_owner, OWNER_NETPLAY); /* published to the game thread by online_lobby_finish's atomic store */
}
int inet_handed(void) { return atomic_load(&g_owner) == OWNER_NETPLAY && sock() != BAD; }
int inet_take(long long* s, void* peer) {
    if (!inet_handed()) return 0;
    *s = (long long)(intptr_t)atomic_load(&g_sock);
    memcpy(peer, &g_peer, sizeof g_peer);
    return 1;
}
