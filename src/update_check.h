#pragma once

#include <string>

namespace tuxime {

struct UpdateCheckResult {
    bool ok = false;
    bool hasUpdate = false;
    std::wstring version;
    std::wstring url;
    std::wstring error;
};

UpdateCheckResult checkLatestRelease();

} // namespace tuxime
