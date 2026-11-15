#include <windows.h>
#include "IpcServer.h"
#include "Log.h"

namespace gs {

bool IpcServer::Start(HWND target, UINT message, Handler handler) {
    target_ = target;
    message_ = message;
    handler_ = std::move(handler);
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
    return true;
}

void IpcServer::Stop() {
    stop_ = true;
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) CloseHandle(stopEvent_);
    stopEvent_ = nullptr;
}

void IpcServer::Dispatch(WPARAM wParam, LPARAM lParam) const {
    auto* req = reinterpret_cast<const std::string*>(wParam);
    auto* resp = reinterpret_cast<std::string*>(lParam);
    *resp = handler_(*req);
}

void IpcServer::Run() {
    SetThreadDescription(GetCurrentThread(), L"GScreen IPC");
    while (!stop_) {
        HANDLE pipe = CreateNamedPipeW(kCorePipeName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       1, 64 * 1024, 64 * 1024, 0, nullptr); // default DACL: this user, SYSTEM, admins
        if (pipe == INVALID_HANDLE_VALUE) {
            LogError("CreateNamedPipe failed: {}", GetLastError());
            WaitForSingleObject(stopEvent_, 1000);
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD err = GetLastError();
        if (!connected && err == ERROR_IO_PENDING) {
            HANDLE waits[2] = {ov.hEvent, stopEvent_};
            DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0) { CancelIo(pipe); CloseHandle(ov.hEvent); CloseHandle(pipe); break; }
        } else if (!connected && err != ERROR_PIPE_CONNECTED) {
            CloseHandle(ov.hEvent);
            CloseHandle(pipe);
            continue;
        }

        std::string buffer;
        char chunk[4096];
        while (!stop_) {
            DWORD read = 0;
            ResetEvent(ov.hEvent);
            if (!ReadFile(pipe, chunk, sizeof chunk, &read, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) break;
                HANDLE waits[2] = {ov.hEvent, stopEvent_};
                if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0) { CancelIo(pipe); break; }
                if (!GetOverlappedResult(pipe, &ov, &read, FALSE) || read == 0) break;
            }
            buffer.append(chunk, read);
            if (buffer.size() > 1024 * 1024) break; // a misbehaving client
            size_t nl;
            while ((nl = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                std::string response;
                SendMessageW(target_, message_, (WPARAM)&line, (LPARAM)&response);
                response += '\n';
                DWORD written = 0;
                ResetEvent(ov.hEvent);
                if (!WriteFile(pipe, response.data(), (DWORD)response.size(), &written, &ov)) {
                    if (GetLastError() != ERROR_IO_PENDING) break;
                    GetOverlappedResult(pipe, &ov, &written, TRUE);
                }
            }
        }
        DisconnectNamedPipe(pipe);
        CloseHandle(ov.hEvent);
        CloseHandle(pipe);
    }
}

} // namespace gs
