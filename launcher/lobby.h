/* Online lobby for the Windows launcher (docs/ONLINE.md "Lobby"): one player hosts, the others find the lobby on
 * the local network or type the host's join code, everyone sees the players' names, and the host starts the race:
 * every launcher then starts the game with SR_NET (host / join) and the names (SR_NET_NAME, SR_NET_PEER).
 * UDP, a background thread; the launcher's frame reads a copy of the state (view()). */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lobby {

enum State { IDLE, HOSTING, JOINING, JOINED, STARTING };
constexpr int kLobbyPort = 7778;      /* the host's lobby socket */
constexpr int kDiscoverPort = 7779;   /* lobby announcements (broadcast) */
constexpr int kGamePort = 7777;       /* the game's own session (SR_NET) */
constexpr int kMaxPlayers = 2;        /* the game's sessions are two players so far */
constexpr int kNameMax = 20;

struct Found {                        /* a lobby announced on the local network */
    std::string host_name, ip;
    int players = 0, max_players = 0;
    bool open = false, same_game = false;
};

struct View {
    State state = IDLE;
    std::vector<std::string> players;  /* in lobby order; the host first */
    int me = 0;                        /* this player's place in players */
    std::string code;                  /* hosting: this lobby's join code */
    std::string host_ip;               /* joined: the host's address (the game joins it) */
    std::string error;                 /* the last problem, for the page */
    std::vector<Found> found;
    bool ready = false;                /* the game file is read (its fingerprint is known) */
};

void init(const std::wstring& game_exe);  /* starts the thread; reads the game's fingerprint */
void shutdown();
void host(const std::string& name);
void join(const std::string& ip, const std::string& name);
void leave();
void start_race();                       /* host: tell everyone, then STARTING */
View view();
void clear_error();
bool code_to_ip(const std::string& code, std::string& ip);
std::string ip_to_code(const std::string& ip);
std::string clean_name(const std::string& s);

}  // namespace lobby
