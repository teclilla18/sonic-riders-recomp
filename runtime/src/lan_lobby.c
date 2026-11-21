/* Portable counterpart of the Windows launcher's SRLB v1 lobby. No game code runs here.
 * The render thread polls nonblocking sockets, with bounded work per frame. */
#include "lan_lobby.h"
#include "net_compat.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Sock;
#define BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Sock;
#define BAD (-1)
#define closesocket close
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif
enum { ANNOUNCE=1, JOIN, LIST, REFUSE, PING, LEAVE, START, FIND };
static LanLobby v;
static Sock discovery=BAD, session=BAD;
static struct sockaddr_in host, peer;
static uint64_t tick, heard, joining, starting, search;
static char my_name[LAN_NAME];
static int networking;
int lan_network_init(void) {
    if (networking) return 1;
#ifdef _WIN32
    WSADATA wd;
    if (WSAStartup(MAKEWORD(2,2), &wd)) return 0;
#endif
#ifdef __SWITCH__
    if (R_FAILED(socketInitializeDefault())) return 0;
#endif
    networking=1;
    return 1;
}
void lan_clean_name(char out[LAN_NAME], const char* name) {
    int n=0;
    for (const unsigned char* p=(const unsigned char*)name; *p && n<LAN_NAME-1; p++)
        if (*p>=32 && *p<127) out[n++]=(char)*p;
    while (n && out[n-1]==' ') n--;
    out[n]=0;
}
static struct sockaddr_in address(uint32_t ip, int port) {
    struct sockaddr_in a;
    memset(&a,0,sizeof a); a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(ip); a.sin_port=htons((uint16_t)port);
    return a;
}
static Sock udp(int port, int reuse) {
    Sock s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s==BAD) return s;
    int on=1;
    if (reuse) setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&on,sizeof on);
    setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char*)&on,sizeof on);
#ifdef _WIN32
    u_long nb=1; ioctlsocket(s,FIONBIO,&nb);
    BOOL off=FALSE; DWORD got;
    WSAIoctl(s,(DWORD)_WSAIOW(IOC_VENDOR,12),&off,sizeof off,NULL,0,&got,NULL,NULL);
#else
    fcntl(s,F_SETFL,fcntl(s,F_GETFL,0)|O_NONBLOCK);
