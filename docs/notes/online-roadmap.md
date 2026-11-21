# Online roadmap: 3-4 players, Super Sonic online rule, quick chat, stage voting, rematches

Status: **plan only** (2026-11-21). Nothing here is implemented. Implementation starts only after the 2-player
Online Beta (LAN + internet invite codes) is stable and the user approves each phase.

## Ground rules for every phase

- **Determinism first.** Anything that changes game memory runs on game steps from data every machine has
  (synced inputs, the host's fixed settings, values sent in WELCOME). Everything else (chat, votes in flight, the
  lobby, pictures) is presentation-only and never touches guest memory.
- **One phase per branch** from `codex/online-beta` (`claude/online-4p`, `claude/online-chat`, ...), each
  merged only after its tests and the user's OK. **Rollback point** = the last merged phase; every phase can be
  turned off by build flag or setting until accepted.
- **NET_COMPAT** goes up with every change to what the game does in a session (protocol, guest memory, rules).
  Old and new builds then refuse each other with the existing "different online version" message.
- **Compatibility kept:** LAN discovery (SRLB v1), internet codes, the PC launcher's lobby, cross-play
  PC / Switch / Android (Switch 2 when its GPU crash is fixed), 2-player sessions exactly as today.
- **Weak devices:** a feature that costs frame time on Switch 1 / phones has an off switch or a light mode; 4
  players is allowed on capable devices even if a weak one cannot run it (the lobby says so).

## Suggested order (and why it differs a little from the user's list)

1. Finish 2P: stale-code keepalive (done on the branch, needs a device check), LAN regression, docs, zips.
2. Switch 2 online GPU crash (frame ~700): it blocks Switch 2 from every later phase.
3. **Super Sonic online rule**: tiny, independent, lowers risk for all later racing tests.
4. **Session events channel** (new, small): one reliable, ordered host->all message stream inside netplay
   (sequence numbers + acks). Chat, votes, ready flags and "player left at frame F" all ride on it. Building it
   once before 3P avoids three ad-hoc protocols. 2P can use it immediately (chat in 2P is a good first test).
5. 3P networking, then 4P (star through the host).
6. Own full-screen view for 3-4P.
7. Lobby for up to 4 + stage voting (voting needs the events channel and the host-driven Track Select).
8. Quick chat UI and reaction bubbles (the transport exists since step 4; this is mostly UI).
9. Rematch / persistent lobby polish, departures without ending the race.
10. Cross-platform performance, stability and compatibility passes (and before each release: cold Sandbox).

## 1. Super Sonic online rules (online only: no held boost, Ring Replenish at half value)

Facts: `runtime/src/boost_hold.c`. A held boost is armed on the boost's first tick (`boost_hold_tick`,
`g_armed[i] = settings_boost_hold() ? held buttons : 0`); Super Sonic's 350 km/h climb, FOV and extra cost only
run while a hold is armed (`super_racer()`: racer character == GAME_CHAR_SUPER_SONIC).

Plan: when the session has the online rules (`g_online_rules` in netplay.c: set for live sessions and for
recordings made with them, identical on every machine), never arm a hold for a Super Sonic racer. His tap boost,
ring magnet, Ring Replenish, flight option, handling and speeds stay exactly as they are; every other character
keeps hold-to-boost. The check is on the racer, not on buttons, so no remapping or alternative input reaches it.
Expose `netplay_online_rules()`; bump NET_COMPAT. Offline code path: unchanged (the new condition is false).

**Ring Replenish at half value online (user, 2026-11-21: "half the value of everything replenish-wise").** All
of Super Sonic's Ring Replenish gains pass two places in `runtime/src/game_levels.c`: `lv_super_rewards()` (every
reward site: grinding, flying, smashed objects, air boxes, plus the trick landing; for ring-powered gear the game
throws these rewards away, so the whole returned amount is the Replenish part) and the spin-section turn
(`SPIN_FUEL`, about 1.45 rings a turn). Online: both halved (integer halving, the same on every machine). The
offline tuning constants stay as they are; ring magnet, caps and the drain are unchanged. Tests: the same
scripted Super Sonic route offline vs online (`SR_BOOSTLOG=1` `[trick]` lines and his ring count each lap):
online gains are half of offline's for grind, flight, objects, tricks and spin turns; checksums equal.

