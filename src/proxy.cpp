// proxy.cpp - dinput8.dll proxy.
//
// Exports DirectInput8Create, forwards everything to the real system dinput8.dll,
// and additionally presents ONE virtual joystick (backed by an SDL game controller)
// through IDirectInput8::EnumDevices / CreateDevice.
//
// Build as 32-bit (the game is a 32-bit process).

#define DIRECTINPUT_VERSION 0x0800
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dinput.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "pad.h"

using namespace dmc3;

// Identity of the virtual device (arbitrary, just needs to be unique and stable).
// {6F1D2B40-5A37-4C0E-9B53-0E1A4F7C2D91}
static const GUID kGuidInstance = {0x6f1d2b40, 0x5a37, 0x4c0e, {0x9b, 0x53, 0x0e, 0x1a, 0x4f, 0x7c, 0x2d, 0x91}};
// {6F1D2B41-5A37-4C0E-9B53-0E1A4F7C2D91}
static const GUID kGuidProduct  = {0x6f1d2b41, 0x5a37, 0x4c0e, {0x9b, 0x53, 0x0e, 0x1a, 0x4f, 0x7c, 0x2d, 0x91}};

static const DWORD kDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
static const char* kDevName = "DMC3Input SDL Gamepad";

// ------------------------------------------------------------ A/W traits ---

template <bool W> struct Tr;

template <> struct Tr<false> {
    using DI         = IDirectInput8A;
    using Dev        = IDirectInputDevice8A;
    using DevInst    = DIDEVICEINSTANCEA;
    using ObjInst    = DIDEVICEOBJECTINSTANCEA;
    using EnumDevCb  = LPDIENUMDEVICESCALLBACKA;
    using EnumObjCb  = LPDIENUMDEVICEOBJECTSCALLBACKA;
    using EnumEffCb  = LPDIENUMEFFECTSCALLBACKA;
    using EffInfoPtr = LPDIEFFECTINFOA;
    using Str        = LPCSTR;
    using ActFmt     = LPDIACTIONFORMATA;
    using SemCb      = LPDIENUMDEVICESBYSEMANTICSCBA;
    using CfgParams  = LPDICONFIGUREDEVICESPARAMSA;
    using ImgHdr     = LPDIDEVICEIMAGEINFOHEADERA;
    static const IID& IidDI()  { return IID_IDirectInput8A; }
    static const IID& IidDev() { return IID_IDirectInputDevice8A; }
};
template <> struct Tr<true> {
    using DI         = IDirectInput8W;
    using Dev        = IDirectInputDevice8W;
    using DevInst    = DIDEVICEINSTANCEW;
    using ObjInst    = DIDEVICEOBJECTINSTANCEW;
    using EnumDevCb  = LPDIENUMDEVICESCALLBACKW;
    using EnumObjCb  = LPDIENUMDEVICEOBJECTSCALLBACKW;
    using EnumEffCb  = LPDIENUMEFFECTSCALLBACKW;
    using EffInfoPtr = LPDIEFFECTINFOW;
    using Str        = LPCWSTR;
    using ActFmt     = LPDIACTIONFORMATW;
    using SemCb      = LPDIENUMDEVICESBYSEMANTICSCBW;
    using CfgParams  = LPDICONFIGUREDEVICESPARAMSW;
    using ImgHdr     = LPDIDEVICEIMAGEINFOHEADERW;
    static const IID& IidDI()  { return IID_IDirectInput8W; }
    static const IID& IidDev() { return IID_IDirectInputDevice8W; }
};

static void CopyStr(char* dst, size_t n, const char* s) { strncpy_s(dst, n, s, _TRUNCATE); }
static void CopyStr(wchar_t* dst, size_t n, const char* s) {
    size_t i = 0;
    for (; s[i] && i + 1 < n; ++i) dst[i] = (unsigned char)s[i];
    dst[i] = 0;
}

static bool IsGameCtrlType(DWORD devType) {
    switch (GET_DIDEVICE_TYPE(devType)) {
        case DI8DEVTYPE_JOYSTICK: case DI8DEVTYPE_GAMEPAD: case DI8DEVTYPE_DRIVING:
        case DI8DEVTYPE_FLIGHT:   case DI8DEVTYPE_1STPERSON: case DI8DEVTYPE_SUPPLEMENTAL:
            return true;
    }
    return false;
}

