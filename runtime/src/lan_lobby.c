/* Portable counterpart of the Windows launcher's SRLB v1 lobby. No game code runs here.
 * The render thread polls nonblocking sockets, with bounded work per frame. */
#include "lan_lobby.h"
#include "net_compat.h"
#include "runtime.h"
#include "inet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
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
enum { ANNOUNCE=1, JOIN, LIST, REFUSE, PING, LEAVE, START, FIND, PUNCH, PONG, WAIT }; /* 9-11: internet mode only */
static LanLobby v;
static Sock discovery=BAD, session=BAD;
static struct sockaddr_in host, peer;
static uint64_t tick, heard, joining, starting, search;
static char my_name[LAN_NAME];
static int networking;
static uint32_t subnet_broadcast; /* host order; 0 = unknown */
static _Atomic uint32_t found_ip; /* written once by the lookup thread */
static _Atomic int lookup_started;
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
/* This device's IPv4 address: a connected UDP socket sends nothing; getsockname gives the chosen interface.
 * Consoles may not send 255.255.255.255 at all, so rooms also go to the /24 network's own broadcast address. */
/* Runs on its own thread: a socket call that hangs on some console network layers must never freeze the lobby. */
static int find_local_address(void* unused) {
    (void)unused;
    uint32_t local_ip=0;
    Sock s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s==BAD) return 0;
    struct sockaddr_in to=address(0xC0A80001u,9), me; socklen_t ml=sizeof me; /* any private address works */
    if (!connect(s,(struct sockaddr*)&to,sizeof to) && !getsockname(s,(struct sockaddr*)&me,&ml))
        local_ip=ntohl(me.sin_addr.s_addr);
    closesocket(s);
#if defined(__SWITCH__) && !defined(__SWITCH2__)
    if (!local_ip) local_ip=ntohl((uint32_t)gethostid());
