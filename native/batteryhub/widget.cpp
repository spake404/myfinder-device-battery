#include "widget.h"

#include "i18n.h"
#include "reserve.h"

#include <commctrl.h>
#include <windowsx.h>
#include <uiautomation.h>
#include <algorithm>
#include <climits>
#include <cmath>

namespace {

HWND g_wnd, g_taskbar, g_tip, g_notify, g_rebar;
bool g_xaml;  // Windows 11 XAML taskbar: the task buttons end early through reserve::Update
UINT g_clickMsg;
bool g_light, g_hover;
int g_hoverFoliaId = 0;
bool g_hoverDevice = false;
COLORREF g_badge = CLR_INVALID;
bool g_background;  // opaque rounded plate behind the text, for transparent / restyled taskbars
bool g_stacked;     // icon above the percentage (menu option, PR #525)
bool g_vertical;    // the primary taskbar is docked left or right: one block per device, stacked (PR #321)
std::vector<WidgetItem> g_items;
FoliaWidgetState g_folia;
struct FoliaHit { int id; RECT rc; };
std::vector<FoliaHit> g_foliaHits;
RECT g_deviceZone{};
HWND g_pressedWnd = nullptr;
int g_pressedKind = -1;
constexpr UINT_PTR kAnimTimerId = 1;
constexpr UINT kAnimIntervalMs = 16;
constexpr ULONGLONG kStateAnimMs = 220;
constexpr ULONGLONG kNudgeAnimMs = 180;
struct FoliaAnimState {
    bool playActive = false;
    bool playFrom = false;
    ULONGLONG playStart = 0;
    bool loopActive = false;
    int loopFrom = 0;
    ULONGLONG loopStart = 0;
    int nudgeId = 0;
    ULONGLONG nudgeStart = 0;
} g_anim;
bool g_animTimerOn = false;
std::wstring g_tipText;
RECT g_lastTray{}, g_lastBar{}, g_lastWidgets{};
int g_lastW = -1, g_lastDpi = 0, g_lastLeft = -1;
SIZE g_size{};
HINSTANCE g_inst;

// Copies of the strip on secondary-monitor taskbars (Shell_SecondaryTrayWnd), so the levels stay visible
// while a game runs fullscreen on the main screen. Same drawing, same click and tooltip; each one sits
// left of its taskbar's clock. The primary strip above is unchanged, and with one monitor none exist.
struct Mirror {
    HWND bar = nullptr, wnd = nullptr;
    bool hover = false;
    int hoverFoliaId = 0;
    bool hoverDevice = false;
    SIZE size{};
    int dpi = 0;
    int lastLeft = INT_MIN;  // client x it was last placed at; INT_MIN = not placed (hidden)
    std::vector<FoliaHit> foliaHits;
    RECT deviceZone{};
};
std::vector<Mirror> g_mirrors;
// Which taskbars show the strip: "all", "main", or one monitor's id (MonitorId), e.g. only monitor 3.
// With one monitor chosen, the primary strip is hidden; if that monitor is not connected, the primary
// strip is shown instead so the levels never disappear.
std::wstring g_choice = L"all";
bool g_primaryHidden = false;
HWND g_dpiBar;  // while a mirror is painted or placed, Px() uses its taskbar's DPI

const wchar_t* kClass = L"MyFinderDeviceBatteryTaskbar";

int Dpi() {
    HWND bar = g_dpiBar ? g_dpiBar : g_taskbar;
    UINT d = bar ? GetDpiForWindow(bar) : 0;
    return d ? (int)d : 96;
}
int Px(int v) { return MulDiv(v, Dpi(), 96); }

// Windows 11 puts the Widgets (weather) button right next to the tray when the taskbar is
// left-aligned (start-aligned on right-to-left Windows). It is XAML, so only UI Automation knows
// where it is. The element is looked up once per taskbar; its rectangle is re-read every few
// seconds because the weather text changes its width.
//
// UI Automation calls go to Explorer and wait for its UI thread, while our widget is a child window
// of the taskbar, so Explorer in turn waits for our UI thread whenever it lays out or sends input to
// the widget. Making those calls from our UI thread deadlocks both: Explorer is reported hung
// ("Cross-process", waiting on BatteryHub.exe) and restarted, again and again. So the lookup
// runs on its own thread, which owns no windows, and the UI thread only reads the last rectangle.
SRWLOCK g_wbLock = SRWLOCK_INIT;
RECT g_wbRect{};             // last Widgets button rectangle (screen), empty when unknown
HWND g_wbRectBar;            // taskbar g_wbRect was found in (guarded by g_wbLock)
HWND g_wbTaskbar;            // taskbar to search, null while there is no widget (guarded by g_wbLock)
HANDLE g_wbWake;             // auto-reset: wakes the lookup thread early

// Where a Windows 11 secondary taskbar's clock, widgets button and task buttons are. Its XAML has no child
// windows for them, so the same lookup thread reads them with UI Automation (the clock has AutomationId
// "SystemTrayIcon", widgets button "WidgetsButton", task buttons class "Taskbar.TaskListButtonAutomationPeer";
// found by PR #107).
struct Span {
    HWND bar = nullptr;
    bool measured = false;   // UI Automation answered for this taskbar
    bool hasClock = false;   // the clock can be turned off on secondary taskbars
    int clockLeft = 0;       // screen x
    bool hasWidgets = false; // Widgets (weather) button on secondary taskbar
    RECT widgets{};          // bounding rectangle of WidgetsButton (screen coords)
    bool hasTasks = false;
    int tasksRight = 0;      // right edge of the last task button left of the clock (screen x)
};
std::vector<Span> g_spans;   // guarded by g_wbLock
bool g_wbMirrors;            // guarded by g_wbLock: measure secondary taskbars too

IUIAutomationCondition* StringCondition(IUIAutomation* uia, PROPERTYID prop, const wchar_t* value) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = SysAllocString(value);
    IUIAutomationCondition* c = nullptr;
    if (v.bstrVal) uia->CreatePropertyCondition(prop, v, &c);
    VariantClear(&v);
    return c;
}

Span MeasureSpan(IUIAutomation* uia, HWND bar) {
    Span s;
    s.bar = bar;
    RECT br{};
    IUIAutomationElement* root = nullptr;
    if (!GetWindowRect(bar, &br) || FAILED(uia->ElementFromHandle(bar, &root)) || !root) return s;
    s.measured = true;
    auto inBar = [&](const RECT& r) { return r.right > r.left && r.left >= br.left && r.right <= br.right; };
    if (IUIAutomationCondition* clock = StringCondition(uia, UIA_AutomationIdPropertyId, L"SystemTrayIcon")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, clock, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {  // leftmost of the clock area's buttons
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.left < s.clockLeft)) {
                    s.clockLeft = r.left;
                    s.hasClock = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        clock->Release();
    }
    if (IUIAutomationCondition* wbCond = StringCondition(uia, UIA_AutomationIdPropertyId, L"WidgetsButton")) {
        IUIAutomationElement* wbEl = nullptr;
        RECT r{};
        if (SUCCEEDED(root->FindFirst(TreeScope_Descendants, wbCond, &wbEl)) && wbEl &&
            SUCCEEDED(wbEl->get_CurrentBoundingRectangle(&r)) && inBar(r)) {
            s.widgets = r;
            s.hasWidgets = true;
        }
        if (wbEl) wbEl->Release();
        wbCond->Release();
    }
    if (IUIAutomationCondition* task = StringCondition(uia, UIA_ClassNamePropertyId, L"Taskbar.TaskListButtonAutomationPeer")) {
        IUIAutomationElementArray* all = nullptr;
        int n = 0;
        if (SUCCEEDED(root->FindAll(TreeScope_Descendants, task, &all)) && all && SUCCEEDED(all->get_Length(&n)))
            for (int i = 0; i < n; i++) {
                IUIAutomationElement* el = nullptr;
                RECT r{};
                if (SUCCEEDED(all->GetElement(i, &el)) && el && SUCCEEDED(el->get_CurrentBoundingRectangle(&r)) && inBar(r) &&
                    (!s.hasClock || r.right <= s.clockLeft) && (!s.hasWidgets || r.right <= s.widgets.left) &&
                    (!s.hasTasks || r.right > s.tasksRight)) {
                    s.tasksRight = r.right;
                    s.hasTasks = true;
                }
                if (el) el->Release();
            }
        if (all) all->Release();
        task->Release();
    }
    root->Release();
    return s;
}

bool IsXamlTaskbar(HWND bar) {
    return FindWindowExW(bar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr) != nullptr;
}

