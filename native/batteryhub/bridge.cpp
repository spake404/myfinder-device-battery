#include "widget.h"
#include "i18n.h"
#include <cwchar>
#include <utility>

const wchar_t* T(const wchar_t* text) { return text; }

std::wstring TF(const wchar_t* text, std::initializer_list<std::pair<const wchar_t*, std::wstring>> args) {
    std::wstring result(text);
    for (const auto& argument : args) {
        std::wstring key = std::wstring(L"{") + argument.first + L"}";
        auto position = result.find(key);
        if (position != std::wstring::npos) result.replace(position, key.size(), argument.second);
    }
    return result;
}

struct TaskbarReading {
    int kind, percent, flags;
    wchar_t label[8];
};

extern "C" __declspec(dllexport) int TaskbarDetectBrand(const wchar_t* provider, int vid, const wchar_t* name, int kind) {
    DeviceStatus device;
    device.key = std::wstring(provider) + L":";
    device.vid = vid > 0 && vid <= 0xffff ? static_cast<unsigned short>(vid) : 0;
    device.name = name;
    device.kind = static_cast<DeviceKind>(kind);
    return static_cast<int>(BrandOf(device));
}

extern "C" __declspec(dllexport) COLORREF TaskbarBrandColor(int accent, BOOL light) {
    const auto color = AccentColor(static_cast<Accent>(accent), light != FALSE);
    return RGB(color.r, color.g, color.b);
}

extern "C" __declspec(dllexport) BOOL TaskbarCreate(HWND owner, UINT message, BOOL light) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&TaskbarCreate), &module)) return FALSE;
    return widget::Create(module, owner, message, light);
}

extern "C" __declspec(dllexport) BOOL TaskbarUpdate(const TaskbarReading* readings, int count,
                                                      const wchar_t* tooltip, BOOL light, COLORREF badge) {
    if (!widget::Alive() || count < 0 || count > 4) return FALSE;
    std::vector<WidgetItem> items;
    for (int index = 0; index < count; ++index) {
        const auto& reading = readings[index];
        WidgetItem item;
        item.kind = static_cast<DeviceKind>(reading.kind);
        item.percent = reading.percent;
        item.charging = (reading.flags & 1) != 0;
        item.full = item.charging && item.percent == 100;
        item.dim = (reading.flags & 2) != 0;
        item.low = (reading.flags & 4) != 0;
        item.accent = StyleOf(static_cast<Accent>(reading.flags >> 8)).accent;
        if (item.percent < 0) item.text = L"--";
        else if (reading.flags & 8) item.text = L"≈" + std::to_wstring(item.percent) + L"%";
        item.label.assign(reading.label, wcsnlen(reading.label, 8));
        items.push_back(std::move(item));
    }
    widget::SetBadge(badge);
    widget::SetLightTheme(light);
    widget::SetItems(items, tooltip);
    return widget::Reposition();
}

extern "C" __declspec(dllexport) BOOL TaskbarReposition() { return widget::Reposition(); }

extern "C" __declspec(dllexport) void TaskbarDestroy() {
    widget::Destroy();
    widget::SetItems({}, L"MyFinder Device Battery");
    widget::SetBadge(CLR_INVALID);
}
