#include <cstdio>

#include "../src/settings.h"

int main() {
    const auto path = tuxime::SettingsStore::profilePath();
    std::printf("profile: %ls\n", path.c_str());
    const auto settings = tuxime::SettingsStore::loadPinyin();
    std::printf("defaultInputMethod=%s pageSize=%d\n",
                settings.defaultInputMethod.c_str(), settings.pageSize);
    return 0;
}
