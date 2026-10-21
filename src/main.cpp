#include "server.h"

#include <cstring>

#include "common/pipe.h"
#include "common/pipe_io.h"

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
    if (!server.initialize()) {
        return 1;
    }
    return server.run(settingsOnly);
}
