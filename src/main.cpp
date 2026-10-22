#include "server.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "common/pipe.h"
#include "common/pipe_io.h"
#include "settings.h"

namespace {

void setupLogFile() {
    const auto directory = tuxime::SettingsStore::appDataDir();
    const auto path = directory / "tuxime.log";
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!error && size > 5 * 1024 * 1024) {
        const auto old = directory / "tuxime.old.log";
        std::filesystem::remove(old, error);
        std::filesystem::rename(path, old, error);
    }
    freopen(path.string().c_str(), "a", stderr);
    freopen(path.string().c_str(), "a", stdout);
}

} // namespace

int main(int argc, char **argv) {
    bool settingsOnly = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--settings") == 0) {
            settingsOnly = true;
        }
    }

    if (settingsOnly) {
        const std::wstring name = tuxime::ipc::pipeName();
        HANDLE pipe = tuxime::ipc::connectPipe(name, 300);
        if (pipe != INVALID_HANDLE_VALUE) {
            tuxime::ipc::writeMessage(
                pipe, tuxime::ipc::Type::ShowSettings, 0, {});
            CloseHandle(pipe);
            return 0;
        }
    }

    tuxime::Server server;
    setupLogFile();
    if (!server.initialize()) {
        return 1;
    }
    return server.run(settingsOnly);
}
