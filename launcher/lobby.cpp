/* Online lobby for the Windows launcher: see lobby.h. Messages (UDP, little-endian):
 *   "SRLB" version type, then by type:
 *   ANNOUNCE  host -> broadcast :7779, every second: game fingerprint u64, players, max, open, host name
 *   JOIN      player -> host :7778 (resent until answered): fingerprint u64, name
 *   LIST      host -> every player (answer to JOIN, then every second and on changes): count, names
 *   REFUSE    host -> player: the reason (full, another game version, already started)
 *   PING      player -> host, every second (the host drops a player not heard from for 6 s)
 *   LEAVE     either way
 *   START     host -> every player (sent several times): the race starts, every launcher starts the game
 * The fingerprint is a hash of the game's exe: the game itself refuses another build, and the lobby says so first.
 * A join code is the host's local IPv4 address in 7 letters/digits (no 0 O 1 I): it works on the same network;
 * over the internet only with UDP 7777-7778 forwarded to the host (the code names its local address, so for now the
 * lobby is a LAN lobby; a lobby server with real codes is planned, docs/notes/online-research.md). */
#include "lobby.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace lobby {
namespace {

enum Type : uint8_t { T_ANNOUNCE = 1, T_JOIN = 2, T_LIST = 3, T_REFUSE = 4, T_PING = 5, T_LEAVE = 6, T_START = 7 };
constexpr uint8_t kVersion = 1;
constexpr double kDropAfter = 6.0;  /* seconds without a word */

struct Member { std::string name; sockaddr_in addr{}; double seen = 0; };
struct Seen { Found f; double at = 0; };

std::mutex g_mu;
std::thread g_thr;
std::atomic<bool> g_run{false};
View g_view;
uint64_t g_fp;                    /* this game's fingerprint */
std::wstring g_exe;
/* requests from the page, done by the thread */
enum Req { R_NONE, R_HOST, R_JOIN, R_LEAVE, R_START };
Req g_req = R_NONE;
std::string g_req_name, g_req_ip;

double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

uint64_t fingerprint(const std::wstring& p) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    uint64_t x = 1469598103934665603ull;
    std::vector<uint8_t> buf(1 << 20);
    DWORD got = 0;
    while (ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr) && got) {
        for (DWORD i = 0; i < got; i++) x = (x ^ buf[i]) * 1099511628211ull;
    }
    CloseHandle(h);
    return x;
}

const char kAlpha[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";  /* 32 symbols: no 0 O 1 I */

/* this PC's addresses: the main one (an adapter with a gateway) and every adapter's broadcast address */
struct Nets { uint32_t main = 0; std::vector<uint32_t> bcast; };
Nets local_nets() {
    Nets n;
    ULONG sz = 16384;
    std::vector<uint8_t> buf(sz);
    auto* aa = (IP_ADAPTER_ADDRESSES*)buf.data();
    ULONG fl = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_INET, fl, nullptr, aa, &sz) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(sz);
        aa = (IP_ADAPTER_ADDRESSES*)buf.data();
        if (GetAdaptersAddresses(AF_INET, fl, nullptr, aa, &sz) != NO_ERROR) return n;
    }
    for (auto* a = aa; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (u->Address.lpSockaddr->sa_family != AF_INET) continue;
            uint32_t ip = ntohl(((sockaddr_in*)u->Address.lpSockaddr)->sin_addr.s_addr);
            if ((ip >> 16) == 0xA9FE) continue;  /* 169.254: no network */
            int pl = u->OnLinkPrefixLength;
            uint32_t mask = pl <= 0 ? 0 : pl >= 32 ? 0xFFFFFFFFu : ~((1u << (32 - pl)) - 1);
            n.bcast.push_back(ip | ~mask);
            bool real = a->FirstGatewayAddress && (a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211);
            if (real && !n.main) n.main = ip;
        }
    }
    return n;
}

std::string ip_str(uint32_t ip) {
    char b[32];
    snprintf(b, sizeof b, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
    return b;
}

sockaddr_in addr_of(uint32_t ip, int port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(ip);
    a.sin_port = htons((u_short)port);
    return a;
}

SOCKET open_udp(int port, bool reuse) {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return s;
    BOOL on = TRUE, off = FALSE;
    if (reuse) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof on);
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof on);
    DWORD got;
    WSAIoctl(s, (DWORD)_WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof off, nullptr, 0, &got, nullptr, nullptr);
    sockaddr_in a = addr_of(INADDR_ANY, port);
    if (bind(s, (sockaddr*)&a, sizeof a)) { closesocket(s); return INVALID_SOCKET; }
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    return s;
}

