#pragma once
#include <windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace gs {

inline constexpr wchar_t kCorePipeName[] = L"\\\\.\\pipe\\GScreen.Core";

// Line-delimited JSON over a local named pipe. One request line → one response line.
// The handler is invoked on the main thread (via SendMessage to `target`).
class IpcServer {
public:
    using Handler = std::function<std::string(const std::string&)>;
    bool Start(HWND target, UINT message, Handler handler);
    void Stop();
    // Called by the window procedure for `message`: wParam = const std::string* request, lParam = std::string* response.
    void Dispatch(WPARAM wParam, LPARAM lParam) const;

private:
    void Run();
    HWND target_ = nullptr;
    UINT message_ = 0;
    Handler handler_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    HANDLE stopEvent_ = nullptr;
};

} // namespace gs