DWORD WINAPI WidgetsButtonThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IUIAutomation* uia = nullptr;
    IUIAutomationElement* btn = nullptr;
    HWND btnBar = nullptr;
    for (;;) {
        WaitForSingleObject(g_wbWake, btnBar ? 5000 : INFINITE);  // no widget (tray-icon mode): sleep until one
        AcquireSRWLockShared(&g_wbLock);
        HWND bar = g_wbTaskbar;
        ReleaseSRWLockShared(&g_wbLock);
        if (btn && bar != btnBar) { btn->Release(); btn = nullptr; }
        btnBar = bar;
        RECT r{};
        if (bar && IsWindow(bar)) {
            if (!uia)
                CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), (void**)&uia);
            if (uia && !btn) {
                IUIAutomationElement* root = nullptr;
                IUIAutomationCondition* cond = nullptr;
                VARIANT id;
                VariantInit(&id);
                id.vt = VT_BSTR;
                id.bstrVal = SysAllocString(L"WidgetsButton");
                if (SUCCEEDED(uia->ElementFromHandle(bar, &root)) && root &&
                    SUCCEEDED(uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &cond)) && cond)
                    root->FindFirst(TreeScope_Descendants, cond, &btn);
                VariantClear(&id);
                if (cond) cond->Release();
                if (root) root->Release();
            }
            if (btn && FAILED(btn->get_CurrentBoundingRectangle(&r))) {
                btn->Release();  // stale (Widgets turned off, Explorer restarted): look it up again next time
                btn = nullptr;
                r = {};
            }
        }
        AcquireSRWLockShared(&g_wbLock);
        bool mirrors = g_wbMirrors;
        ReleaseSRWLockShared(&g_wbLock);
        std::vector<Span> spans;
        if (bar && mirrors && uia)
            for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
                 t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr))
                if (IsXamlTaskbar(t)) spans.push_back(MeasureSpan(uia, t));  // Windows 10 bars have real child windows
        AcquireSRWLockExclusive(&g_wbLock);
        if (bar && g_wbTaskbar == bar) {  // drop a result for a taskbar we no longer sit on
            g_wbRect = r;
            g_wbRectBar = bar;
        }
        g_spans = std::move(spans);
        ReleaseSRWLockExclusive(&g_wbLock);
    }
}

// Points the lookup thread at `taskbar` (null to idle it), starting the thread on first use.
// The last rectangle is kept for its taskbar, so switching from tray-icon back to taskbar mode on the
// same taskbar steps past the Widgets button straight away instead of after the next lookup.
void WatchWidgetsButton(HWND taskbar) {
    AcquireSRWLockExclusive(&g_wbLock);
    g_wbTaskbar = taskbar;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (!g_wbWake) {
        g_wbWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_wbWake ? CreateThread(nullptr, 0, WidgetsButtonThread, nullptr, 0, nullptr) : nullptr;
        if (t) CloseHandle(t);
    }
    if (g_wbWake) SetEvent(g_wbWake);
}

// Never blocks: returns what the lookup thread last found on the current taskbar.
RECT WidgetsButtonRect() {
    AcquireSRWLockShared(&g_wbLock);
    RECT r = g_wbRectBar && g_wbRectBar == g_wbTaskbar ? g_wbRect : RECT{};
    ReleaseSRWLockShared(&g_wbLock);
    return r;
}

void ApplyTipTheme() {
    if (!g_tip) return;
    using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    static auto setTheme = ux ? (SetWindowThemeFn)GetProcAddress(ux, "SetWindowTheme") : nullptr;
    if (setTheme) {
        setTheme(g_tip, g_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        SendMessageW(g_tip, WM_THEMECHANGED, 0, 0);
    }
}

wchar_t Glyph(DeviceKind k, bool bluetooth) {
    switch (k) {
        case DeviceKind::Headset:  return 0xE7F6;  // Headphone
        case DeviceKind::Mouse:    return 0xE962;  // Mouse
        case DeviceKind::Keyboard: return 0xE765;  // KeyboardClassic
        case DeviceKind::Gamepad:  return 0xE7FC;  // Game
        case DeviceKind::Phone:    return 0xE8EA;  // CellPhone
        case DeviceKind::Microphone: return 0xE720;  // Microphone
        default:                   return bluetooth ? 0xE702 : 0xE772;  // Bluetooth / Devices
    }
}
constexpr wchar_t kBolt = 0xE945;            // LightningBolt
constexpr wchar_t kCheck = 0xE73E;           // CheckMark, in place of the bolt once fully charged
constexpr wchar_t kBatteryUnknown = 0xE996;  // shown when there is nothing to display

// Creates a font, falling back when the preferred face is not installed (GDI would silently substitute).
HFONT MakeFont(int px, int weight, const wchar_t* face, const wchar_t* fallback, int fallbackPx = 0) {
    for (const wchar_t* f : { face, fallback }) {
        int h_px = (f == fallback && fallbackPx > 0) ? fallbackPx : px;
        HFONT h = CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, f);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ o = SelectObject(dc, h);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, o);
        DeleteDC(dc);
        if (!_wcsicmp(got, f) || f == fallback) return h;
        DeleteObject(h);
    }
    return nullptr;
}

// Grayscale coverage mask of a string, cropped to its ink bounds.
struct Mask {
    int w = 0, h = 0;
    std::vector<BYTE> a;
};

Mask RenderMask(const std::wstring& s, HFONT font, bool darkOnLight) {
    Mask m;
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ of = SelectObject(dc, font);
    SIZE sz;
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    int W = sz.cx + 8, H = sz.cy + 8;
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, darkOnLight ? 0xFF : 0x00, (size_t)W * H * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, darkOnLight ? RGB(0, 0, 0) : RGB(255, 255, 255));
    TextOutW(dc, 4, 4, s.c_str(), (int)s.size());
    GdiFlush();
    auto* p = static_cast<DWORD*>(bits);
    auto cover = [&](DWORD v) -> BYTE {
        int c = (int)((v & 0xFF) + ((v >> 8) & 0xFF) + ((v >> 16) & 0xFF)) / 3;
        return (BYTE)(darkOnLight ? 255 - c : c);
    };
    int x0 = W, y0 = H, x1 = -1, y1 = -1;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            if (cover(p[y * W + x])) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
    if (x1 >= 0) {
        m.w = x1 - x0 + 1;
        m.h = y1 - y0 + 1;
        m.a.resize((size_t)m.w * m.h);
        for (int y = 0; y < m.h; y++)
            for (int x = 0; x < m.w; x++) m.a[y * m.w + x] = cover(p[(y0 + y) * W + x0 + x]);
    }
    SelectObject(dc, ob);
    SelectObject(dc, of);
    DeleteObject(bmp);
    DeleteDC(dc);
    return m;
}

Mask ThickenMask(const Mask& src) {
    if (src.w <= 0 || src.h <= 0) return src;
    Mask out;
    out.w = src.w + 1;
    out.h = src.h + 1;
    out.a.resize((size_t)out.w * out.h, 0);
    auto at = [&](int x, int y) -> int {
        if (x < 0 || y < 0 || x >= src.w || y >= src.h) return 0;
        return src.a[(size_t)y * src.w + x];
    };
    for (int y = 0; y < out.h; y++) {
        for (int x = 0; x < out.w; x++) {
            int v = std::max(at(x, y), std::max(at(x - 1, y), at(x, y - 1)) * 20 / 100);
            out.a[(size_t)y * out.w + x] = static_cast<BYTE>(std::min(255, v));
        }
    }
    return out;
}

struct Rgb { int r, g, b; };

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float EaseOutCubic(float t) {
    float inv = 1.0f - Clamp01(t);
    return 1.0f - inv * inv * inv;
}

float EaseOutBack(float t) {
    float x = Clamp01(t) - 1.0f;
    return 1.0f + 2.70158f * x * x * x + 1.70158f * x * x;
}

Rgb LerpRgb(Rgb a, Rgb b, float t) {
    float u = Clamp01(t);
    return {
        static_cast<int>(a.r + (b.r - a.r) * u + 0.5f),
        static_cast<int>(a.g + (b.g - a.g) * u + 0.5f),
        static_cast<int>(a.b + (b.b - a.b) * u + 0.5f)
    };
}

float AnimProgress(bool active, ULONGLONG start, ULONGLONG dur, ULONGLONG now) {
    if (!active) return 1.0f;
    if (now <= start) return 0.0f;
    ULONGLONG elapsed = now - start;
    if (elapsed >= dur) return 1.0f;
    return static_cast<float>(elapsed) / static_cast<float>(dur);
}

void SyncAnimTimer(ULONGLONG now) {
    if (g_anim.playActive && now - g_anim.playStart >= kStateAnimMs) g_anim.playActive = false;
    if (g_anim.loopActive && now - g_anim.loopStart >= kStateAnimMs) g_anim.loopActive = false;
    if (g_anim.nudgeId > 0 && now - g_anim.nudgeStart >= kNudgeAnimMs) g_anim.nudgeId = 0;
    bool need = g_anim.playActive || g_anim.loopActive || g_anim.nudgeId > 0;
    if (need && g_wnd && !g_animTimerOn) {
        SetTimer(g_wnd, kAnimTimerId, kAnimIntervalMs, nullptr);
        g_animTimerOn = true;
    } else if (!need && g_animTimerOn) {
        if (g_wnd) KillTimer(g_wnd, kAnimTimerId);
        g_animTimerOn = false;
    }
}

