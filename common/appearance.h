#pragma once

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>

namespace tuxime {

struct Appearance {
    int fontSize = 16;
    bool dark = false;
    bool showStatus = true;
};

inline std::wstring appearancePath() {
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) {
        return {};
    }
    return std::wstring(appData) + L"\\TuxIME\\ui.ini";
}

inline Appearance loadAppearance() {
    Appearance appearance;
    std::ifstream in(std::filesystem::path(appearancePath()),
                     std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "FontSize") {
            appearance.fontSize = std::clamp(std::atoi(value.c_str()), 12, 24);
        } else if (key == "Dark") {
            appearance.dark = value == "1" || value == "True" ||
                              value == "true";
        } else if (key == "ShowStatus") {
            appearance.showStatus = value == "1" || value == "True" ||
                                    value == "true";
        }
    }
    return appearance;
}

inline void saveAppearance(const Appearance &appearance) {
    const std::wstring path = appearancePath();
    if (path.empty()) {
        return;
    }
    std::ofstream out(std::filesystem::path(path),
                      std::ios::binary | std::ios::trunc);
    out << "FontSize=" << appearance.fontSize << '\n';
    out << "Dark=" << (appearance.dark ? "1" : "0") << '\n';
    out << "ShowStatus=" << (appearance.showStatus ? "1" : "0") << '\n';
}

} // namespace tuxime
