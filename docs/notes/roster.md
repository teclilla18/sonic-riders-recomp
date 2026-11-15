# Expanded character roster: research notes

Goal (2026-11-14): new characters on their own Player Entry tiles next to the 16 originals, added by
community character mods (anyone can add any character); a mod manager that installs them. Stages on hold.
Research phase only so far: nothing in the game is changed yet. Addresses are the port's (REL at 0x81800000).

## How Tournament Edition (SRTE 2.4.6) does it (studied first-hand, 2026-11-14)

- TE is a **full rebuild**: no `_Main.rel`; one `main.dol` with a single 3.4 MB text section that holds the
  relinked game code (DOL and REL) plus new C++ (libstdc++ `basic_string`, C++20 `std::format`, e.g.
  `SH{}.ADX`). `codes.exo` on the disc is a decoy. Disc id `GXSRTE`.
- Function match of vanilla against TE (dtk symbols; relocation fields and branch targets masked): 3385
  functions identical, ~650 changed, ~720 rewritten or moved inside. TE is also a gameplay rebalance, so
  most changes are not roster changes.
- **Ids:** new characters take ids 17 and up. Old 17-entry tables are guarded (`if (id > 16) id = 0`, e.g. in
  the voice-file loader `fn_1_B8250`), and the new characters' data comes from TE's new code.
- **Player Entry:** tile index = character id (index 11, the hidden id, is skipped). TE rewrote the cursor
  function `fn_1_141720` for 3 rows of 8 (wrap 16 -> 24; row starts 8 / 17); Player Entry `fn_1_13D2F0`
  character loops 17 -> 22/23, gear count 49 -> 61. Grid 24 tiles: 16 originals unchanged, row 3 = new ids.
- **Layout:** to fit 3 rows TE shrank the tiles (~15%) and removed the "Player Entry / A Select / B Back"
  header bar.
- **Files:** gear models split out of the character packs into shared `PB##` (+ `RB4#`); per character new
  `P<l>CO`, `P<l>CB`, `P<l>X`, `P<l>7x`; new characters use letters `3`-`8` (+ `E` reuse); voices
  `10<NAME>.DAT`; `CSPS` / `10CSSV.DAT` new select-screen data. "Ex Load" (L) = costumes, not roster slots.

**TE's source is public** (GPL v3, so study only, no code copied into this MPL project):
github.com/RidersBoulevard/sonicriderste, a shiftable disassembly (`asm/`) plus their C++ (`src/`), built with
devkitPro. Useful files: `include/riders/character.hpp` (character record field names, ids 17-22 = Silver,
Metal Sonic, Emerl, Blaze, Chaos, Tikal; `static_assert(Total < 32)` because the gear table's character
compatibility `Gear::useFlags` is a 32-bit mask), `src/handlers/files/LoadCharacterDATFile.cpp` (voice files
per id, hard-coded arrays), `src/handlers/ingame/customcss.cpp`, `src/gears/tweaks/newgearselect.cpp`,
`src/data/trick_strings/characters/*`. TE's roster is a fixed set compiled in; ours must be data-driven.

## Vanilla facts found

- **Character ids** (order of the 17-entry voice-file table `lbl_1_data_12220`, 0x819E9CE0 J / 0x819E9EBC E,
  17 x 0x1C): 0 Sonic, 1 Tails, 2 Knuckles, 3 Amy, 4 Jet, 5 Storm, 6 Wave, 7 Eggman, 8 Cream, 9 Rouge,
  10 Shadow, 11 and 12 both `10SS.DAT` (11 = the hidden one, likely Super Sonic; 12 likely NiGHTS),
  13 AiAi, 14 Ulala, 15 E-10000G, 16 E-10000R.
- **Character info table** `lbl_1_rodata_3B28` at **0x819C4CC0**, 17 x 0x20 (one per id): +0 type (0 speed,
  1 fly, 2 power), +1 type again [uncertain: second/tag type], +2 flag [uncertain], **+3 file letter**
  (`STKAJMWECRDP012ZO`: 11 = `P` Super Sonic, 12 = `0` NiGHTS, 13 = `1` AiAi, 14 = `2` Ulala, 15 = `Z`
  E-10000R, 16 = `O` E-10000G), +4 flag, +8 float (model scale: Tails 0.8, Knuckles 1.1, Eggman 1.2), +0xC
  float [uncertain], +0x10..+0x1F select-screen stat stars (Dash, Limit, Power, Cornering; 4 bytes each =
  levels). TE's names for the fields: type, statPreset, mirroredModel (+2), model (+3), +4 unknown,
  cameraHeight (+8, not model scale), speedMultiplier (+0xC), dash/limit/power/cornering. Readers: Player Entry `fn_1_13D2F0`, `fn_1_13F7F8`, `fn_1_13FB68`, `fn_1_14008C`, `fn_1_1406B4`,
  `fn_1_1409E0`; race setup `fn_1_3BAC8`, `fn_1_3DED0`; `fn_1_16B9C`, `fn_1_192F44`, `fn_1_1988B4`.
  Three more 0x220-byte data tables (`lbl_1_data_1B0F0` sprite rects; `1CB18`, `1D370` colour ramps) may be
  per-character by coincidence of size only [uncertain].