/* message building */
struct Msg {
    uint8_t b[512];
    int n = 0;
    explicit Msg(Type t) { memcpy(b, "SRLB", 4); b[4] = kVersion; b[5] = t; n = 6; }
    void u8(uint8_t v) { b[n++] = v; }
    void u64(uint64_t v) { memcpy(b + n, &v, 8); n += 8; }
    void str(const std::string& s) {
        size_t l = s.size() > 63 ? 63 : s.size();
        b[n++] = (uint8_t)l;
        memcpy(b + n, s.data(), l);
        n += (int)l;
    }
    void send(SOCKET s, const sockaddr_in& a) const { sendto(s, (const char*)b, n, 0, (const sockaddr*)&a, sizeof a); }
};
struct Reader {
    const uint8_t* p;
    int n, i = 6;
    bool ok = true;
    uint8_t u8() { if (i + 1 > n) { ok = false; return 0; } return p[i++]; }
    uint64_t u64() { uint64_t v = 0; if (i + 8 > n) { ok = false; return 0; } memcpy(&v, p + i, 8); i += 8; return v; }
    std::string str() {
        int l = u8();
        if (!ok || i + l > n) { ok = false; return {}; }
        std::string s((const char*)p + i, (size_t)l);
        i += l;
        return s;
    }
};

bool same_addr(const sockaddr_in& a, const sockaddr_in& b) { return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port; }

