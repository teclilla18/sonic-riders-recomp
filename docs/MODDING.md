# Modding Sonic Riders

[← Back to the README](../README.md)

> [!NOTE]
> Modding is new and in development. **File mods** (replacing or adding the game's files, such as
> character models, textures, music and stages) and **stat mods** (Extreme Gear stats, memory writes,
> simple Gecko codes) work in the PC version now (not yet in a release: build from source or wait for
> the next version). New track slots and code mods are planned; see [Roadmap](#roadmap). The Switch version reads the same `mods` folder from the SD card
> but hasn't been tested yet.

- [How mods work](#how-mods-work)
- [Installing a mod](#installing-a-mod)
- [Making a file mod](#making-a-file-mod)
- [Changing stats](#changing-stats)
- [What's on the disc](#whats-on-the-disc)
- [Sharing mods](#sharing-mods)
- [Roadmap](#roadmap)

## How mods work

The game reads everything (models, textures, sounds, stages) from your disc image while it runs.
When it starts, the port builds a **virtual disc**: the files in your `mods` folder replace the disc
files with the same name, or are added as new files. The game then loads them like any other file.
Your disc image is never changed, and turning a mod off is as simple as editing one line.

The game's *code* (`main.dol`, `_Main.rel`) is the exception: it is translated to native code when
you install, so it can't be swapped at runtime. Code mods are on the [roadmap](#roadmap).

## Installing a mod

1. Find the `mods` folder:
   - **PC:** in the install folder (next to `Sonic Riders.lnk`). Create it if it isn't there.
   - **Switch:** `switch/sonicriders/mods/` on the SD card.
2. Put the mod's folder inside it, so that you have `mods/<ModName>/files/...`.
3. Start the game. The mod is turned on and added to `mods/mods.ini`.

**Turning mods on and off, and their order:** `mods/mods.ini` lists your mods, one per line.

```ini
# Sonic Riders mods, applied top to bottom: when two mods replace the same file, the
# lower one wins. Put a # in front of a mod to turn it off. Changes apply on the next start.
SuperSonicSkin
# TrackPackTest
```

Here `SuperSonicSkin` is on and `TrackPackTest` is off. Changes apply the next time you start the game.

You can also turn mods on and off in game: open the settings menu and choose **Mods** (it lists every
mod with On/Off; the order is the order in `mods.ini`). Restart the game to apply the changes.

**Did it work?** The game's log (`sonicriders.log`) lists every mod at the start, and each mod file
the first time the game reads it:

```
[mods] on  SuperSonicSkin: "Super Sonic skin" v1.0 by someone, 62 files
[mods] virtual disc: 62 files replaced, 0 added (5 MB from mods)
[mods] the game reads mods/SuperSonicSkin/files/PS00
```

## Making a file mod

A mod is a folder with an optional description and a `files` folder:

```
mods/
  MyMod/
    mod.ini          name, author, version, description (all optional)
    files/
      PS00           replaces the disc file PS00
      MYNEW.DAT      a new file (the game only reads it if something asks for it)
```

`mod.ini`:

```ini
[mod]
name = My first mod
author = your name
version = 1.0
description = What it changes.
```

Rules:

- File names match the disc's without regard to upper/lower case (`ps00` replaces `PS00`).
- A replacement can be any size, bigger or smaller than the original.
- `_Main.rel` can't be replaced (see [How mods work](#how-mods-work)); the log says so if a mod tries.
- New files can only go in folders the disc already has (the disc keeps all its files in the root).

### Getting the original files

You need the files from your own disc image to edit them. Two ways:

- **Dolphin:** right-click the game → *Properties* → *Filesystem* → right-click the disc →
  *Extract Entire Disc* (or single files).
- **This project's tool** (Python 3, the setup installs one):

  ```
  python tools/disc_files.py list    "<your image>.iso" "PS*"         # names and sizes
  python tools/disc_files.py extract "<your image>.iso" extracted "PS*"
  ```

### Example: character swap

Copy Sonic's files over Tails's, so that whoever picks Tails rides as Sonic:

```
python tools/disc_files.py extract "<your image>.iso" sonic "PS*"
```

Then rename each `PSnn` / `PSnnM` file to `PTnn` / `PTnnM` and put them in `mods/SonicAsTails/files/`.
(This is the test used during development: in a 4-player race, player 2 picked Tails and rode as Sonic.)

## Changing stats

Stat changes go in the mod's `mod.ini`, under sections of their own. A mod can have stats, files, or
both. They are applied when the game starts (Gecko codes every frame), in load order: when two mods
change the same value, the lower one in `mods.ini` wins. `;` starts a comment.

### Extreme Gear stats

One section per gear, by name or number (0–40): `[gear.HighBooster]` or `[gear.1]`.

```ini
[gear.HighBooster]
additive_speed_kmh = 15        ; top speed bonus, km/h as shown in game
level1_boost_speed_kmh = 260   ; boost speed at level 1 (level2_ / level3_ for the others)
level1_max_air = 120000
stat_dash = 2                  ; the stat bars on the gear select screen
```

Gear names: Default, HighBooster, AutoSlider, PowerfulGear, Fastest, TurboStar, SpeedBalancer,
BlueStarII, Access, Beginner, Accelerator, TrapGear, LightBoard, SlideBooster, Legend, MagicCarpet,
AirBroom, Hovercraft, ChaosEmerald, Faster, Gambler, PowerGear, OpaOpa, TheCrazy, Berserker, ERider,
AirTank, HeavyBike, Destroyer, Omnipotence, CoverS, CoverF, CoverP, HangOn, SuperHangOn, Darkness,
Grinder, AdvantageS, AdvantageF, AdvantageP, Cannonball.

| Field | Type | |
|---|---|---|
| `who_can_select` | bit mask (hex) | which characters may ride it |
| `gear_type`, `gear_model` | number | |
| `acceleration` | decimal | |
| `additive_speed` / `additive_speed_kmh` | decimal | top speed bonus (raw / km/h) |
| `offroad_speed`, `speed_handling_multiplier`, `weight` | decimal | |
| `extra_types` | bit mask | |
| `turn_speed_loss`, `turn_low_speed_multiplier`, `turn_acceleration`, `turn_max_radius` | decimal | |
| `drift_max_turn_radius`, `drift_momentum`, `drift_min_radius`, `drift_acceleration` | decimal | |
| `drift_boost_frames` | number | |
| `air_gain_trick`, `air_gain_shortcut`, `air_gain_autorotate`, `jump_air_multiplier` | decimal | air gained |
| `special_flags` | bit mask | |
| `stat_dash`, `stat_limit`, `stat_power`, `stat_cornering` | −128..127 | the bars on the select screen |
| `levelN_max_air`, `levelN_passive_air_drain`, `levelN_drift_air_cost`, `levelN_boost_cost`, `levelN_tornado_cost` | number | N = 1, 2, 3 |
| `levelN_drift_dash_speed` / `…_kmh`, `levelN_boost_speed` / `…_kmh` | decimal | |

The field names and layout come from the Sonic Riders PC modding community's research
([Sewer56.SonicRiders](https://github.com/Sewer56/Sewer56.SonicRiders)); the GameCube table has the
same layout. What each field does in practice is best found by trying.

### Memory writes and Gecko codes

For anything not mapped yet: write values to memory (hex), using the **original GameCube addresses**
(the same ones Dolphin cheat codes use; the port moves addresses inside `_Main.rel` to where it keeps
the module). The value's digit count sets the size: 2 = byte, 4 = halfword, 8 = word.

```ini
[memory]
805E6348 = 00030D40

[gecko]
045E6178 00030D40
```

`[gecko]` takes Gecko / Action Replay code lines and applies them every frame, like a Gecko handler.
Only the simple write codes are supported for now: `00` (byte), `02` (halfword), `04` (word), with
`01`/`03`/`05` for addresses above `0x81000000`. Others (conditions, code injection) are skipped
with a note in the log.

**Did it work?** The log shows `[mods] MyMod: 3 data changes, 1 Gecko codes`, and names any unknown
gear, field or section.

## What's on the disc

All 1,669 files sit in the disc's root folder. What is known so far; corrections and additions are
very welcome (open an issue or a pull request that edits this page):

| Files | What they are |
|---|---|
| `P` + letter + two digits (`PS00`–`PS34`), each with an `…M` file | Per-character data, one set per character letter. **[confirmed]** that the letter is the character for S = Sonic, T = Tails (swapping them swaps the model). [likely] K = Knuckles; the others still need mapping. The two-digit number probably matches the Extreme Gear. |
| `P000`… (digit after `P`) | [uncertain] Shared or generic racer data. |
| A number (`1`, `10`, …) with `M`, `V`, `TE`, `TJ` variants | [likely] Stage data: geometry, models, visuals and English/Japanese text per stage. |
| `S##S#F`, `S##S#J` | [uncertain] |
| `*.ADX` | Music (CRI ADX audio). `E##E.ADX`: [likely] event/cutscene audio. |
| `*.AFS` | CRI sound archives (voices: `VOICE_E.AFS`). |
| `10SONIC.DAT`, `10TAILS.DAT`, … | [uncertain] Per-character data for one mode or stage. |

A good way to find out what a file does: replace it with another file of the same kind (as in the
character-swap example) and see what changes in game.

## Sharing mods

- Share **what you made**: your models, textures, music, or tools and instructions. Avoid uploading
  the game's own files unchanged; they're SEGA's.
- Never share disc images, the port's builds (`.exe`, `.nro`) or console keys.
- Include a `mod.ini` with your name and a version, so players can tell mods apart in the log.

## Roadmap

| Step | What | Status |
|---|---|---|
| 1 | **File mods**: virtual disc, `mods.ini` load order, logging | Works on PC (next release) |
| 2 | **Mods page in the settings menu**: turn mods on and off in game (applies on restart) | Done (next release) |
| 3 | **Stats**: Extreme Gear stats by name in `mod.ini`, raw memory writes, simple Gecko / Action Replay write codes | Done (next release). Next: character stats, more Gecko code types |
| 4 | **Tracks**: replacing a track's files works with step 1; adding *new* track slots needs research into how the game lists its tracks | Research |
| 5 | **Code mods**: C hooks or a modified `main.dol` / `_Main.rel`, applied when the installer translates the game | Later |
| 6 | Switch: the same `mods` folder on the SD card, tested on hardware | After step 2 |
