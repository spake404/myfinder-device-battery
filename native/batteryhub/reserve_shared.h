#pragma once
#include <windows.h>

// Shared between MyFinderTaskbar.dll (the strip) and MyFinderTaskbarReserve.dll (loaded into Explorer).
constexpr wchar_t kReserveClass[] = L"MyFinderTaskbarReserve";
constexpr UINT kReserveMessage = WM_APP + 0x4D;  // wParam: margin in DIPs, lParam: owner process id
constexpr CLSID kReserveTapClsid = { 0x4c0610fa, 0x01cf, 0x4a56, { 0x91, 0xd6, 0x62, 0xb0, 0x82, 0xfe, 0x05, 0xc1 } };
