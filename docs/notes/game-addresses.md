# Known game addresses

**Reference.** Moved from docs/REFERENCE.md on 2026-11-03 (content unchanged). Index: [REFERENCE.md](../REFERENCE.md).

Legend: **[confirmed]** = verified directly; **[likely]** = strong evidence, not proven; **[uncertain]** = guess / needs work.

## Known game addresses

- `8021B8C0` (.sdata, `GAME_TOP_MODE`): 1 = intro / title / attract / menus (Player Entry seen), 8 = races (demo
  and Free Race seen). Found 2026-11-03 by RAM dumps on the phone + a PC menu route; other modes unchecked.
  **Not "racing now":** 8 can stay on in menus (Player Entry in a non-deterministic run; the user's Extras and
  Theater after story mode).
- `8021C1AC` (.sdata, `GAME_MENU_FLAG`): 1 in every menu dumped (title, save select, Player Entry, Normal Race,
  Extras), 0 in races, paused or in the quit dialog; blips to 1 for single frames during a race (found
  2026-11-07, RAM dumps, det route) [likely]. `8021C2FC` looks like a screen id (2 title, 1 Normal Race menu,
  0 Extras, 0x10 race) [uncertain].

Guest addresses as the port sees them (REL linked at 0x81800000; original GameCube REL addresses + 0x01404300).
`fn_`/`f_` names are dtk's. Where they were found: grep the address in HISTORY.md.

| What | Address | Notes |
|---|---|---|
| `main` | 80007A48 | |
| Frame end | `fn_8000610C` | GXCopyDisp 800312F0 → GXDrawDone 80030464 → VISetNextFrameBuffer; draw sync token 0xB00B (GXSetDrawSync 800303B0); retrace-count loop 800061C8 |
| VIWaitForRetrace | 80017038 | hooked by frame_pacing.c |
| Game's 30 fps mode | 8021BB7C (mode, u32), 8021BB80 (toggle), 8021BBF4 (f32 step multiplier = mode+1) | only setter `fn_1_4D310`; scenes use (mode+1) × 8021B8EC |
| Idle loop (SelectThread) | 8000D460 (OSEnableInterrupts) from 80010574 | shows as game-core idle in `[profile]` |
| DSPCheckMailToDSP / DSP handler | 80025E84 / `fn_8002612C` | |
| `__AI_SRC_INIT` | `fn_8001A5CC` | AISCNT 0xCC006C08 timing (boot stall fix) |
| DVDLowRead | `fn_80012408` | |
| memcpy / memset / memmove | 80003540 / 80003458 / 8003A354 | native in hle_misc.c |
| PSMTX/PSVEC routines | 8004C58C-8004D09C | e.g. 8004CF6C PSVECNormalize |
| Projection (widescreen) | `fn_800645DC`, REL text 0x14115C | patches.txt |
| r13 set / GQR clear | 80003334 / 8000A33C | |
| Player count | 8021B8C8 | stage suffix table REL rodata 0x2DA8+8 = "VMM" |
| Scene counter | 8021BB64 | resets per scene (perf test) |
| OS controller type | 80122014 | |
| Gear table | 0x819EA240 | 41 records × 0x1D0; +0x00 character mask, +0x10 top speed mod; level blocks 0x68/0x84/0xA0 (the older "+0x64" is off by 4); gear 18 = Chaos Emerald; speeds × 216 = km/h |
| Player array | 0x81A0D740 | 8 × 0x1080; +0x1034 gear id [uncertain, see Super Sonic notes], +0x1E4 position, level stats +0x8DC/+0x914/+0x94C |
| Camera | 0x81A05D74 | 0x38 per viewport; eye +0x14, target +0x20; view matrix 0x81A0583C; writer not found |
| Save data struct | 81AAD000 (0x6F0 bytes = card file offset 0x1460) | tracks +0x004..+0x013; +0x002 course flag [likely]; gear +0x348+i (bit 0 owned; Chaos Emerald byte 0x81AAD348+18); characters +0x3A0+c (== 1); time records +0x3BA [likely]; +0x6B8 ring total? [uncertain] |
| Gear selector / table | 81999080 / 819D55F8 | 54 × (u16 flag index, s16 character) |
| Super Sonic voice | `f_8188BE1C` | transformation = character Sonic+14 |
| CRI streaming loader | `f_819594F4` | loader thread 81A93B68, consumer 81A93E80 (prio 31), pump 81A93850, "available" counter 81A8FBAC |
| Loading-screen text table | REL bss 0x8CF14 | overwritten by the old IRQ stack bug |
| Speed-blur copy | phys 0x01106980 | 320×240 RGB565 sampled as 512×256 |
| Save icon | `_Main.rel` file 0x1F7660 (palette 0x1F7420) | tools/make_icon.py |
| Card file | `SONICRIDERS_GAMEDATA_01` | game data at file offset 0x1440-0x1B50 |