struct Canvas {
    int w, h;
    std::vector<DWORD> px;  // premultiplied BGRA
    Canvas(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_, 0) {}
    void Blend(int x, int y, Rgb c, int a) {  // source-over, a = 0..255
        if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
        DWORD d = px[y * w + x];
        int inv = 255 - a;
        int da = d >> 24, dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int oa = a + da * inv / 255;
        int orr = c.r * a / 255 + dr * inv / 255;
        int og = c.g * a / 255 + dg * inv / 255;
        int ob = c.b * a / 255 + db * inv / 255;
        px[y * w + x] = ((DWORD)oa << 24) | ((DWORD)orr << 16) | ((DWORD)og << 8) | (DWORD)ob;
    }
    void Draw(const Mask& m, int x, int y, Rgb c, int alpha) {
        for (int j = 0; j < m.h; j++)
            for (int i = 0; i < m.w; i++) Blend(x + i, y + j, c, m.a[j * m.w + i] * alpha / 255);
    }
    void DrawScaled(const Mask& m, float cx, float cy, float scale, Rgb c, int alpha) {
        if (m.w <= 0 || m.h <= 0 || scale <= 0.01f || alpha <= 0) return;
        float halfW = m.w * scale * 0.5f;
        float halfH = m.h * scale * 0.5f;
        int x0 = std::max(0, static_cast<int>(std::floor(cx - halfW)) - 1);
        int y0 = std::max(0, static_cast<int>(std::floor(cy - halfH)) - 1);
        int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx + halfW)) + 1);
        int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy + halfH)) + 1);
        float invScale = 1.0f / scale;
        auto sample = [&](int x, int y) -> float {
            if (x < 0 || y < 0 || x >= m.w || y >= m.h) return 0.0f;
            return static_cast<float>(m.a[(size_t)y * m.w + x]);
        };
        for (int py = y0; py <= y1; py++) {
            float sy = (py + 0.5f - cy) * invScale + m.h * 0.5f - 0.5f;
            int iy = static_cast<int>(std::floor(sy));
            float fy = sy - iy;
            for (int px = x0; px <= x1; px++) {
                float sx = (px + 0.5f - cx) * invScale + m.w * 0.5f - 0.5f;
                int ix = static_cast<int>(std::floor(sx));
                float fx = sx - ix;
                float v00 = sample(ix, iy);
                float v10 = sample(ix + 1, iy);
                float v01 = sample(ix, iy + 1);
                float v11 = sample(ix + 1, iy + 1);
                float cov = (v00 * (1.0f - fx) + v10 * fx) * (1.0f - fy) +
                            (v01 * (1.0f - fx) + v11 * fx) * fy;
                int a = static_cast<int>(cov * alpha / 255.0f + 0.5f);
                if (a > 0) Blend(px, py, c, std::min(255, a));
            }
        }
    }
};

// Soft glow around a glyph, like a lit RGB device: the coverage mask padded by r pixels, box-blurred
// twice and brightened so a thin icon outline still gives a visible glow.
Mask Halo(const Mask& m, int r) {
    Mask g;
    g.w = m.w + 2 * r;
    g.h = m.h + 2 * r;
    std::vector<int> a((size_t)g.w * g.h, 0), tmp(a.size(), 0);
    for (int y = 0; y < m.h; y++)
        for (int x = 0; x < m.w; x++) a[(y + r) * g.w + x + r] = m.a[y * m.w + x];
    int n = 2 * r + 1;
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (x + k >= 0 && x + k < g.w) s += a[y * g.w + x + k];
                tmp[y * g.w + x] = s / n;
            }
        for (int y = 0; y < g.h; y++)
            for (int x = 0; x < g.w; x++) {
                int s = 0;
                for (int k = -r; k <= r; k++) if (y + k >= 0 && y + k < g.h) s += tmp[(y + k) * g.w + x];
                a[y * g.w + x] = s / n;
            }
    }
    g.a.resize(a.size());
    for (size_t i = 0; i < a.size(); i++) g.a[i] = (BYTE)std::min(255, a[i] * 3);
    return g;
}

// One drawn piece of the strip. Normal pieces follow each other from left to right. A piece with an
// `anchor` (index of an earlier piece) is a caption: drawn over that piece, `bx`/`by` from its top-left
// corner, and taking no room of its own. Branded devices add a glow behind the glyph and a level bar.
struct Piece {
    Mask m;
    Rgb color;
    int alpha;
    int gapAfter;
    int anchor = -1;
    int bx = 0, by = 0;
    int barPct = -1;  // >= 0: thin level bar under the text (branded devices)
    int barAlpha = 255;
    float yLift = 0.0f;
    Rgb barColor{};
    Mask halo;        // glow drawn behind the glyph (branded devices)
    int haloR = 0, haloAlpha = 0;
    int px = INT_MIN, py = 0;  // fixed top-left position (stacked and vertical layouts); INT_MIN = flow left to right
    int foliaId = 0;
    int slotW = 0;
    Mask oldM;
    Rgb oldColor{};
    int oldAlpha = 0;
    int animKind = 0;  // 1: play/pause morph, 2: loop morph
    float animT = 1.0f;
};

