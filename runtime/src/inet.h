/* Internet play without a server we run: one UDP socket for STUN, hole punching, the lobby and the game,
 * a best-effort UPnP port mapping, and numeric invite codes (public IPv4, and the port when it is not 7777).
 * The socket has one owner at a time: the lobby (render thread) until inet_handoff(), then netplay (game thread). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { INET_PORT = 7777, INET_PORT_ALT = 7787 };
enum { INET_UPNP_PENDING, INET_UPNP_MAPPED, INET_UPNP_NONE, INET_UPNP_DOUBLE_NAT };
typedef struct {
    int open, ready;            /* ready: the code is final (STUN answered or failed, UPnP done or timed out) */
    int local_port;             /* 7777, or 7787 when 7777 is taken */
    uint32_t stun_ip;           /* host order, 0 = no answer */
    int stun_port, stun_port2;  /* the mapped port seen by the first and the second STUN server (0 = none) */
    int upnp;                   /* INET_UPNP_* */
    uint32_t upnp_ext_ip; int upnp_ext_port;
    uint32_t pub_ip; int pub_port; /* what the code says */
    char code[20];              /* digits only, empty when unknown */
    char note[200];             /* one plain line about the network, may be empty */
    int changes;                /* times the code changed after it was shown (the network moved the port) */
} InetInfo;

int inet_open(void);                    /* lobby: bind 7777 (or 7787), start the address search; 1 = ok */
const InetInfo* inet_info(void);        /* lobby: call each poll; sends STUN requests and finishes the code */
int inet_recv(unsigned char* b, int cap, void* from_sockaddr_in); /* lobby: one datagram (STUN consumed); <0 none */
void inet_send(const void* b, int n, const void* to_sockaddr_in);
void inet_close(void);                  /* closes the socket exactly once and removes the UPnP mapping */
void inet_handoff(const void* peer_sockaddr_in); /* lobby -> netplay: the socket and the peer's learned address */
int inet_take(long long* sock, void* peer_sockaddr_in); /* netplay: 1 when a handed-over socket exists */
int inet_handed(void);

/* Codes: 10 digits = IPv4 as one number with port 7777; 15 digits = ip * 100000 + port. Spaces/dashes ignored. */
void inet_code_encode(uint32_t ip, int port, char out[20]);
const char* inet_code_decode(const char* code, uint32_t* ip, int* port); /* NULL = ok, else a plain reason */
void inet_code_pretty(const char* code, char* out, int cap); /* 1234-567-890, 15 digits: + " - 12345" */
int inet_public_ip(uint32_t ip);
void inet_mask(uint32_t ip, char* out, int cap); /* a.b.x.x for logs */
#ifdef __cplusplus
}
#endif