The boost change only decides whether an already-started boost is *extended*; it never touches how a boost starts, so
the tap boost (the game's own code path) cannot be affected. That is checked, not assumed:

Tests (PC pair online, `SR_BOOSTLOG=1`, scripted inputs, both machines' logs compared):
- **Taps**: Super Sonic taps B repeatedly: every tap boosts exactly as offline (same timer 120, same air cost,
  same boost speed), compared frame by frame with an offline run of the same inputs.
- **Held B**: Super Sonic holds B continuously for 10 s: one boost of the normal length, then it ends; no 350 km/h
  climb, no wider camera, no extra cost, and holding B does not start new boosts (the game needs a fresh press);
  releasing and pressing again boosts normally.
- **X as boost and every input method** (pad, keyboard, touch boost button): same results, since the rule looks
  at the racer, not the button.
- Other characters (e.g. Knuckles) holding B in the same race: hold-to-boost still works.
- Checksums equal through a full race; an online recording replays identically; offline Super Sonic still
  climbs to 350 km/h (regression).

## 2. 3 and 4 players

What exists: `MAXP 4` input rings, INPUT packets carry the player number, online menu rules already loop over
`g_ports`, `netplay_view_player()` shows the own half of a 2P split. Limits: one peer address, one
`g_recv_upto` / `g_peer_ack`, one checksum stream, `g_players != 2` refused, lobby `names[2]`.

**Topology: star through the host** (each joiner punches only to the host, so internet codes and the
both-codes fallback keep working per joiner; a full mesh would need N*(N-1)/2 punches and fails more often).
- Joiner -> host: its own inputs + its checksums (as today).
- Host -> each joiner: a bundle per frame range with every player's inputs, sent once the host has all of them,
  plus the host's checksums. Joiners wait only for bundles. Cost: joiner-to-joiner latency = two hops.
- Per-player state on the host: address, acked frame, received-up-to, checksum ring, last-heard time.
- Delay: from the slowest pair: `max over joiners i,j of (rtt_i + rtt_j) / 2` (host to joiner: rtt_i / 2), plus
  2x the worst jitter, as in 2P. Manual - / + stays.
- Checksums: the host compares every joiner against itself; a joiner compares against the host's.
- Handshake: the lobby hands the host a list of approved addresses; WELCOME carries the player number and count;
  the session starts when all have said HELLO (timeout: back to the lobby with a plain message).
- Protocol 6 (bundle message), NET_COMPAT bump. 2P keeps working through the same code (a star with one joiner).

**Benchmark first (no network):** an offline 4-player split race (SR_PADS=4, scripted inputs) on PC, Switch 1
(state mode + clocks), Switch 2, S23: fps, CPU/GPU split. This tells whether 4 views fit on each device. Then 3P.

**Views:** step 1 = the own quarter scaled to the screen (extend the copy-rows trick in `gx_render.c` to rows and
columns; low resolution but cheap). Step 2 = the own quarter laid out full-frame like `ownview_frame` does for 2P
(viewport classes 0-3 instead of top/bottom; HUD moved to the edges). Presentation-only: other quarters' draws
can be skipped on each machine to save GPU (never changes guest memory).

**Departures:** first version: anyone leaving ends the race for all (as now). Later: the host announces on the
events channel "player p is neutral from frame F" with F a little in the future; every machine switches that
player's input to neutral at F (deterministic), the race goes on, the player is dropped at the next menu.

Tests: 3 then 4 PC copies on loopback (two-copy runner extended), a race with all checksums equal; packet loss and
delay injected with a test proxy (`SR_NET_LOSS`, `SR_NET_LAG` on the socket send path, test builds only);
disconnect of a joiner and of the host; desync injection (`SR_NET_TEST_DESYNC`); then PC + OLED + S23 (+ Switch 2).

## 3. Session events channel (supports 4, 5, 7, 8)

`M_EVENT`: host-ordered, numbered, acked, resent; joiners send requests (chat, vote, ready) to the host, the host
re-broadcasts them in order. Small payloads (<= 32 bytes). Two kinds, kept strictly apart:
- **Presentation events** (chat, emoji, votes, ready ticks): drawn by the overlay, never read by game code, never
  written to guest memory. They may arrive at different times on different machines; that is harmless.
- **Game events** (a player becomes neutral, a session rule changes): carry an **effect frame F** chosen by the
  host far enough ahead (> input delay + resend margin, e.g. F = host frame + 2 x delay + 30). Every machine
  applies the event exactly at frame F inside `netplay_latch` (the same place inputs are applied). If a machine
  reaches F without having the event acknowledged, it waits like it waits for missing inputs; it never applies
  it late. Game events are included in the checksummed state's inputs, so a mistake shows up as a desync at once.
Stage voting and rematches need **no** game events: their result reaches the game only as the host's own
generated pad presses, which travel like any other input.
Rate limits are enforced by the host (drops extras), so a modified client cannot spam others.

## 4. Quick chat and reactions

- **Preset messages only**, stored as numbers (message id, emoji id): no free text, no voice, nothing typed.
  Each build shows its own translation of the id; an unknown id is ignored.
- Sets (wheel pages): Greetings & reactions (Hey! / Good luck! / Nice race! / GG! / Well played! / That was
  close! / Wow! / What a finish! / So fast! / Nice trick!), Ready (Ready! / One more race? / Let's go again! /
  Rematch? / Give me a minute! / Be right back!), Leaving (Last race for me! / I have to go! / Thanks for
  playing! / See you next time!), Emoji (😂 😮 🔥 👍 ❤️ 👏 😎 🏁). Emoji are drawn as small textures in the
  Riders Plus overlay style (no system emoji font needed on Switch).
- Input: one button opens a radial/grid wheel (Switch/pads: a free button not used in races, e.g. the right
  stick click or minus; PC: a key + mouse; touch: a small chat button in the corner, tap a choice). Closes on
  choice or after 3 s. Never on START/B/A/X so racing inputs are untouched.
- Display: a bubble next to the sender's name in the lobby; in races a small bubble at the screen edge for 2.5 s
  with the sender's name, never over the centre. Option "Show reactions: on / lobby only / off".
- Spam: 1 message per 2 s per player, at most 5 per 20 s, duplicates within 5 s dropped; enforced by the host.
- Determinism: chat goes on the events channel, is drawn by the overlay only, and never enters guest memory.

## 5. Stage voting (day and night)

Facts: Grand Prix stages 1-16; stage *k* and *k+8* share their song (`docs/notes/music.md`) and are paired
variants of one theme (Metal City 1 / Night Chase 9 [confirmed ids]). Only some pairs are a true day/night
version of the same place (Metal City / Night Chase and Sand Ruins / Dark Desert [likely]); others are different
courses in the same theme [uncertain, to be checked by playing each pair]. The vote screen groups the pairs and
labels a pair "Day / Night" only where that is true; the rest are shown as two normal choices. Current stage: u32 at REL bss + 0x17928. Track Select is menu screen 0x16;
today only the host's pad drives it (`online_rules`).

Plan:
- Voting happens in our overlay while the game sits at Track Select (the game is in lockstep but nothing moves
  there until the host's pad presses). 16 choices in 8 paired cards; pictures are made at
  run time from the player's own disc (the game's track-select thumbnails), never shipped.
- Each player has one vote (changeable until the countdown ends, 20 s); counts or each player's choice shown
  live. Votes travel on the events channel; the host is the only tally.
- Result: most votes wins; ties: the host picks among the tied ones with a random draw and announces the result
  (only the host's choice matters, so no shared random seed is needed). A late or disconnected player's vote is
  simply not counted; duplicates are replaced.
- Applying it: the host's **own pad presses are generated** (cursor moves + A) to pick that stage on Track
  Select. Those presses are the host's inputs, sent to everyone like any input, so every machine loads the same
  stage by construction (no memory poke). Verified by reading the current-stage value on every machine after
  loading (logged; a mismatch would already be a desync).
- Concern: the Track Select cursor layout (grid order, locked stages on a fresh online save) must be read from
  the game's menu work, not assumed; first step is mapping it with `SR_INPUT` runs on PC.

## 6. Rematch and persistent sessions

- The session already continues across races (lockstep never stops between races). After the results, the
  host's pad is the one that matters in the end-of-race menu. Plan: a small overlay "Rematch?" with ready ticks
  per player; when all are ready (or the host presses Start anyway), the host's generated presses choose
  "back to Track Select", where voting starts again.
- Leaving gracefully: "Leave" sends the Leaving preset + a game event (neutral from frame F) and then closes;
  others see "<name> left". First version: leaving during a race ends the race for everyone (documented).
- Later (persistent rooms): dropped players are removed at the next menu; new players cannot join a running
  session (no save states: a joiner's game must start from boot, see docs/ONLINE.md "Joining"); a full room
  restart through the lobby is the way to add someone.

## Technical concerns

- **4 views on weak devices**: unknown until measured; the game runs all 4 viewports' logic on every machine.
- **Star latency**: joiner-to-joiner is two hops; on mobile data this may need 8-12 frames of delay. Rollback
  (ONLINE.md phase 6) is the long-term answer, not in scope here.
- **NAT**: each joiner must reach the host; carrier NATs that change ports for every destination cannot be
  traversed without a relay (seen once in testing; the keepalive fixed a stale-code case, not that one).
- **Menu automation** (voting, rematch) depends on menu layouts and the online save's unlocks; mapped first, then
  automated, with a fallback where the host navigates by hand.
- **Emoji pictures** must be drawn by us (no system emoji on Switch) and fit the overlay style.
- **PC launcher lobby** (launcher/lobby.cpp, SRLB v1, 2 players): needs the same 4-player LIST change or stays 2P.

## Test checklist per phase (minimum)

Protocol tests (`tools/test_online_protocol.py`) extended per phase; PC copies on loopback with checksums through
a full race; packet loss/lag proxy runs; PC + Switch 1 + Android over LAN and over the internet (state mode +
clocks for Switch); offline regression (a normal race and a recording replay byte-identical); LAN regression.
