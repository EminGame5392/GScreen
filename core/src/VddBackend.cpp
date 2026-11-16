#include "VddBackend.h"
#include <cwctype>
#include "Log.h"
#include <windows.h>
#include <fstream>
#include <set>
#include <sstream>

namespace gs {

static const wchar_t* kPipeName = L"\\\\.\\pipe\\MTTVirtualDisplayPipe";

std::wstring VddBackend::SettingsPath() { return L"C:\\VirtualDisplayDriver\\vdd_settings.xml"; }

std::wstring DecodeVddReply(const unsigned char* data, size_t size) {
    // Measured against VDD 25.7.23: PING answers "PONG" as 4 ANSI bytes, GETSETTINGS answers in UTF-16LE.
    // ASCII text in UTF-16LE has a zero in every odd byte; use that to tell the two apart.
    bool utf16 = size >= 2 && size % 2 == 0;
    for (size_t i = 1; utf16 && i < size; i += 2)
        if (data[i] != 0) utf16 = false;
    std::wstring out;
    if (utf16) out.assign(reinterpret_cast<const wchar_t*>(data), size / 2);
    else
        for (size_t i = 0; i < size; ++i) out += (wchar_t)data[i];
    while (!out.empty() && (out.back() == L'\0' || out.back() == L' ')) out.pop_back();
    return out;
}

bool IsVddMonitorPath(const std::wstring& devicePath) {
    std::wstring upper = devicePath;
    for (auto& c : upper) c = (wchar_t)towupper(c);
    return upper.find(L"#MTT1337#") != std::wstring::npos;
}

bool VddBackend::Send(const std::wstring& command, std::wstring* reply) const {
    if (!WaitNamedPipeW(kPipeName, 2000)) return false;
    HANDLE h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
    DWORD written = 0;
    // UTF-16 without a terminating null: with the null appended the driver does not recognise the command.
    bool ok = WriteFile(h, command.c_str(), (DWORD)(command.size() * sizeof(wchar_t)), &written, nullptr) != 0;
    if (ok && reply) {
        unsigned char buf[1024]{};
        DWORD read = 0;
        if (ReadFile(h, buf, sizeof buf, &read, nullptr) || GetLastError() == ERROR_MORE_DATA) *reply = DecodeVddReply(buf, read);
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

std::vector<int> ZoneWidthGrid(int panelW) {
    std::set<int> widths;
    int maxW = panelW - kMinZoneWidth; // the widest zone still leaves room for one more
    if (maxW < kMinZoneWidth) return {panelW};
    for (int w = kMinZoneWidth; w <= maxW; w += kWidthAlign) {
        widths.insert(w);                                       // aligned zones
        if (int last = panelW - w; last >= kMinZoneWidth) widths.insert(last); // the last zone absorbs rounding
        for (int w2 = kMinZoneWidth; w2 <= maxW; w2 += kWidthAlign) // 3 zones: last = panelW - a - b
            if (int last = panelW - w - w2; last >= kMinZoneWidth) widths.insert(last);
    }
    return {widths.begin(), widths.end()};
}

std::string PatchVddSettings(const std::string& original, int panelW, int panelH, int refreshHz, const std::string& gpuName) {
    std::string xml = original.empty() ? "<?xml version='1.0' encoding='utf-8'?>\n<vdd_settings>\n</vdd_settings>\n" : original;

    // <count> lives inside <monitors>; the driver matches by element name only.
    ReplaceElement(xml, "count", std::to_string(kMaxZones));
    if (!gpuName.empty()) ReplaceElement(xml, "friendlyname", gpuName);

    std::ostringstream res;
    res << "\n";
    const int h = panelH;
    for (int w : ZoneWidthGrid(panelW)) {
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

bool VddBackend::Configure(int panelW, int panelH, int refreshHz, const std::string& gpuName) {
    std::string original;
    {
        std::ifstream f(SettingsPath(), std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); original = ss.str(); }
    }
    std::string patched = PatchVddSettings(original, panelW, panelH, refreshHz, gpuName);
    if (patched == original) {
        LogInfo("VDD settings already match the panel; no driver reload");
        return true;
    }
    {
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
    // SETDISPLAYCOUNT writes <count> and reloads the adapter, which re-reads the modes. On VDD 25.7.23 the
    // reload is a driver crash that Windows recovers from, hence the "only when the file changed" rule above.
    if (!Send(L"SETDISPLAYCOUNT " + std::to_wstring(kMaxZones))) {
        LogError("VDD pipe did not accept SETDISPLAYCOUNT");
        return false;
    }
    LogInfo("VDD settings rewritten ({} monitors, {} widths, height {}, {} Hz); driver reloading",
            kMaxZones, ZoneWidthGrid(panelW).size(), panelH, refreshHz);
    return true;
}

} // namespace gs
