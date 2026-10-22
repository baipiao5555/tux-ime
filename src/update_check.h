#pragma once

#include <string>

namespace tuxime {

struct UpdateCheckResult {
    bool ok = false;
    bool hasUpdate = false;
    std::wstring version;
    std::wstring url;
    std::wstring assetUrl;
    std::wstring error;
};

struct UpdateDownloadResult {
    bool ok = false;
    std::wstring path;
    std::wstring error;
};

UpdateCheckResult checkLatestRelease();
UpdateDownloadResult downloadAndVerify(const std::wstring &url,
                                       const std::wstring &version);

} // namespace tuxime
