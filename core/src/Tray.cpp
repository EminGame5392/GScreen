#include "Tray.h"
#include <shellapi.h>
#include <cwchar>

namespace gs {

static NOTIFYICONDATAW MakeData(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uID = 1;
    return nid;
}

bool TrayAdd(HWND hwnd) {
    auto nid = MakeData(hwnd);
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_GS_TRAY;
    nid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"GScreen");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
    return true;
}

void TrayRemove(HWND hwnd) {
    auto nid = MakeData(hwnd);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TraySetTip(HWND hwnd, const wchar_t* tip) {
    auto nid = MakeData(hwnd);
    nid.uFlags = NIF_TIP;
    wcsncpy_s(nid.szTip, tip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

UINT TrayShowMenu(HWND hwnd, bool split) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, kTrayOpen, L"Открыть GScreen");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (split ? MF_CHECKED : 0), kTraySplit, L"Разделить на зоны\tCtrl+Alt+1…4");
    AppendMenuW(m, MF_STRING | (!split ? MF_CHECKED : 0), kTrayMerge, L"Единый экран (игры)\tCtrl+Alt+G");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, kTrayRestore, L"Аварийный возврат\tCtrl+Alt+Shift+R");
    AppendMenuW(m, MF_STRING, kTrayExit, L"Выход");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // required so the menu closes when clicking elsewhere
    UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    return cmd;
}

} // namespace gs
