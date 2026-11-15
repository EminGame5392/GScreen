#pragma once
#include <string>
#include <format>

namespace gs {

void LogInit();
void LogWrite(const std::string& level, const std::string& msg);

template <class... A> void LogInfo(std::format_string<A...> f, A&&... a) { LogWrite("INFO", std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogWarn(std::format_string<A...> f, A&&... a) { LogWrite("WARN", std::format(f, std::forward<A>(a)...)); }
template <class... A> void LogError(std::format_string<A...> f, A&&... a) { LogWrite("ERROR", std::format(f, std::forward<A>(a)...)); }

std::string Narrow(const std::wstring& w);
std::wstring Widen(const std::string& s);
std::wstring AppDataDir(); // %APPDATA%\GScreen, created on demand

} // namespace gs