// Three arrangements of the same pieces:
//  * normal: "[icon] 30%  [icon] 93%" in one row (unchanged default)
//  * stacked (menu option, from PR #525): each device's icon above its percentage, to save width;
//    used only when the taskbar is tall enough, otherwise the normal row
//  * vertical (left / right taskbar, from PR #321): one device per row block, stacked top to bottom
// `width` and `height` return the strip's size; in the vertical layout the width is the bar's.
std::vector<Piece> Layout(int barW, int barH, int& width, int& height) {
    Rgb fg = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    Rgb red = g_light ? Rgb{ 196, 30, 40 } : Rgb{ 255, 90, 90 };
    Rgb charge = g_light ? Rgb{ 0, 120, 212 } : Rgb{ 96, 205, 255 };
    Rgb full = g_light ? Rgb{ 16, 124, 16 } : Rgb{ 108, 203, 95 };  // Windows "success" green
    HFONT icons = MakeFont(Px(16), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT bolt = MakeFont(Px(11), FW_NORMAL, L"Segoe Fluent Icons", L"Segoe MDL2 Assets");
    HFONT text = MakeFont(Px(13), FW_NORMAL, L"Segoe UI Variable Text", L"Segoe UI", Px(12));
    HFONT caption = MakeFont(std::max(8, Px(7)), FW_SEMIBOLD, L"Segoe UI Variable Text", L"Segoe UI");  // never under 8 px

    std::vector<Piece> out;
    struct Slot { int icon, label, bolt; };  // indices of one device's pieces
    std::vector<Slot> slots;
    if (!g_vertical && g_folia.mode > 0) {
        const wchar_t kMusicNote = 0xE8D6;
        const wchar_t kPrev = 0xE892;
        const wchar_t kPlay = 0xE768;
        const wchar_t kPause = 0xE769;
        ULONGLONG now = GetTickCount64();
        float playT = AnimProgress(g_anim.playActive, g_anim.playStart, kStateAnimMs, now);
        float loopT = AnimProgress(g_anim.loopActive, g_anim.loopStart, kStateAnimMs, now);
        float playEase = EaseOutCubic(playT);
        int tailGap = g_items.empty() ? 0 : Px(16);
        if (g_folia.mode == 1) {
            float barFactor = g_anim.playActive ? (g_folia.playing ? playEase : (1.0f - playEase)) : (g_folia.playing ? 1.0f : 0.0f);
            Rgb noteColor = g_anim.playActive ? LerpRgb(g_anim.playFrom ? charge : fg, g_folia.playing ? charge : fg, playEase) : (g_folia.playing ? charge : fg);
            Piece p{ RenderMask(std::wstring(1, kMusicNote), icons, g_light), noteColor, 255, tailGap };
            p.foliaId = 5;
            if (g_anim.playActive) {
                p.barPct = static_cast<int>(100.0f * barFactor + 0.5f);
                p.barAlpha = static_cast<int>(255.0f * barFactor + 0.5f);
                p.yLift = barFactor;
                p.barColor = charge;
            } else if (g_folia.playing) {
                p.barPct = 100;
                p.barAlpha = 255;
                p.yLift = 1.0f;
                p.barColor = charge;
            }
            out.push_back(std::move(p));
        } else if (g_folia.mode == 2) {
            const wchar_t kNext = 0xE893;
            const wchar_t kRepeatAll = 0xE8EE;
            const wchar_t kRepeatOne = 0xE8ED;
            Piece prevP{ RenderMask(std::wstring(1, kPrev), icons, g_light), fg, 235, Px(14) };
            prevP.foliaId = 1;
            out.push_back(std::move(prevP));
            Rgb playColor = g_anim.playActive ? LerpRgb(g_anim.playFrom ? charge : fg, g_folia.playing ? charge : fg, playEase) : (g_folia.playing ? charge : fg);
            Piece playP{ RenderMask(std::wstring(1, g_folia.playing ? kPause : kPlay), icons, g_light), playColor, 255, Px(14) };
            playP.foliaId = 2;
            Mask altPlayM = RenderMask(std::wstring(1, g_folia.playing ? kPlay : kPause), icons, g_light);
            playP.slotW = std::max(playP.m.w, altPlayM.w);
            if (g_anim.playActive && playT < 1.0f) {
                playP.oldM = (g_anim.playFrom != g_folia.playing) ? std::move(altPlayM) : RenderMask(std::wstring(1, g_anim.playFrom ? kPause : kPlay), icons, g_light);
                playP.oldColor = g_anim.playFrom ? charge : fg;
                playP.oldAlpha = 255;
                playP.animKind = 1;
                playP.animT = playT;
            }
            out.push_back(std::move(playP));
            Piece nextP{ RenderMask(std::wstring(1, kNext), icons, g_light), fg, 235, Px(18) };
            nextP.foliaId = 3;
            out.push_back(std::move(nextP));
            float barFactor = g_anim.playActive ? (g_folia.playing ? playEase : (1.0f - playEase)) : (g_folia.playing ? 1.0f : 0.0f);
            Rgb menuColor = g_anim.playActive ? LerpRgb(g_anim.playFrom ? charge : fg, g_folia.playing ? charge : fg, playEase) : (g_folia.playing ? charge : fg);
            Piece menuP{ RenderMask(std::wstring(1, kMusicNote), icons, g_light), menuColor, 255, Px(14) };
            menuP.foliaId = 4;
            if (g_anim.playActive) {
                menuP.barPct = static_cast<int>(100.0f * barFactor + 0.5f);
                menuP.barAlpha = static_cast<int>(255.0f * barFactor + 0.5f);
                menuP.yLift = barFactor;
                menuP.barColor = charge;
            } else if (g_folia.playing) {
                menuP.barPct = 100;
                menuP.barAlpha = 255;
                menuP.yLift = 1.0f;
                menuP.barColor = charge;
            }
            out.push_back(std::move(menuP));
            wchar_t loopGlyph = g_folia.loopMode == 2 ? kRepeatOne : kRepeatAll;
            Rgb loopColor = g_folia.loopMode > 0 ? charge : fg;
            int loopAlpha = g_folia.loopMode > 0 ? 255 : 235;
            Piece loopP{ ThickenMask(RenderMask(std::wstring(1, loopGlyph), icons, g_light)), loopColor, loopAlpha, tailGap };
            loopP.foliaId = 6;
            Mask altLoopM = ThickenMask(RenderMask(std::wstring(1, g_folia.loopMode == 2 ? kRepeatAll : kRepeatOne), icons, g_light));
            loopP.slotW = std::max(loopP.m.w, altLoopM.w);
            if (g_anim.loopActive && loopT < 1.0f) {
                wchar_t oldLoopGlyph = g_anim.loopFrom == 2 ? kRepeatOne : kRepeatAll;
                loopP.oldM = (oldLoopGlyph != loopGlyph) ? std::move(altLoopM) : ThickenMask(RenderMask(std::wstring(1, oldLoopGlyph), icons, g_light));
                loopP.oldColor = g_anim.loopFrom > 0 ? charge : fg;
                loopP.oldAlpha = g_anim.loopFrom > 0 ? 255 : 235;
                loopP.animKind = 2;
                loopP.animT = loopT;
            }
            out.push_back(std::move(loopP));
        }
    }
    if (g_items.empty() && g_folia.mode == 0) out.push_back({ RenderMask(std::wstring(1, kBatteryUnknown), icons, g_light), fg, 110, 0 });
    for (size_t i = 0; i < g_items.size(); i++) {
        const auto& it = g_items[i];
        int a = it.dim ? 120 : 255;
        Rgb tc = it.low ? red : fg;
        bool mark = it.charging || it.full;  // bolt while charging, green check mark once full
        size_t glyph = out.size();
        slots.push_back({ (int)glyph, (int)glyph + 1, mark ? (int)glyph + 2 : -1 });
        bool branded = it.accent != Accent::None;
        BrandColor bc = AccentColor(it.accent, g_light);
        Rgb accent{ bc.r, bc.g, bc.b };
        Piece icon{ RenderMask(std::wstring(1, Glyph(it.kind, it.bluetooth)), icons, g_light), branded ? accent : fg, a, Px(5) };
        if (branded && !it.dim) {
            icon.haloR = Px(3);
            icon.halo = Halo(icon.m, icon.haloR);
            icon.haloAlpha = g_light ? 60 : 140;
        }
        out.push_back(std::move(icon));
        std::wstring t = !it.text.empty() ? it.text : it.percent >= 0 ? std::to_wstring(it.percent) + L"%" : L"?";
        Piece label{ RenderMask(t, text, g_light), tc, a, mark ? Px(2) : 0 };
        if (branded && it.percent >= 0) {
            label.barPct = it.percent;
            label.barColor = it.low ? red : accent;
        }
        out.push_back(std::move(label));
        if (mark) out.push_back({ RenderMask(std::wstring(1, it.full ? kCheck : kBolt), bolt, g_light), it.full ? full : charge, a, 0 });
        out.back().gapAfter = i + 1 < g_items.size() ? Px(14) : 0;
        if (!it.label.empty()) {  // tiny caption in the bottom-right corner of the icon
            Piece c{ RenderMask(it.label, caption, g_light), fg, a, 0 };
            c.anchor = (int)glyph;
            c.bx = out[glyph].m.w - c.m.w + Px(3);
            c.by = out[glyph].m.h - c.m.h + Px(3);
            out.push_back(c);
        }
    }
    DeleteObject(icons);
    DeleteObject(bolt);
    DeleteObject(text);
    DeleteObject(caption);

    height = barH;
    const int rowGap = Px(2), barRoom = Px(5);  // barRoom: space for a brand level bar under the text
    int iconH = 0, textH = 0;
    for (const Slot& s : slots) {
        iconH = std::max(iconH, out[s.icon].m.h);
        textH = std::max(textH, out[s.label].m.h);
    }
    bool stacked = !g_vertical && g_stacked && !slots.empty() && barH >= iconH + rowGap + textH + barRoom + Px(6);
    if (g_vertical) {
        // One block per device, centred in the bar's width: icon, then the percentage (and bolt) under it.
        width = barW;
        int y = Px(6);
        if (slots.empty()) {
            // No devices yet (startup, or the last one disconnected): only the placeholder icon. Without this
            // branch the row layout below would keep height = barH, and on a left / right taskbar barH is the
            // whole screen height, so the strip covered the entire taskbar.
            Piece& ic = out[0];
            ic.px = (barW - ic.m.w) / 2;
            ic.py = y;
            height = y + ic.m.h + Px(6);
            return out;
        }
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& lb = out[s.label];
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            ic.px = (barW - ic.m.w) / 2;
            ic.py = y;
            y += ic.m.h + rowGap;
            lb.px = (barW - lb.m.w - boltW) / 2;
            lb.py = y;
            if (s.bolt >= 0) {
                out[s.bolt].px = lb.px + lb.m.w + Px(2);
                out[s.bolt].py = y + (lb.m.h - out[s.bolt].m.h) / 2;
            }
            y += lb.m.h + (lb.barPct >= 0 ? barRoom : 0) + Px(8);
        }
        height = y;
        return out;
    }
    if (stacked) {
        // Icon on top (bolt to its right), percentage centred under it; the pair centred in the bar's height.
        int top = (barH - (iconH + rowGap + textH + barRoom)) / 2;
        int x = Px(8);
        for (const Slot& s : slots) {
            Piece& ic = out[s.icon];
            Piece& lb = out[s.label];
            int boltW = s.bolt >= 0 ? Px(2) + out[s.bolt].m.w : 0;
            int slotW = std::max(ic.m.w + boltW, lb.m.w);
            ic.px = x + (slotW - ic.m.w - boltW) / 2;
            ic.py = top + (iconH - ic.m.h) / 2;
            if (s.bolt >= 0) {
                out[s.bolt].px = ic.px + ic.m.w + Px(2);
                out[s.bolt].py = top + (iconH - out[s.bolt].m.h) / 2;
            }
            lb.px = x + (slotW - lb.m.w) / 2;
            lb.py = top + iconH + rowGap;
            x += slotW + Px(10);
        }
        width = x - Px(10) + Px(8);
        return out;
    }
    width = Px(8) * 2;
    for (auto& p : out)
        if (p.anchor < 0) width += std::max(p.m.w, p.slotW) + p.gapAfter;
    return out;
}

