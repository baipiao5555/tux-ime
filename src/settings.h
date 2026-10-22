#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace tuxime {

struct SyncConfig {
    bool enabled = false;
    std::string serverUrl;
    std::string username;
    std::string password;
};

struct PinyinSettings {
    std::string defaultInputMethod = "pinyin";
    std::string shuangpinProfile = "Ziranma";
    int pageSize = 7;
    bool fuzzyZ = false;
    bool fuzzyC = false;
    bool fuzzyS = false;
    bool fuzzyL = false;
    bool fuzzyAn = false;
    bool fuzzyEn = false;
    bool fuzzyIn = false;
    bool fuzzyF = false;
    bool spellEnabled = true;
    bool symbolsEnabled = true;
    bool chaiziEnabled = true;
    bool extBEnabled = true;
    bool predictionEnabled = false;
    bool pinyinInPreedit = false;
};

struct CustomPhrase {
    std::string code;
    std::string text;
};

class SettingsStore {
public:
    static std::filesystem::path appDataDir();
    static std::filesystem::path syncConfigPath();
    static std::filesystem::path pinyinConfigPath();
    static std::filesystem::path profilePath();
    static std::filesystem::path pinyinUserDictPath();

    static SyncConfig loadSync();
    static void saveSync(const SyncConfig &config);

    static PinyinSettings loadPinyin();
    static void savePinyin(const PinyinSettings &settings);
    static void ensureAddonDefaults();

    static std::filesystem::path customPhrasePath();
    static std::vector<CustomPhrase> loadPhrases();
    static void savePhrases(const std::vector<CustomPhrase> &phrases);

private:
    static bool setIniValue(const std::filesystem::path &path,
                            const std::string &section,
                            const std::string &key,
                            const std::string &value);
};

} // namespace tuxime
