# Third-party headers

| File | Source | License |
|---|---|---|
| `stb_image.h` (v2.32) | github.com/nothings/stb, commit 9600522de53973336fa033a289858a4354cbb04c | public domain (Unlicense) or MIT, see the end of the file |
| `stb_image_write.h` (v1.18) | same commit | public domain (Unlicense) or MIT |
| `stb_vorbis.c` | github.com/nothings/stb, commit 6b5799bae8a1 (master, 2026-11-20) | public domain (Unlicense) or MIT, see the end of the file |
| `dr_mp3.h` (v0.7.5), `dr_flac.h` (v0.13.5), `dr_wav.h` (v0.14.7) | github.com/mackron/dr_libs, commit e381981afa52 (master, 2026-11-20) | public domain (Unlicense) or MIT-0, see the end of each file |
| `bcdec.h` | github.com/iOrange/bcdec, commit 80859ed3b7af (main, 2026-11-16) | MIT or Unlicense, see the end of the file |

Used by `texpack.c` (PNG and DDS texture packs, `SR_DUMPTEX_DOLPHIN`). Unmodified.

`stb_vorbis.c` and the `dr_*` headers are used by `music_convert.c` (songs in music packs: MP3, OGG, FLAC, WAV
converted to the game's ADX format). Unmodified.
