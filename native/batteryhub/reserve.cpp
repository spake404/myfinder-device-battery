#include "reserve.h"

#include "reserve_shared.h"

#include <string>
#include <vector>

namespace {

typedef HRESULT(WINAPI* InitializeXamlDiagnosticsExFn)(PCWSTR, DWORD, PCWSTR, PCWSTR, CLSID, PCWSTR);

DWORD g_injectedPid, g_lastPid;
ULONGLONG g_lastAttempt;
int g_attempts;
int g_lastDips = -1;
HWND g_lastWnd;
volatile LONG g_busy;
std::wstring g_source;

struct InjectJob {
    DWORD pid;
    std::wstring source;
};

void Log(const wchar_t* format, ...) {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (!n || n > MAX_PATH - 64) return;
    lstrcatW(path, L"\\MyFinderDeviceBattery\\taskbar-reserve.log");
    wchar_t line[512];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int len = wsprintfW(line, L"%02d:%02d:%02d strip: ", st.wHour, st.wMinute, st.wSecond);
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

// Explorer keeps the library open until it restarts, so it loads a copy named after the build; the copy
// in the install folder stays replaceable by updates.
std::wstring StableCopy(const std::wstring& source) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(source.c_str(), GetFileExInfoStandard, &info)) return L"";
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n > MAX_PATH - 100) return L"";
    std::wstring dir = std::wstring(base) + L"\\MyFinderDeviceBattery";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\taskbar";
    CreateDirectoryW(dir.c_str(), nullptr);
    wchar_t name[96];
    wsprintfW(name, L"\\MyFinderTaskbarReserve-%08X%08X-%X.dll", info.ftLastWriteTime.dwHighDateTime,
              info.ftLastWriteTime.dwLowDateTime, info.nFileSizeLow);
    std::wstring target = dir + name;
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileW(source.c_str(), target.c_str(), TRUE))
        return L"";
    return target;
}

DWORD WINAPI InjectThread(LPVOID param) {
    InjectJob* job = static_cast<InjectJob*>(param);
    std::wstring path = StableCopy(job->source);
    HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto init = xaml ? reinterpret_cast<InitializeXamlDiagnosticsExFn>(GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"))
                     : nullptr;
    if (init && !path.empty())
        for (int i = 1; i <= 5; i++) {
            wchar_t connection[48];
            wsprintfW(connection, L"VisualDiagConnection%d", i);
            HRESULT hr = init(connection, job->pid, L"", path.c_str(), kReserveTapClsid, nullptr);
            if (SUCCEEDED(hr)) break;
            Log(L"inject %s into pid %u failed: 0x%08X", connection, (unsigned)job->pid, (unsigned)hr);
        }
    else
        Log(L"cannot inject into pid %u: xaml=%p path=%s", (unsigned)job->pid, (void*)init, path.c_str());
    delete job;
    InterlockedExchange(&g_busy, 0);
    return 0;
}

std::vector<HWND> ReserveWindows(DWORD pid) {
    std::vector<HWND> out;
    HWND wnd = nullptr;
    while ((wnd = FindWindowExW(HWND_MESSAGE, wnd, kReserveClass, nullptr)) != nullptr) {
        DWORD owner = 0;
        GetWindowThreadProcessId(wnd, &owner);
        if (owner == pid) out.push_back(wnd);
    }
    return out;
}

}  // namespace

namespace reserve {

void Update(HWND taskbar, HINSTANCE module, int pixels) {
    DWORD pid = 0;
    if (taskbar) GetWindowThreadProcessId(taskbar, &pid);
    if (!pid) pid = g_lastPid;
    if (!pid) return;
    if (pid != g_lastPid) {
        g_lastPid = pid;
        g_lastDips = -1;
        g_lastWnd = nullptr;
        g_attempts = 0;
    }
    UINT dpi = taskbar ? GetDpiForWindow(taskbar) : 96;
    int dips = pixels > 0 ? MulDiv(pixels, 96, dpi ? dpi : 96) : 0;
    std::vector<HWND> windows = ReserveWindows(pid);
    if (windows.empty()) {
        if (!dips || !module || g_busy) return;
        ULONGLONG now = GetTickCount64();
        if (g_injectedPid == pid && (g_attempts >= 3 || now - g_lastAttempt < 10000)) return;
        if (g_source.empty()) {
            wchar_t path[MAX_PATH];
            DWORD n = GetModuleFileNameW(module, path, MAX_PATH);
            if (!n || n >= MAX_PATH) return;
            g_source = path;
            g_source = g_source.substr(0, g_source.find_last_of(L'\\') + 1) + L"MyFinderTaskbarReserve.dll";
        }
        if (GetFileAttributesW(g_source.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        g_injectedPid = pid;
        g_lastAttempt = now;
        g_attempts++;
        InterlockedExchange(&g_busy, 1);
        HANDLE thread = CreateThread(nullptr, 0, InjectThread, new InjectJob{ pid, g_source }, 0, nullptr);
        if (thread) CloseHandle(thread);
        else InterlockedExchange(&g_busy, 0);
        return;
    }
    if (dips == g_lastDips && windows.front() == g_lastWnd) return;
    for (HWND wnd : windows) PostMessageW(wnd, kReserveMessage, (WPARAM)dips, (LPARAM)GetCurrentProcessId());
    g_lastDips = dips;
    g_lastWnd = windows.front();
}

}  // namespace reserve