void thread_main() {
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    uint64_t fp = fingerprint(g_exe);
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_fp = fp;
        g_view.ready = true;
    }
    SOCKET disc = open_udp(kDiscoverPort, true);  /* hears every lobby on the network */
    SOCKET sock = INVALID_SOCKET;                  /* host: the lobby port; player: any port */
    State st = IDLE;
    std::string my_name, host_name;
    std::vector<Member> members;                   /* host: the players who joined */
    sockaddr_in host_addr{};
    double last_tick = 0, join_t0 = 0, last_heard = 0, start_until = 0;
    std::vector<Seen> seen;
    Nets nets;
    double nets_at = -100;

    auto publish = [&]() {  /* the page's copy (under g_mu) */
        g_view.state = st;
        if (st == HOSTING || st == STARTING) {
            if (st == HOSTING || !g_view.players.size()) {
                g_view.players.clear();
                g_view.players.push_back(my_name);
                for (auto& m : members) g_view.players.push_back(m.name);
                g_view.me = 0;
            }
        }
        g_view.found.clear();
        for (auto& s : seen) g_view.found.push_back(s.f);
    };
    auto close_sock = [&]() { if (sock != INVALID_SOCKET) closesocket(sock); sock = INVALID_SOCKET; };
    auto fail = [&](const std::string& why) {
        close_sock();
        members.clear();
        st = IDLE;
        std::lock_guard<std::mutex> l(g_mu);
        g_view.error = why;
        g_view.players.clear();
        g_view.code.clear();
    };
    auto send_list = [&](Type t) {
        Msg m(t);
        m.u8((uint8_t)(members.size() + 1));
        m.str(my_name);
        for (auto& x : members) m.str(x.name);
        for (auto& x : members) m.send(sock, x.addr);
    };

    while (g_run) {
        /* requests from the page */
        Req req;
        std::string rname, rip;
        {
            std::lock_guard<std::mutex> l(g_mu);
            req = g_req;
            g_req = R_NONE;
            rname = g_req_name;
            rip = g_req_ip;
        }
        double t = now_s();
        if (t - nets_at > 10) { nets = local_nets(); nets_at = t; }
        if (req == R_HOST && st == IDLE) {
            sock = open_udp(kLobbyPort, false);
            if (sock == INVALID_SOCKET) fail("Could not open the lobby (UDP port 7778 is in use: another lobby open on this PC?).");
            else {
                st = HOSTING;
                my_name = rname;
                members.clear();
                std::lock_guard<std::mutex> l(g_mu);
                g_view.error.clear();
                g_view.code = nets.main ? ip_to_code(ip_str(nets.main)) : std::string();
                if (!nets.main) g_view.error = "No network found: other players can't reach this PC.";
            }
        } else if (req == R_JOIN && st == IDLE) {
            sock = open_udp(0, false);
            in_addr ia{};
            if (sock == INVALID_SOCKET || inet_pton(AF_INET, rip.c_str(), &ia) != 1) fail("That address is not valid.");
            else {
                st = JOINING;
                my_name = rname;
                host_addr = addr_of(ntohl(ia.s_addr), kLobbyPort);
                join_t0 = t;
                last_tick = 0;
                std::lock_guard<std::mutex> l(g_mu);
                g_view.error.clear();
                g_view.host_ip = rip;
            }
        } else if (req == R_LEAVE) {
            if (st == HOSTING) for (int k = 0; k < 2; k++) for (auto& m : members) Msg(T_LEAVE).send(sock, m.addr);
            if (st == JOINED || st == JOINING) for (int k = 0; k < 2; k++) Msg(T_LEAVE).send(sock, host_addr);
            close_sock();
            members.clear();
            st = IDLE;
            std::lock_guard<std::mutex> l(g_mu);
            g_view.players.clear();
            g_view.code.clear();
        } else if (req == R_START && st == HOSTING && !members.empty()) {
            st = STARTING;
            start_until = t + 1.5;
            last_tick = 0;
            std::lock_guard<std::mutex> l(g_mu);
            publish();
        }

        /* the network */
        fd_set rs;
        FD_ZERO(&rs);
        if (disc != INVALID_SOCKET) FD_SET(disc, &rs);
        if (sock != INVALID_SOCKET) FD_SET(sock, &rs);
        timeval tv{0, 50000};
        select(0, &rs, nullptr, nullptr, &tv);
        uint8_t b[1500];
        for (SOCKET s : {disc, sock}) {
            if (s == INVALID_SOCKET) continue;
            for (;;) {
                sockaddr_in from{};
                int fl = sizeof from;
                int n = recvfrom(s, (char*)b, sizeof b, 0, (sockaddr*)&from, &fl);
                if (n < 6) break;
                if (memcmp(b, "SRLB", 4) || b[4] != kVersion) continue;
                Reader r{b, n};
                uint8_t type = b[5];
                t = now_s();
                if (type == T_ANNOUNCE && s == disc) {
                    Found f;
                    uint64_t gfp = r.u64();
                    f.players = r.u8();
                    f.max_players = r.u8();
                    f.open = r.u8() != 0;
                    f.host_name = r.str();
                    if (!r.ok) continue;
                    f.ip = ip_str(ntohl(from.sin_addr.s_addr));
                    f.same_game = gfp == g_fp;
                    if (st == HOSTING && ntohs(from.sin_port) == kLobbyPort && f.host_name == my_name) {
                        bool mine = false;  /* our own announcement */
                        for (uint32_t ip : {nets.main, 0x7F000001u}) mine |= ntohl(from.sin_addr.s_addr) == ip;
                        if (mine) continue;
                    }
                    bool upd = false;
                    for (auto& x : seen)
                        if (x.f.ip == f.ip) { x.f = f; x.at = t; upd = true; }
                    if (!upd) seen.push_back({f, t});
                } else if (st == HOSTING && s == sock && type == T_JOIN) {
                    uint64_t gfp = r.u64();
                    std::string name = clean_name(r.str());
                    if (!r.ok) continue;
                    Member* m = nullptr;
                    for (auto& x : members)
                        if (same_addr(x.addr, from)) m = &x;
                    if (!m) {
                        const char* why = gfp != g_fp ? "The host has a different version of the game."
                                          : (int)members.size() + 1 >= kMaxPlayers ? "The lobby is full." : nullptr;
                        if (why) {
                            Msg rf(T_REFUSE);
                            rf.str(why);
                            rf.send(sock, from);
                            continue;
                        }
                        std::string base = name.empty() ? std::string("Player") : name, nm = base;
                        for (int k = 2;; k++) {  /* names are unique in a lobby */
                            bool clash = nm == my_name;
                            for (auto& x : members) clash |= x.name == nm;
                            if (!clash) break;
                            nm = base + " (" + std::to_string(k) + ")";
                        }
                        members.push_back({nm, from, t});
                        m = &members.back();
                    }
                    m->seen = t;
                    send_list(T_LIST);
                } else if (st == HOSTING && s == sock && type == T_PING) {
                    for (auto& x : members)
                        if (same_addr(x.addr, from)) x.seen = t;
                } else if (st == HOSTING && s == sock && type == T_LEAVE) {
                    for (size_t k = 0; k < members.size(); k++)
                        if (same_addr(members[k].addr, from)) { members.erase(members.begin() + (long)k); break; }
                    send_list(T_LIST);
                } else if ((st == JOINING || st == JOINED) && s == sock && from.sin_addr.s_addr == host_addr.sin_addr.s_addr) {
                    last_heard = t;
                    if (type == T_LIST) {
                        int cnt = r.u8();
                        std::vector<std::string> names;
                        for (int k = 0; k < cnt; k++) names.push_back(r.str());
                        if (!r.ok || names.empty()) continue;
                        st = JOINED;
                        host_name = names[0];
                        std::lock_guard<std::mutex> l(g_mu);
                        g_view.players = names;
                        g_view.me = 0;
                        for (int k = 1; k < cnt; k++)
                            if (names[k] == my_name || names[k].rfind(my_name + " (", 0) == 0) g_view.me = k;
                    } else if (type == T_REFUSE) {
                        std::string why = r.str();
                        fail(why.empty() ? "The host refused." : why);
                    } else if (type == T_LEAVE) {
                        fail("The host closed the lobby.");
                    } else if (type == T_START && st == JOINED) {
                        st = STARTING;
                        close_sock();
                    }
                }
            }
        }

        /* timers */
        t = now_s();
        for (size_t k = 0; k < seen.size();)
            if (t - seen[k].at > 4.0) seen.erase(seen.begin() + (long)k);
            else k++;
        if (st == HOSTING) {
            for (size_t k = 0; k < members.size();)
                if (t - members[k].seen > kDropAfter) { members.erase(members.begin() + (long)k); send_list(T_LIST); }
                else k++;
            if (t - last_tick > 1.0) {
                last_tick = t;
                Msg a(T_ANNOUNCE);
                a.u64(g_fp);
                a.u8((uint8_t)(members.size() + 1));
                a.u8((uint8_t)kMaxPlayers);
                a.u8((int)members.size() + 1 < kMaxPlayers);
                a.str(my_name);
                std::vector<uint32_t> to = nets.bcast;
                to.push_back(0xFFFFFFFFu);
                to.push_back(0x7F000001u);  /* a second launcher on this PC (testing) */
                for (uint32_t ip : to) a.send(sock, addr_of(ip, kDiscoverPort));
                send_list(T_LIST);
            }
        } else if (st == STARTING && sock != INVALID_SOCKET) {  /* the host tells everyone a few times */
            if (t - last_tick > 0.2) {
                last_tick = t;
                for (auto& m : members) Msg(T_START).send(sock, m.addr);
            }
            if (t > start_until) close_sock();
        } else if (st == JOINING) {
            if (t - last_tick > 0.5) {
                last_tick = t;
                Msg j(T_JOIN);
                j.u64(g_fp);
                j.str(my_name);
                j.send(sock, host_addr);
            }
            if (t - join_t0 > 6.0) fail("No lobby answered at that address. Check the code, and that the host's lobby is open.");
        } else if (st == JOINED) {
            if (t - last_tick > 1.0) {
                last_tick = t;
                Msg(T_PING).send(sock, host_addr);
            }
            if (t - last_heard > kDropAfter) fail("Lost the host (no answer for a few seconds).");
        }
        std::lock_guard<std::mutex> l(g_mu);
        publish();
    }
    close_sock();
    if (disc != INVALID_SOCKET) closesocket(disc);
    WSACleanup();
}

}  // namespace

