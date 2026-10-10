# DMC3Input – SDL controller support for Devil May Cry 3 Special Edition

A `dinput8.dll` proxy. Drop it in the game folder and the game sees one
DirectInput joystick that is actually an SDL game controller (Xbox, DualShock/DualSense,
Switch Pro, 8BitDo, etc.). Every other DirectInput call is forwarded to the real system DLL.

## Build

Requirements: Visual Studio 2022, CMake, vcpkg (`vcpkg install sdl2:x86-windows`).

Run `build.bat`.

Output in `build86/Release/`: `dinput8.dll`, `SDL2.dll`, `DMC3Input.ini`.
It must be **32-bit** (`-A Win32`).

## Install

Copy those three files into the game folder (next to the game exe). Uninstall by deleting
`dinput8.dll`.

## Mapping

The game's default bindings are expressed as DirectInput button *numbers*, so the layout
matters. Two presets are provided (`Preset=xbox` / `ps2`); if buttons land wrong, set
individual `ButtonN=` entries in `[Buttons]`. `[Axes]` lets you move the right stick between
Rx/Ry and Z/Rz. LT/RT are exposed as real buttons, addressing the "transform button has no
free key" complaint. Extra SDL mappings can go in `gamecontrollerdb.txt` next to the DLL.
