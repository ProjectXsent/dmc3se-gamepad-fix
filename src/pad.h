// pad.h - SDL gamepad backend for the DMC3Input dinput8 proxy.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>

namespace dmc3 {

// Where a virtual DirectInput axis gets its value from.
enum AxisSrc {
    AX_NONE = 0,   // held at centre
    AX_LX, AX_LY,  // left stick
    AX_RX, AX_RY,  // right stick
    AX_TRIG,       // RT - LT combined (what XInput-over-DirectInput does)
    AX_LT, AX_RT,  // individual triggers
    AX_COUNT
};

// Button source values: 0..20 are SDL_GameControllerButton values, plus:
constexpr int SRC_NONE = -1;
constexpr int SRC_LT   = 100;  // left trigger as a digital button
constexpr int SRC_RT   = 101;  // right trigger as a digital button

struct Config {
    bool  log           = false;
    bool  hideReal      = true;   // hide real gamepads from the game while we are active
    bool  alwaysExpose  = true;   // expose the virtual pad even with no controller attached
    int   padIndex      = 0;      // which connected SDL controller to use
    float deadzone      = 0.15f;  // radial stick deadzone, 0..1
    float triggerThreshold = 0.5f;// trigger-as-button threshold, 0..1
    int   numButtons    = 16;     // 1..32
    int   buttonMap[32];          // virtual button -> SDL button / SRC_*
    int   axisMap[8];             // X,Y,Z,Rx,Ry,Rz,Slider0,Slider1 -> AxisSrc
    std::string Preset = "ps2";
    std::string X = "RightY";
    std::string Y = "RightX";
    std::string Z = "LeftY";
    std::string Rx = "LeftX";
    std::string Ry = "None";
    std::string Rz = "None";
    std::string Slider0 = "None";
    std::string Slider1 = "None";

    std::string Button0 = "X";
    std::string Button1 = "Y";
    std::string Button2 = "A";
    std::string Button3 = "B";
    std::string Button4 = "LB";
    std::string Button5 = "RB";
    std::string Button6 = "LT";
    std::string Button7 = "RT";
    std::string Button8 = "LeftStick";
    std::string Button9 = "RightStick";
    std::string Button10 = "Back";
    std::string Button11 = "Start";
};

struct PadState {
    float   src[AX_COUNT];  // normalised -1..1, indexed by AxisSrc
    int32_t pov;            // centidegrees (0 = up, clockwise), -1 = centred
    uint8_t btn[32];        // 0 / 1
};

void SetModule(HMODULE m);     // called from DllMain
const Config& GetConfig();     // loads DMC3Input.ini next to the DLL on first use
bool PadConnected();           // true if an SDL gamepad is currently open
void PadPoll(PadState& out);   // neutral state if nothing is connected
void Log(const char* fmt, ...);

} // namespace dmc3
