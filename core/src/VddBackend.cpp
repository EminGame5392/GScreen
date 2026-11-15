#include "VddBackend.h"
#include "Log.h"
#include <windows.h>
#include <fstream>
#include <set>
#include <sstream>

namespace gs {

static const wchar_t* kPipeName = L"\\\\.\\pipe\\MTTVirtualDisplayPipe";

std::wstring VddBackend::SettingsPath() { return L"C:\\VirtualDisplayDriver\\vdd_settings.xml"; }

bool VddBackend::Send(const std::wstring& command, std::wstring* reply) const {
    if (!WaitNamedPipeW(kPipeName, 2000)) return false;
    HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    DWORD written = 0;
    // The driver reads UTF-16 and matches commands by prefix.
    bool ok = WriteFile(h, command.c_str(), (DWORD)((command.size() + 1) * sizeof(wchar_t)), &written, nullptr) != 0;
    if (ok && reply) {
        wchar_t buf[512]{};
        DWORD read = 0;
        if (ReadFile(h, buf, sizeof buf - sizeof(wchar_t), &read, nullptr)) *reply = std::wstring(buf, read / sizeof(wchar_t));
    }
    CloseHandle(h);
    return ok;
}

bool VddBackend::Ping() const {
    std::wstring reply;
    return Send(L"PING", &reply) && reply.find(L"PONG") != std::wstring::npos;
}

bool VddBackend::IsInstalled() const {
    return GetFileAttributesW(SettingsPath().c_str()) != INVALID_FILE_ATTRIBUTES && Ping();
}

// Replaces the first <tag>…</tag> with the given inner text; appends before the root's closing tag if absent.
static void ReplaceElement(std::string& xml, const std::string& tag, const std::string& inner) {
    std::string open = "<" + tag + ">", close = "</" + tag + ">";
    size_t a = xml.find(open), b = a == std::string::npos ? a : xml.find(close, a);
    if (a != std::string::npos && b != std::string::npos) {
        xml.replace(a + open.size(), b - a - open.size(), inner);
        return;
    }
    size_t rootEnd = xml.rfind("</");
    std::string block = open + inner + close + "\n";
    if (rootEnd == std::string::npos) xml += block;
    else xml.insert(rootEnd, block);
}

std::string PatchVddSettings(const std::string& original, int count, const std::vector<ZoneRect>& zones, int refreshHz,
                             const std::string& gpuName) {
    std::string xml = original.empty() ? "<?xml version='1.0' encoding='utf-8'?>\n<vdd_settings>\n</vdd_settings>\n" : original;

    // <count> lives inside <monitors>; the driver matches by element name only.
    ReplaceElement(xml, "count", std::to_string(count));
    if (!gpuName.empty()) ReplaceElement(xml, "friendlyname", gpuName);

    std::set<std::pair<int, int>> sizes;
    for (auto& z : zones) sizes.insert({z.width, z.height});
    std::ostringstream res;
    res << "\n";
    for (auto& [w, h] : sizes) {
        res << "    <resolution>\n"
            << "      <width>" << w << "</width>\n"
            << "      <height>" << h << "</height>\n"
            << "      <refresh_rate>" << refreshHz << "</refresh_rate>\n"
            << "    </resolution>\n";
    }
    res << "  ";
    ReplaceElement(xml, "resolutions", res.str());
    return xml;
}

bool VddBackend::Configure(const std::vector<ZoneRect>& zones, int refreshHz, const std::string& gpuName) {
    std::string original;
    {
        std::ifstream f(SettingsPath(), std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); original = ss.str(); }
    }
    std::string patched = PatchVddSettings(original, (int)zones.size(), zones, refreshHz, gpuName);
    if (patched != original) {
        // Keep a one-time backup of the user's original settings.
        std::wstring backup = SettingsPath() + L".gscreen-backup";
        if (!original.empty() && GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::ofstream b(backup, std::ios::binary);
            b << original;
        }
        std::ofstream f(SettingsPath(), std::ios::binary | std::ios::trunc);
        if (!f) {
            LogError("Cannot write {} (run GScreen once as administrator or fix folder permissions)", Narrow(SettingsPath()));
            return false;
        }
        f << patched;
    }
    // SETDISPLAYCOUNT writes <count> and reloads the adapter, which also re-reads the modes.
    if (!Send(L"SETDISPLAYCOUNT " + std::to_wstring(zones.size()))) {
        LogError("VDD pipe did not accept SETDISPLAYCOUNT");
        return false;
    }
    LogInfo("VDD configured: {} monitor(s) at {} Hz", zones.size(), refreshHz);
    return true;
}

} // namespace gs
