# DMC3SE Gamepad Fix

A universal SDL2 gamepad proxy for **Devil May Cry 3: Special Edition** (2006, Retail/Steam PC).

Works with **any SDL2-supported controller** — DualSense, DualShock 4, Xbox pads, 8BitDo, and more — not just XInput devices. Fixes the game's broken native gamepad handling: the shared trigger axis, dead diagonals, and a nasty axis-binding bug that makes the Controls menu unable to tell your stick's vertical and horizontal axes apart.

## Features

- **Works with any pad SDL2 supports**, no XInput wrapper required.
- **Left and Right analog are properly working**.
- **Fully configurable** via a plain-text ini: sensitivity, deadzone, stick range calibration for worn sticks, button remapping, axis inversion, and more — no recompiling needed.
- **Retail/Steam compatible** (DMC3SE.exe v1.3 update is required).

## Issues

- Controller hotplugging isn't implemented yet.
- All buttons are working except for the triggers (LT/RT/L2/R2) for now.

## Installation

1. Download the latest release from GitHub.
2. Extract the files into the folder where the game's exe (`DMC3SE.exe` / `dmc3se.exe`) lives.
3. Rename/move/delete `DMC3SE.ini`.
4. Turn off **Steam Input** for this game (Steam Input remaps your controller before the game ever sees it, which interferes with this proxy).
5. Plug in your controller and launch the game.

### Control Layout

| Action | Binding |
|---|---|
| Move | Left stick — **X, Y** axes |
| Camera | Right stick — **RX, RY** axes |
| Jump / Confirm | **A** / Cross |
| Style / Interact / Cancel | **B** / Circle |
| Gun Attack | **X** / Square |
| Melee Attack | **Y** / Triangle |
| Devil Trigger | **LB** / L1 |
| Lock-On Target | **RB** / R1 |
| Change Devil Arms | **RT** / R2 |
| Change Gun | **LT** / L2 |
| Reset Camera | **RS** / R3 |
| Start / Pause | **Start** |
| Taunt | **Back** / Select |
| Menu navigation | **D-pad** |

Bindings are saved by the game itself (`sav0.sav`), so you only need to do this once per controller.

## Configuration

All settings live in `dmc3se_config.ini`, next to `dinput8.dll`. The file is heavily commented — open it in any text editor (Notepad works fine). Every setting is active out of the box at its default value; just edit the number after `=` for anything you want to change. No ini present = defaults are used.

Highlights:

- **`[Sensitivity]`** — separate deadzone for movement and camera, camera turn speed (as a percentage, 65 default), and `MoveMaxStickRange` / `CameraMaxStickRange` for calibrating a worn stick that never reaches full physical deflection.
- **`[Axes]`** — invert any axis, swap which trigger drives which direction.
- **`[Buttons]`** — remap any face/shoulder/stick-click button to a different in-game slot.
- **`[Spoof]`** — optionally present a fixed controller identity to the game so your bindings persist even when switching between different physical pads.
- **`[General]`** — `EnableLog=1` turns on `dmc3se.log` for troubleshooting; leave off otherwise.

## Building from source

**Requirements:**
- [CMake](https://cmake.org)
- [Microsoft Visual Studio](https://visualstudio.microsoft.com) (2019 and above or use Visual Studio Build Tools)

**On Windows (primary path, matches Pad-Within):**
- Run `build_release_x86.bat`.

**Linux/MinGW (for CI or sandbox verification only):**
```
SDL_ROOT=/path/to/mingw-sdl2 make
```
Either way, for the actual release you still want to ship the official
2.32.8.0 `SDL2.dll` alongside the built `dinput8.dll`.

## Credits

Built with [SDL2](https://www.libsdl.org/). Thanks to the Prince of Persia PC modding community (ThirteenAG, Xidi) for prior art on DirectInput proxying for this game.
Also mrnecromancer247 for the Pad Within source code.