// Draws the strip for one window: the primary one or a mirror on a secondary taskbar.
void PaintStrip(HWND wnd, HWND taskbar, bool hover, int hoverFoliaId, bool hoverDevice, std::vector<FoliaHit>& foliaHits, RECT& deviceZone, SIZE& size) {
    RECT bar;
    GetClientRect(taskbar, &bar);
    int W = 0, H = 0;
    auto pieces = Layout(bar.right - bar.left, bar.bottom - bar.top, W, H);
    foliaHits.clear();
    deviceZone = RECT{};
    if (W <= 0 || H <= 0) return;
    Canvas cv(W, H);

    // Nearly transparent background so the whole strip receives mouse input,
    // plus a rounded hover highlight like the system tray icons have.
    for (auto& p : cv.px) p = 0x01000000;
    auto roundRectBox = [&](int left, int top, int right, int bottom, int r, Rgb c, int a) {
        int cl = std::max(0, left), ct = std::max(0, top), cr = std::min(W, right), cb = std::min(H, bottom);
        for (int y = ct; y < cb; y++)
            for (int x = cl; x < cr; x++) {
                int dx = x < cl + r ? (cl + r) - x : (x >= cr - r ? x - (cr - r - 1) : 0);
                int dy = y < ct + r ? (ct + r) - y : (y >= cb - r ? y - (cb - r - 1) : 0);
                if (dx * dx + dy * dy <= r * r) cv.Blend(x, y, c, a);
            }
    };
    int x = Px(8);
    Rgb barColor = g_light ? Rgb{ 243, 243, 243 } : Rgb{ 32, 32, 32 };
    Rgb track = g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 };
    std::vector<int> xs(pieces.size()), ys(pieces.size());
    std::vector<int> slotXs(pieces.size()), slotWs(pieces.size());
    std::vector<size_t> foliaIndices;
    int firstDevIdx = -1;
    int bh = std::max(2, Px(2)), barGap = Px(3);
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (p.anchor < 0) {
            bool fixed = p.px != INT_MIN;  // stacked / vertical layout: Layout() placed it
            int pw = std::max(p.m.w, p.slotW);
            slotXs[i] = fixed ? p.px : x;
            slotWs[i] = pw;
            xs[i] = fixed ? p.px : (x + (pw - p.m.w) / 2);
            ys[i] = fixed ? p.py : (H - p.m.h) / 2;
            if (!fixed && (p.yLift > 0.0f || p.barPct >= 0)) {
                float lift = p.yLift > 0.0f ? p.yLift : (p.barPct >= 0 ? 1.0f : 0.0f);
                float baseY = static_cast<float>(H - p.m.h) * 0.5f;
                float liftedY = static_cast<float>(H - (p.m.h + barGap + bh)) * 0.5f;
                ys[i] = static_cast<int>(std::round(baseY + (liftedY - baseY) * lift));
            }
            if (p.foliaId > 0) foliaIndices.push_back(i);
            else if (firstDevIdx < 0) firstDevIdx = static_cast<int>(i);
            if (!fixed) x += pw + p.gapAfter;
        }
    }
    for (size_t k = 0; k < foliaIndices.size(); k++) {
        size_t idx = foliaIndices[k];
        int left = (k == 0) ? 0 : (slotXs[foliaIndices[k - 1]] + slotWs[foliaIndices[k - 1]] + slotXs[idx]) / 2;
        int right = W;
        if (k + 1 < foliaIndices.size()) {
            size_t nextIdx = foliaIndices[k + 1];
            right = (slotXs[idx] + slotWs[idx] + slotXs[nextIdx]) / 2;
        } else if (firstDevIdx >= 0) {
            right = (slotXs[idx] + slotWs[idx] + slotXs[firstDevIdx]) / 2;
        }
        foliaHits.push_back({ pieces[idx].foliaId, RECT{ left, 0, right, H } });
    }
    int foliaEndX = foliaHits.empty() ? 0 : foliaHits.back().rc.right;
    if (firstDevIdx >= 0) {
        deviceZone = RECT{ foliaEndX, 0, W, H };
    }
    // Optional plate in the taskbar's own colours: keeps the text readable when a taskbar theme
    // (e.g. Windhawk's Taskbar Styler) makes the bar transparent over the wallpaper.
    if (g_background) {
        if (!foliaHits.empty()) roundRectBox(0, Px(4), foliaEndX - (deviceZone.right > deviceZone.left ? Px(2) : 0), H - Px(4), Px(6), barColor, 240);
        if (deviceZone.right > deviceZone.left) roundRectBox(deviceZone.left + (foliaHits.empty() ? 0 : Px(2)), Px(4), W, H - Px(4), Px(6), barColor, 240);
    }
    if (hover && hoverDevice && deviceZone.right > deviceZone.left) {
        int dLeft = foliaHits.empty() ? 0 : deviceZone.left + Px(2);
        roundRectBox(dLeft, Px(4), W, H - Px(4), Px(4), g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, g_light ? 20 : 26);
    }
    for (size_t idx : foliaIndices) {
        const Piece& p = pieces[idx];
        if (hover && hoverFoliaId == p.foliaId) {
            int padX = Px(4);
            roundRectBox(slotXs[idx] - padX, Px(4), slotXs[idx] + slotWs[idx] + padX, H - Px(4), Px(4), g_light ? Rgb{ 0, 0, 0 } : Rgb{ 255, 255, 255 }, g_light ? 24 : 30);
        }
    }
    for (size_t i = 0; i < pieces.size(); i++) {
        const Piece& p = pieces[i];
        if (p.anchor < 0) {
            if (p.haloR) cv.Draw(p.halo, xs[i] - p.haloR, ys[i] - p.haloR, p.color, p.haloAlpha * p.alpha / 255);
            if (p.barPct >= 0 && p.barAlpha > 0) {
                // The bar spans the text's width, like Synapse's battery bar; in the row layout text and bar
                // are centred together.
                int by = ys[i] + p.m.h + barGap;
                int fill = std::max(p.barPct > 0 ? 1 : 0, (p.barPct * p.m.w + 50) / 100);
                int ba = p.barAlpha * p.alpha / 255;
                for (int yy = by; yy < by + bh; yy++)
                    for (int xx = 0; xx < p.m.w; xx++)
                        cv.Blend(xs[i] + xx, yy, xx < fill ? p.barColor : track, xx < fill ? ba : 45 * ba / 255);
            }
        } else {  // caption: one-pixel outline in the taskbar's colour so it reads over the icon's lines
            xs[i] = xs[p.anchor] + p.bx;
            ys[i] = ys[p.anchor] + p.by;
            for (int oy = -1; oy <= 1; oy++)
                for (int ox = -1; ox <= 1; ox++)
                    if (ox || oy) cv.Draw(p.m, xs[i] + ox, ys[i] + oy, barColor, 255);
        }
        bool isPressed = (p.foliaId > 0 && g_pressedWnd == wnd && g_pressedKind == p.foliaId);
        bool isNudging = (p.foliaId > 0 && g_anim.nudgeId == p.foliaId);
        bool isLifting = (p.px == INT_MIN && p.yLift > 0.0f && p.yLift < 1.0f);
        if (p.animKind > 0 && p.animT < 1.0f) {
            float e = EaseOutCubic(p.animT);
            float pop = EaseOutBack(p.animT);
            float pressScale = isPressed ? 0.88f : 1.0f;
            float cx = xs[i] + p.m.w * 0.5f;
            float cy = ys[i] + p.m.h * 0.5f;
            float slide = static_cast<float>(Px(3)) * (p.animKind == 1 ? 1.0f : 0.7f);
            int oldA = static_cast<int>(p.oldAlpha * (1.0f - e) + 0.5f);
            if (oldA > 0 && p.oldM.w > 0) {
                float oldScale = (1.0f - 0.28f * e) * pressScale;
                cv.DrawScaled(p.oldM, cx, cy - slide * e, oldScale, p.oldColor, oldA);
            }
            int newA = static_cast<int>(p.alpha * e + 0.5f);
            if (newA > 0) {
                float newScale = (0.72f + 0.28f * pop) * pressScale;
                cv.DrawScaled(p.m, cx, cy + slide * (1.0f - e), newScale, p.color, newA);
            }
        } else if (isPressed || isNudging || isLifting) {
            float cx = xs[i] + p.m.w * 0.5f;
            float cy = ys[i] + p.m.h * 0.5f;
            if (isLifting) {
                float baseY = static_cast<float>(H - p.m.h) * 0.5f;
                float liftedY = static_cast<float>(H - (p.m.h + barGap + bh)) * 0.5f;
                cy = baseY + (liftedY - baseY) * p.yLift + p.m.h * 0.5f;
            }
            float scale = isPressed ? 0.88f : 1.0f;
            if (isNudging) {
                float nt = AnimProgress(true, g_anim.nudgeStart, kNudgeAnimMs, GetTickCount64());
                float pulse = std::sin(Clamp01(nt) * 3.14159265f);
                if (p.foliaId == 1) cx -= static_cast<float>(Px(2)) * pulse;
                else if (p.foliaId == 3) cx += static_cast<float>(Px(2)) * pulse;
                else scale *= (1.0f + 0.14f * pulse);
            }
            cv.DrawScaled(p.m, cx, cy, scale, p.color, p.alpha);
        } else {
            cv.Draw(p.m, xs[i], ys[i], p.color, p.alpha);
        }
    }
    if (g_badge != CLR_INVALID) {
        Rgb badge{ GetRValue(g_badge), GetGValue(g_badge), GetBValue(g_badge) };
        int radius = Px(2);
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
                if (dx * dx + dy * dy <= radius * radius)
                    cv.Blend(W - Px(5) + dx, H / 2 - Px(8) + dy, badge, 255);
    }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bits || !mem) {
        if (bmp) DeleteObject(bmp);
        if (mem) DeleteDC(mem);
        if (screen) ReleaseDC(nullptr, screen);
        size = {};
        return;
    }
    memcpy(bits, cv.px.data(), (size_t)W * H * 4);
    HGDIOBJ ob = SelectObject(mem, bmp);
    SIZE sz{ W, H };
    POINT src{ 0, 0 };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    size = UpdateLayeredWindow(wnd, screen, nullptr, &sz, mem, &src, 0, &bf, ULW_ALPHA) ? sz : SIZE{};
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Paint() {
    if (!g_wnd) return;
    PaintStrip(g_wnd, g_taskbar, g_hover, g_hoverFoliaId, g_hoverDevice, g_foliaHits, g_deviceZone, g_size);
    g_lastW = -1;  // force a re-position with the new size
}

