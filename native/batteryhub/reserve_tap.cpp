// Loaded into Explorer through the documented XAML diagnostics entry point (InitializeXamlDiagnosticsEx).
// It only touches the Windows 11 taskbar's app list (the element named "TaskbarFrameRepeater"): its right
// margin grows by the width the battery strip asks for, so Explorer lays the task buttons out in a
// shorter row and they end left of the strip. The clock, the tray and the taskbar itself keep their size.
// The strip's process sends the width (in DIPs) to a message-only window created on the taskbar's XAML
// thread; when that process exits, the original margin comes back.
#include <windows.h>
#include <ocidl.h>
#include <initguid.h>
#include <xamlom.h>
#include <new>
#include <vector>

#include "reserve_shared.h"

namespace {

struct Thickness { double Left, Top, Right, Bottom; };

const IID kIFrameworkElement = { 0xa391d09b, 0x4a99, 0x4b7c, { 0x9d, 0x8d, 0x6f, 0xa5, 0xd0, 0x1f, 0x6f, 0xbf } };
// Windows.UI.Xaml.IFrameworkElement vtable slots (IInspectable takes 0-5).
constexpr int kGetMargin = 31, kPutMargin = 32, kGetName = 33;
typedef HRESULT(STDMETHODCALLTYPE* GetMarginFn)(IInspectable*, Thickness*);
typedef HRESULT(STDMETHODCALLTYPE* PutMarginFn)(IInspectable*, Thickness);
typedef HRESULT(STDMETHODCALLTYPE* GetNameFn)(IInspectable*, HSTRING*);

struct Target {
    InstanceHandle handle;
    IInspectable* element;
    Thickness original;
    DWORD thread;
};

HMODULE g_module;
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<Target> g_targets;
IXamlDiagnostics* g_diag;
int g_dips;
HANDLE g_owner;
thread_local HWND t_window;

void Log(const wchar_t* format, ...) {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 64) return;
    lstrcatW(path, L"\\MyFinderDeviceBattery\\taskbar-reserve.log");
    WIN32_FILE_ATTRIBUTE_DATA size;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &size) && size.nFileSizeHigh == 0 && size.nFileSizeLow > 65536)
        DeleteFileW(path);
    wchar_t line[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int len = wsprintfW(line, L"%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond);
    va_list args;
    va_start(args, format);
    len += wvsprintfW(line + len, format, args);
    va_end(args);
    lstrcatW(line, L"\r\n");
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    char utf8[1024];
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    DWORD written;
    if (bytes > 1) WriteFile(file, utf8, bytes - 1, &written, nullptr);
    CloseHandle(file);
}

template <typename Fn>
Fn Slot(IInspectable* element, int index) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(element))[index]);
}

bool NameIs(IInspectable* element, const wchar_t* expected) {
    static auto rawBuffer = reinterpret_cast<PCWSTR(WINAPI*)(HSTRING, UINT32*)>(
        GetProcAddress(GetModuleHandleW(L"combase.dll"), "WindowsGetStringRawBuffer"));
    static auto deleteString = reinterpret_cast<HRESULT(WINAPI*)(HSTRING)>(
        GetProcAddress(GetModuleHandleW(L"combase.dll"), "WindowsDeleteString"));
    if (!rawBuffer || !deleteString) return false;
    HSTRING name = nullptr;
    if (FAILED(Slot<GetNameFn>(element, kGetName)(element, &name))) return false;
    bool same = lstrcmpW(rawBuffer(name, nullptr), expected) == 0;
    deleteString(name);
    return same;
}

void ApplyOne(const Target& target, int dips) {
    Thickness margin = target.original;
    margin.Right += dips;
    HRESULT hr = Slot<PutMarginFn>(target.element, kPutMargin)(target.element, margin);
    if (FAILED(hr)) Log(L"margin right %d failed: 0x%08X", (int)margin.Right, (unsigned)hr);
}

void ApplyThread(int dips) {
    DWORD thread = GetCurrentThreadId();
    AcquireSRWLockShared(&g_lock);
    std::vector<Target> mine;
    for (const auto& t : g_targets)
        if (t.thread == thread) mine.push_back(t);
    ReleaseSRWLockShared(&g_lock);
    for (const auto& t : mine) ApplyOne(t, dips);
}

// Explorer can lay the app list out again on its own (DPI change, theme change, resize) and drop the
// margin; put it back if it drifted.
void VerifyThread(int dips) {
    DWORD thread = GetCurrentThreadId();
    AcquireSRWLockShared(&g_lock);
    std::vector<Target> mine;
    for (const auto& t : g_targets)
        if (t.thread == thread) mine.push_back(t);
    ReleaseSRWLockShared(&g_lock);
    for (const auto& t : mine) {
        Thickness current{};
        if (FAILED(Slot<GetMarginFn>(t.element, kGetMargin)(t.element, &current))) continue;
        double wanted = t.original.Right + dips;
        if (current.Right < wanted - 0.5 || current.Right > wanted + 0.5) ApplyOne(t, dips);
    }
}

