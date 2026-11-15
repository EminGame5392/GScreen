#pragma once
#include <windows.h>
#include <atomic>
#include <thread>

namespace gs {

// Window mode keeps the real panel on the desktop, parked in a corner and hidden under the compositor window.
// PanelGuard stops the cursor and application windows from ending up on that parked panel.
class PanelGuard {
public:
    ~PanelGuard() { Stop(); }
    // panel: the parked panel's desktop rect. zones: the whole row of virtual monitors.
    // home: where stray windows go (zone A). ignore: our own compositor window.
    void Start(RECT panel, RECT zones, RECT home, HWND ignore);
    void Stop();
    // Moves any top-level window that sits on the panel into the home zone. Called from a timer on the UI thread.
    void RelocateStrayWindows() const;

private:
    static LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp);
    void HookThread();

    RECT panel_{}, home_{};
    HWND ignore_ = nullptr;
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<bool> running_{false};
};

} // namespace gs
