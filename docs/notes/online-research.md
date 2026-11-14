# Online play and cross-play: research and recommendation (2026-11-14)

Kind: **Draft** (research and design; nothing here is implemented yet). Current mechanics: [../ONLINE.md](../ONLINE.md).

User's decision (2026-11-14): online play and cross-play (PC, Switch, Android, iOS) are the focus until v1.

## Where we start

On `main` (runtime/src/netplay.c): delay-based lockstep over plain UDP (`SR_NET=host:<port>` /
`join:<addr>:<port>`, delay 4 frames, resent until acknowledged), a rolling 1 MB-per-frame state checksum with a
desync message, the generated online save, the host's camera aspect fixed for the session, and the x86 NaN-sign fix.
Proven: PC-PC on localhost (35,400 frames), a PC recording replayed on the Switch (23,847 frames equal), and a
Switch-host / PC-join LAN session (equal until the NaN difference that was fixed later).

Limits: 2 players, direct IP only (port forwarding over the internet), started from env variables at boot, no menu.

## How other projects connect players

| Project | How players connect | Server |
|---|---|---|
| Dolphin netplay | The host registers with a **traversal server** and gets an 8-character host code. The joiner sends the code; the server asks the host to send a UDP packet to the joiner (hole punch: `ConnectPlease` -> `PleaseSendPacket` -> `ConnectReady` / `ConnectFailed`, Source/Core/Common/TraversalProto.h). Game traffic is peer-to-peer. **No relay**: fails on routers that remap ports; the fallback is direct IP with port forwarding. | stun.dolphin-emu.org, port 6262; small custom UDP protocol |
| Slippi (Dolphin fork, rollback) | Matchmaking service; STUN learns the public mapping and keepalives hold it open; same public IP -> LAN addresses (SlippiMatchmaking.cpp); ENet between peers. **No relay**: a failed connection goes back to the queue. | Their own service |
| Anchor (Ship of Harkinian, 2Ship) | Client-server **relay**: every packet goes to the server with a room id, the server forwards it to the room (optionally one client). Syncs flags and items, not lockstep. | Go; public anchor.hm64.org:43383; self-host with a binary or Docker |
| sm64coopdx (CoopNet) | Signalling / lobby server plus **libjuice** (ICE: STUN, TURN relay) for peer-to-peer. | AGPL-3.0 server |
| Ryujinx LDN, ryu_ldn_nx (Switch sysmodule) | Relay through a master server over TCP by default; P2P only with UPnP or port forwarding. | Port 30456, self-hostable |
| switch-lan-play | Everything relayed over UDP. | Community relay servers |
| Zelda64Recomp, UnleashedRecomp | No official online play (Majora's Mask has an early community sync API). | - |

## What this tells us

- Every mature project runs a **small central server**. The ones without a relay (Dolphin, Slippi) leave some pairs
  of players unable to connect: hole punching works for most home routers (often quoted ~80% of pairs) but not for
  symmetric NAT or mobile carrier-grade NAT. Phones on mobile data are the worst case, so cross-play with
  Android/iOS needs a relay fallback.
- Our traffic is tiny: one input packet per frame (~100 bytes, 60 a second) per player. Relaying a 4-player race
  costs a server roughly 25-50 KB/s; bandwidth caps do not matter.
- Libraries: **libjuice** (MPL-2.0 like this project, no dependencies, ICE with a built-in STUN/TURN server) is the
  best off-the-shelf option, but it needs a port to libnx and SDP-style signalling. ENet adds reliability that
  netplay.c already has. A Dolphin-style custom protocol (a few hundred lines, the same sockets code on all four
  platforms) fits better; libjuice stays the fallback if our punching success rate is poor.
- Free hosting: Fly.io no longer has a free tier for new accounts. Oracle Cloud Always Free (ARM VM, "while
  capacity exists", home region only) is the only generous free VPS; Google Cloud's always-free e2-micro is small but
  enough for a relay. A $3-5 a month VPS is the reliable fallback. Self-hosting must always be possible (Anchor's
  model), and direct IP must keep working without any server.
- Not Steam (Steam Datagram Relay needs Steam; no Switch, Android or iOS). Not Tailscale/ZeroTier (no Switch
  client), though PC players can already use them with today's direct IP.

## The Sonic Riders netplay community (sonicriders.org/netplay, read 2026-11-14)

The Tournament Edition (SRTE) community plays the SRTE mod on a Riders Dolphin fork (v2.0.2) with Dolphin netplay:
the better-connected, lower-ping player hosts; ping under 30 ms good, 50+ poor, some play at 100-200 ms; "pad buffer"
(input delay in frames) about 3 on strong connections and up to 15 on decent ones (over 15 poor, over 20
unplayable); "host input authority" never on (it favours the host); tournaments need steady 60 fps and a buffer
under 15. For us: the same delay-based model works for this community, but the delay must be adjustable (about
3-15, ours is fixed at 4: `SR_NET_DELAY` only) and the lobby should show the ping and suggest a delay. A session
with Dolphin players is not possible (different engine); SRTE itself is a separate mod (porting it would need its
makers' consent, as with DX).

## Facts from our tree that the plan depends on

- The Android manifest has **no INTERNET permission** (only VIBRATE), so sockets fail on Android today.
- A session must start from boot (determinism from the first frame), so the lobby runs **before** the game boots:
  the launcher on PC, a native pre-boot screen on the Switch and phones.
- In lockstep the slowest link sets the input delay. A relay adds one hop (a single region adds roughly 20-80 ms
  one way for far players), so the delay should come from the round trip measured when the session starts.
- Every machine must use the same camera aspect (already fixed by the host, protocol 3).

## Recommendation

**One small, self-hostable rendezvous + relay server, lobby codes, hole punching first, relay fallback, direct IP
always available.** Lockstep stays as it is; only the way packets reach the other machine changes.

- Server (`tools/sr-lobby/`, C, one UDP port, Linux and Windows, a Docker file): rooms with a 6-character code
  (without 0/O/1/I). It sees each member's public address and their reported LAN address, tells members to send
  packets to each other at the same moment (punch), and if no direct packet gets through in ~2 s it forwards that
  room's packets (relay). Memory only, no accounts, no address logs, rate-limited, rooms expire.
- Client (netplay.c): a transport layer under the existing messages: direct, punched or relayed (relayed = the
  same packet plus a room header). An "always relay" option hides a player's IP address from the others.
- 3-4 players: a star through the host (the host forwards inputs and checksums; packets already carry a player
  number). Through the relay it is a star anyway.
- The default server address is a setting and can be changed. The user decides who runs the public server.

## Steps (each tested before the next)

1. This note.
2. Tests of today's build on real networks, no new code: PC-PC over the LAN (laptop), Switch-Switch (two
   consoles), PC-Switch; the 2P race route, a whole race. Proves cross-play determinism on real networks first.
3. Android and iOS in sessions: add the INTERNET permission; replay a PC recording on the phone, then a phone joins
   a PC over the LAN.
4. sr-lobby server and client transport (codes, punching, relay): two copies on one PC through a local server, then
   a hosted server, then a phone on mobile data (forces the relay).
5. Lobby screens before boot: a PC launcher page and an in-game pre-boot screen (Switch software keyboard, phone
   keyboard): Host (shows the code), Join (enter a code), Direct IP. Player docs: ports, firewall, privacy.
6. 3-4 players (host star), input delay from the measured round trip.
7. Later, optional: rollback (ONLINE.md phase 6).

Decisions for the user when step 4 starts: who runs the default public server (Oracle Always Free or a cheap VPS,
or none: self-host and direct IP only); code length and ports (proposal: 6 characters, UDP 7777 for games, 7778
for the server).

## Sources

- Dolphin traversal protocol: github.com/dolphin-emu/dolphin, Source/Core/Common/TraversalProto.h
- Slippi matchmaking: github.com/project-slippi/Ishiiruka, Source/Core/Core/Slippi/SlippiMatchmaking.cpp
- Anchor: github.com/garrettjoecox/anchor
- CoopNet: github.com/coop-deluxe/coopnet ("signaling server and library for ICE using libjuice")
- libjuice: github.com/paullouisageneau/libjuice
- Ryujinx LDN / ryu_ldn_nx: ethiquema.github.io/ryu_ldn_nx
- switch-lan-play: github.com/spacemeowx2/switch-lan-play
- Free-tier hosting: Oracle Cloud Always Free and Fly.io pricing pages and 2026 reviews (checked 2026-11-14)
