#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
    bool vertical = false;
    bool customColors = false;
    uint32_t backgroundColor = 0xF6FAFAFA;
    uint32_t textColor = 0xFF1C1E22;
    uint32_t selectedBackgroundColor = 0xFFD6E8FF;
    uint32_t selectedTextColor = 0xFF466EA0;
    uint32_t borderColor = 0xDCC0C3CD;
    uint32_t statusColor = 0xFF6E7682;
};

inline void setDefaultColors(Appearance &appearance) {
    if (appearance.dark) {
        appearance.backgroundColor = 0xF61E2024;
        appearance.textColor = 0xFFEBECEE;
        appearance.selectedBackgroundColor = 0xFF304E82;
        appearance.selectedTextColor = 0xFF8CB9FF;
        appearance.borderColor = 0xDC464A52;
        appearance.statusColor = 0xFF969BA5;
    } else {
        appearance.backgroundColor = 0xF6FAFAFA;
        appearance.textColor = 0xFF1C1E22;
        appearance.selectedBackgroundColor = 0xFFD6E8FF;
        appearance.selectedTextColor = 0xFF466EA0;
        appearance.borderColor = 0xDCC0C3CD;
        appearance.statusColor = 0xFF6E7682;
    }
}

inline std::wstring appearancePath() {
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) == 0) {
        return {};
    }
    return std::wstring(appData) + L"\\TuxIME\\ui.ini";
}

inline Appearance loadAppearance() {
    Appearance appearance;
    auto parseColor = [](const std::string &value) -> uint32_t {
        try {
            const auto parsed = std::stoul(value, nullptr, 16);
            if (parsed <= 0xffffffffULL) {
                return static_cast<uint32_t>(parsed);
            }
        } catch (...) {
        }
        return 0;
    };
    bool anyColor = false;
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
        } else if (key == "Vertical") {
            appearance.vertical = value == "1" || value == "True" ||
                                  value == "true";
        } else if (key == "CustomColors") {
            appearance.customColors = value == "1" || value == "True" ||
                                      value == "true";
        } else if (key == "BackgroundColor") {
            appearance.backgroundColor = parseColor(value);
            anyColor = true;
        } else if (key == "TextColor") {
            appearance.textColor = parseColor(value);
            anyColor = true;
        } else if (key == "SelectedBackgroundColor") {
            appearance.selectedBackgroundColor = parseColor(value);
            anyColor = true;
        } else if (key == "SelectedTextColor") {
            appearance.selectedTextColor = parseColor(value);
            anyColor = true;
        } else if (key == "BorderColor") {
            appearance.borderColor = parseColor(value);
            anyColor = true;
        } else if (key == "StatusColor") {
            appearance.statusColor = parseColor(value);
            anyColor = true;
        }
    }
    if (!appearance.customColors || !anyColor) {
        setDefaultColors(appearance);
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
    out << "Vertical=" << (appearance.vertical ? "1" : "0") << '\n';
    out << "CustomColors=" << (appearance.customColors ? "1" : "0") << '\n';
    if (appearance.customColors) {
        auto hex = [](uint32_t color) {
            char buffer[9];
            std::snprintf(buffer, sizeof(buffer), "%08X", color);
            return std::string(buffer);
        };
        out << "BackgroundColor=" << hex(appearance.backgroundColor) << '\n';
        out << "TextColor=" << hex(appearance.textColor) << '\n';
        out << "SelectedBackgroundColor="
            << hex(appearance.selectedBackgroundColor) << '\n';
        out << "SelectedTextColor=" << hex(appearance.selectedTextColor)
            << '\n';
        out << "BorderColor=" << hex(appearance.borderColor) << '\n';
        out << "StatusColor=" << hex(appearance.statusColor) << '\n';
    }
}

} // namespace tuxime
