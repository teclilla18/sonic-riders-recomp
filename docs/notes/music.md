# Music packs (music.c, music_convert.c, the Music tab)

Branch `claude/music-loader` (2026-11-20), not released. Players add their own songs; none are shipped.

## Layout and behaviour

- `mods/<pack>/music/<stage>/<songs>` with `<stage>` = a number 1-16 and/or the stage name ("01 Metal City").
  Formats: `.adx`, `.mp3`, `.ogg`, `.flac`, `.wav`. Optional `music.ini` in a stage folder (pack defaults):
  `shuffle = on|off`, `original = off|rare|normal|often`, `<file> = off|rare|normal|often`.
- The player's choices: `music.ini` next to `settings.ini` (`saves/` on PC, the game folder on Switch), sections
  `[Metal City]`, keys `shuffle`, `original`, `<pack>/<file>`; written by the in-game Music tab, read after the packs.
- Pick order: d-pad held by player 1 (song 1-4) > shuffle off (first `often`, else song 1) > weighted shuffle that
  skips the last song. A song that can't be converted falls back to the original and is set to off.

## How it hooks the game [confirmed on PC and Switch 1]

- The game's stage songs are 8 files, `S1.ADX`-`S8.ADX`; stage *k* and *k+8* share `S<k>` (REL table
  `0x805E5380+stage-1` in the original layout: song ids 0-7; name table `0x805E56BC`: S1..S8, SV1). Menu/other music
  is a second name table (S20, S21, S30, S32, S22-S26, S60-S63; which screen each is: not mapped yet).
- `mods.c` turns `S<k>.ADX` into a **music slot** (`music_vfile`): FST size = its largest song (or 48 MB while a song
  is not converted yet; reads past the served file return zeros, the ADX player stops at its sample count).
  `mods_read` calls `music_pick()` when a read starts at offset 0 after another slot or a 2 s gap (a loop back to
  the start while streaming keeps the song).
- **Current stage**: u32 at **REL bss + 0x17928** (= `0x806129A8` in the original game, where bss follows the module;
  in the port the REL runs at 0x81800000, bss at `g_rel_bss`). 1-16 in Grand Prix stages [confirmed: Metal City 1,
  Night Chase 9, Babylon Garden 6 on Switch 1].
- **D-pad**: player 1 controller struct `0x801AF9F0` + 0x8 (held). The highest of bits 0x80/0x40/0x20/0x10 picks song 1-4,
  0x01 the original (the game reads its stage song at `0x80438AF0`).
  All four bits seen on Switch 1 (0080, 0040, 0020, 0010); which physical direction is which: still to note.

## Conversion (music_convert.c)

- dr_mp3 / dr_flac / dr_wav / stb_vorbis -> stereo -> 48 kHz (linear) -> ADX v3 (closed-loop 4-bit ADPCM, 500 Hz
  filter, header 0x330 + "(c)CRI", data at 0x334: the layout of the game's S1.ADX), whole-song loop or the OGG's
  LOOPSTART/LOOPLENGTH/LOOPEND (samples). Cached as `<stage folder>/.converted/<file>.<size>.adx`.
- Checked with ffmpeg as decoder: FLAC source 50 dB SNR, MP3 30 dB (incl. its encoder delay); OGG loop tags carried.
- Converted during the race's loading the first time a song is picked (PC: well under 1 s for 20 s of music);
  Switch conversion time not measured yet.

## Open

- PC launcher Music page (drag and drop, stage picker, preview), the player guide `docs/MUSIC.md`, menu music
  categories (S20...), naming the d-pad directions, Switch timing for MP3 conversion.
- Design mockup the user saw: https://claude.ai/artifact/Xd8GembqJmoXYtM55yVLER (categories, Off/Rare/Normal/Often,
  shuffle, "What plays next", drag and drop, in-game view).