void PaintMirror(Mirror& m) {
    g_dpiBar = m.bar;
    m.dpi = Dpi();
    PaintStrip(m.wnd, m.bar, m.hover, m.hoverFoliaId, m.hoverDevice, m.foliaHits, m.deviceZone, m.size);
    g_dpiBar = nullptr;
    m.lastLeft = INT_MIN;  // force a re-position with the new size
}

void HideMirror(Mirror& m) {
    if (m.lastLeft != INT_MIN || IsWindowVisible(m.wnd)) ShowWindow(m.wnd, SW_HIDE);
    m.lastLeft = INT_MIN;
}

// Places a mirror left of its taskbar's clock (and left of the Widgets button if present), or hides it
// when that spot is unknown or taken by task buttons: never guess and cover something.
void PositionMirror(Mirror& m) {
    RECT bar{};
    if (!GetWindowRect(m.bar, &bar) || (GetWindowLongW(m.bar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL)) { HideMirror(m); return; }
    g_dpiBar = m.bar;
    if (Dpi() != m.dpi || m.size.cy != bar.bottom - bar.top) PaintMirror(m), g_dpiBar = m.bar;
    int right = 0, minLeft = bar.left;
    bool known = false;
    // StartAllBack can put a full notification area on each secondary taskbar.
    // Use its left edge, as on the primary taskbar, to clear all of the tray icons.
    HWND tray = FindWindowExW(m.bar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tr{};
    if (tray && IsWindowVisible(tray) && GetWindowRect(tray, &tr) &&
        tr.left > bar.left && tr.right <= bar.right && tr.right > tr.left &&
        tr.bottom > bar.top && tr.top < bar.bottom) {
        right = tr.left;
        known = true;
    } else if (!IsXamlTaskbar(m.bar)) {  // Windows 10: the clock is a real child window (absent when it is turned off)
        HWND clock = FindWindowExW(m.bar, nullptr, L"ClockButton", nullptr);
        RECT cr{};
        right = clock && IsWindowVisible(clock) && GetWindowRect(clock, &cr) ? cr.left : bar.right - Px(8);
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(m.bar, nullptr, cls, nullptr);
            RECT fr{};
            if (feeds && IsWindowVisible(feeds) && GetWindowRect(feeds, &fr) && fr.left < right && fr.right > fr.left)
                right = fr.left;
        }
        known = true;
    } else {
        AcquireSRWLockShared(&g_wbLock);
        for (const auto& s : g_spans)
            if (s.bar == m.bar && s.measured) {
                right = s.hasClock ? s.clockLeft : bar.right - Px(8);
                if (s.hasWidgets && s.widgets.left < right && s.widgets.right > right - m.size.cx - Px(2))
                    right = s.widgets.left;
                if (s.hasTasks) minLeft = s.tasksRight + Px(4);
                known = true;
            }
        ReleaseSRWLockShared(&g_wbLock);
    }
    DWORD offset = 0, sz = sizeof(offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"SecondaryTaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset, &sz) ==
        ERROR_SUCCESS)
        right -= Px((int)offset);
    int left = right - m.size.cx - Px(2);
    g_dpiBar = nullptr;
    if (!known || m.size.cx <= 0 || left < minLeft) { HideMirror(m); return; }
    POINT p{ left, bar.top };
    ScreenToClient(m.bar, &p);
    bool covered = GetWindow(m.wnd, GW_HWNDPREV) != nullptr;
    if (!covered && p.x == m.lastLeft) return;
    m.lastLeft = p.x;
    SetWindowPos(m.wnd, HWND_TOP, p.x, 0, m.size.cx, m.size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void AddTool(HWND w) {
    if (!g_tip) return;
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = g_notify;
    ti.uId = (UINT_PTR)w;
    ti.lpszText = const_cast<wchar_t*>(g_tipText.empty() ? L"BatteryHub" : g_tipText.c_str());
    SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

void DestroyMirror(Mirror& m) {
    if (g_tip) {
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.hwnd = g_notify;
        ti.uId = (UINT_PTR)m.wnd;
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    if (m.wnd && IsWindow(m.wnd)) DestroyWindow(m.wnd);
    m.wnd = nullptr;
}

void SetMeasureMirrors(bool on) {
    AcquireSRWLockExclusive(&g_wbLock);
    bool changed = g_wbMirrors != on;
    g_wbMirrors = on;
    ReleaseSRWLockExclusive(&g_wbLock);
    if (changed && on && g_wbWake) SetEvent(g_wbWake);  // measure the new taskbar now, not in 5 s
}

std::vector<HWND> SecondaryTaskbars() {
    std::vector<HWND> out;
    for (HWND t = FindWindowExW(nullptr, nullptr, L"Shell_SecondaryTrayWnd", nullptr); t;
         t = FindWindowExW(nullptr, t, L"Shell_SecondaryTrayWnd", nullptr)) {
        RECT r{};
        if (IsWindowVisible(t) && GetWindowRect(t, &r) && r.right - r.left > r.bottom - r.top) out.push_back(t);  // horizontal only
    }
    return out;
}

// Keeps one mirror per secondary taskbar: adds them for new monitors, removes them for gone ones.
// Called from Reposition() about once a second; with one monitor it is a single FindWindowEx call.
// A monitor's lasting identity: its device interface name, which survives reboots, Explorer restarts and
// Windows renumbering the displays (falls back to "\\.\DISPLAYn").
std::wstring MonitorId(HWND bar) {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONULL);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return L"";
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, EDD_GET_DEVICE_INTERFACE_NAME) && dd.DeviceID[0]) return dd.DeviceID;
    return mi.szDevice;
}

// "Display 3 (right)": the number Windows gives the display and where it sits next to the main one.
std::wstring MonitorLabel(HWND bar) {
    MONITORINFOEXW mi{}, pm{};
    mi.cbSize = pm.cbSize = sizeof(MONITORINFOEXW);
    HMONITOR mon = MonitorFromWindow(bar, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi)) return T(L"Other monitor");
    std::wstring dev = mi.szDevice;
    size_t d = dev.find(L"DISPLAY");
    std::wstring label = d != std::wstring::npos ? TF(L"Display {number}", { { L"number", dev.substr(d + 7) } }) : T(L"Other monitor");
    POINT origin{ 0, 0 };
    if (GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &pm)) {
        int dx = (mi.rcMonitor.left + mi.rcMonitor.right - pm.rcMonitor.left - pm.rcMonitor.right) / 2;
        int dy = (mi.rcMonitor.top + mi.rcMonitor.bottom - pm.rcMonitor.top - pm.rcMonitor.bottom) / 2;
        label += std::wstring(L" (") + T(abs(dx) >= abs(dy) ? (dx < 0 ? L"left" : L"right") : (dy < 0 ? L"above" : L"below")) + L")";
    }
    return label;
}

// Hides or shows the strip on the main taskbar (it keeps running, just out of sight).
void SetPrimaryHidden(bool hide) {
    if (hide == g_primaryHidden || !g_wnd) return;
    g_primaryHidden = hide;
    if (hide) {
        ShowWindow(g_wnd, SW_HIDE);
        if (g_rebar && IsWindow(g_rebar)) {  // Windows 10: give the task buttons their full width back
            RECT r;
            GetClientRect(g_taskbar, &r);
            PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
        }
    }
    g_lastW = -1;  // re-place (and show) on the next pass
}

void SyncMirrors() {
    std::vector<HWND> all = g_wnd ? SecondaryTaskbars() : std::vector<HWND>{}, bars;
    bool chosenFound = false;
    if (g_choice == L"all") {
        bars = all;
    } else if (g_choice != L"main") {
        for (HWND b : all)
            if (MonitorId(b) == g_choice) {
                bars.push_back(b);
                chosenFound = true;
                break;
            }
    }
    SetPrimaryHidden(chosenFound);
    for (auto it = g_mirrors.begin(); it != g_mirrors.end();) {
        if (std::find(bars.begin(), bars.end(), it->bar) == bars.end() || !IsWindow(it->wnd)) {
            DestroyMirror(*it);
            it = g_mirrors.erase(it);
        } else {
            ++it;
        }
    }
    for (HWND b : bars) {
        if (std::any_of(g_mirrors.begin(), g_mirrors.end(), [b](const Mirror& m) { return m.bar == b; })) continue;
        Mirror m;
        m.bar = b;
        m.wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, b, nullptr,
                                g_inst, nullptr);
        if (!m.wnd) continue;
        AddTool(m.wnd);
        g_mirrors.push_back(m);
        PaintMirror(g_mirrors.back());
    }
    SetMeasureMirrors(!g_mirrors.empty());
    for (auto& m : g_mirrors) PositionMirror(m);
}

void PaintAll() {
    SyncAnimTimer(GetTickCount64());
    Paint();
    for (auto& m : g_mirrors) PaintMirror(m);
}