- **File names:** `P` + letter + gear `00` (+ `M` for CPU / multiplayer), boards/animation `P<letter>O`; built
  byte by byte (no format string). All opens go through the SDK's `DVDConvertPathToEntrynum` =
  **`fn_80012EA8`** (called from `DVDOpen` `fn_8001319C`), mostly from a CRI file-system thread (names on its
  stack around 0x8022C904): the single place where any file name can be redirected.
- **Racer character id:** racer + 0xBA (signed byte; racer array 0x81A0D740, stride 0x1080); + 0xBF holds the
  same value in a race (a second copy; which one Super Sonic changes is to check).
- **Player Entry works on the racer structs directly:** racer[n] + 0xBA is the selection while the screen is up
  (cursor function `fn_1_141720` edits it; grid 2 x 8, index 11 skipped); work + 0x3C + player (work 0x803E2958
  in the PC route) only drives the highlight. Test route: `SR_POKEAT=3520:803E2994:02,3520:81A0D7FA:02` on
  the 1P route picks Knuckles (both bytes; the racer byte alone is the choice). Scripted D-pad / stick
  presses for player 1 do not move this cursor (player 2's do; cause unknown).
- **Id 17 on unmodified vanilla** (poke racer + 0xBA = 0x11): no crash; the info panel is empty (no name,
  stats, portrait) and A does not confirm. The panel looks up per-id resources and finds none.
- **Vanilla vs TE character files:** vanilla has one full pack per character per gear (`PS00`..`PS49`, ~100 KB:
  character + gear) plus a lighter `...M` (CPU / multiplayer) and animation sets `P<l>B/O/P/S`. TE splits the
  character model out (`P<l>CO`, `P<l>CB` bike), keeps a small default gear `P<l>00` and shared gear models
  `PB##`; only gears 70-75 stay as vanilla-format full packs (TE's `PS70` = vanilla's size).
- **Disc loads (SR_DVDLOG, 1P Metal City):** menus `TSGME`/`TSTS`/`MEXPE`; at Player Entry only the highlighted
  character's `P<l>00`; in the race per racer `P<l>00M` + `P<l>O` + `10<NAME>.DAT`.
- **Readers of racer + 0xBA in one 1P race** (load tracer, below): 27 functions. Use classes:
  - table index (must grow for new ids): `fn_1_13570` (x16), `fn_1_336C0` (x4), `fn_1_A7CD0` (x24),
    `fn_1_6EC2C` (x24), `fn_1_19E858` (x51), `fn_1_76D08` (x8), `fn_1_CD40C` (x32), `fn_1_1899C` (88 + id),
    `fn_1_45508` (id x 5 + 3), `fn_1_B8250` (voice table x 0x1C);
  - character special cases (compare): `fn_1_B6710` (== 0 Sonic), `fn_1_95BA0` (== 1 Tails),
    `fn_1_97318` (== 10 Shadow), `fn_1_19EA40` (>= 15 robots);
  - copies into other structs: `fn_1_7696C`, `fn_1_38658` (follow these);
  - Player Entry: `fn_1_13F7F8`, `fn_1_14008C`, `fn_1_1406B4`, `fn_1_1409E0`, `fn_1_140F64`, `fn_1_1414A4`,
    `fn_1_141720`, `fn_1_13F960`, `fn_1_13D2F0`.
  Static count: 172 functions load some byte at + 0xBA (several structs share the offset).

## Injection tests (2026-11-14, PC, local test mod `mods/LetterTest` = Sonic's files renamed `PX..`, never committed)

- **New letter + mod files, no code [confirmed]:** Sonic's record letter poked to `X` (`SR_POKEAT=1000:819C4CC3:58`):
  the game loads `PX00` / `PXO` from the mod (virtual disc "6 added") and the 1P Metal City race runs as
  Sonic. A character's model and animation files are found through the letter alone.
- **Model of one character on another id crashes [confirmed]:** Tails (id 1) with letter `X` (Sonic's files):
  guest read through a wild pointer right after `PXO` loads, inside `fn_1_45508` (called from
  `fn_1_45EC0`), the function that builds per-character animation slots as id x 5 + n. Animation data is
  chosen by id, so a model must be paired with the animation slots of a character with the same skeleton.
  TE does exactly this with a table (`CharacterAnimationFiles`: Silver -> Shadow's, Metal Sonic -> Sonic's,
  Emerl -> Tails', Blaze -> Wave's, Chaos -> Knuckles', Tikal -> Cream's).
- **Select screen:** portraits and names are textures picked by id from shared menu archives (TE sets texture
  ids such as 0xA9 / 0x101 per costume); `10CSSV.DAT` is one sound bank with every character's select voice.
  A new character's portrait / name therefore needs a texture the screen can reference (texture injection).

## Recommended design for Step 3 (approved by the user 2026-11-14: all of points 1-5, layout = 2 x 8 pages)

Page switching (the user left it to us, 2026-11-14):
- **Buttons:** L / R flip to the previous / next page (keyboard and touch use their L / R mappings); moving the
  cursor off the left or right edge of the grid also continues onto the neighbouring page. Check first that
  vanilla Player Entry does not already use L / R (if it does, use the cursor-edge move plus Z).
- **Animation:** the 16 tiles slide sideways as a strip (old page out, new page in from the side pressed),
  about 12 frames (0.2 s) with an ease-out; a small page indicator ("1 / 3" or dots) under the grid. No
  animation when there is only the vanilla page (no character mods installed: the screen is unchanged).
- **Shared page:** the grid shows one page for everyone; any player who has not locked in can flip it.
  Cursors keep their row and column; a player's chosen character stays in their own panel whatever page is
  shown.

1. **Unlimited roster, 8 race slots.** At most 8 racers exist in a race, so the game never needs more than
   8 custom characters at once. The roster (select screen, mod registry) can be any size. When a race loads,
   each chosen custom character is given a race slot id 17..24, and the native code fills those 8 rows of
   every per-character table from the mod's data. The vanilla tables grow by a fixed 8 rows (relocated once
   to the free area above the REL, 0x81AB9A80+), the id stays a byte, and no limit is baked in: not TE's
   32 (gear mask), not 127 (signed byte).
2. **Inherit from a base character.** Each custom character names a base (e.g. `base = Shadow`). Every table
   the mod does not override is copied from the base's row: type, stats preset, camera height, animation
   slots (required: the model must use the base's skeleton, as TE's characters do), voices, gear
   compatibility, trick text. Character-specific special code (`id == 0` Sonic etc.) does not fire for a
   custom id unless a later version adds opt-in behaviours.
3. **Files by letter.** Each race slot gets a free file letter; the slot's `P<l>00`, `P<l>00M`, `P<l>O/B/P/S`
   names come from the mod through the existing virtual disc (proven: no file-loading code needed). Gear
   variants fall back to the default gear file if the mod has none.
4. **Select screen.** Tile index = id. Keep the original 2 x 8 page exactly as it is and add pages of custom
   characters (L / R or a page tab), each 2 x 8 at the original tile size: nothing is shrunk, nothing gets
   cramped, and the number of pages grows with the installed mods. (TE's single 3 x 8 grid shrinks every tile
   and drops the header.) Portrait, name and info-panel art come from the mod's PNGs through texture
   injection in our renderer (the same machinery as Dolphin-format texture packs).
5. **Saves stay vanilla.** Custom ids are always unlocked and never written to the memory card.
6. **Online:** the session handshake carries the roster (mod ids + file hashes); mismatches refuse to join.
7. **Character mod format** (folder or zip): `mod.ini` with `[mod]` (id, name, author, version, `game = GXEE8P`,
   description, thumbnail) and `[character]` (name, base, optional type / stats / camera height / voice /
   gears), `files/` with the model packs, `ui/` with portrait, tile and name PNGs.

Open research for Step 3's first tasks: the remaining per-character tables in other modes (story, tag,
survival, WGP, results, gear shop) via the load tracer; the select-screen draw code (where tile textures and
positions come from, for pages); gear compatibility (`useFlags` in the gear table, gear selector).

