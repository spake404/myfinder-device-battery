#pragma once
#include <windows.h>

// Asks the Windows 11 taskbar to end its row of task buttons `pixels` before the notification area, so
// the strip there never covers a button. 0 gives the full width back.
namespace reserve {
void Update(HWND taskbar, HINSTANCE module, int pixels);
}