// Hover highlight for the strip under the mouse: the primary one or a mirror.
std::wstring ActiveTipText(HWND h) {
    int foliaId = 0;
    if (h == g_wnd) foliaId = g_hover ? g_hoverFoliaId : 0;
    else {
        for (const auto& m : g_mirrors) if (m.wnd == h) { foliaId = m.hover ? m.hoverFoliaId : 0; break; }
    }
    if (foliaId == 1) return g_folia.title.empty() ? L"切换上一首歌" : (L"切换上一首歌：" + g_folia.title);
    if (foliaId == 2) return g_folia.playing ? L"暂停" : L"播放";
    if (foliaId == 3) return g_folia.title.empty() ? L"切换下一首歌" : (L"切换下一首歌：" + g_folia.title);
    if (foliaId == 4 || foliaId == 5) return L"音乐设置";
    if (foliaId == 6) return g_folia.loopMode == 1 ? L"播放模式：列表循环" : (g_folia.loopMode == 2 ? L"播放模式：单曲循环" : L"播放模式：顺序播放");
    return g_tipText;
}

void UpdateTipTextFor(HWND h) {
    if (!g_tip || !h) return;
    std::wstring text = ActiveTipText(h);
    TTTOOLINFOW ti{ sizeof(ti) };
    ti.hwnd = g_notify;
    ti.uId = (UINT_PTR)h;
    ti.lpszText = const_cast<wchar_t*>(text.c_str());
    SendMessageW(g_tip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
}

void UpdateAllTips() {
    if (!g_tip) return;
    if (g_wnd) UpdateTipTextFor(g_wnd);
    for (const auto& m : g_mirrors) if (m.wnd) UpdateTipTextFor(m.wnd);
}

void SetHover(HWND h, bool on, int foliaId = 0, bool dev = false) {
    if (h == g_wnd) {
        int nextId = on ? foliaId : 0;
        bool nextDev = on && dev && nextId == 0;
        if (g_hover == on && g_hoverFoliaId == nextId && g_hoverDevice == nextDev) return;
        bool tipChanged = (g_hoverFoliaId != nextId) || (g_hover != on);
        g_hover = on;
        g_hoverFoliaId = nextId;
        g_hoverDevice = nextDev;
        if (tipChanged) UpdateTipTextFor(h);
        Paint();
        widget::Reposition();
        return;
    }
    for (auto& m : g_mirrors) {
        int nextId = on ? foliaId : 0;
        bool nextDev = on && dev && nextId == 0;
        if (m.wnd == h && (m.hover != on || m.hoverFoliaId != nextId || m.hoverDevice != nextDev)) {
            bool tipChanged = (m.hoverFoliaId != nextId) || (m.hover != on);
            m.hover = on;
            m.hoverFoliaId = nextId;
            m.hoverDevice = nextDev;
            if (tipChanged) UpdateTipTextFor(h);
            PaintMirror(m);
            PositionMirror(m);
        }
    }
}

int HitFoliaAt(HWND h, POINT pt) {
    const std::vector<FoliaHit>* hits = nullptr;
    if (h == g_wnd) hits = &g_foliaHits;
    else {
        for (const auto& m : g_mirrors) if (m.wnd == h) { hits = &m.foliaHits; break; }
    }
    if (!hits) return 0;
    for (const auto& hit : *hits) {
        if (PtInRect(&hit.rc, pt)) return hit.id;
    }
    return 0;
}

bool HitDeviceAt(HWND h, POINT pt) {
    if (h == g_wnd) return PtInRect(&g_deviceZone, pt) != 0;
    for (const auto& m : g_mirrors) if (m.wnd == h) return PtInRect(&m.deviceZone, pt) != 0;
    return false;
}

LRESULT CALLBACK WidgetProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int foliaId = HitFoliaAt(h, pt);
            bool dev = (foliaId == 0) && HitDeviceAt(h, pt);
            SetHover(h, true, foliaId, dev);
            return 0;
        }
        case WM_MOUSELEAVE:
            SetHover(h, false, 0, false);
            if (g_pressedWnd == h && g_pressedKind > 0) {
                g_pressedWnd = nullptr;
                g_pressedKind = -1;
                PaintAll();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int foliaId = HitFoliaAt(h, pt);
            g_pressedWnd = h;
            if (foliaId > 0) {
                g_pressedKind = foliaId;
                PaintAll();
                return 0;
            }
            if (HitDeviceAt(h, pt)) {
                g_pressedKind = 0;
                PostMessageW(g_notify, g_clickMsg, msg, GetMessagePos());
                return 0;
            }
            g_pressedKind = -1;
            return 0;
        }
        case WM_LBUTTONUP:
        {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            int hitId = HitFoliaAt(h, pt);
            int downKind = (g_pressedWnd == h) ? g_pressedKind : -1;
            g_pressedWnd = nullptr;
            g_pressedKind = -1;
            if (downKind > 0) {
                if (hitId == downKind) {
                    if (hitId == 2) {
                        g_anim.playActive = true;
                        g_anim.playFrom = g_folia.playing;
                        g_anim.playStart = GetTickCount64();
                        g_folia.playing = !g_folia.playing;
                        UpdateAllTips();
                    } else if (hitId == 6) {
                        g_anim.loopActive = true;
                        g_anim.loopFrom = g_folia.loopMode;
                        g_anim.loopStart = GetTickCount64();
                        g_folia.loopMode = (g_folia.loopMode + 1) % 3;
                        UpdateAllTips();
                    } else if (hitId == 1 || hitId == 3 || hitId == 4 || hitId == 5) {
                        g_anim.nudgeId = hitId;
                        g_anim.nudgeStart = GetTickCount64();
                    }
                    PostMessageW(g_notify, g_clickMsg, static_cast<WPARAM>(0x8000 | hitId), GetMessagePos());
                }
                PaintAll();
                return 0;
            }
            if (hitId == 0 && (downKind == 0 || (downKind == -1 && HitDeviceAt(h, pt)))) {
                PostMessageW(g_notify, g_clickMsg, msg, GetMessagePos());
            }
            return 0;
        }
        case WM_RBUTTONUP:
            if (HitFoliaAt(h, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }) == 0) {
                PostMessageW(g_notify, g_clickMsg, msg, GetMessagePos());
            }
            return 0;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_TIMER:
            if (wp == kAnimTimerId) {
                PaintAll();
                return 0;
            }
            break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

namespace widget {

bool Create(HINSTANCE inst, HWND notifyWnd, UINT clickMsg, bool lightTheme) {
    Destroy();
    g_taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!g_taskbar || !FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr) ||
        FindWindowExW(g_taskbar, nullptr, L"WirelessBatteryWidget", nullptr)) { g_taskbar = nullptr; return false; }
    RECT tb;
    GetWindowRect(g_taskbar, &tb);
    g_vertical = tb.bottom - tb.top > tb.right - tb.left;  // left / right taskbar: devices stacked in a column
    // Windows 11's taskbar is a XAML island; on Windows 10 the task buttons live in ReBarWindow32
    // and have to be narrowed so they never slide under the widget.
    g_rebar = FindWindowExW(g_taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr)
                  ? nullptr
                  : FindWindowExW(g_taskbar, nullptr, L"ReBarWindow32", nullptr);
    g_xaml = !g_rebar && FindWindowExW(g_taskbar, nullptr, L"Windows.UI.Composition.DesktopWindowContentBridge", nullptr);
    g_notify = notifyWnd;
    g_clickMsg = clickMsg;
    g_light = lightTheme;
    g_inst = inst;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc = WidgetProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1,
                            g_taskbar, nullptr, inst, nullptr);
    if (!g_wnd) { g_taskbar = nullptr; return false; }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                            notifyWnd, nullptr, inst, nullptr);
    if (g_tip) {
        ApplyTipTheme();
        TTTOOLINFOW ti{ sizeof(ti) };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = notifyWnd;
        ti.uId = (UINT_PTR)g_wnd;
        ti.lpszText = const_cast<wchar_t*>(g_tipText.c_str());
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(g_tip, TTM_SETMAXTIPWIDTH, 0, 600);
    }
    WatchWidgetsButton(g_taskbar);
    g_lastDpi = Dpi();
    Paint();
    g_lastW = -1;
    if (!Reposition()) { Destroy(); return false; }
    if (!g_primaryHidden) ShowWindow(g_wnd, SW_SHOWNOACTIVATE);  // hidden when only another monitor is chosen
    return true;
}

void Destroy() {
    if (g_xaml) reserve::Update(g_taskbar && IsWindow(g_taskbar) ? g_taskbar : nullptr, g_inst, 0);
    g_xaml = false;
    if (g_rebar && IsWindow(g_rebar) && g_taskbar && IsWindow(g_taskbar)) {
        // Let Explorer lay the task buttons out again over the full width.
        RECT r;
        GetClientRect(g_taskbar, &r);
        PostMessageW(g_taskbar, WM_SIZE, SIZE_RESTORED, MAKELPARAM(r.right, r.bottom));
    }
    g_rebar = nullptr;
    for (auto& m : g_mirrors) DestroyMirror(m);
    g_mirrors.clear();
    SetMeasureMirrors(false);
    g_primaryHidden = false;  // the next Create() decides again
    if (g_wbWake) WatchWidgetsButton(nullptr);
    g_foliaHits.clear();
    g_hoverFoliaId = 0;
    g_hoverDevice = false;
    g_deviceZone = RECT{};
    g_pressedWnd = nullptr;
    g_pressedKind = -1;
    if (g_animTimerOn && g_wnd) KillTimer(g_wnd, kAnimTimerId);
    g_animTimerOn = false;
    g_anim = FoliaAnimState{};
    if (g_tip) { DestroyWindow(g_tip); g_tip = nullptr; }
    if (g_wnd) { DestroyWindow(g_wnd); g_wnd = nullptr; }
    g_taskbar = nullptr;
    g_hover = false;
}

