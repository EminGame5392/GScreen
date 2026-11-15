#include "PanelGuard.h"
#include "Log.h"
#include <algorithm>

namespace gs {

static PanelGuard* g_guard = nullptr;
static RECT g_panel{}, g_zones{};

void PanelGuard::Start(RECT panel, RECT zones, RECT home, HWND ignore) {
    Stop();
    panel_ = panel;
    home_ = home;
    ignore_ = ignore;
    g_panel = panel;
    g_zones = zones;
    g_guard = this;
    running_ = true;
    thread_ = std::thread([this] { HookThread(); });
}

void PanelGuard::Stop() {
    if (!running_) return;
    running_ = false;
    if (threadId_) PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    threadId_ = 0;
    g_guard = nullptr;
}

LRESULT CALLBACK PanelGuard::MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_MOUSEMOVE) {
        auto* m = reinterpret_cast<MSLLHOOKSTRUCT*>(lp);
        if (PtInRect(&g_panel, m->pt)) {
            // The panel touches the zone row only at one corner; pin the cursor to the nearest point of the row.
            LONG x = std::clamp(m->pt.x, g_zones.left, g_zones.right - 1);
            LONG y = std::clamp(m->pt.y, g_zones.top, g_zones.bottom - 1);
            SetCursorPos(x, y);
            return 1; // swallow the move
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void PanelGuard::HookThread() {
    SetThreadDescription(GetCurrentThread(), L"GScreen cursor guard");
    // LL hooks run on the installing thread; it needs high priority so the cursor never stutters.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE); // create the message queue before publishing the id
    threadId_ = GetCurrentThreadId();
    HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, MouseHook, GetModuleHandleW(nullptr), 0);
    if (!hook) LogError("SetWindowsHookEx(WH_MOUSE_LL) failed: {}", GetLastError());
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    }
    if (hook) UnhookWindowsHookEx(hook);
}

void PanelGuard::RelocateStrayWindows() const {
    if (!running_) return;
    struct Ctx { const PanelGuard* self; };
    Ctx ctx{this};
    EnumWindows(
        [](HWND w, LPARAM lp) -> BOOL {
            auto* self = reinterpret_cast<Ctx*>(lp)->self;
            if (w == self->ignore_ || !IsWindowVisible(w) || IsIconic(w)) return TRUE;
            if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
            RECT r;
            if (!GetWindowRect(w, &r)) return TRUE;
            RECT inter;
            if (!IntersectRect(&inter, &r, &self->panel_)) return TRUE;
            // More than half of it on the panel → move it into zone A, keeping its size where it fits.
            LONG area = (r.right - r.left) * (r.bottom - r.top);
            LONG onPanel = (inter.right - inter.left) * (inter.bottom - inter.top);
            if (area <= 0 || onPanel * 2 < area) return TRUE;
            WINDOWPLACEMENT wp{sizeof wp};
            GetWindowPlacement(w, &wp);
            bool maximized = wp.showCmd == SW_SHOWMAXIMIZED;
            if (maximized) ShowWindow(w, SW_RESTORE);
            const RECT& h = self->home_;
            int width = std::min<LONG>(r.right - r.left, h.right - h.left - 80);
            int height = std::min<LONG>(r.bottom - r.top, h.bottom - h.top - 80);
            SetWindowPos(w, nullptr, h.left + 40, h.top + 40, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
            if (maximized) ShowWindow(w, SW_MAXIMIZE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
}

} // namespace gs
