#include <cstdio>

#include "../src/settings.h"

int main() {
    const auto path = tuxime::SettingsStore::profilePath();
    std::printf("profile: %ls\n", path.c_str());
    const auto settings = tuxime::SettingsStore::loadPinyin();
    std::printf("keyboardDefault=%d pageSize=%d\n",
                settings.keyboardDefault ? 1 : 0, settings.pageSize);
    return 0;
}