// Should the real gamepads be hidden / replaced by the virtual one right now?
static bool TakeOver() {
    const Config& c = GetConfig();
    return c.hideReal && (PadConnected() || c.alwaysExpose);
}

template <class Inst>
static void FillInstance(Inst& i) {
    memset(&i, 0, sizeof(i));
    i.dwSize       = sizeof(i);
    i.guidInstance = kGuidInstance;
    i.guidProduct  = kGuidProduct;
    i.dwDevType    = kDevType;
    i.wUsagePage   = 0x01;  // generic desktop
    i.wUsage       = 0x05;  // game pad
    CopyStr(i.tszInstanceName, MAX_PATH, kDevName);
    CopyStr(i.tszProductName,  MAX_PATH, kDevName);
}

// ----------------------------------------------------- the virtual device ---

template <bool W>
class VirtualDevice final : public Tr<W>::Dev {
    using T = Tr<W>;

    enum Kind { K_NONE, K_AXIS, K_POV, K_BUTTON };
    struct Slot { DWORD ofs; Kind kind; int idx; };   // where to write what in the game's buffer
    struct Obj  { const GUID* guid; DWORD type; DWORD flags; Kind kind; int idx; char name[24]; };

    LONG              ref_ = 1;
    std::vector<Slot> slots_;
    LONG              range_[8][2];   // per-axis [min,max], DirectInput default 0..65535
    bool              loggedState_ = false;

    static bool Eq(const GUID* a, const GUID& b) { return a && IsEqualGUID(*a, b); }

    void DefaultSlots() {
        slots_.clear();
        const DWORD ax[8] = {
            offsetof(DIJOYSTATE, lX),  offsetof(DIJOYSTATE, lY),  offsetof(DIJOYSTATE, lZ),
            offsetof(DIJOYSTATE, lRx), offsetof(DIJOYSTATE, lRy), offsetof(DIJOYSTATE, lRz),
            offsetof(DIJOYSTATE, rglSlider), offsetof(DIJOYSTATE, rglSlider) + 4};
        for (int i = 0; i < 8; ++i) slots_.push_back({ax[i], K_AXIS, i});
        for (int i = 0; i < 4; ++i)  slots_.push_back({(DWORD)offsetof(DIJOYSTATE, rgdwPOV) + 4 * i, K_POV, i});
        for (int i = 0; i < 32; ++i) slots_.push_back({(DWORD)offsetof(DIJOYSTATE, rgbButtons) + i, K_BUTTON, i});
    }

    DWORD OfsOf(Kind k, int idx) const {
        for (const Slot& s : slots_) if (s.kind == k && s.idx == idx) return s.ofs;
        return 0;
    }

    std::vector<Obj> Objects() const {
        static const GUID* axGuid[6] = {&GUID_XAxis, &GUID_YAxis, &GUID_ZAxis, &GUID_RxAxis, &GUID_RyAxis, &GUID_RzAxis};
        static const char* axName[6] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation"};
        std::vector<Obj> v;
        for (int i = 0; i < 6; ++i) {
            Obj o{axGuid[i], DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i), DIDOI_ASPECTPOSITION, K_AXIS, i, {}};
            strcpy_s(o.name, axName[i]);
            v.push_back(o);
        }
        Obj p{&GUID_POV, DIDFT_POV | DIDFT_MAKEINSTANCE(0), 0, K_POV, 0, {}};
        strcpy_s(p.name, "Hat Switch");
        v.push_back(p);
        const int nb = GetConfig().numButtons;
        for (int i = 0; i < nb; ++i) {
            Obj b{&GUID_Button, DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(i), 0, K_BUTTON, i, {}};
            sprintf_s(b.name, "Button %d", i);
            v.push_back(b);
        }
        return v;
    }

    void FillObj(typename T::ObjInst& d, const Obj& o) const {
        memset(&d, 0, sizeof(d));
        d.dwSize   = sizeof(d);
        d.guidType = *o.guid;
        d.dwOfs    = OfsOf(o.kind, o.idx);
        d.dwType   = o.type;
        d.dwFlags  = o.flags;
        CopyStr(d.tszName, MAX_PATH, o.name);
    }

    // Which of our 8 axes does a DIPROPHEADER address? (bitmask)
    unsigned AxesFor(const DIPROPHEADER* h) const {
        if (h->dwHow == DIPH_DEVICE) return 0xFFu;
        if (h->dwHow == DIPH_BYOFFSET) {
            for (const Slot& s : slots_)
                if (s.kind == K_AXIS && s.ofs == h->dwObj) return 1u << s.idx;
            return 0;
        }
        if (h->dwHow == DIPH_BYID) {
            if ((DIDFT_GETTYPE(h->dwObj) & DIDFT_AXIS) && DIDFT_GETINSTANCE(h->dwObj) < 8)
                return 1u << DIDFT_GETINSTANCE(h->dwObj);
        }
        return 0;
    }

    LONG AxisValue(int axis, const PadState& s) const {
        float v = s.src[GetConfig().axisMap[axis]];
        v = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
        const float lo = (float)range_[axis][0], hi = (float)range_[axis][1];
        return (LONG)std::lround(lo + (v + 1.f) * 0.5f * (hi - lo));
    }

