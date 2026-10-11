// pad.cpp - SDL2 game controller polling, INI config, logging.
#include "pad.h"

#include <SDL.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace dmc3 {

static HMODULE g_module = nullptr;
void SetModule(HMODULE m) { g_module = m; }

static std::wstring ModuleDir() {
    wchar_t p[MAX_PATH] = {};
    GetModuleFileNameW(g_module, p, MAX_PATH);
    wchar_t* s = wcsrchr(p, L'\\');
    if (s) *s = 0;
    return p;
}

// ----- Config -----

struct Name { const wchar_t* n; int v; };

static const Name kButtonNames[] = {
    {L"None", SRC_NONE},
    {L"A", SDL_CONTROLLER_BUTTON_A},
    {L"B", SDL_CONTROLLER_BUTTON_B},
    {L"X", SDL_CONTROLLER_BUTTON_X},
    {L"Y", SDL_CONTROLLER_BUTTON_Y},
    {L"Back", SDL_CONTROLLER_BUTTON_BACK},
    {L"Guide", SDL_CONTROLLER_BUTTON_GUIDE},
    {L"Start", SDL_CONTROLLER_BUTTON_START},
    {L"LeftStick", SDL_CONTROLLER_BUTTON_LEFTSTICK},
    {L"RightStick", SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    {L"LB", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {L"RB", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {L"DpadUp", SDL_CONTROLLER_BUTTON_DPAD_UP},
    {L"DpadDown", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {L"DpadLeft", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {L"DpadRight", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {L"Misc1", SDL_CONTROLLER_BUTTON_MISC1},
    {L"Paddle1", SDL_CONTROLLER_BUTTON_PADDLE1},
    {L"Paddle2", SDL_CONTROLLER_BUTTON_PADDLE2},
    {L"Paddle3", SDL_CONTROLLER_BUTTON_PADDLE3},
    {L"Paddle4", SDL_CONTROLLER_BUTTON_PADDLE4},
    {L"Touchpad", SDL_CONTROLLER_BUTTON_TOUCHPAD},
    {L"LT", SRC_LT}, {L"LeftTrigger", SRC_LT},
    {L"RT", SRC_RT}, {L"RightTrigger", SRC_RT},
};

static const Name kAxisNames[] = {
    {L"None", AX_NONE},
    {L"LeftX", AX_LX},
    {L"LeftY", AX_LY},
    {L"RightX", AX_RX},
    {L"RightY", AX_RY},
    {L"Triggers", AX_TRIG},
    {L"LeftTrigger", AX_LT},
    {L"RightTrigger", AX_RT},
};

static const wchar_t* kAxisKeys[8] = {L"X", L"Y", L"Z", L"Rx", L"Ry", L"Rz", L"Slider0", L"Slider1"};

template <size_t N>
static int ReadName(const wchar_t* sec, const wchar_t* key, const Name (&tbl)[N], int def,
                    const wchar_t* ini) {
    wchar_t buf[64] = {};
    GetPrivateProfileStringW(sec, key, L"", buf, 64, ini);
    if (!buf[0]) return def;
    for (const Name& e : tbl)
        if (!_wcsicmp(buf, e.n)) return e.v;
    return def;
}

template <size_t N>
static int Lookup(const std::string& s, const Name (&tbl)[N], int def) {
    if (s.empty()) return def;
    const std::wstring w(s.begin(), s.end());   // names are ASCII
    for (const Name& e : tbl)
        if (!_wcsicmp(w.c_str(), e.n)) return e.v;
    return def;
}

// Turn the string defaults in Config into buttonMap/axisMap.
static void ApplyStrings(Config& c) {
    for (int& b : c.buttonMap) b = SRC_NONE;
    for (int& a : c.axisMap) a = AX_NONE;

    const std::string* axes[8] = {&c.X, &c.Y, &c.Z, &c.Rx,
                                  &c.Ry, &c.Rz, &c.Slider0, &c.Slider1};
    for (int i = 0; i < 8; ++i)
        c.axisMap[i] = Lookup(*axes[i], kAxisNames, AX_NONE);

    const std::string* btn[12] = {&c.Button0, &c.Button1, &c.Button2,  &c.Button3,
                                  &c.Button4, &c.Button5, &c.Button6,  &c.Button7,
                                  &c.Button8, &c.Button9, &c.Button10, &c.Button11};
    for (int i = 0; i < 12; ++i)
        c.buttonMap[i] = Lookup(*btn[i], kButtonNames, SRC_NONE);
}

static void ApplyPreset(Config& c, bool ps2) {
    for (int& b : c.buttonMap) b = SRC_NONE;
    for (int& a : c.axisMap) a = AX_NONE;
    if (!ps2) {
        // Same order Windows gives an Xbox 360 pad over DirectInput, plus the two
        // triggers as buttons 10/11 (DirectInput normally merges them onto one axis).
        const int b[] = {SDL_CONTROLLER_BUTTON_A,
                         SDL_CONTROLLER_BUTTON_B,
                         SDL_CONTROLLER_BUTTON_X,
                         SDL_CONTROLLER_BUTTON_Y,
                         SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
                         SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                         SDL_CONTROLLER_BUTTON_BACK,
                         SDL_CONTROLLER_BUTTON_START,
                         SDL_CONTROLLER_BUTTON_LEFTSTICK,
                         SDL_CONTROLLER_BUTTON_RIGHTSTICK,
                         SRC_LT,
                         SRC_RT};
        for (int i = 0; i < 12; ++i) c.buttonMap[i] = b[i];
        c.axisMap[0] = AX_LX;
        c.axisMap[1] = AX_LY;
        c.axisMap[2] = AX_TRIG;
        c.axisMap[3] = AX_RX;
        c.axisMap[4] = AX_RY;
    } else {
        // Typical generic PS2->USB adapter layout (SDL names are Xbox-positional:
        // Cross=A, Circle=B, Square=X, Triangle=Y). Right stick on Z/Rz.
        const int b[] = {SDL_CONTROLLER_BUTTON_Y,
                         SDL_CONTROLLER_BUTTON_B,
                         SDL_CONTROLLER_BUTTON_A,
                         SDL_CONTROLLER_BUTTON_X,
                         SRC_LT,
                         SRC_RT,
                         SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
                         SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                         SDL_CONTROLLER_BUTTON_BACK,
                         SDL_CONTROLLER_BUTTON_START,
                         SDL_CONTROLLER_BUTTON_LEFTSTICK,
                         SDL_CONTROLLER_BUTTON_RIGHTSTICK};
        for (int i = 0; i < 12; ++i) c.buttonMap[i] = b[i];
        c.axisMap[0] = AX_LX;
        c.axisMap[1] = AX_LY;
        c.axisMap[2] = AX_RX;
        c.axisMap[5] = AX_RY;
    }
}

static Config LoadConfig() {   // NOTE: must not call Log() (static-init recursion)
    Config c;
    const std::wstring ini = ModuleDir() + L"\\DMC3Input.ini";
    const wchar_t* f = ini.c_str();

    /*wchar_t preset[32] = {};
    GetPrivateProfileStringW(L"General", L"Preset", L"xbox", preset, 32, f);
    ApplyPreset(c, _wcsicmp(preset, L"ps2") == 0);*/

    ApplyStrings(c);   // defaults come from the strings in pad.h

    wchar_t preset[32] = {};
    GetPrivateProfileStringW(L"General", L"Preset", L"", preset, 32, f);
    if (preset[0]) ApplyPreset(c, _wcsicmp(preset, L"ps2") == 0);

    c.log          = GetPrivateProfileIntW(L"General", L"Log", 0, f) != 0;
    c.hideReal     = GetPrivateProfileIntW(L"General", L"HideRealGamepads", 1, f) != 0;
    c.alwaysExpose = GetPrivateProfileIntW(L"General", L"AlwaysExpose", 1, f) != 0;
    c.padIndex     = (int)GetPrivateProfileIntW(L"General", L"ControllerIndex", 0, f);
    c.deadzone     = std::fmin(0.9f, std::fmax(0.f, GetPrivateProfileIntW(L"General", L"DeadzonePercent", 15, f) / 100.f));
    c.triggerThreshold = std::fmin(1.f, std::fmax(0.05f, GetPrivateProfileIntW(L"General", L"TriggerThresholdPercent", 50, f) / 100.f));
    int n = (int)GetPrivateProfileIntW(L"General", L"ButtonCount", 16, f);
    c.numButtons = n < 1 ? 1 : (n > 32 ? 32 : n);

    for (int i = 0; i < 32; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"Button%d", i);
        c.buttonMap[i] = ReadName(L"Buttons", key, kButtonNames, c.buttonMap[i], f);
    }
    for (int i = 0; i < 8; ++i)
        c.axisMap[i] = ReadName(L"Axes", kAxisKeys[i], kAxisNames, c.axisMap[i], f);
    return c;
}

const Config& GetConfig() {
    static const Config cfg = LoadConfig();
    return cfg;
}

// ----- Logging -----

void Log(const char* fmt, ...) {
    if (!GetConfig().log) return;
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    const std::wstring path = ModuleDir() + L"\\DMC3Input.log";
    FILE* f = _wfopen(path.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ----- SDL -----

static std::mutex            g_mx;
static std::once_flag        g_once;
static bool                  g_ok  = false;
static SDL_GameController*   g_pad = nullptr;
static SDL_JoystickID        g_padId = -1;

static void OpenPad() {
    const int want = GetConfig().padIndex;
    int seen = 0;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        if (seen++ != want) continue;
        SDL_GameController* gc = SDL_GameControllerOpen(i);
        if (gc) {
            g_pad = gc;
            g_padId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
            Log("Opened controller: %s", SDL_GameControllerName(gc));
        } else {
            Log("SDL_GameControllerOpen failed: %s", SDL_GetError());
        }
        return;
    }
}

// Fallback for when SDL's device-added event never arrives (message window
// lives on another thread). Cheap: runs at most once a second, only with no pad.
static void Rescan() {
    if (g_pad) return;
    static DWORD last = 0;
    const DWORD now = GetTickCount();
    if (now - last < 1000) return;
    last = now;
    SDL_JoystickUpdate();   // forces the drivers to re-detect devices
    OpenPad();              // no-op if nothing is found
}

static void PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_CONTROLLERDEVICEADDED) {
            Log("Event: controller added (index %d)", e.cdevice.which);
            if (!g_pad) OpenPad();
        }
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            Log("Event: controller removed (id %d)", (int)e.cdevice.which);
            if (g_pad && e.cdevice.which == g_padId) {
                Log("Controller removed");
                SDL_GameControllerClose(g_pad);
                g_pad = nullptr;
                g_padId = -1;
                OpenPad();
            }
        }
    }
    Rescan();
}

static bool Ensure() {
    std::call_once(g_once, [] {
        // The game owns the window; SDL must not need focus or a video subsystem.
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
            Log("SDL init failed: %s", SDL_GetError());
            return;
        }
        // Optional extra mappings (https://github.com/mdqinc/SDL_GameControllerDB).
        const std::wstring db = ModuleDir() + L"\\gamecontrollerdb.txt";
        char utf8[MAX_PATH * 3] = {};
        WideCharToMultiByte(CP_UTF8, 0, db.c_str(), -1, utf8, sizeof(utf8), nullptr, nullptr);
        int added = SDL_GameControllerAddMappingsFromFile(utf8);
        if (added >= 0) Log("Loaded %d mappings from gamecontrollerdb.txt", added);
        g_ok = true;
        std::lock_guard<std::mutex> lock(g_mx);
        OpenPad();
    });
    return g_ok;
}

bool PadConnected() {
    if (!Ensure()) return false;
    std::lock_guard<std::mutex> lock(g_mx);
    PumpEvents();
    return g_pad != nullptr;
}

static void Stick(float& x, float& y, float dz) {
    const float m = std::sqrt(x * x + y * y);
    if (m < dz) { x = y = 0.f; return; }
    float s = (m - dz) / (1.f - dz);
    if (s > 1.f) s = 1.f;
    x = x / m * s;
    y = y / m * s;
}

void PadPoll(PadState& o) {
    o = PadState{};
    o.pov = -1;
    o.src[AX_LT] = -1.f;
    o.src[AX_RT] = -1.f;
    if (!Ensure()) return;

    std::lock_guard<std::mutex> lock(g_mx);
    PumpEvents();
    if (!g_pad) return;
    SDL_GameControllerUpdate();

    const Config& c = GetConfig();
    auto axis = [&](SDL_GameControllerAxis a) {
        return std::fmax(-1.f, SDL_GameControllerGetAxis(g_pad, a) / 32767.f);
    };
    float lx = axis(SDL_CONTROLLER_AXIS_LEFTX),  ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    float rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
    Stick(lx, ly, c.deadzone);
    Stick(rx, ry, c.deadzone);
    const float lt = std::fmax(0.f, axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    const float rt = std::fmax(0.f, axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT));

    o.src[AX_LX] = lx;  o.src[AX_LY] = ly;
    o.src[AX_RX] = rx;  o.src[AX_RY] = ry;
    o.src[AX_TRIG] = rt - lt;
    o.src[AX_LT] = lt * 2.f - 1.f;
    o.src[AX_RT] = rt * 2.f - 1.f;

    for (int i = 0; i < c.numButtons; ++i) {
        const int s = c.buttonMap[i];
        bool on = false;
        if (s == SRC_LT)      on = lt > c.triggerThreshold;
        else if (s == SRC_RT) on = rt > c.triggerThreshold;
        else if (s >= 0 && s < SDL_CONTROLLER_BUTTON_MAX)
            on = SDL_GameControllerGetButton(g_pad, (SDL_GameControllerButton)s) != 0;
        o.btn[i] = on ? 1 : 0;
    }

    const bool up = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP) != 0;
    const bool dn = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) != 0;
    const bool lf = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) != 0;
    const bool rg = SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) != 0;
    const int dx = (rg ? 1 : 0) - (lf ? 1 : 0);
    const int dy = (dn ? 1 : 0) - (up ? 1 : 0);
    if (dx || dy) {
        float a = std::atan2((float)dx, (float)-dy) * 57.29578f;  // 0 = up, clockwise
        if (a < 0.f) a += 360.f;
        o.pov = (int32_t)std::lround(a * 100.f) % 36000;
    }
}

} // namespace dmc3
