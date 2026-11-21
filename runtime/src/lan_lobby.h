/* LAN lobby, SRLB v1, compatible with launcher/lobby.cpp. Call from one thread only.
 * Packets use little-endian integers and length-prefixed strings; names are never account names. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { LAN_IDLE, LAN_HOSTING, LAN_JOINING, LAN_JOINED, LAN_STARTING };
enum { LAN_MAX_FOUND = 16, LAN_NAME = 21 };
typedef struct {
    char name[LAN_NAME], ip[48];
    int players, max_players, open, compatible;
    uint64_t seen;
} LanFound;
typedef struct {
    int state, me, players, count, ready;
    char names[2][LAN_NAME], host_ip[48], my_ip[48], error[192]; /* my_ip: empty when unknown */
    LanFound found[LAN_MAX_FOUND];
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
#ifdef __cplusplus
}
#endif
