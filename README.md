# DMC3Input

A `dinput8.dll` proxy for Devil May Cry 3 Special Edition. 

Drop it in the game folder and the game will see one DirectInput joystick 
that is actually an SDL game controller (Xbox, DualShock/DualSense,
Switch Pro, 8BitDo, etc.). Every other DirectInput call is forwarded to the real system DLL.

## Building

Requirements: Visual Studio 2022, CMake, vcpkg (`vcpkg install sdl2:x86-windows`).

Run `build.bat`.

Output in `build86/Release/`: `dinput8.dll`, `SDL2.dll`, `DMC3Input.ini`.
It must be **32-bit** (`-A Win32`).

## Installation

Download the latest release and extract files into the game folder (next to the game exe). 

Deleting `dinput8.dll` will disable/uninstall this fix.

## Mapping

This is already configured so there's no need to edit the buttons in `DMC3Input.ini`
Extra SDL mappings can go in `gamecontrollerdb.txt` next to the DLL.