#endif
    struct sockaddr_in a=address(INADDR_ANY,port);
    if (bind(s,(struct sockaddr*)&a,sizeof a)) { closesocket(s); return BAD; }
    return s;
}
typedef struct { unsigned char b[256]; int n; } Packet;
static Packet packet(int type) { Packet p={{'S','R','L','B',1,0},6}; p.b[5]=(unsigned char)type; return p; }
static void string(Packet* p,const char* s) {
    size_t n=strlen(s); if (n>63) n=63;
    p->b[p->n++]=(unsigned char)n; memcpy(p->b+p->n,s,n); p->n+=(int)n;
}
static void compat(Packet* p) {
    for (int i=0;i<8;i++) p->b[p->n++]=(unsigned char)((uint64_t)NET_COMPAT>>(i*8));
}
static void send_packet(Packet p,Sock s,const struct sockaddr_in* a) {
    if (s!=BAD) sendto(s,(const char*)p.b,p.n,0,(const struct sockaddr*)a,sizeof *a);
}
static int read_string(const unsigned char* b,int n,int* pos,char* out,int cap) {
    if (*pos>=n) return 0;
    int len=b[(*pos)++]; if (*pos+len>n) return 0;
    int k=len<cap-1?len:cap-1; memcpy(out,b+*pos,(size_t)k); out[k]=0; *pos+=len;
    for (int i=0;i<k;i++) if ((unsigned char)out[i]<32 || (unsigned char)out[i]>126) out[i]='?';
    return 1;
}
static int same(const struct sockaddr_in* a,const struct sockaddr_in* b) {
    return a->sin_addr.s_addr==b->sin_addr.s_addr && a->sin_port==b->sin_port;
}
static void close_session(void) { if (session!=BAD) closesocket(session); session=BAD; }
static void fail(const char* error) {
    close_session(); v.state=LAN_IDLE; v.players=0; v.ready=0;
    snprintf(v.error,sizeof v.error,"%s",error);
}
static void list(void) {
    Packet p=packet(LIST); p.b[p.n++]=(unsigned char)v.players;
    for (int i=0;i<v.players;i++) string(&p,v.names[i]);
    if (v.players==2) send_packet(p,session,&peer);
}
static void announce(const struct sockaddr_in* to) {
    Packet p=packet(ANNOUNCE); compat(&p);
    p.b[p.n++]=(unsigned char)v.players; p.b[p.n++]=2; p.b[p.n++]=(unsigned char)(v.state==LAN_HOSTING && v.players<2);
    string(&p,my_name); send_packet(p,session,to);
}
int lan_lobby_open(void) {
    memset(&v,0,sizeof v); tick=heard=joining=starting=search=0;
    if (!lan_network_init()) { fail("Could not start networking. Connect both devices to the same home network."); return 0; }
    discovery=udp(7779,1);
    if (discovery==BAD) { fail("Could not search for races (UDP 7779). Close another online lobby and try again."); return 0; }
    return 1;
}
void lan_lobby_host(const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    session=udp(7778,0);
    if (session==BAD) { fail("Could not host a race (UDP 7778). Close another hosted lobby and try again."); return; }
    v.error[0]=0; v.state=LAN_HOSTING; v.me=0; v.players=1; tick=0;
    snprintf(v.names[0],LAN_NAME,"%s",my_name);
}
void lan_lobby_join(const char* ip,const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    host=address(0,7778);
    if (inet_pton(AF_INET,ip,&host.sin_addr)!=1) return;
    session=udp(0,0); if (session==BAD) { fail("Could not join. Please report it with sonicriders.log from both machines."); return; }
    snprintf(v.host_ip,sizeof v.host_ip,"%s",ip); v.error[0]=0;
    v.state=LAN_JOINING; v.me=1; joining=platform_now_ns(); tick=0;
}
void lan_lobby_leave(void) {
    Packet p=packet(LEAVE);
    for (int i=0;i<3;i++) {
        if (v.state==LAN_HOSTING && v.players==2) send_packet(p,session,&peer);
        else if (v.state==LAN_JOINING || v.state==LAN_JOINED) send_packet(p,session,&host);
    }
    close_session(); v.state=LAN_IDLE; v.players=v.ready=0; v.error[0]=0;
}
void lan_lobby_close(void) { close_session(); if (discovery!=BAD) closesocket(discovery); discovery=BAD; }
void lan_lobby_start(void) {
    if (v.state!=LAN_HOSTING || v.players!=2) return;
    v.state=LAN_STARTING; starting=platform_now_ns(); tick=0;
}
const LanLobby* lan_lobby_poll(void) {
    uint64_t now=platform_now_ns();
    Sock sockets[2]={discovery,session};
    for (int s=0;s<2;s++) if (sockets[s]!=BAD) for (int budget=0;budget<32;budget++) {
        unsigned char b[512]; struct sockaddr_in from; socklen_t fl=sizeof from;
        int n=(int)recvfrom(sockets[s],(char*)b,sizeof b,0,(struct sockaddr*)&from,&fl);
        if (n<0) break;
        if (n<6 || memcmp(b,"SRLB",4) || b[4]!=1) continue;
        int pos=6;
        if (b[5]==ANNOUNCE && s==0 && n>=18) {
            char name[LAN_NAME]; pos=17; if (!read_string(b,n,&pos,name,sizeof name)) continue;
            char ip[48]; inet_ntop(AF_INET,&from.sin_addr,ip,sizeof ip);
            int k; for (k=0;k<v.count;k++) if (!strcmp(ip,v.found[k].ip)) break;
            if (k==v.count && v.count==LAN_MAX_FOUND) continue;
            if (k==v.count) v.count++;
            LanFound* f=&v.found[k]; snprintf(f->name,sizeof f->name,"%s",name); snprintf(f->ip,sizeof f->ip,"%s",ip);
            uint64_t c=0; for (int i=0;i<8;i++) c|=(uint64_t)b[6+i]<<(8*i);
            f->compatible=c==NET_COMPAT; f->players=b[14]; f->max_players=b[15]; f->open=b[16]!=0; f->seen=now;
        } else if (v.state==LAN_HOSTING && s==1) {
            if (b[5]==FIND) announce(&from);
            else if (b[5]==JOIN && n>=15) {
                uint64_t c=0; for (int i=0;i<8;i++) c|=(uint64_t)b[6+i]<<(8*i);
                char name[LAN_NAME]; pos=14; if (!read_string(b,n,&pos,name,sizeof name) || !name[0]) continue;
                if (c!=NET_COMPAT || (v.players==2 && !same(&peer,&from))) {
                    Packet p=packet(REFUSE); string(&p,c!=NET_COMPAT?"Different online version. Install the same Online Beta on both devices.":"This race is full."); send_packet(p,session,&from);
                } else {
                    peer=from; heard=now; v.players=2; snprintf(v.names[1],LAN_NAME,"%s",name); list();
                }
            } else if (v.players==2 && same(&peer,&from)) {
                if (b[5]==PING) heard=now;
                else if (b[5]==LEAVE) { v.players=1; list(); }
            }
        } else if ((v.state==LAN_JOINING || v.state==LAN_JOINED) && s==1 && same(&host,&from)) {
            if (b[5]==LIST && n>=7 && b[6]==2) {
                char names[2][LAN_NAME]; pos=7;
                if (!read_string(b,n,&pos,names[0],LAN_NAME) || !read_string(b,n,&pos,names[1],LAN_NAME)) continue;
                memcpy(v.names,names,sizeof names); v.players=2; v.state=LAN_JOINED; heard=now;
            } else if (b[5]==REFUSE) {
                char reason[160]; if (read_string(b,n,&pos,reason,sizeof reason)) fail(reason);
            } else if (b[5]==LEAVE) fail("The host closed the race.");
            else if (b[5]==START && v.state==LAN_JOINED) { v.state=LAN_STARTING; v.ready=1; }
        }
    }
    for (int k=0;k<v.count;) {
        if (now-v.found[k].seen>4000000000ull) { v.found[k]=v.found[--v.count]; } else k++;
    }
    if (v.state==LAN_IDLE && now-search>1000000000ull) {
        search=now; Packet p=packet(FIND);
        struct sockaddr_in a=address(INADDR_BROADCAST,7778); send_packet(p,discovery,&a);
        a=address(INADDR_LOOPBACK,7778); send_packet(p,discovery,&a);
    }
    if (v.state==LAN_HOSTING) {
        if (v.players==2 && now-heard>6000000000ull) v.players=1;
        if (now-tick>1000000000ull) {
            tick=now; struct sockaddr_in a=address(INADDR_BROADCAST,7779); announce(&a);
            a=address(INADDR_LOOPBACK,7779); announce(&a); list();
        }
    } else if (v.state==LAN_JOINING && now-joining>6000000000ull)
        fail("No race answered. Check both devices are on the same home network. Please report it with sonicriders.log from both machines.");
    else if (v.state==LAN_JOINED && now-heard>6000000000ull) fail("Lost the host. Please report it with sonicriders.log from both machines.");
    if (v.state==LAN_JOINING && now-tick>500000000ull) {
        tick=now; Packet p=packet(JOIN); compat(&p); string(&p,my_name); send_packet(p,session,&host);
    } else if (v.state==LAN_JOINED && now-tick>1000000000ull) { tick=now; send_packet(packet(PING),session,&host); }
    else if (v.state==LAN_STARTING && v.me==0) {
        if (now-tick>200000000ull) { tick=now; send_packet(packet(START),session,&peer); }
        if (now-starting>1500000000ull) v.ready=1;
    }
    return &v;
}