LRESULT CALLBACK WindowProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kReserveMessage) {
        int dips = (int)wp;
        if (dips < 0 || dips > 2000) dips = 0;
        DWORD pid = (DWORD)lp;
        if (pid) {
            HANDLE owner = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (owner) {
                HANDLE old = (HANDLE)InterlockedExchangePointer(&g_owner, owner);
                if (old) CloseHandle(old);
            }
        }
        if (dips != g_dips) {
            g_dips = dips;
            ApplyThread(dips);
            Log(L"margin right %d requested by pid %u", dips ? dips : 0, (unsigned)lp);
        }
        SetTimer(wnd, 1, 2000, nullptr);
        return 0;
    }
    if (msg == WM_TIMER) {
        HANDLE owner = g_owner;
        if (!owner || WaitForSingleObject(owner, 0) == WAIT_OBJECT_0) {
            KillTimer(wnd, 1);
            if (g_dips) {
                g_dips = 0;
                ApplyThread(0);
                Log(L"owner gone, margin restored");
            }
        } else if (g_dips) {
            VerifyThread(g_dips);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void EnsureWindow() {
    if (t_window && IsWindow(t_window)) return;
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = g_module;
        wc.lpszClassName = kReserveClass;
        registered = RegisterClassW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    t_window = CreateWindowExW(0, kReserveClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_module, nullptr);
}

void OnElementAdded(const VisualElement& element) {
    if (!element.Name || lstrcmpW(element.Name, L"TaskbarFrameRepeater") != 0 || !g_diag) return;
    AcquireSRWLockShared(&g_lock);
    bool known = false;
    for (const auto& t : g_targets)
        if (t.handle == element.Handle) known = true;
    ReleaseSRWLockShared(&g_lock);
    if (known) return;
    IInspectable* object = nullptr;
    if (FAILED(g_diag->GetIInspectableFromHandle(element.Handle, &object)) || !object) return;
    IInspectable* fe = nullptr;
    HRESULT hr = object->QueryInterface(kIFrameworkElement, reinterpret_cast<void**>(&fe));
    object->Release();
    if (FAILED(hr) || !fe) { Log(L"repeater without IFrameworkElement: 0x%08X", (unsigned)hr); return; }
    if (!NameIs(fe, L"TaskbarFrameRepeater")) { Log(L"vtable check failed, not touching"); fe->Release(); return; }
    Target target{ element.Handle, fe, {}, GetCurrentThreadId() };
    Slot<GetMarginFn>(fe, kGetMargin)(fe, &target.original);
    AcquireSRWLockExclusive(&g_lock);
    g_targets.push_back(target);
    ReleaseSRWLockExclusive(&g_lock);
    EnsureWindow();
    Log(L"repeater found, margin right %d, thread %u", (int)target.original.Right, (unsigned)target.thread);
    if (g_dips) ApplyOne(target, g_dips);
}

void OnElementRemoved(InstanceHandle handle) {
    AcquireSRWLockExclusive(&g_lock);
    for (auto it = g_targets.begin(); it != g_targets.end(); ++it)
        if (it->handle == handle) {
            it->element->Release();
            g_targets.erase(it);
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
}

class Watcher : public IVisualTreeServiceCallback2 {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IVisualTreeServiceCallback || riid == IID_IVisualTreeServiceCallback2) {
            *out = static_cast<IVisualTreeServiceCallback2*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(ParentChildRelation, VisualElement element,
                                                 VisualMutationType mutation) override {
        if (mutation == Add) OnElementAdded(element);
        else if (mutation == Remove) OnElementRemoved(element.Handle);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnElementStateChanged(InstanceHandle, VisualElementState, LPCWSTR) override {
        return S_OK;
    }
};
Watcher g_watcher;

DWORD WINAPI AdviseThread(LPVOID) {
    IVisualTreeService3* service = nullptr;
    HRESULT hr = g_diag->QueryInterface(IID_IVisualTreeService3, reinterpret_cast<void**>(&service));
    if (SUCCEEDED(hr)) {
        hr = service->AdviseVisualTreeChange(&g_watcher);
        service->Release();
    }
    Log(L"advise hr 0x%08X", (unsigned)hr);
    return 0;
}

class Tap : public IObjectWithSite {
    LONG refs = 1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IObjectWithSite) {
            *out = static_cast<IObjectWithSite*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG left = InterlockedDecrement(&refs);
        if (!left) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override {
        if (!site || g_diag) return S_OK;
        if (FAILED(site->QueryInterface(IID_IXamlDiagnostics, reinterpret_cast<void**>(&g_diag)))) return E_FAIL;
        HMODULE pinned;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&AdviseThread), &pinned);
        HANDLE thread = CreateThread(nullptr, 0, AdviseThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** out) override {
        if (!g_diag) { *out = nullptr; return E_FAIL; }
        return g_diag->QueryInterface(riid, out);
    }
    virtual ~Tap() = default;
};

class Factory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *out = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** out) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        Tap* tap = new (std::nothrow) Tap();
        if (!tap) return E_OUTOFMEMORY;
        HRESULT hr = tap->QueryInterface(riid, out);
        tap->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};
Factory g_factory;

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = inst;
        DisableThreadLibraryCalls(inst);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID riid, void** out) {
    if (clsid != kReserveTapClsid) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, out);
}

extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE DllCanUnloadNow() { return S_FALSE; }