void init(const std::wstring& game_exe) {
    if (g_run) return;
    g_exe = game_exe;
    g_run = true;
    g_thr = std::thread(thread_main);
}
void shutdown() {
    if (!g_run) return;
    {
        std::lock_guard<std::mutex> l(g_mu);
        g_req = R_LEAVE;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(120));  /* the LEAVE goes out */
    g_run = false;
    if (g_thr.joinable()) g_thr.join();
}
static void request(Req r, const std::string& name, const std::string& ip) {
    std::lock_guard<std::mutex> l(g_mu);
    g_req = r;
    g_req_name = name;
    g_req_ip = ip;
}
void host(const std::string& name) { request(R_HOST, clean_name(name), ""); }
void join(const std::string& ip, const std::string& name) { request(R_JOIN, clean_name(name), ip); }
void leave() { request(R_LEAVE, "", ""); }
void start_race() { request(R_START, "", ""); }
View view() {
    std::lock_guard<std::mutex> l(g_mu);
    return g_view;
}
void clear_error() {
    std::lock_guard<std::mutex> l(g_mu);
    g_view.error.clear();
}

std::string ip_to_code(const std::string& ip) {
    in_addr ia{};
    if (inet_pton(AF_INET, ip.c_str(), &ia) != 1) return {};
    uint32_t v = ntohl(ia.s_addr);
    std::string c;
    for (int i = 6; i >= 0; i--) c += kAlpha[(v >> (5 * i)) & 31];  /* 35 bits, the top 3 zero */
    return c.substr(0, 3) + "-" + c.substr(3);
}
bool code_to_ip(const std::string& code, std::string& ip) {
    uint64_t v = 0;
    int n = 0;
    for (char ch : code) {
        if (ch == '-' || ch == ' ') continue;
        char u = (char)toupper((unsigned char)ch);
        if (u == 'O') u = '0';
        if (u == 'I' || u == 'L') u = '1';
        const char* p = strchr(kAlpha, u);
        if (!p || !u) return false;
        v = v << 5 | (uint64_t)(p - kAlpha);
        n++;
    }
    if (n != 7 || v > 0xFFFFFFFFull) return false;
    ip = ip_str((uint32_t)v);
    return true;
}
std::string clean_name(const std::string& s) {
    std::string o;
    for (char ch : s) {
        unsigned char u = (unsigned char)ch;
        if (u >= 32 && u != 127 && u != '"') o += ch;  /* UTF-8 bytes pass */
        if (o.size() >= (size_t)kNameMax) break;
    }
    while (!o.empty() && o.back() == ' ') o.pop_back();
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    return o;
}

}  // namespace lobby
