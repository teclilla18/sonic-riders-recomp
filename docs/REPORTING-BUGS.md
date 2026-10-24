# Reporting bugs

[← Back to the README](../README.md)

Open an issue with the [bug report form](../../../issues/new/choose). The more of these it has, the
faster it gets fixed:

1. **What happened and where:** game mode, stage, character, number of players, and what you did just
   before.
2. **Your platform:** Switch (clocks: overclocked or stock, console model) or PC (graphics card).
3. **The logs:** `sonicriders.log` (this run) and `sonicriders.prev.log` (the run before).
   - Switch: in `switch/sonicriders/` on the SD card.
   - PC: in the install folder.

   If the game crashed or froze, copy them soon: each start moves the last log to
   `sonicriders.prev.log` and deletes the older one. They contain no game data.
4. **A screenshot** for anything visual (wrong colours, missing or see-through objects, garbled text).

It helps to say what it *should* look like: a photo or video of the same spot on a GameCube, or a
Dolphin screenshot.

**Never attach** your disc image, saves, builds (`.nro`, `.exe`) or keys.

## Taking a screenshot

1. When the problem is on screen, open the settings menu: **hold − (Minus)** on the Switch, or
   **hold F1** / the gamepad's Back button on PC.
2. Choose **Save screenshot (A)** and press **A**. The menu closes and the next game frame is saved,
   without the menu in it. (The Switch's own screenshot button often doesn't work for homebrew.)
3. Take as many as you need; each one is a new file.
   - Switch: `switch/sonicriders/shot_<number>.bmp` on the SD card.
   - PC: `build\shot_<number>.png` in the install folder.

   The number is the frame, which also helps match the picture to the log.
4. GitHub doesn't accept `.bmp` files: open the picture in Paint and *Save as* PNG, or put the files
   in a zip. Then drag them into the bug report.
