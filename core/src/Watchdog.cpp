// Watchdog mode of GScreen.Core.exe ("--watchdog --pid N"): a second, GPU-free instance of the same
// binary that brings the panel back when the main core dies. It lives in the core exe (not a separate
// file) so that Smart App Control, which judges each unsigned file on its own, can't allow the core
// and block its guard.
//  * The core exits with a non-zero code (crash, killed): run "GScreen.Core.exe --restore".
//  * Ctrl+Alt+Shift+R at any time: kill a hung core, then restore.
#include "Watchdog.h"
#include "Log.h"
#include <windows.h>
#include <string>

namespace gs {

namespace {

std::wstring SelfPath() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return path;
}

void RunRestore() {
    std::wstring exe = SelfPath();
    std::wstring dir = exe.substr(0, exe.find_last_of(L'\\'));
    std::wstring cmd = L"\"" + exe + L"\" --restore";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        LogError("Watchdog: could not start the restore ({})", GetLastError());
    }
}

} // namespace

int RunWatchdog(DWORD pid) {
    if (!pid) return 2;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GScreen.Watchdog");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    HANDLE core = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!core) { LogError("Watchdog: cannot open core process {} ({})", pid, GetLastError()); return 3; }

    const int kHotRestore = 1;
    if (!RegisterHotKey(nullptr, kHotRestore, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'R'))
        LogWarn("Watchdog: Ctrl+Alt+Shift+R is taken by another program");
    LogInfo("Watchdog guarding core {}", pid);

    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &core, FALSE, INFINITE, QS_HOTKEY);
        if (w == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(core, &code);
            if (code != 0) {
                LogWarn("Watchdog: core exited with 0x{:X}; restoring the display", code);
                RunRestore();
            }
            break;
        }
        MSG msg;
        bool restore = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            if (msg.message == WM_HOTKEY && msg.wParam == kHotRestore) restore = true;
        if (restore) {
            LogWarn("Watchdog: emergency restore (Ctrl+Alt+Shift+R)");
            TerminateProcess(core, 0xDEAD);
            WaitForSingleObject(core, 5000);
            RunRestore();
            break;
        }
    }
    CloseHandle(core);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}

} // namespace gs
