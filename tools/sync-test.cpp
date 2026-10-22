#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>

#include "../src/clipboard_sync.h"

namespace {
std::string toUtf8(const std::wstring &text) {
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
        nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 1) {
        tuxime::ClipboardSync sync;
        tuxime::SyncConfig config;
        config.serverUrl = argv[1];
        config.username = argc > 2 ? argv[2] : "";
        config.password = argc > 3 ? argv[3] : "";
        const std::wstring status = sync.testConnection(config);
        std::printf("%s\n", toUtf8(status).c_str());
        return 0;
    }
    struct Case {
        const char *url;
        bool expected;
    };
    const std::vector<Case> cases = {
        {"http://127.0.0.1:8080", false},
        {"http://localhost:8080", false},
        {"http://10.1.2.3", false},
        {"http://192.168.1.2", false},
        {"http://172.16.0.1", false},
        {"http://169.254.1.1", false},
        {"http://[::1]", false},
        {"http://[fe80::1]", false},
        {"http://224.0.0.1", false},
        {"ftp://example.com", false},
        {"http://1.1.1.1", true},
        {"https://8.8.8.8", true},
    };

    int failures = 0;
    for (const auto &test : cases) {
        std::wstring error;
        const bool result = tuxime::validateSyncUrl(test.url, error);
        const bool ok = result == test.expected;
        if (!ok) {
            ++failures;
        }
        std::printf("%s %s expected=%d got=%d %ls\n",
                    ok ? "PASS" : "FAIL", test.url, test.expected, result,
                    error.c_str());
    }

    tuxime::ClipboardSync sync;
    tuxime::SyncConfig config;
    config.serverUrl = "https://httpbin.org";
    const std::wstring connection = sync.testConnection(config);
    std::printf("HTTP probe: %ls\n", connection.c_str());

    return failures == 0 ? 0 : 1;
}
