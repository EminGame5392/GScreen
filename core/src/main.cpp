// GScreen.Core — background process that owns the split/merge state.
//   GScreen.Core.exe            run with tray icon, hotkeys and IPC
//   GScreen.Core.exe --preview  show the composite in a window; the panel stays on the desktop (safe for testing)
//   GScreen.Core.exe --restore  emergency: bring the panel back and drop the virtual monitors, then exit
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>
#include "Watchdog.h"
#include "Engine.h"
#include "IpcServer.h"
#include "Log.h"
#include "Tray.h"
#include <string>

using namespace gs;

namespace {

constexpr UINT WM_GS_IPC = WM_APP + 2;
constexpr UINT_PTR kTickTimer = 1;
enum HotkeyId { kHotToggle = 1, kHotPreset1 = 11, kHotPreset4 = 14 };

Engine g_engine;
IpcServer g_ipc;
bool g_busy = false; // guards against re-entry: engine operations pump messages while they wait

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L'\\'));
}

bool LaunchSibling(const wchar_t* exe, const std::wstring& args) {
    std::wstring path = ExeDir() + L"\\" + exe;
    auto rc = (INT_PTR)ShellExecuteW(nullptr, L"open", path.c_str(), args.empty() ? nullptr : args.c_str(), ExeDir().c_str(), SW_SHOWNORMAL);
    if (rc <= 32) LogWarn("Could not start {}: ShellExecute {} (error {})", Narrow(exe), (long long)rc, GetLastError());
    return rc > 32;
}

void UpdateTip(HWND hwnd) {
    auto s = g_engine.Status();
    std::wstring tip = s["mode"].asString() == "split"
                           ? L"GScreen — зоны: " + std::to_wstring(s["zones"].asArray().size())
                           : L"GScreen — единый экран";
    TraySetTip(hwnd, tip.c_str());
}

// Runs an engine operation unless one is already in progress.
template <class F> std::string Guarded(F&& f) {
    if (g_busy) return R"({"ok":false,"error":"Подождите, предыдущая команда ещё выполняется"})";
    g_busy = true;
    std::string err;
    bool ok = f(err);
    g_busy = false;
    Json r(Json::Object{});
    r.set("ok", ok);
    if (!ok) r.set("error", err);
    r.set("status", g_engine.Status());
    return r.dump();
}

std::string HandleRequest(const std::string& line) {
    auto req = Json::parse(line);
    if (!req) return R"({"ok":false,"error":"Неверный JSON"})";
    const std::string cmd = (*req)["cmd"].asString();
    if (cmd == "status") {
        Json r(Json::Object{});
        r.set("ok", true);
        r.set("busy", g_busy);
        r.set("status", g_engine.Status());
        return r.dump();
    }
    if (cmd == "apply") return Guarded([&](std::string& e) { return g_engine.Apply(LayoutFromJson((*req)["layout"]), e); });
    if (cmd == "preset") return Guarded([&](std::string& e) { return g_engine.ApplyPreset((*req)["id"].asString(), e); });
    if (cmd == "toggle") return Guarded([&](std::string& e) { return g_engine.ToggleMode(e); });
    if (cmd == "merge") return Guarded([&](std::string&) { g_engine.RestoreMerged(); return true; });
    return R"({"ok":false,"error":"Неизвестная команда"})";
}

void RunAsync(HWND hwnd, const std::string& cmdJson) {
    // Hotkeys and tray clicks use the same path as IPC so every entry point shares the busy guard.
    std::string resp = HandleRequest(cmdJson);
    auto j = Json::parse(resp);
    if (j && !(*j)["ok"].asBool()) {
        std::wstring msg = Widen((*j)["error"].asString());
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof nid;
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        wcscpy_s(nid.szInfoTitle, L"GScreen");
        wcsncpy_s(nid.szInfo, msg.c_str(), _TRUNCATE);
        nid.dwInfoFlags = NIIF_WARNING;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
    UpdateTip(hwnd);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GS_IPC:
        g_ipc.Dispatch(wp, lp);
        return 0;
    case WM_HOTKEY:
        if (wp == kHotToggle) RunAsync(hwnd, R"({"cmd":"toggle"})");
        else if (wp >= kHotPreset1 && wp <= kHotPreset4) {
            auto presets = DefaultPresets();
            size_t i = wp - kHotPreset1;
            if (i < presets.size()) RunAsync(hwnd, R"({"cmd":"preset","id":")" + presets[i].id + "\"}");
        }
        return 0;
    case WM_TIMER:
        if (wp == kTickTimer && !g_busy) g_engine.Tick();
        return 0;
    case WM_GS_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK:
        case NIN_SELECT:
            LaunchSibling(L"GScreen.App.exe", L"");
            break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP: {
            bool split = g_engine.Status()["mode"].asString() == "split";
            switch (TrayShowMenu(hwnd, split)) {
            case kTrayOpen: LaunchSibling(L"GScreen.App.exe", L""); break;
            case kTraySplit: RunAsync(hwnd, R"({"cmd":"preset","id":")" + g_engine.GetConfig().layout.presetId + "\"}"); break;
            case kTrayMerge: RunAsync(hwnd, R"({"cmd":"merge"})"); break;
            case kTrayRestore: RunAsync(hwnd, R"({"cmd":"merge"})"); break;
            case kTrayExit: DestroyWindow(hwnd); break;
            }
            break;
        }
        }
        return 0;
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wp) g_engine.RestoreMerged();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kTickTimer);
        g_ipc.Stop();
        g_engine.RestoreMerged();
        TrayRemove(hwnd);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    LogInit();
    std::wstring args = cmdLine ? cmdLine : L"";

    if (args.find(L"--watchdog") != std::wstring::npos) {
        size_t at = args.find(L"--pid ");
        return RunWatchdog(at == std::wstring::npos ? 0 : wcstoul(args.c_str() + at + 6, nullptr, 10));
    }

    if (args.find(L"--restore") != std::wstring::npos) {
        LogInfo("--restore requested");
        g_engine.Init();
        g_engine.RestoreMerged();
        return 0;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GScreen.Core");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0; // one core per user session

    if (!g_engine.Init()) LogWarn("Engine started without a physical panel");
    if (args.find(L"--preview") != std::wstring::npos) g_engine.SetPreview(true);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"GScreenCore";
    RegisterClassW(&wc);
    // A real (hidden) top-level window, not HWND_MESSAGE: it has to receive WM_ENDSESSION and tray messages.
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"GScreen.Core", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    TrayAdd(hwnd);
    if (!RegisterHotKey(hwnd, kHotToggle, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'G'))
        LogWarn("Ctrl+Alt+G is taken by another program");
    for (int i = 0; i < 4; ++i)
        RegisterHotKey(hwnd, kHotPreset1 + i, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, '1' + i);
    g_ipc.Start(hwnd, WM_GS_IPC, HandleRequest);
    SetTimer(hwnd, kTickTimer, 500, nullptr);
    UpdateTip(hwnd);

    // The watchdog (this exe in --watchdog mode) brings the panel back if this process crashes or hangs.
    g_engine.SetWatchdogRunning(
        LaunchSibling(L"GScreen.Core.exe", L"--watchdog --pid " + std::to_wstring(GetCurrentProcessId())));

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}
