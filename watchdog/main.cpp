// GScreen.Watchdog — tiny guard process. It never touches the GPU, so it survives whatever kills the core.
//  * If GScreen.Core exits with a non-zero code (crash), it runs "GScreen.Core.exe --restore".
//  * Ctrl+Alt+Shift+R at any time: kills a hung core and runs the restore.
#include <windows.h>
#include <cwchar>
#include <string>

static std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L'\\'));
}

static void RunRestore() {
    std::wstring cmd = L"\"" + ExeDir() + L"\\GScreen.Core.exe\" --restore";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, ExeDir().c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 60000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    DWORD pid = 0;
    if (const wchar_t* p = wcsstr(cmdLine, L"--pid ")) pid = wcstoul(p + 6, nullptr, 10);
    if (!pid) return 2;

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\GScreen.Watchdog");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    HANDLE core = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    if (!core) return 3;

    const int kHotRestore = 1;
    RegisterHotKey(nullptr, kHotRestore, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_NOREPEAT, 'R');

    for (;;) {
        DWORD w = MsgWaitForMultipleObjects(1, &core, FALSE, INFINITE, QS_HOTKEY);
        if (w == WAIT_OBJECT_0) {
            DWORD code = 0;
            GetExitCodeProcess(core, &code);
            if (code != 0) RunRestore(); // crashed while possibly holding the panel
            break;
        }
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_HOTKEY && msg.wParam == kHotRestore) {
                TerminateProcess(core, 0xDEAD);
                WaitForSingleObject(core, 5000);
                RunRestore();
                CloseHandle(core);
                ReleaseMutex(mutex);
                return 0;
            }
        }
    }
    CloseHandle(core);
    ReleaseMutex(mutex);
    return 0;
}
