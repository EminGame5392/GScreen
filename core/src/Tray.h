#pragma once
#include <windows.h>

namespace gs {

enum TrayCommand : UINT {
    kTrayOpen = 1001,
    kTraySplit,
    kTrayMerge,
    kTrayRestore,
    kTrayExit,
};

inline constexpr UINT WM_GS_TRAY = WM_APP + 1;

bool TrayAdd(HWND hwnd);
void TrayRemove(HWND hwnd);
void TraySetTip(HWND hwnd, const wchar_t* tip);
// Shows the context menu; returns the chosen TrayCommand or 0.
UINT TrayShowMenu(HWND hwnd, bool split);

} // namespace gs