## Tools (debug only)

- **Load tracer:** `python configure.py ... --trace-loads` (separate `build/<target>-trace`; move the written
  `build.ninja` aside, e.g. `build/build-trace.ninja`, and run ninja with `-f`). Run with
  `SR_TRACE_LOADS=LO,LEN[,STRIDE]` (hex) and `SR_TRACE_LOADS_FILE`: every 5 s a table of (last function entered,
  its return address, offset, size, count). A load made after a call returned is charged to the callee: the
  real reader is the function (of the two) that has a load at that offset in `build/gen`.
- Local research scripts (not in the repo): vanilla-vs-TE function matcher, side-by-side disassembly, constant
  diff, "which functions use this address", TE in Dolphin with a DSU pad.

## Still to do (research)

- Complete the inventory over other modes (story, tag, survival, WGP, results, records, gear shop) and the
  struct copies; map each table (address, record size, contents).
- Player Entry: portrait / name / type textures and where they come from (`TSGME`?), panel art, unlock bytes
  (0x81AAD3A0 + id, 17 bytes), "taken" bytes (work + 0x20 + id).
- Model / animation / board file naming: how `P<l>00`, `P<l>00M`, `P<l>O` are built (letter source).
- Gear compatibility: gear table character mask (+0x00), gear selector (54 x (flag, character)).
- Test: inject one character pack (local dev test) as id 17.
- Layout options that do not crowd the originals (mock-ups).
