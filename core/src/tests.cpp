// Self-tests for pure logic (no GPU, no driver). Run: GScreen.Tests.exe → exit code 0 on success.
#include "Json.h"
#include "Layout.h"
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
    CHECK(z[0].width == 1704 && z[1].width == 1704 && z[2].width == 1712);
    CHECK(z[1].x == 1704 && z[2].x == 3408);
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

    std::printf(g_failed ? "%d check(s) failed\n" : "all checks passed\n", g_failed);
    return g_failed ? 1 : 0;
}