bool Alive() { return g_wnd && IsWindow(g_wnd) && g_taskbar && IsWindow(g_taskbar); }

void SetItems(const std::vector<WidgetItem>& items, const std::wstring& tooltip) {
    g_items = items;
    if (tooltip != g_tipText) {
        g_tipText = tooltip;
        UpdateAllTips();
    }
    PaintAll();
    Reposition();
}

void SetLightTheme(bool light) {
    if (g_light == light) return;
    g_light = light;
    ApplyTipTheme();
    PaintAll();
}

void SetFolia(const FoliaWidgetState& folia) {
    bool visualSame = (g_folia.mode == folia.mode && g_folia.playing == folia.playing && g_folia.liked == folia.liked && g_folia.loopMode == folia.loopMode);
    if (visualSame && g_folia.title == folia.title) return;
    FoliaWidgetState prev = g_folia;
    g_folia = folia;
    UpdateAllTips();
    if (visualSame) return;
    if (prev.mode > 0 && g_folia.mode > 0) {
        ULONGLONG now = GetTickCount64();
        if (prev.playing != g_folia.playing) {
            g_anim.playActive = true;
            g_anim.playFrom = prev.playing;
            g_anim.playStart = now;
        }
        if (prev.loopMode != g_folia.loopMode) {
            g_anim.loopActive = true;
            g_anim.loopFrom = prev.loopMode;
            g_anim.loopStart = now;
        }
    }
    PaintAll();
}

void SetBadge(COLORREF color) { g_badge = color; }

void SetBackground(bool on) {
    g_background = on;
    PaintAll();
}

void SetStacked(bool on) {
    g_stacked = on;
    PaintAll();
    if (g_wnd) Reposition();
}

void SetMonitorChoice(const std::wstring& choice) {
    g_choice = choice.empty() ? L"all" : choice;
    SyncMirrors();  // adds, removes or hides strips right away
    if (g_wnd) Reposition();
}

std::vector<MonitorOption> OtherMonitors() {
    std::vector<MonitorOption> out;
    for (HWND b : SecondaryTaskbars()) out.push_back({ MonitorId(b), MonitorLabel(b) });
    std::sort(out.begin(), out.end(), [](const MonitorOption& a, const MonitorOption& b) { return a.label < b.label; });
    return out;
}

bool Reposition() {
    if (!Alive() || g_size.cx <= 0 || g_size.cy <= 0 ||
        FindWindowExW(g_taskbar, nullptr, L"WirelessBatteryWidget", nullptr)) return false;
    SyncMirrors();
    if (g_primaryHidden) {  // only another monitor's taskbar shows the strip
        if (g_xaml) reserve::Update(g_taskbar, g_inst, 0);
        return true;
    }
    if (Dpi() != g_lastDpi) { g_lastDpi = Dpi(); Paint(); }
    HWND tray = FindWindowExW(g_taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    if (!tray) return false;
    RECT tr, bar;
    GetWindowRect(tray, &tr);
    GetWindowRect(g_taskbar, &bar);
    // The taskbar can be moved between a horizontal and a vertical edge while the app runs.
    bool vertical = bar.bottom - bar.top > bar.right - bar.left;
    if (vertical != g_vertical) {
        g_vertical = vertical;
        Paint();
    }
    if (g_xaml && g_vertical) reserve::Update(g_taskbar, g_inst, 0);
    if (g_vertical) {
        // Left or right taskbar (PR #321): the column sits just above the notification area, centred in
        // the bar's width.
        RECT client{};
        GetClientRect(g_taskbar, &client);
        POINT anchor{ bar.left, tr.top };
        ScreenToClient(g_taskbar, &anchor);
        int top = anchor.y - g_size.cy - Px(2);
        DWORD offset = 0, osz = sizeof(offset);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset", RRF_RT_REG_DWORD, nullptr, &offset,
                         &osz) == ERROR_SUCCESS)
            top -= Px((int)offset);
        top = std::clamp(top, 0, (int)std::max<LONG>(0, client.bottom - g_size.cy));
        int left = (int)std::max<LONG>(0, (client.right - g_size.cx) / 2);
        // Windows 10: shorten the task-button area when it would reach behind the column.
        if (g_rebar && IsWindow(g_rebar)) {
            RECT rr{};
            GetWindowRect(g_rebar, &rr);
            POINT tl{ rr.left, rr.top };
            ScreenToClient(g_taskbar, &tl);
            if (tl.y < top && tl.y + (rr.bottom - rr.top) > top)
                MoveWindow(g_rebar, tl.x, tl.y, rr.right - rr.left, top - tl.y, TRUE);
        }
        bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
        if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && g_lastW == g_size.cy && g_lastLeft == top)
            return true;
        g_lastTray = tr;
        g_lastBar = bar;
        g_lastW = g_size.cy;
        g_lastLeft = top;
        SetWindowPos(g_wnd, HWND_TOP, left, top, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return true;
    }
    // Right-to-left Windows (Hebrew, Arabic) mirrors the taskbar: client x runs from the screen's right
    // edge, so a window's client-space left edge is its screen-space right edge.
    bool rtl = GetWindowLongW(g_taskbar, GWL_EXSTYLE) & WS_EX_LAYOUTRTL;
    int edge = rtl ? tr.right : tr.left;  // screen x we sit next to
    // Windows 10: News and Interests / Weather sits immediately left of the tray. The window that hosts it
    // depends on the build: ShellFeedsTaskbarViewWindow on some, DynamicContent2 on others (e.g. 19045,
    // where the search highlights button is DynamicContent1). Both are direct children of the taskbar and
    // only a visible one counts. Its rectangle is read on every pass because the weather text changes its
    // width, so the widget follows it.
    if (!rtl)
        for (const wchar_t* cls : { L"ShellFeedsTaskbarViewWindow", L"DynamicContent2" }) {
            HWND feeds = FindWindowExW(g_taskbar, nullptr, cls, nullptr);
            if (!feeds || !IsWindowVisible(feeds)) continue;
            RECT fr;
            GetWindowRect(feeds, &fr);
            if (fr.left < edge && fr.right > fr.left) edge = fr.left;
        }
    // Step past the Widgets button when it sits where we would go.
    RECT wb = WidgetsButtonRect();
    if (wb.right > wb.left && wb.top < bar.bottom && wb.bottom > bar.top &&
        (rtl ? wb.left < edge + g_size.cx + Px(2) && wb.right > edge : wb.right > edge - g_size.cx - Px(2) && wb.left < edge))
        edge = rtl ? wb.right : wb.left;
    POINT p{ edge, bar.top };
    ScreenToClient(g_taskbar, &p);
    int left = p.x - g_size.cx - Px(2);


    // Optional user override in DPI-independent pixels (e.g. for custom padding).
    DWORD customOffset = 0, sz = sizeof(customOffset);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\WirelessBattery", L"TaskbarOffset",
                     RRF_RT_REG_DWORD, nullptr, &customOffset, &sz) == ERROR_SUCCESS) {
        left -= Px((int)customOffset);
    }

    if (left < 0) return false;

    if (g_xaml) {
        POINT trayLeft{ tr.left, tr.top };
        ScreenToClient(g_taskbar, &trayLeft);
        reserve::Update(g_taskbar, g_inst, rtl ? 0 : trayLeft.x - left + Px(4));
    }

    // Windows 10: Explorer re-expands the task buttons on its own layout passes; narrow them again.
    if (g_rebar && IsWindow(g_rebar)) {
        RECT rr;
        GetWindowRect(g_rebar, &rr);
        POINT tl{ rtl ? rr.right : rr.left, rr.top };
        ScreenToClient(g_taskbar, &tl);
        if (tl.x + (rr.right - rr.left) > left && left > tl.x)
            MoveWindow(g_rebar, tl.x, tl.y, left - tl.x, rr.bottom - rr.top, TRUE);
    }

    // Keep us above the taskbar's own content (our sibling windows).
    bool covered = GetWindow(g_wnd, GW_HWNDPREV) != nullptr;
    if (!covered && EqualRect(&tr, &g_lastTray) && EqualRect(&bar, &g_lastBar) && EqualRect(&wb, &g_lastWidgets) &&
        g_lastW == g_size.cx && g_lastLeft == left)
        return true;
    g_lastTray = tr;
    g_lastWidgets = wb;
    g_lastBar = bar;
    g_lastW = g_size.cx;
    g_lastLeft = left;
    SetWindowPos(g_wnd, HWND_TOP, left, 0, g_size.cx, g_size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

}  // namespace widget
