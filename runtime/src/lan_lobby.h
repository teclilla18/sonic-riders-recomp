/* LAN lobby, SRLB v1, compatible with launcher/lobby.cpp. Call from one thread only.
 * Packets use little-endian integers and length-prefixed strings; names are never account names. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { LAN_IDLE, LAN_HOSTING, LAN_JOINING, LAN_JOINED, LAN_STARTING };
enum { LAN_MAX_FOUND = 16, LAN_NAME = 21, LAN_MAX_PLAYERS = 4,
       LAN_ROOM_LIMIT = 2 };  /* Online Beta 1: rooms admit 2 (3-4 draw every split view, user 2026-11-25); arrays stay 4 */
typedef struct {
    char name[LAN_NAME], ip[48];
    int players, max_players, open, compatible;
    uint64_t seen;
} LanFound;
typedef struct {
    int state, me, players, count, ready;
    char names[LAN_MAX_PLAYERS][LAN_NAME], host_ip[48], my_ip[48], error[192]; /* my_ip: empty when unknown */
    LanFound found[LAN_MAX_FOUND];
    /* internet mode (inet.c): one UDP socket on 7777 for the lobby and the game */
    int inet, inet_ready, waiting, pending, punching;
    char my_code[20], note[200], pending_name[LAN_NAME];
    int rtt_ms, jitter_ms, samples, delay, delay_auto, delay_manual;
} LanLobby;
int lan_network_init(void);  /* once per process, also used by netplay */
int lan_lobby_open(void);
const LanLobby* lan_lobby_poll(void);
void lan_lobby_host(const char* name);
void lan_lobby_join(const char* ip, const char* name);
void lan_lobby_leave(void);
void lan_lobby_start(void);
void lan_lobby_close(void);
void lan_clean_name(char out[LAN_NAME], const char* name);
/* Internet: host/join by invite code; the host approves each player; both may add the other's code. */
void lan_lobby_host_inet(const char* name);
void lan_lobby_join_inet(const char* code, const char* name);
void lan_lobby_add_code(const char* code); /* host: punch toward the friend's own code */
void lan_lobby_answer(int accept);         /* host: the waiting player */
void lan_lobby_set_delay(int frames);      /* host: 0 = automatic from the measured ping */
const char* lan_lobby_local_name(void);
void lan_lobby_handoff(void);  /* when ready: socket and addresses go to netplay (call before close) */
int lan_lobby_take_game(long long* socket, void* peers_sockaddr_in, int* delay);
#ifdef __cplusplus
}
#endif
