#include "Log.h"
#include <windows.h>
#include <shlobj.h>
#include <mutex>
#include <cstdio>
#include <chrono>

namespace gs {

static std::mutex g_logMutex;
static FILE* g_logFile = nullptr;

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring AppDataDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = std::wstring(p) + L"\\GScreen";
        CoTaskMemFree(p);
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    return dir;
}

void LogInit() {
    std::lock_guard lock(g_logMutex);
    if (g_logFile) return;
    std::wstring path = AppDataDir() + L"\\core.log";
    _wfopen_s(&g_logFile, path.c_str(), L"a, ccs=UTF-8");
}

void LogWrite(const std::string& level, const std::string& msg) {
    auto now = std::chrono::zoned_time{std::chrono::current_zone(), std::chrono::system_clock::now()};
    std::string line = std::format("{:%Y-%m-%d %H:%M:%S} [{}] {}\n", now, level, msg);
    std::lock_guard lock(g_logMutex);
    OutputDebugStringW(Widen(line).c_str());
    if (g_logFile) {
        fputws(Widen(line).c_str(), g_logFile);
        fflush(g_logFile);
    }
}

} // namespace gs
