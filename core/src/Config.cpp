#include "Config.h"
#include "Log.h"
#include <fstream>
#include <sstream>
#include <windows.h>

namespace gs {

static std::wstring ConfigPath() { return AppDataDir() + L"\\config.json"; }

Config LoadConfig() {
    Config c;
    std::ifstream f(ConfigPath(), std::ios::binary);
    if (!f) return c;
    std::stringstream ss;
    ss << f.rdbuf();
    auto j = Json::parse(ss.str());
    if (!j) {
        LogWarn("config.json is not valid JSON; using defaults");
        return c;
    }
    c.layout = LayoutFromJson((*j)["layout"]);
    // Never start in split mode: a crash loop while the panel is detached would leave the user with no picture.
    c.layout.mode = Mode::Merged;
    return c;
}

void SaveConfig(const Config& c) {
    Json j(Json::Object{});
    j.set("layout", LayoutToJson(c.layout));
    std::wstring path = ConfigPath();
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << j.dump();
    }
    MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

} // namespace gs
