#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

#include "settings.h"

namespace tuxime {

class ClipboardSync {
public:
    using StatusCallback = std::function<void(const std::wstring &)>;

    ClipboardSync();
    ~ClipboardSync();

    ClipboardSync(const ClipboardSync &) = delete;
    ClipboardSync &operator=(const ClipboardSync &) = delete;

    bool start(const SyncConfig &config, StatusCallback callback);
    void stop();
    void updateConfig(const SyncConfig &config);
    void trigger();
    std::wstring testConnection(const SyncConfig &config) const;
    bool running() const { return running_; }

private:
    void run();
    void onClipboardUpdate();
    void poll();
    bool pushText(const std::wstring &text);
    void setClipboardText(const std::wstring &text);
    std::wstring readClipboardText() const;
    void report(const std::wstring &status);
    SyncConfig config() const;

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);

    mutable std::mutex configMutex_;
    SyncConfig config_;
    StatusCallback callback_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    HWND hwnd_ = nullptr;
    DWORD threadId_ = 0;
    std::wstring lastHash_;
    std::wstring lastAppliedHash_;
    std::string lastEtag_;
};

bool validateSyncUrl(const std::string &url, std::wstring &error);

} // namespace tuxime
