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

// Inner text of the first <tag>…</tag> at or after `from`; npos-safe. `end` gets the position after </tag>.
static std::string Inner(const std::string& xml, const std::string& tag, size_t from = 0, size_t* end = nullptr) {
    std::string open = "<" + tag + ">", close = "</" + tag + ">";
    size_t a = xml.find(open, from);
    if (a == std::string::npos) return {};
    size_t b = xml.find(close, a);
    if (b == std::string::npos) return {};
    if (end) *end = b + close.size();
    return xml.substr(a + open.size(), b - a - open.size());
}

static int ToInt(const std::string& s) {
    try { return std::stoi(s); } catch (...) { return 0; }
}

VddSettings ParseVddSettings(const std::string& xml) {
    VddSettings s;
    s.count = ToInt(Inner(xml, "count"));
    s.gpu = Inner(xml, "friendlyname");
    std::string res = Inner(xml, "resolutions");
    size_t pos = 0, next = 0;
    for (;;) {
        std::string block = Inner(res, "resolution", pos, &next);
        if (block.empty()) break;
        int w = ToInt(Inner(block, "width")), h = ToInt(Inner(block, "height"));
        if (w > 0 && h > 0) s.sizes.insert({w, h});
        pos = next;
    }
    return s;
}

std::set<int> RequiredWidths(int panelW, const std::vector<ZoneRect>& zones) {
    std::set<int> widths;
    for (auto& p : DefaultPresets())
        for (auto& z : ComputeZones(panelW, 1, p.ratios)) widths.insert(z.width);
    for (auto& z : zones) widths.insert(z.width);
    return widths;
}

bool VddSettingsSatisfy(const std::string& xml, const std::set<int>& widths, int panelH, const std::string& gpuName) {
    VddSettings s = ParseVddSettings(xml);
    if (s.count != kMaxZones) return false;
    if (!gpuName.empty() && s.gpu != gpuName) return false;
    for (int w : widths)
        if (!s.sizes.count({w, panelH})) return false;
    return true;
}

std::string PatchVddSettings(const std::string& original, const std::set<int>& widthsIn, int panelH, int refreshHz,
                             const std::string& gpuName) {
    std::string xml = original.empty() ? "<?xml version='1.0' encoding='utf-8'?>\n<vdd_settings>\n</vdd_settings>\n" : original;

    // Keep widths registered earlier (so going back to an old custom layout needs no reload), but cap the
    // list: VDD 25.7.23 creates no monitors at all with ~120 resolutions.
    std::set<int> widths = widthsIn;
    for (auto& [w, h] : ParseVddSettings(xml).sizes)
        if (h == panelH && w >= kMinZoneWidth) widths.insert(w);
    if (widths.size() > kMaxVddWidths) widths = widthsIn;

    // <count> lives inside <monitors>; the driver matches by element name only.
    ReplaceElement(xml, "count", std::to_string(kMaxZones));
    if (!gpuName.empty()) ReplaceElement(xml, "friendlyname", gpuName);

    std::ostringstream res;
    res << "\n";
    for (int w : widths) {
        res << "    <resolution>\n"
            << "      <width>" << w << "</width>\n"
            << "      <height>" << panelH << "</height>\n"
            << "      <refresh_rate>" << refreshHz << "</refresh_rate>\n"
            << "    </resolution>\n";
    }
    res << "  ";
    ReplaceElement(xml, "resolutions", res.str());
    return xml;
}

bool VddBackend::Configure(const std::vector<ZoneRect>& zones, int panelW, int panelH, int refreshHz,
                           const std::string& gpuName) {
    std::string original;
    {
        std::ifstream f(SettingsPath(), std::ios::binary);
        if (f) { std::stringstream ss; ss << f.rdbuf(); original = ss.str(); }
    }
    // Compare content, not text: the driver re-serialises the file on its own reload.
    std::set<int> widths = RequiredWidths(panelW, zones);
    if (VddSettingsSatisfy(original, widths, panelH, gpuName)) {
        LogInfo("VDD already offers these modes; no driver reload");
        return true;
    }
    std::string patched = PatchVddSettings(original, widths, panelH, refreshHz, gpuName);
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
    // reload is a driver crash that Windows recovers from, so it must stay rare (first run, new custom width).
    if (!Send(L"SETDISPLAYCOUNT " + std::to_wstring(kMaxZones))) {
        LogError("VDD pipe did not accept SETDISPLAYCOUNT");
        return false;
    }
    LogInfo("VDD settings rewritten ({} monitors, {} widths, height {}); driver reloading",
            kMaxZones, ParseVddSettings(patched).sizes.size(), panelH);
    return true;
}

} // namespace gs