public:
    VirtualDevice() {
        for (auto& r : range_) { r[0] = 0; r[1] = 65535; }
        DefaultSlots();
        Log("VirtualDevice created (%s)", W ? "W" : "A");
    }

    // --- IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, T::IidDev())) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return (ULONG)r;
    }

    // --- the parts that matter
    HRESULT STDMETHODCALLTYPE GetCapabilities(LPDIDEVCAPS caps) override {
        if (!caps || caps->dwSize < sizeof(DIDEVCAPS_DX3) || caps->dwSize > sizeof(DIDEVCAPS))
            return DIERR_INVALIDPARAM;
        const DWORD sz = caps->dwSize;
        memset(caps, 0, sz);
        caps->dwSize    = sz;
        caps->dwFlags   = DIDC_ATTACHED;
        caps->dwDevType = kDevType;
        caps->dwAxes    = 6;
        caps->dwButtons = (DWORD)GetConfig().numButtons;
        caps->dwPOVs    = 1;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE EnumObjects(typename T::EnumObjCb cb, LPVOID ref, DWORD flags) override {
        if (!cb) return DIERR_INVALIDPARAM;
        Log("EnumObjects flags=0x%lx", flags);
        for (const Obj& o : Objects()) {
            if (flags != DIDFT_ALL && !(flags & o.type & 0xFF)) continue;
            typename T::ObjInst d;
            FillObj(d, o);
            if (cb(&d, ref) == DIENUM_STOP) break;
        }
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetObjectInfo(typename T::ObjInst* p, DWORD obj, DWORD how) override {
        if (!p || p->dwSize < sizeof(DIDEVICEOBJECTINSTANCE_DX3A)) return DIERR_INVALIDPARAM;
        for (const Obj& o : Objects()) {
            const bool hit = (how == DIPH_BYOFFSET) ? (OfsOf(o.kind, o.idx) == obj)
                           : (how == DIPH_BYID)     ? ((o.type & 0x00FFFFFF) == (obj & 0x00FFFFFF))
                           : false;
            if (!hit) continue;
            typename T::ObjInst d;
            FillObj(d, o);
            const DWORD n = p->dwSize < sizeof(d) ? p->dwSize : (DWORD)sizeof(d);
            const DWORD keep = p->dwSize;
            memcpy(p, &d, n);
            p->dwSize = keep;
            return DI_OK;
        }
        return DIERR_OBJECTNOTFOUND;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceInfo(typename T::DevInst* p) override {
        if (!p || p->dwSize < sizeof(DIDEVICEINSTANCE_DX3A)) return DIERR_INVALIDPARAM;
        typename T::DevInst d;
        FillInstance(d);
        const DWORD keep = p->dwSize;
        memcpy(p, &d, keep < sizeof(d) ? keep : sizeof(d));
        p->dwSize = keep;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE SetDataFormat(LPCDIDATAFORMAT df) override {
        if (!df || !df->rgodf) return DIERR_INVALIDPARAM;
        Log("SetDataFormat dataSize=%lu numObjs=%lu flags=0x%lx", df->dwDataSize, df->dwNumObjs, df->dwFlags);
        slots_.clear();
        int nBtn = 0, nPov = 0, nSlider = 0;
        for (DWORD i = 0; i < df->dwNumObjs; ++i) {
            const DIOBJECTDATAFORMAT& o = df->rgodf[i];
            Slot s{o.dwOfs, K_NONE, 0};
            const GUID* g = o.pguid;
            if      (Eq(g, GUID_XAxis))  { s.kind = K_AXIS; s.idx = 0; }
            else if (Eq(g, GUID_YAxis))  { s.kind = K_AXIS; s.idx = 1; }
            else if (Eq(g, GUID_ZAxis))  { s.kind = K_AXIS; s.idx = 2; }
            else if (Eq(g, GUID_RxAxis)) { s.kind = K_AXIS; s.idx = 3; }
            else if (Eq(g, GUID_RyAxis)) { s.kind = K_AXIS; s.idx = 4; }
            else if (Eq(g, GUID_RzAxis)) { s.kind = K_AXIS; s.idx = 5; }
            else if (Eq(g, GUID_Slider)) { if (nSlider < 2) { s.kind = K_AXIS; s.idx = 6 + nSlider; } ++nSlider; }
            else if (Eq(g, GUID_POV))    { s.kind = K_POV; s.idx = nPov++; }
            else if (Eq(g, GUID_Button) || (!g && (o.dwType & DIDFT_BUTTON))) {
                const DWORD inst = DIDFT_GETINSTANCE(o.dwType);
                s.kind = K_BUTTON;
                s.idx  = (inst != 0xFFFF && !g) ? (int)inst : nBtn;
                ++nBtn;
            }
            slots_.push_back(s);
        }
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceState(DWORD cb, LPVOID data) override {
        if (!data) return E_POINTER;
        PadState s;
        PadPoll(s);
        if (!loggedState_) { loggedState_ = true; Log("First GetDeviceState cb=%lu", cb); }

        memset(data, 0, cb);
        uint8_t* base = static_cast<uint8_t*>(data);
        for (const Slot& sl : slots_) {
            switch (sl.kind) {
            case K_AXIS:
                if (sl.ofs + 4 <= cb) *reinterpret_cast<LONG*>(base + sl.ofs) = AxisValue(sl.idx, s);
                break;
            case K_POV:
                if (sl.ofs + 4 <= cb)
                    *reinterpret_cast<DWORD*>(base + sl.ofs) = (sl.idx == 0) ? (DWORD)s.pov : 0xFFFFFFFFu;
                break;
            case K_BUTTON:
                if (sl.ofs + 1 <= cb) base[sl.ofs] = (sl.idx < 32 && s.btn[sl.idx]) ? 0x80 : 0x00;
                break;
            default: break;
            }
        }
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceData(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD n, DWORD) override {
        // Buffered mode is not supported; report "no events". Check the log if the game uses it.
        static bool once = false;
        if (!once) { once = true; Log("GetDeviceData called (buffered input NOT implemented)"); }
        if (n) *n = 0;
        return DI_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(REFGUID g, LPDIPROPHEADER h) override {
        if (!h || h->dwSize < sizeof(DIPROPHEADER)) return DIERR_INVALIDPARAM;
        if (&g == &DIPROP_RANGE) {
            if (h->dwSize < sizeof(DIPROPRANGE)) return DIERR_INVALIDPARAM;
            const unsigned m = AxesFor(h);
            if (!m) return DIERR_OBJECTNOTFOUND;
            int a = 0; while (!(m & (1u << a))) ++a;
            auto* r = reinterpret_cast<DIPROPRANGE*>(h);
            r->lMin = range_[a][0];
            r->lMax = range_[a][1];
            return DI_OK;
        }
        if (h->dwSize < sizeof(DIPROPDWORD)) return DIERR_INVALIDPARAM;
        auto* d = reinterpret_cast<DIPROPDWORD*>(h);
        if (&g == &DIPROP_DEADZONE || &g == &DIPROP_AXISMODE || &g == &DIPROP_BUFFERSIZE ||
            &g == &DIPROP_JOYSTICKID) { d->dwData = 0; return DI_OK; }   // 0 == DIPROPAXISMODE_ABS
        if (&g == &DIPROP_SATURATION)  { d->dwData = 10000; return DI_OK; }
        if (&g == &DIPROP_GRANULARITY) { d->dwData = 1; return DI_OK; }
        return DIERR_UNSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE SetProperty(REFGUID g, LPCDIPROPHEADER h) override {
        if (!h || h->dwSize < sizeof(DIPROPHEADER)) return DIERR_INVALIDPARAM;
        if (&g == &DIPROP_RANGE) {
            if (h->dwSize < sizeof(DIPROPRANGE)) return DIERR_INVALIDPARAM;
            auto* r = reinterpret_cast<const DIPROPRANGE*>(h);
            const unsigned m = AxesFor(h);
            Log("SetProperty RANGE [%ld,%ld] mask=0x%x", r->lMin, r->lMax, m);
            for (int a = 0; a < 8; ++a)
                if (m & (1u << a)) { range_[a][0] = r->lMin; range_[a][1] = r->lMax; }
            return DI_OK;
        }
        // Accepted and ignored: we apply our own deadzone, and delivery is always absolute/unbuffered.
        if (&g == &DIPROP_DEADZONE || &g == &DIPROP_SATURATION || &g == &DIPROP_AXISMODE ||
            &g == &DIPROP_BUFFERSIZE || &g == &DIPROP_FFGAIN || &g == &DIPROP_AUTOCENTER)
            return DI_OK;
        Log("SetProperty unsupported prop");
        return DIERR_UNSUPPORTED;
    }

    // --- trivial
    HRESULT STDMETHODCALLTYPE Acquire() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Unacquire() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Poll() override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE SetEventNotification(HANDLE) override { return DI_POLLEDDEVICE; }
    HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }

    // --- force feedback / effects / action maps: not supported
    HRESULT STDMETHODCALLTYPE CreateEffect(REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumEffects(typename T::EnumEffCb, LPVOID, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE GetEffectInfo(typename T::EffInfoPtr, REFGUID) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetForceFeedbackState(LPDWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SendForceFeedbackCommand(DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumCreatedEffectObjects(LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) override { return DI_OK; }
    HRESULT STDMETHODCALLTYPE Escape(LPDIEFFESCAPE) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SendDeviceData(DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumEffectsInFile(typename T::Str, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE WriteEffectToFile(typename T::Str, DWORD, LPDIFILEEFFECT, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE BuildActionMap(typename T::ActFmt, typename T::Str, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE SetActionMap(typename T::ActFmt, typename T::Str, DWORD) override { return DIERR_UNSUPPORTED; }
    HRESULT STDMETHODCALLTYPE GetImageInfo(typename T::ImgHdr) override { return DIERR_UNSUPPORTED; }
};

// ------------------------------------------------- IDirectInput8 wrapper ---

template <bool W>
class Proxy final : public Tr<W>::DI {
    using T = Tr<W>;
    typename T::DI* real_;
    LONG            ref_ = 1;

    struct EnumCtx { typename T::EnumDevCb cb; LPVOID ref; bool hide; };

    struct EnumState { EnumCtx ctx; bool stopped; };
    static BOOL FAR PASCAL EnumThunkTrack(const typename T::DevInst* inst, LPVOID p) {
        auto* st = static_cast<EnumState*>(p);
        if (st->ctx.hide && IsGameCtrlType(inst->dwDevType)) return DIENUM_CONTINUE;  // hide real pads
        const BOOL r = st->ctx.cb(inst, st->ctx.ref);
        if (r == DIENUM_STOP) st->stopped = true;
        return r;
    }

public:
    explicit Proxy(typename T::DI* real) : real_(real) { Log("Proxy created (%s)", W ? "W" : "A"); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, LPVOID* ppv) override {
        if (!ppv) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, T::IidDI())) {
            *ppv = this; AddRef(); return S_OK;
        }
        return real_->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) { real_->Release(); delete this; }
        return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE CreateDevice(REFGUID g, typename T::Dev** out, LPUNKNOWN outer) override {
        if (!out) return E_POINTER;
        if (IsEqualGUID(g, kGuidInstance)) {
            *out = new VirtualDevice<W>();
            return DI_OK;
        }
        typename T::Dev* dev = nullptr;
        HRESULT hr = real_->CreateDevice(g, &dev, outer);
        if (SUCCEEDED(hr) && dev && TakeOver()) {
            // The game may remember a real pad's GUID from an old config; redirect it to ours.
            typename T::DevInst info;
            memset(&info, 0, sizeof(info));
            info.dwSize = sizeof(info);
            if (SUCCEEDED(dev->GetDeviceInfo(&info)) && IsGameCtrlType(info.dwDevType)) {
                Log("Redirecting real gamepad CreateDevice to virtual device");
                dev->Release();
                *out = new VirtualDevice<W>();
                return DI_OK;
            }
        }
        *out = dev;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE EnumDevices(DWORD type, typename T::EnumDevCb cb, LPVOID ref, DWORD flags) override {
        if (!cb) return real_->EnumDevices(type, cb, ref, flags);
        const bool wantPad = (type == DI8DEVCLASS_ALL || type == DI8DEVCLASS_GAMECTRL) &&
                             !(flags & DIEDFL_FORCEFEEDBACK);
        const bool expose  = wantPad && (PadConnected() || (GetConfig().alwaysExpose && !(flags & DIEDFL_ATTACHEDONLY)));

        EnumState st{{cb, ref, expose && GetConfig().hideReal}, false};
        HRESULT hr = real_->EnumDevices(type, &EnumThunkTrack, &st, flags);
        if (SUCCEEDED(hr) && expose && !st.stopped) {
            typename T::DevInst inst;
            FillInstance(inst);
            Log("EnumDevices: reporting virtual gamepad");
            cb(&inst, ref);
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetDeviceStatus(REFGUID g) override {
        if (IsEqualGUID(g, kGuidInstance)) return PadConnected() || GetConfig().alwaysExpose ? DI_OK : DI_NOTATTACHED;
        return real_->GetDeviceStatus(g);
    }
    HRESULT STDMETHODCALLTYPE RunControlPanel(HWND h, DWORD f) override { return real_->RunControlPanel(h, f); }
    HRESULT STDMETHODCALLTYPE Initialize(HINSTANCE h, DWORD v) override { return real_->Initialize(h, v); }
    HRESULT STDMETHODCALLTYPE FindDevice(REFGUID c, typename T::Str n, LPGUID g) override { return real_->FindDevice(c, n, g); }
    HRESULT STDMETHODCALLTYPE EnumDevicesBySemantics(typename T::Str u, typename T::ActFmt a,
                                                     typename T::SemCb cb, LPVOID r, DWORD f) override {
        Log("EnumDevicesBySemantics called (action mapping is NOT virtualised)");
        return real_->EnumDevicesBySemantics(u, a, cb, r, f);
    }
    HRESULT STDMETHODCALLTYPE ConfigureDevices(LPDICONFIGUREDEVICESCALLBACK cb, typename T::CfgParams p,
                                               DWORD f, LPVOID r) override {
        Log("ConfigureDevices called (virtual device will not appear in that dialog)");
        return real_->ConfigureDevices(cb, p, f, r);
    }
};

// ------------------------------------------------------------- exports ---

using PFN_Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static PFN_Create LoadReal() {
    static PFN_Create fn = []() -> PFN_Create {
        wchar_t p[MAX_PATH] = {};
        GetSystemDirectoryW(p, MAX_PATH);   // SysWOW64 for a 32-bit process on 64-bit Windows
        wcscat_s(p, L"\\dinput8.dll");
        HMODULE m = LoadLibraryW(p);
        return m ? reinterpret_cast<PFN_Create>(GetProcAddress(m, "DirectInput8Create")) : nullptr;
    }();
    return fn;
}

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD ver, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    PFN_Create real = LoadReal();
    if (!real) return DIERR_GENERIC;
    Log("DirectInput8Create ver=0x%lx", ver);
    HRESULT hr = real(hinst, ver, riid, out, outer);
    if (FAILED(hr) || !out || !*out) return hr;

    if (IsEqualIID(riid, IID_IDirectInput8A))
        *out = new Proxy<false>(static_cast<IDirectInput8A*>(*out));
    else if (IsEqualIID(riid, IID_IDirectInput8W))
        *out = new Proxy<true>(static_cast<IDirectInput8W*>(*out));
    return hr;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        dmc3::SetModule(h);
    }
    return TRUE;
}
