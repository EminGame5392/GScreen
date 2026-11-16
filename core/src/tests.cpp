// Self-tests for pure logic (no GPU, no driver). Run: GScreen.Tests.exe → exit code 0 on success.
#include "Json.h"
#include "Layout.h"
#include "VddBackend.h"
#include <algorithm>
#include <set>
#include <cstdio>

using namespace gs;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static int sumW(const std::vector<ZoneRect>& z) { int s = 0; for (auto& r : z) s += r.width; return s; }

int main() {
    // Three equal zones on the Odyssey G9 panel.
    auto z = ComputeZones(5120, 1440, {1, 1, 1});
    CHECK(z.size() == 3);
    CHECK(sumW(z) == 5120);
    CHECK(z[0].width == 1696 && z[1].width == 1696 && z[2].width == 1728);
    CHECK(z[1].x == 1696 && z[2].x == 3392);
    for (auto& r : z) CHECK(r.height == 1440);

    // 1:2:1 focus layout.
    z = ComputeZones(5120, 1440, {1, 2, 1});
    CHECK(z.size() == 3 && z[0].width == 1280 && z[1].width == 2560 && z[2].width == 1280);

    // Two halves, game + chat.
    z = ComputeZones(5120, 1440, {1, 1});
    CHECK(z.size() == 2 && z[0].width == 2560 && z[1].width == 2560);
    z = ComputeZones(5120, 1440, {7, 3});
    CHECK(z.size() == 2 && z[0].width == 3584 && sumW(z) == 5120);

    // Extreme ratio is clamped to the minimum zone width.
    z = ComputeZones(5120, 1440, {100, 1});
    CHECK(z[1].width >= kMinZoneWidth && sumW(z) == 5120);
    z = ComputeZones(5120, 1440, {1, 100, 1});
    CHECK(z[0].width >= kMinZoneWidth && z[2].width >= kMinZoneWidth && sumW(z) == 5120);

    // Narrow panel can't fit three zones.
    z = ComputeZones(1600, 900, {1, 1, 1});
    CHECK(z.size() == 2 && sumW(z) == 1600);

    // Invalid input → single zone.
    z = ComputeZones(5120, 1440, {});
    CHECK(z.size() == 1 && z[0].width == 5120);
    z = ComputeZones(5120, 1440, {-1, 0});
    CHECK(z.size() == 1);

    // Widths stay aligned.
    z = ComputeZones(5120, 1440, {0.37, 0.41, 0.22});
    for (size_t i = 0; i + 1 < z.size(); ++i) CHECK(z[i].width % kWidthAlign == 0);
    CHECK(sumW(z) == 5120);

    // JSON round trip.
    auto j = Json::parse(R"({"cmd":"apply","layout":{"mode":"split","ratios":[1,2,1],"refreshHz":240,"name":"Фокус \"центр\""}})");
    CHECK(j.has_value());
    CHECK((*j)["cmd"].asString() == "apply");
    Layout l = LayoutFromJson((*j)["layout"]);
    CHECK(l.mode == Mode::Split && l.ratios.size() == 3 && l.ratios[1] == 2 && l.refreshHz == 240);
    auto again = Json::parse(LayoutToJson(l).dump());
    CHECK(again.has_value() && LayoutFromJson(*again).ratios == l.ratios);
    CHECK((*j)["layout"]["name"].asString() == "Фокус \"центр\"");
    CHECK(!Json::parse("{bad").has_value());
    CHECK(!Json::parse("[1,2").has_value());
    CHECK(Json::parse("\"\\u0410\"")->asString() == "\xD0\x90");

    // VDD pipe replies, as measured on VDD 25.7.23.
    const unsigned char pong[] = {'P', 'O', 'N', 'G'};
    CHECK(DecodeVddReply(pong, 4) == L"PONG");
    const unsigned char settings[] = {'S', 0, 'E', 0, 'T', 0, ' ', 0, 0, 0};
    CHECK(DecodeVddReply(settings, sizeof settings) == L"SET");
    CHECK(DecodeVddReply(pong, 0).empty());

    // VDD monitors report HDMI as output technology; the device path identifies them.
    CHECK(IsVddMonitorPath(LR"(\\?\DISPLAY#MTT1337#1&28a6823a&0&UID256#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7})"));
    CHECK(IsVddMonitorPath(LR"(\\?\display#mtt1337#1&28a6823a&0&uid257#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7})"));
    CHECK(!IsVddMonitorPath(LR"(\\?\DISPLAY#SAME05C#5&1acee2bb&0&UID4357#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7})"));
    CHECK(!IsVddMonitorPath(L""));

    // Settings patch against the layout of the driver's own default vdd_settings.xml.
    const std::string vddDefault =
        "<?xml version='1.0' encoding='utf-8'?>\n<vdd_settings>\n    <monitors>\n        <count>1</count>\n    </monitors>\n"
        "    <gpu>\n        <friendlyname>default</friendlyname>\n    </gpu>\n"
        "\t<global>\n\t\t<g_refresh_rate>60</g_refresh_rate>\n\t</global>\n"
        "    <resolutions>\n        <resolution>\n            <width>800</width>\n            <height>600</height>\n"
        "            <refresh_rate>30</refresh_rate>\n        </resolution>\n    </resolutions>\n"
        "    <options>\n\t\t<HardwareCursor>true</HardwareCursor>\n    </options>\n</vdd_settings>\n";
    // Widths registered with the driver: every built-in preset on the G9 (multiples of 32), plus the custom zones.
    const std::set<int> presetWidths{1280, 1536, 1696, 1728, 2560, 3584};
    CHECK(RequiredWidths(5120, {}) == presetWidths);
    auto custom = ComputeZones(5120, 1440, {0.37, 0.41, 0.22});
    auto withCustom = RequiredWidths(5120, custom);
    for (auto& c : custom) CHECK(withCustom.count(c.width) == 1);

    const std::string gpu = "NVIDIA GeForce RTX 4060";
    std::string patched = PatchVddSettings(vddDefault, presetWidths, 1440, 240, gpu);
    CHECK(patched.find("<count>3</count>") != std::string::npos);
    CHECK(patched.find("<friendlyname>NVIDIA GeForce RTX 4060</friendlyname>") != std::string::npos);
    CHECK(patched.find("<width>800</width>") == std::string::npos);
    CHECK(patched.find("<width>1696</width>") != std::string::npos);
    CHECK(patched.find("<width>3584</width>") != std::string::npos);
    CHECK(patched.find("<refresh_rate>240</refresh_rate>") != std::string::npos);
    CHECK(patched.find("<HardwareCursor>true</HardwareCursor>") != std::string::npos); // other options untouched
    CHECK(patched.find("<g_refresh_rate>60</g_refresh_rate>") != std::string::npos);
    CHECK(ParseVddSettings(patched).sizes.size() == presetWidths.size());
    CHECK(PatchVddSettings(patched, presetWidths, 1440, 240, gpu) == patched); // stable

    // "Already configured" is judged by content: the driver re-serialises the file after its reload.
    CHECK(!VddSettingsSatisfy(vddDefault, presetWidths, 1440, gpu));
    CHECK(VddSettingsSatisfy(patched, presetWidths, 1440, gpu));
    std::string reserialised = patched;
    reserialised.replace(0, reserialised.find("?>") + 2, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    for (size_t at; (at = reserialised.find("      <width>")) != std::string::npos;) reserialised.replace(at, 6, "\t\t\t");
    CHECK(VddSettingsSatisfy(reserialised, presetWidths, 1440, gpu));
    CHECK(!VddSettingsSatisfy(patched, withCustom, 1440, gpu)); // a new custom width needs a reload
    CHECK(!VddSettingsSatisfy(patched, presetWidths, 1440, "Intel(R) UHD Graphics"));

    // Earlier custom widths stay registered, so going back to that layout needs no reload.
    std::string withOld = PatchVddSettings(patched, withCustom, 1440, 240, gpu);
    CHECK(VddSettingsSatisfy(PatchVddSettings(withOld, presetWidths, 1440, 240, gpu), withCustom, 1440, gpu));

    // An oversized list (the 121-width one that made VDD create no monitors) is cut back to what is needed.
    std::string huge = "<vdd_settings><count>3</count><resolutions>";
    for (int w = 640; w <= 4480; w += 32)
        huge += "<resolution><width>" + std::to_string(w) + "</width><height>1440</height></resolution>";
    huge += "</resolutions></vdd_settings>";
    CHECK(ParseVddSettings(huge).sizes.size() == 121);
    CHECK(ParseVddSettings(PatchVddSettings(huge, presetWidths, 1440, 240, gpu)).sizes.size() == presetWidths.size());

    std::printf(g_failed ? "%d check(s) failed\n" : "all checks passed\n", g_failed);
    return g_failed ? 1 : 0;
}