#endif
    if (local_ip && (local_ip>>24)!=127) atomic_store(&found_ip,local_ip);
    return 0;
}
static void use_local_address(void) { /* poll thread: adopt the lookup's result once */
    uint32_t ip=atomic_load(&found_ip);
    if (!ip || v.my_ip[0]) return;
#ifndef __SWITCH2__
    subnet_broadcast=ip|0xFFu; /* Switch 2: sending to it blocks for ~200 ms per packet on hardware */
#endif
    struct in_addr a; a.s_addr=htonl(ip); inet_ntop(AF_INET,&a,v.my_ip,sizeof v.my_ip);
    rt_log("[lobby] this device: %s%s",v.my_ip,subnet_broadcast?", rooms also sent to x.x.x.255":"");
}
static void send_everywhere(Sock s,const void* data,int n,int port) {
    uint32_t to[3]={INADDR_BROADCAST,subnet_broadcast,INADDR_LOOPBACK};
    for (int i=0;i<3;i++) if (to[i]) {
        struct sockaddr_in a=address(to[i],port);
        if (s!=BAD) sendto(s,(const char*)data,n,0,(const struct sockaddr*)&a,sizeof a);
    }
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
/* ---- internet mode: the same states, but packets go through inet.c's one socket (inet_send / inet_recv) ---- */
static int inet_mode, handed;
static uint64_t sid;                       /* random session id from the host, in every internet packet after JOIN */
static struct sockaddr_in pend, punch_to;  /* host: the player waiting for approval; the friend's own code */
static uint64_t pend_heard, punch_since, punch_last, join_window, ping_last, first_contact;
static int join_count;
static struct { uint32_t ip; uint64_t until; } refused[20];
static uint32_t code_ip; /* joiner: the host's address from the code (its port may be rewritten by a router) */
static int rtt_us[16], rtt_n, rtt_i;
static uint64_t rnd(void) {
    static uint64_t x;
    if (!x) x=platform_now_ns()^((uint64_t)(uintptr_t)&x<<21)^0xD1B54A32D192ED03ull;
    x^=x<<13; x^=x>>7; x^=x<<17; return x;
}
static void ssend(Packet p,const struct sockaddr_in* a) {
    if (inet_mode) inet_send(p.b,p.n,a); else send_packet(p,session,a);
}
static void put_sid(Packet* p) { for (int i=0;i<8;i++) p->b[p->n++]=(unsigned char)(sid>>(8*i)); }
static int sid_ok(const unsigned char* b,int n) { /* bytes 6..13 */
    if (n<14) return 0;
    uint64_t s=0; for (int i=0;i<8;i++) s|=(uint64_t)b[6+i]<<(8*i);
    return s==sid;
}

static void close_session(void) {
    if (inet_mode) { if (!handed) inet_close(); }
    else if (session!=BAD) closesocket(session);
    session=BAD;
}
static void fail(const char* error) {
    close_session(); v.state=LAN_IDLE; v.players=0; v.ready=0; v.waiting=v.pending=v.punching=0;
    snprintf(v.error,sizeof v.error,"%s",error);
    rt_log("[lobby] stopped: %.120s",error);
}
static void list(void) {
    Packet p=packet(LIST); p.b[p.n++]=(unsigned char)v.players;
    for (int i=0;i<v.players;i++) string(&p,v.names[i]);
    if (inet_mode) put_sid(&p);
    if (v.players==2) ssend(p,&peer);
}
static void announce(const struct sockaddr_in* to) {
    if (inet_mode) return; /* internet rooms are never broadcast */
    Packet p=packet(ANNOUNCE); compat(&p);
    p.b[p.n++]=(unsigned char)v.players; p.b[p.n++]=2; p.b[p.n++]=(unsigned char)(v.state==LAN_HOSTING && v.players<2);
    string(&p,my_name);
    if (to) send_packet(p,session,to); else send_everywhere(session,p.b,p.n,7779);
}
int lan_lobby_open(void) {
    memset(&v,0,sizeof v); tick=heard=joining=starting=search=0;
    if (!lan_network_init()) { fail("Could not start networking. Connect both devices to the same home network."); return 0; }
    subnet_broadcast=0;
#ifndef __SWITCH2__
    if (!atomic_exchange(&lookup_started,1)) rt_thread_start(find_local_address,NULL);
#endif
    discovery=udp(7779,1);
    if (discovery==BAD) { fail("Could not search for races (UDP 7779). Close another online lobby and try again."); return 0; }
    return 1;
}
static void inet_reset(void) {
    inet_mode=1; handed=0; sid=0; rtt_n=rtt_i=0; join_count=0; join_window=ping_last=first_contact=0;
    v.inet=1; v.inet_ready=0; v.my_code[0]=v.note[0]=0; v.waiting=v.pending=v.punching=0;
    v.rtt_ms=v.jitter_ms=v.samples=0; v.delay_auto=v.delay=6; v.delay_manual=0;
}
void lan_lobby_host(const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    inet_mode=0; v.inet=0;
    session=udp(7778,0);
    if (session==BAD) { fail("Could not host a race (UDP 7778). Close another hosted lobby and try again."); return; }
    v.error[0]=0; v.state=LAN_HOSTING; rt_log("[lobby] hosting as \"%s\" at %s",my_name,v.my_ip[0]?v.my_ip:"(address unknown)"); v.me=0; v.players=1; tick=0;
    snprintf(v.names[0],LAN_NAME,"%s",my_name);
}
void lan_lobby_join(const char* ip,const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    inet_mode=0; v.inet=0;
    host=address(0,7778);
    if (inet_pton(AF_INET,ip,&host.sin_addr)!=1) { fail("That address is not valid. It looks like 10.0.0.114."); return; }
    rt_log("[lobby] joining %s",ip);
    session=udp(0,0); if (session==BAD) { fail("Could not join. Please report it with sonicriders.log from both machines."); return; }
    snprintf(v.host_ip,sizeof v.host_ip,"%s",ip); v.error[0]=0;
    v.state=LAN_JOINING; v.me=1; joining=platform_now_ns(); tick=0;
}
static int inet_start(void) {
    inet_reset();
    if (!inet_open()) { inet_mode=0; v.inet=0; fail("Could not open the game's internet port (UDP 7777 and 7787 are busy). Close other copies of the game."); return 0; }
    return 1;
}
void lan_lobby_host_inet(const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    if (!inet_start()) return;
    v.error[0]=0; v.state=LAN_HOSTING; v.me=0; v.players=1; tick=0;
    snprintf(v.names[0],LAN_NAME,"%s",my_name);
    rt_log("[lobby] hosting an internet race as \"%s\"",my_name);
}
void lan_lobby_join_inet(const char* code,const char* name) {
    if (v.state!=LAN_IDLE) return;
    lan_clean_name(my_name,name); if (!my_name[0]) return;
    uint32_t ip; int port;
    const char* bad=inet_code_decode(code,&ip,&port);
    if (bad) { snprintf(v.error,sizeof v.error,"%s",bad); return; }
    if (!inet_start()) return;
    host=address(ip,port); code_ip=ip;
    inet_ntop(AF_INET,&host.sin_addr,v.host_ip,sizeof v.host_ip);
    v.error[0]=0; v.state=LAN_JOINING; v.me=1; joining=platform_now_ns(); tick=0;
    char masked[20]; inet_mask(ip,masked,sizeof masked);
    rt_log("[lobby] joining an internet race by code (%s, port %d)",masked,port);
}
void lan_lobby_add_code(const char* code) {
    if (!inet_mode || v.state!=LAN_HOSTING) return;
    uint32_t ip; int port;
    const char* bad=inet_code_decode(code,&ip,&port);
    if (bad) { snprintf(v.error,sizeof v.error,"%s",bad); return; }
    punch_to=address(ip,port); punch_since=platform_now_ns(); punch_last=0; v.punching=1; v.error[0]=0;
    rt_log("[lobby] host punching toward the friend's code (port %d)",port);
}
void lan_lobby_answer(int accept) {
    if (!inet_mode || !v.pending || v.state!=LAN_HOSTING) return;
    v.pending=0;
    if (accept && v.players<2) {
        peer=pend; sid=rnd(); heard=platform_now_ns(); v.players=2; v.punching=0;
        snprintf(v.names[1],LAN_NAME,"%s",v.pending_name);
        char ip[48]; inet_mask(ntohl(peer.sin_addr.s_addr),ip,sizeof ip);
        rt_log("[lobby] host accepted %s from %s:%u",v.pending_name,ip,ntohs(peer.sin_port));
        list();
    } else {
        Packet p=packet(REFUSE); string(&p,"The host said no."); for (int i=0;i<3;i++) ssend(p,&pend);
        int k=0; for (int i=1;i<20;i++) if (refused[i].until<refused[k].until) k=i;
        refused[k].ip=pend.sin_addr.s_addr; refused[k].until=platform_now_ns()+60000000000ull;
        rt_log("[lobby] host refused %s",v.pending_name);
    }
}
void lan_lobby_set_delay(int frames) { v.delay_manual=frames<0?0:frames>15?15:frames; v.delay=v.delay_manual?v.delay_manual:v.delay_auto; }
void lan_lobby_handoff(void) {
    if (!inet_mode || handed) return;
    handed=1;
    inet_handoff(v.me==0?&peer:&host);
}
void lan_lobby_leave(void) {
    Packet p=packet(LEAVE); if (inet_mode) put_sid(&p);
    for (int i=0;i<3;i++) {
        if (v.state==LAN_HOSTING && v.players==2) ssend(p,&peer);
        else if (v.state==LAN_JOINING || v.state==LAN_JOINED) ssend(p,&host);
    }
    close_session(); v.state=LAN_IDLE; v.players=v.ready=0; v.error[0]=0; v.waiting=v.pending=v.punching=0;
    if (inet_mode) { inet_mode=0; v.inet=0; }
}
void lan_lobby_close(void) { close_session(); if (discovery!=BAD) closesocket(discovery); discovery=BAD; }
void lan_lobby_start(void) {
    if (v.state!=LAN_HOSTING || v.players!=2) return;
    v.state=LAN_STARTING; starting=platform_now_ns(); tick=0;
    if (inet_mode) rt_log("[lobby] starting: ping %d ms, jitter %d ms (%d samples), input delay %d frames%s",
                          v.rtt_ms,v.jitter_ms,v.samples,v.delay,v.delay_manual?" (chosen by hand)":" (automatic)");
}
static void rtt_add(int us) {
    rtt_us[rtt_i]=us; rtt_i=(rtt_i+1)%16; if (rtt_n<16) rtt_n++;
    int s[16]; memcpy(s,rtt_us,sizeof s);
    for (int i=1;i<rtt_n;i++) for (int j=i;j>0 && s[j]<s[j-1];j--) { int t=s[j]; s[j]=s[j-1]; s[j-1]=t; }
    int med=s[rtt_n/2]; long dev=0; for (int i=0;i<rtt_n;i++) dev+=labs((long)rtt_us[i]-med);
    int jit=(int)(dev/rtt_n);
    v.rtt_ms=(med+500)/1000; v.jitter_ms=(jit+500)/1000; v.samples=rtt_n;
    /* frames the input must wait: half the round trip plus twice the jitter, rounded up, plus one */
    int d=(int)((med/2+2*jit+16666)/16667)+1;
    v.delay_auto=d<2?2:d>15?15:d;
    if (v.state!=LAN_STARTING) v.delay=v.delay_manual?v.delay_manual:v.delay_auto; /* fixed once Start is pressed */
}
static int refused_ip(uint32_t ip,uint64_t now) { for (int i=0;i<20;i++) if (refused[i].ip==ip && refused[i].until>now) return 1; return 0; }
static void inet_packet(const unsigned char* b,int n,const struct sockaddr_in* from,uint64_t now) {
    int t=b[5], pos=6;
    if (!first_contact) first_contact=now;
    if ((t==PING || t==PONG) && n==22 && sid && sid_ok(b,n)) { /* timestamped keepalives (both directions) */
        int ours=v.me==0 ? same(&peer,from) : same(&host,from);
        if (!ours) return;
        heard=now;
        if (t==PING) { Packet p=packet(PONG); memcpy(p.b+6,b+6,16); p.n=22; ssend(p,from); }
        else { uint64_t sent=0; for (int i=0;i<8;i++) sent|=(uint64_t)b[14+i]<<(8*i);
               if (sent<=now && now-sent<5000000000ull) rtt_add((int)((now-sent)/1000)); }
        return;
    }
    if (v.state==LAN_HOSTING || (v.state==LAN_STARTING && v.me==0)) {
        if (t==JOIN && n>=15 && v.state==LAN_HOSTING) {
            if (now-join_window>1000000000ull) { join_window=now; join_count=0; }
            if (++join_count>8) return; /* more than 8 join packets a second: ignore the rest */
            uint64_t c=0; for (int i=0;i<8;i++) c|=(uint64_t)b[6+i]<<(8*i);
            char name[LAN_NAME]; pos=14; if (!read_string(b,n,&pos,name,sizeof name) || !name[0]) return;
            const char* no=NULL;
            if (c!=NET_COMPAT) no="Different online version. Install the same Online Beta on both devices.";
            else if (refused_ip(from->sin_addr.s_addr,now)) no="The host said no.";
            else if (v.players==2) { if (same(&peer,from)) { list(); return; } no="This race is full."; }
            else if (v.pending && !same(&pend,from)) no="The host is answering another player. Try again in a moment.";
            if (no) { Packet p=packet(REFUSE); string(&p,no); ssend(p,from); return; }
            if (!v.pending) {
                char ip[48]; inet_mask(ntohl(from->sin_addr.s_addr),ip,sizeof ip);
                rt_log("[lobby] %s asks to join from %s:%u",name,ip,ntohs(from->sin_port));
                pend=*from; v.pending=1; snprintf(v.pending_name,LAN_NAME,"%s",name);
            }
            pend_heard=now;
            ssend(packet(WAIT),from);
        } else if (v.players==2 && same(&peer,from) && t==LEAVE && sid_ok(b,n)) { v.players=1; sid=0; rt_log("[lobby] the other player left"); }
        return;
    }
    if (v.state!=LAN_JOINING && v.state!=LAN_JOINED) return;
    /* joiner: before the host answers, accept its address with any port (a router may rewrite it); after, exactly */
    int from_host=v.state==LAN_JOINED||sid ? same(&host,from) : from->sin_addr.s_addr==htonl(code_ip);
    if (!from_host) return;
    if (!sid && !same(&host,from)) {
        rt_log("[lobby] the host answers from port %u (code said %u)",ntohs(from->sin_port),ntohs(host.sin_port));
        host=*from;
    }
    heard=now;
    if (t==PUNCH && !sid) { tick=0; } /* the host opened its side: send JOIN right away */
    else if (t==WAIT && !v.waiting) { v.waiting=1; rt_log("[lobby] reached the host; waiting to be let in"); }
    else if (t==LIST && n>=7 && b[6]==2) {
        char names[2][LAN_NAME]; pos=7;
        if (!read_string(b,n,&pos,names[0],LAN_NAME) || !read_string(b,n,&pos,names[1],LAN_NAME) || pos+8!=n) return;
        uint64_t s=0; for (int i=0;i<8;i++) s|=(uint64_t)b[pos+i]<<(8*i);
        if (sid && s!=sid) return;
        if (!sid) rt_log("[lobby] the host let us in");
        sid=s; memcpy(v.names,names,sizeof names); v.players=2; v.state=LAN_JOINED; v.waiting=0;
    } else if (t==REFUSE) {
        char reason[160]; if (read_string(b,n,&pos,reason,sizeof reason)) fail(reason);
    } else if (t==LEAVE && sid_ok(b,n)) fail("The host closed the race.");
    else if (t==START && v.state==LAN_JOINED && n==15 && sid_ok(b,n)) {
        v.delay=b[14]; v.state=LAN_STARTING; v.ready=1;
        rt_log("[lobby] host started: ping %d ms, input delay %d frames",v.rtt_ms,v.delay);
    }
}
static void inet_timers(uint64_t now) {
    const InetInfo* in=inet_info();
    if (in->ready && v.inet_ready && strcmp(v.my_code,in->code)) { /* the network moved our port: a new code */
        snprintf(v.my_code,sizeof v.my_code,"%s",in->code);
        snprintf(v.note,sizeof v.note,"Your code changed because your network moved the game's port. Send the new code.");
    }
    if (in->ready && !v.inet_ready) {
        v.inet_ready=1; snprintf(v.my_code,sizeof v.my_code,"%s",in->code); snprintf(v.note,sizeof v.note,"%s",in->note);
        if (!in->code[0] && v.state==LAN_HOSTING) fail(in->note);
    }
    if (v.state==LAN_HOSTING) {
        if (v.pending && now-pend_heard>6000000000ull) { v.pending=0; rt_log("[lobby] the waiting player stopped asking"); }
        if (v.players==2 && now-heard>6000000000ull) { v.players=1; sid=0; rt_log("[lobby] lost the other player"); }
        if (v.punching && v.players<2) { /* both send at once: 10 a second for 3 s, then 4 a second, up to 60 s */
            uint64_t age=now-punch_since, gap=age<3000000000ull?100000000ull:250000000ull;
            if (age>60000000000ull) { v.punching=0; snprintf(v.error,sizeof v.error,"Could not reach your friend's code. Check it, and ask them to try joining again."); }
            else if (now-punch_last>gap) { punch_last=now; Packet p=packet(PUNCH); compat(&p); ssend(p,&punch_to); }
        }
    } else if (v.state==LAN_JOINING) {
        uint64_t age=now-joining, gap=age<3000000000ull?100000000ull:500000000ull;
        if (now-tick>gap) { tick=now; Packet p=packet(JOIN); compat(&p); string(&p,my_name); ssend(p,&host); }
        if (v.waiting && now-heard>6000000000ull) fail("Lost the host while waiting. Ask them to host again.");
        else if (!v.waiting && age>90000000000ull) /* long enough for the host to add this player's code */
            fail("Could not reach your friend. Try: your friend hosts and you join, or the other way round; both add each other's codes; "
                 "or turn on UPnP on the router. Some mobile and work networks cannot connect directly without a relay server, which this beta doesn't have.");
    } else if (v.state==LAN_JOINED && now-heard>6000000000ull) fail("Lost the host. Please report it with sonicriders.log from both machines.");
    if (v.state==LAN_STARTING && v.me==0) {
        if (now-tick>200000000ull) { tick=now; Packet p=packet(START); put_sid(&p); p.b[p.n++]=(unsigned char)v.delay; ssend(p,&peer); }
        if (now-starting>1500000000ull) v.ready=1;
    }
    int connected=(v.state==LAN_HOSTING && v.players==2) || v.state==LAN_JOINED || (v.state==LAN_STARTING && !v.ready);
    if (connected && sid && now-ping_last>250000000ull) {
        ping_last=now; Packet p=packet(PING); put_sid(&p);
        for (int i=0;i<8;i++) p.b[p.n++]=(unsigned char)(now>>(8*i));
        ssend(p,v.me==0?&peer:&host);
    }
}

const LanLobby* lan_lobby_poll(void) {
    uint64_t now=platform_now_ns();
    use_local_address();
    for (int s=0;s<2;s++) for (int budget=0;budget<32;budget++) {
        unsigned char b[512]; struct sockaddr_in from; socklen_t fl=sizeof from;
        int n;
        if (s==1 && inet_mode) { if (handed) break; n=inet_recv(b,sizeof b,&from); }
        else { Sock sk=s?session:discovery; if (sk==BAD) break; n=(int)recvfrom(sk,(char*)b,sizeof b,0,(struct sockaddr*)&from,&fl); }
        if (n<0) break;
        if (n<6 || memcmp(b,"SRLB",4) || b[4]!=1) continue;
        int pos=6;
        if (s==1 && inet_mode) { inet_packet(b,n,&from,now); continue; }
        if (b[5]==ANNOUNCE && s==0 && n>=18) {
            char name[LAN_NAME]; pos=17; if (!read_string(b,n,&pos,name,sizeof name)) continue;
            char ip[48]; inet_ntop(AF_INET,&from.sin_addr,ip,sizeof ip);
            if (v.state==LAN_HOSTING && !strcmp(name,my_name) && ntohs(from.sin_port)==7778 && (ntohl(from.sin_addr.s_addr)>>24)!=127)
                atomic_store(&found_ip,ntohl(from.sin_addr.s_addr)); /* our own room, heard back */
            int k; for (k=0;k<v.count;k++) if (!strcmp(ip,v.found[k].ip)) break;
            if (k==v.count && v.count==LAN_MAX_FOUND) continue;
            if (k==v.count) { v.count++; rt_log("[lobby] found %s's race at %s",name,ip); }
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
                    if (v.players!=2) { char ip[48]; inet_ntop(AF_INET,&from.sin_addr,ip,sizeof ip); rt_log("[lobby] %s joined from %s",name,ip); }
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
        search=now; Packet p=packet(FIND); send_everywhere(discovery,p.b,p.n,7778);
    }
    if (inet_mode) { if (!handed) inet_timers(now); return &v; }
    if (v.state==LAN_HOSTING) {
        if (v.players==2 && now-heard>6000000000ull) v.players=1;
        if (now-tick>1000000000ull) {
            tick=now; announce(NULL); list();
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
