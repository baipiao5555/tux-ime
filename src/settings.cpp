#include "settings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>
#include <windows.h>
#include <dpapi.h>
#include <shlobj.h>

namespace tuxime {

namespace {

std::string trim(const std::string &value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return value;
}

const char kBase64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<uint8_t> &data) {
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < data.size() ? data[i + 1] : 0;
        const uint32_t c = i + 2 < data.size() ? data[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        result.push_back(kBase64[(value >> 18) & 0x3f]);
        result.push_back(kBase64[(value >> 12) & 0x3f]);
        result.push_back(i + 1 < data.size() ? kBase64[(value >> 6) & 0x3f]
                                             : '=');
        result.push_back(i + 2 < data.size() ? kBase64[value & 0x3f] : '=');
    }
    return result;
}

std::vector<uint8_t> base64Decode(const std::string &text) {
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kBase64[i])] = i;
    }
    std::vector<uint8_t> result;
    uint32_t value = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (c == '=') {
            break;
        }
        const int digit = table[c];
        if (digit < 0) {
            continue;
        }
        value = (value << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<uint8_t>((value >> bits) & 0xff));
        }
    }
    return result;
}

std::string protectPassword(const std::string &password) {
    if (password.empty()) {
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(password.size()),
                    reinterpret_cast<BYTE *>(
                        const_cast<char *>(password.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"TuxIME clipboard sync", nullptr, nullptr,
                          nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    std::vector<uint8_t> bytes(output.pbData, output.pbData + output.cbData);
    LocalFree(output.pbData);
    return base64Encode(bytes);
}

std::string unprotectPassword(const std::string &encoded) {
    const auto bytes = base64Decode(encoded);
    if (bytes.empty()) {
        return {};
    }
    DATA_BLOB input{static_cast<DWORD>(bytes.size()),
                    reinterpret_cast<BYTE *>(
                        const_cast<uint8_t *>(bytes.data()))};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return {};
    }
    std::string result(reinterpret_cast<const char *>(output.pbData),
                       output.cbData);
    LocalFree(output.pbData);
    return result;
}

void writeLines(const std::filesystem::path &path,
                const std::vector<std::string> &lines) {
    std::filesystem::create_directories(path.parent_path());
    const auto temp = path.wstring() + L".tmp";
    {
        std::ofstream out(std::filesystem::path(temp),
                          std::ios::binary | std::ios::trunc);
        for (const auto &line : lines) {
            out << line << '\n';
        }
    }
    MoveFileExW(temp.c_str(), path.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

std::vector<std::string> readLines(const std::filesystem::path &path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::string iniValue(const std::vector<std::string> &lines,
                     const std::string &section, const std::string &key,
                     const std::string &fallback = {}) {
    std::string currentSection;
    for (const auto &line : lines) {
        const auto value = trim(line);
        if (value.size() >= 2 && value.front() == '[' &&
            value.back() == ']') {
            currentSection = value.substr(1, value.size() - 2);
            continue;
        }
        if (currentSection != section || value.empty() || value[0] == '#') {
            continue;
        }
        const auto equals = value.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (trim(value.substr(0, equals)) == key) {
            return trim(value.substr(equals + 1));
        }
    }
    return fallback;
}

bool boolValue(const std::string &value) {
    const auto normalized = lower(value);
    return normalized == "true" || normalized == "1" ||
           normalized == "yes" || normalized == "on";
}

} // namespace

std::filesystem::path SettingsStore::appDataDir() {
    PWSTR path = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr,
                                        &path))) {
        result = std::filesystem::path(path) / "TuxIME";
        CoTaskMemFree(path);
    }
    if (result.empty()) {
        wchar_t buffer[MAX_PATH] = {};
        GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
        result = std::filesystem::path(buffer) / "TuxIME";
    }
    std::filesystem::create_directories(result);
    return result;
}

std::filesystem::path SettingsStore::syncConfigPath() {
    return appDataDir() / "sync.ini";
}

std::filesystem::path SettingsStore::pinyinConfigPath() {
    return appDataDir().parent_path() / "Fcitx5" / "config" / "fcitx5" /
           "conf" / "pinyin.conf";
}

std::filesystem::path SettingsStore::profilePath() {
    return appDataDir().parent_path() / "Fcitx5" / "config" / "fcitx5" /
           "profile";
}

std::filesystem::path SettingsStore::pinyinUserDictPath() {
    return appDataDir().parent_path() / "Fcitx5" / "pinyin" / "user.dict";
}

SyncConfig SettingsStore::loadSync() {
    SyncConfig config;
    const auto lines = readLines(syncConfigPath());
    if (lines.empty()) {
        return config;
    }
    config.enabled = boolValue(iniValue(lines, "Sync", "Enabled", "False"));
    config.serverUrl = iniValue(lines, "Sync", "Url");
    config.username = iniValue(lines, "Sync", "Username");
    config.password =
        unprotectPassword(iniValue(lines, "Sync", "Password"));
    return config;
}

void SettingsStore::saveSync(const SyncConfig &config) {
    const auto path = syncConfigPath();
    setIniValue(path, "Sync", "Enabled", config.enabled ? "True" : "False");
    setIniValue(path, "Sync", "Url", config.serverUrl);
    setIniValue(path, "Sync", "Username", config.username);
    setIniValue(path, "Sync", "Password",
                protectPassword(config.password));
}

PinyinSettings SettingsStore::loadPinyin() {
    PinyinSettings settings;
    const auto lines = readLines(pinyinConfigPath());
    if (lines.empty()) {
        return settings;
    }
    const auto pageSize = iniValue(lines, "", "PageSize", "7");
    try {
        settings.pageSize = std::clamp(std::stoi(pageSize), 3, 10);
    } catch (...) {
        settings.pageSize = 7;
    }
    settings.shuangpinProfile =
        iniValue(lines, "", "ShuangpinProfile", "Ziranma");
    settings.fuzzyZ = boolValue(iniValue(lines, "Fuzzy", "Z_ZH", "False"));
    settings.fuzzyC = boolValue(iniValue(lines, "Fuzzy", "C_CH", "False"));
    settings.fuzzyS = boolValue(iniValue(lines, "Fuzzy", "S_SH", "False"));
    settings.fuzzyL = boolValue(iniValue(lines, "Fuzzy", "L_N", "False"));
    settings.fuzzyAn = boolValue(iniValue(lines, "Fuzzy", "AN_ANG", "False"));
    settings.fuzzyEn = boolValue(iniValue(lines, "Fuzzy", "EN_ENG", "False"));
    settings.fuzzyIn = boolValue(iniValue(lines, "Fuzzy", "IN_ING", "False"));
    settings.fuzzyF = boolValue(iniValue(lines, "Fuzzy", "F_H", "False"));
    settings.spellEnabled = boolValue(iniValue(lines, "", "SpellEnabled", "True"));
    settings.symbolsEnabled =
        boolValue(iniValue(lines, "", "SymbolsEnabled", "True"));
    settings.chaiziEnabled =
        boolValue(iniValue(lines, "", "ChaiziEnabled", "True"));
    settings.extBEnabled = boolValue(iniValue(lines, "", "ExtBEnabled", "True"));
    settings.predictionEnabled =
        boolValue(iniValue(lines, "", "Prediction", "False"));
    settings.pinyinInPreedit =
        boolValue(iniValue(lines, "", "PinyinInPreedit", "False"));

    const auto profile = readLines(profilePath());
    settings.defaultInputMethod =
        iniValue(profile, "Groups/0", "DefaultIM", "pinyin");
    if (settings.defaultInputMethod.empty()) {
        settings.defaultInputMethod = "pinyin";
    }
    return settings;
}

void SettingsStore::savePinyin(const PinyinSettings &settings) {
    const auto path = pinyinConfigPath();
    const auto profile = profilePath();
    if (!std::filesystem::exists(profile)) {
        writeLines(profile,
                   {"[Groups/0]",
                    "Name=Default",
                    "Default Layout=us",
                    "DefaultIM=pinyin",
                    "",
                    "[Groups/0/Items/0]",
                    "Name=tux-layout",
                    "",
                    "[Groups/0/Items/1]",
                    "Name=tux-keyboard",
                    "",
                    "[Groups/0/Items/2]",
                    "Name=pinyin",
                    "",
                    "[GroupOrder]",
                    "0=Default"});
    }
    setIniValue(path, "", "PageSize", std::to_string(settings.pageSize));
    setIniValue(path, "", "ShuangpinProfile", settings.shuangpinProfile);
    setIniValue(path, "Fuzzy", "Z_ZH",
                settings.fuzzyZ ? "True" : "False");
    setIniValue(path, "Fuzzy", "C_CH",
                settings.fuzzyC ? "True" : "False");
    setIniValue(path, "Fuzzy", "S_SH",
                settings.fuzzyS ? "True" : "False");
    setIniValue(path, "Fuzzy", "L_N",
                settings.fuzzyL ? "True" : "False");
    setIniValue(path, "Fuzzy", "AN_ANG",
                settings.fuzzyAn ? "True" : "False");
    setIniValue(path, "Fuzzy", "EN_ENG",
                settings.fuzzyEn ? "True" : "False");
    setIniValue(path, "Fuzzy", "IN_ING",
                settings.fuzzyIn ? "True" : "False");
    setIniValue(path, "Fuzzy", "F_H",
                settings.fuzzyF ? "True" : "False");
    setIniValue(path, "", "SpellEnabled",
                settings.spellEnabled ? "True" : "False");
    setIniValue(path, "", "SymbolsEnabled",
                settings.symbolsEnabled ? "True" : "False");
    setIniValue(path, "", "ChaiziEnabled",
                settings.chaiziEnabled ? "True" : "False");
    setIniValue(path, "", "ExtBEnabled",
                settings.extBEnabled ? "True" : "False");
    setIniValue(path, "", "Prediction",
                settings.predictionEnabled ? "True" : "False");
    setIniValue(path, "", "PinyinInPreedit",
                settings.pinyinInPreedit ? "True" : "False");
    setIniValue(profilePath(), "Groups/0", "DefaultIM",
                settings.defaultInputMethod.empty()
                    ? "pinyin"
                    : settings.defaultInputMethod);
}

void SettingsStore::ensureAddonDefaults() {
    const auto confDir = appDataDir().parent_path() / "Fcitx5" / "config" /
                         "fcitx5" / "conf";
    const auto fullwidth = confDir / "fullwidth.conf";
    if (!std::filesystem::exists(fullwidth)) {
        setIniValue(fullwidth, "Hotkey", "0", "Shift+space");
    }
}

std::filesystem::path SettingsStore::customPhrasePath() {
    return appDataDir().parent_path() / "fcitx5" / "pinyin" /
           "customphrase";
}

std::vector<CustomPhrase> SettingsStore::loadPhrases() {
    std::vector<CustomPhrase> phrases;
    std::ifstream in(customPhrasePath(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto value = trim(line);
        if (value.empty() || value[0] == '#') {
            continue;
        }
        const auto comma = value.find(',');
        const auto equals = value.find('=');
        if (comma == std::string::npos || equals == std::string::npos ||
            comma > equals) {
            continue;
        }
        CustomPhrase phrase;
        phrase.code = trim(value.substr(0, comma));
        phrase.text = trim(value.substr(equals + 1));
        if (phrase.code.empty() || phrase.text.empty()) {
            continue;
        }
        if (phrase.text.size() >= 2 && phrase.text.front() == '"' &&
            phrase.text.back() == '"') {
            phrase.text = phrase.text.substr(1, phrase.text.size() - 2);
        }
        phrases.push_back(std::move(phrase));
    }
    return phrases;
}

void SettingsStore::savePhrases(const std::vector<CustomPhrase> &phrases) {
    const auto path = customPhrasePath();
    std::filesystem::create_directories(path.parent_path());
    const auto temp = path.wstring() + L".tmp";
    {
        std::ofstream out(std::filesystem::path(temp),
                          std::ios::binary | std::ios::trunc);
        for (const auto &phrase : phrases) {
            std::string text = phrase.text;
            const bool quote = text.find_first_of("\n\r\"\\") !=
                               std::string::npos;
            if (quote) {
                std::string escaped;
                for (char c : text) {
                    if (c == '"' || c == '\\') {
                        escaped.push_back('\\');
                    }
                    escaped.push_back(c);
                }
                text = "\"" + escaped + "\"";
            }
            out << phrase.code << ",1=" << text << '\n';
        }
    }
    MoveFileExW(temp.c_str(), path.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

bool SettingsStore::setIniValue(const std::filesystem::path &path,
                                const std::string &section,
                                const std::string &key,
                                const std::string &value) {
    auto lines = readLines(path);

    if (section.empty()) {
        size_t rootEnd = lines.size();
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto text = trim(lines[i]);
            if (text.size() >= 2 && text.front() == '[' &&
                text.back() == ']') {
                rootEnd = i;
                break;
            }
        }
        bool replaced = false;
        for (size_t i = 0; i < rootEnd; ++i) {
            const auto text = trim(lines[i]);
            if (text.empty() || text[0] == '#') {
                continue;
            }
            const auto equals = text.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            if (trim(text.substr(0, equals)) == key) {
                lines[i] = key + "=" + value;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            lines.insert(lines.begin() + static_cast<ptrdiff_t>(rootEnd),
                         key + "=" + value);
        }
        writeLines(path, lines);
        return true;
    }

    std::string currentSection;
    size_t sectionStart = std::string::npos;
    size_t sectionEnd = lines.size();
    for (size_t i = 0; i < lines.size(); ++i) {
        const auto text = trim(lines[i]);
        if (text.size() >= 2 && text.front() == '[' &&
            text.back() == ']') {
            if (currentSection == section) {
                sectionEnd = i;
                break;
            }
            currentSection = text.substr(1, text.size() - 2);
            if (currentSection == section) {
                sectionStart = i;
            }
        }
    }

    if (sectionStart == std::string::npos) {
        if (!lines.empty() && !lines.back().empty()) {
            lines.emplace_back();
        }
        lines.push_back("[" + section + "]");
        lines.push_back(key + "=" + value);
    } else {
        bool replaced = false;
        for (size_t i = sectionStart + 1; i < sectionEnd; ++i) {
            const auto text = trim(lines[i]);
            if (text.empty() || text[0] == '#') {
                continue;
            }
            const auto equals = text.find('=');
            if (equals == std::string::npos) {
                continue;
            }
            if (trim(text.substr(0, equals)) == key) {
                lines[i] = key + "=" + value;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            lines.insert(lines.begin() + static_cast<ptrdiff_t>(sectionEnd),
                         key + "=" + value);
        }
    }

    writeLines(path, lines);
    return true;
}

} // namespace tuxime
