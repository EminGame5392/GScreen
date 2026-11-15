#include "Layout.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace gs {

std::vector<ZoneRect> ComputeZones(int panelW, int panelH, const std::vector<double>& ratiosIn) {
    std::vector<double> ratios;
    for (double r : ratiosIn)
        if (std::isfinite(r) && r > 0) ratios.push_back(r);
    if (ratios.size() > (size_t)kMaxZones) ratios.resize(kMaxZones);

    // Not enough room for the requested zones → fewer zones.
    while (ratios.size() > 1 && (int)ratios.size() * kMinZoneWidth > panelW) ratios.pop_back();
    if (ratios.empty() || panelW <= 0 || panelH <= 0) return {{0, 0, std::max(panelW, 0), std::max(panelH, 0)}};

    const size_t n = ratios.size();
    const double sum = std::accumulate(ratios.begin(), ratios.end(), 0.0);
    std::vector<int> w(n);
    int used = 0;
    for (size_t i = 0; i + 1 < n; ++i) {
        int ideal = (int)std::lround(panelW * ratios[i] / sum / kWidthAlign) * kWidthAlign;
        int remainingMin = (int)(n - 1 - i) * kMinZoneWidth;
        int maxHere = panelW - used - remainingMin;
        w[i] = std::clamp(ideal, kMinZoneWidth, std::max(kMinZoneWidth, maxHere / kWidthAlign * kWidthAlign));
        used += w[i];
    }
    w[n - 1] = panelW - used;

    std::vector<ZoneRect> zones;
    int x = 0;
    for (size_t i = 0; i < n; ++i) {
        zones.push_back({x, 0, w[i], panelH});
        x += w[i];
    }
    return zones;
}

std::vector<Preset> DefaultPresets() {
    return {
        {"three-equal", "Три равные", {1, 1, 1}},
        {"focus-center", "Фокус в центре", {1, 2, 1}},
        {"two-halves", "Две половины", {1, 1}},
        {"game-chat", "Игра + чат", {7, 3}},
    };
}

const char* ModeName(Mode m) { return m == Mode::Split ? "split" : "merged"; }

Json LayoutToJson(const Layout& l) {
    Json::Array r;
    for (double v : l.ratios) r.push_back(Json(v));
    Json j(Json::Object{});
    j.set("mode", ModeName(l.mode));
    j.set("ratios", Json(std::move(r)));
    j.set("presetId", l.presetId);
    j.set("refreshHz", l.refreshHz);
    j.set("showDividers", l.showDividers);
    return j;
}

Layout LayoutFromJson(const Json& j) {
    Layout l;
    if (j["mode"].asString() == "split") l.mode = Mode::Split;
    std::vector<double> r;
    for (auto& v : j["ratios"].asArray())
        if (v.isNumber() && v.asNumber() > 0) r.push_back(v.asNumber());
    if (r.size() >= 2 && r.size() <= (size_t)kMaxZones) l.ratios = r;
    l.presetId = j["presetId"].asString(l.presetId);
    l.refreshHz = std::clamp(j["refreshHz"].asInt(l.refreshHz), 30, 500);
    l.showDividers = j["showDividers"].asBool(false);
    return l;
}

} // namespace gs
