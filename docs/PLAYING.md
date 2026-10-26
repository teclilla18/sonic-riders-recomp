# Playing

[← Back to the README](../README.md)

## Controls

| GameCube | Switch (Pro Controller, Joy-Con pair, handheld) | PC keyboard | PC gamepad |
|---|---|---|---|
| Control stick | Left stick | W A S D | Left stick |
| C-stick | Right stick | Arrow keys | Right stick |
| A / B / X / Y | A / B / X / Y | J / K / L / I | A / B / X / Y |
| START | + | Enter | Start / Menu |
| L / R | ZL / ZR | U / O | Left / right trigger |
| Z | R | P | Right shoulder |
| D-pad | D-pad | T F G H | D-pad |
| Settings menu | hold − (Minus) | hold F1 | hold Back / View |
| Fullscreen | – | F11 or Alt+Enter | (settings menu) |

HOME exits on the Switch.

## Settings menu

Hold **−** (Minus) for half a second on the Switch, or **F1** / the gamepad's **Back** button on PC.
**L / R** (PC: **Q / E** or the shoulder buttons) switch tabs, the D-pad moves, **A** chooses, **B**
goes back or closes. Selecting an option shows a short explanation. **Close** and **Quit Game**
(asks first) sit at the bottom of every tab. Changes are saved at once.

| Tab | Options |
|---|---|
| Display | Screen: 4:3, 16:9 widescreen or 16:9 stretched. Resolution 1x / 1.5x / 2x (Switch) or fullscreen (PC) |
| Performance | Frame rate: **Auto** (default: 60 fps when possible; when the game is busy it draws fewer frames but keeps its real speed), **30 fps** (a steady 30 at full speed, the game's own mode), **60 fps** (60 while the console keeps up, a steady 30 at full speed in scenes too busy for 60). Frame-rate counter. Switch: Nintendo's 384 MHz handheld graphics setting (on by default, not an overclock) |
| Controls | Switch: the system's controller screen (pair or split Joy-Cons, player order) and the button layout. PC: keyboard and controller layout |
| Gameplay | Unlock the Chaos Emerald gear |
| Mods | Turn installed mods on or off (applies on restart); see [Modding](MODDING.md) |
| About | Version; **Save screenshot** for bug reports (see [Reporting bugs](REPORTING-BUGS.md#taking-a-screenshot)) |

## Multiplayer

Up to 4 players: connect more controllers; each player joins at Player Entry with their own
controller.

- **Switch:** a **single Joy-Con held sideways** works as one controller (SL/SR = L/R, stick click = Z,
  +/− = START; hold it for the settings menu).
- **PC:** extra gamepads become players 2–4.

## Debug options

Diagnostics are turned on with variables: on the Switch, an `env.txt` in `sdmc:/switch/sonicriders/`
with one `NAME=VALUE` per line; on PC, environment variables. For example:

| Variable | Effect |
|---|---|
| `SR_PROFILE=1` | CPU profile in the log every 10 seconds |
| `SR_SHOTS=1` | Periodic screenshots |
| `SR_WATCHDOG=<seconds>` | Dump the game's state after that many seconds |

The full list is in [NOTES.md](../NOTES.md).
