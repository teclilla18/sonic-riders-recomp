# Your own music

Every one of the 16 stages can play your own songs instead of (or mixed with) the game's music. No songs come
with the game: add music you own.

Songs can be **MP3, OGG, FLAC, WAV or ADX**. The game converts a song the first time it plays (during the race's
loading screen) and keeps the converted copy, so the next time is instant.

## On a PC: the launcher's Music page

1. Open the launcher and click **Music** on the left.
2. Click the stage you want (for example **01 Metal City**).
3. Click **Add songs...** and choose your files, or drag the files onto the launcher window.
4. Play. The songs play from the next race.

The launcher copies the songs into `mods\My Music\music\01 Metal City\` (one folder per stage), so your original
files stay where they were.

## On a Switch, Android or by hand

Make the folders yourself and copy the songs in:

```
mods/
  My Music/
    music/
      01 Metal City/
        my song.mp3
        another song.ogg
      09 Night Chase/
        night song.flac
```

- On a Switch, `mods` is in the game's folder on the SD card (`/switch/sonicriders/mods/`).
- The stage folder can be the number, the name or both: `01 Metal City`, `1` and `Metal City` all work.
- The stages: 1 Metal City, 2 Splash Canyon, 3 Egg Factory, 4 Green Cave, 5 Sand Ruins, 6 Babylon Garden,
  7 Digital Dimension, 8 Sega Carnival, 9 Night Chase, 10 Red Canyon, 11 Ice Factory, 12 White Cave,
  13 Dark Desert, 14 Sky Road, 15 Babylon Guardian, 16 Sega Illusion.
- Restart the game after adding songs.

## Choosing what plays

In the launcher's Music page, or in the game's settings menu (**Music** tab):

- **Shuffle every race** (on at first): each race picks one of the stage's songs, never the same one twice in a row.
- **Off / Rare / Normal / Often** for each song: how likely it is. **Off** never plays.
- **Original game song**: Off at first. Set it to Rare, Normal or Often to mix the game's own music in.
- Shuffle off: one song plays every time (in the launcher, **Use this** picks it).
- **During a race's loading screen, hold the D-pad** to choose song 1, 2, 3 or 4 for that race.

## Music packs

A music pack is a mod with a `music` folder: put it in `mods/<pack name>/music/<stage>/`. Turn packs on and off on
the Mods page like any other mod. A pack can set its own defaults with a `music.ini` in a stage folder
(`shuffle = on`, `original = rare`, `my song.ogg = often`); your own choices always win.

## Online

Music packs can make two players' games pick different songs and break an online race. Until the song choice is
shared online, turn the music packs off for online races.

## If a song does not play

- Check the file type (MP3, OGG, FLAC, WAV or ADX) and the stage folder's name.
- A song that cannot be converted, or is longer than about 8 minutes, is skipped and set to Off; the game's own
  song plays instead. The log (`sonicriders.log`) has a `[music]` line saying why.
