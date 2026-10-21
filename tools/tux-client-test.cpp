#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <windows.h>

#include "common/ipc.h"
#include "common/pipe.h"
#include "common/pipe_io.h"

namespace {

void printResult(const tuxime::ipc::KeyResult &result,
                 tuxime::ipc::Reader &reader) {
    const std::string preedit = reader.str();
    const std::string aux = reader.str();
    const std::string commit = reader.str();
    std::printf("handled=%u flags=%u selected=%d candidates=%u\n",
                result.handled, result.flags, result.selected,
                result.candidateCount);
    if (!preedit.empty()) {
        std::printf("  preedit: %s\n", preedit.c_str());
    }
    if (!aux.empty()) {
        std::printf("  aux: %s\n", aux.c_str());
    }
    if (!commit.empty()) {
        std::printf("  commit: %s\n", commit.c_str());
    }
    for (uint32_t i = 0; i < result.candidateCount; ++i) {
        const std::string label = reader.str();
        const std::string text = reader.str();
        const std::string comment = reader.str();
        std::printf("  [%s] %s %s\n", label.c_str(), text.c_str(),
                    comment.c_str());
    }
}

bool requestKey(HANDLE pipe, uint64_t request, uint32_t vk,
                uint32_t modifiers) {
    tuxime::ipc::Writer writer;
    writer.u32(vk);
    writer.u32(0);
    writer.u32(modifiers);
    writer.u32(0);
    if (!tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::Key, request,
                                   writer.take())) {
        return false;
    }

    tuxime::ipc::Header header{};
    std::vector<uint8_t> payload;
    if (!tuxime::ipc::readMessage(pipe, header, payload)) {
        return false;
    }
    tuxime::ipc::Reader reader(payload.data(), payload.size());
    tuxime::ipc::KeyResult result{};
    result.handled = reader.u32();
    result.flags = reader.u32();
    result.selected = reader.i32();
    result.candidateCount = reader.u32();
    std::printf("key %u: ", vk);
    printResult(result, reader);
    return true;
}

} // namespace

int main() {
    const std::wstring name = tuxime::ipc::pipeName();
    if (name.empty()) {
        std::printf("no pipe name\n");
        return 1;
    }
    HANDLE pipe = tuxime::ipc::connectPipe(name, 2000);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::printf("failed to connect to server\n");
        return 1;
    }

    {
        tuxime::ipc::Writer writer;
        writer.u32(GetCurrentProcessId());
        writer.u32(tuxime::ipc::kProtocolVersion);
        if (!tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::Hello, 1,
                                       writer.take())) {
            return 1;
        }
        tuxime::ipc::Header header{};
        std::vector<uint8_t> payload;
        if (!tuxime::ipc::readMessage(pipe, header, payload)) {
            return 1;
        }
        tuxime::ipc::Reader reader(payload.data(), payload.size());
        std::printf("session=%u version=%u\n", reader.u32(), reader.u32());
    }

    tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::FocusIn, 2, {});
    {
        tuxime::ipc::Writer writer;
        writer.i32(100);
        writer.i32(100);
        writer.i32(1);
        writer.i32(20);
        tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::Cursor, 3,
                                  writer.take());
    }

    uint64_t request = 10;
    for (const char *text = "nihao"; *text; ++text) {
        if (!requestKey(pipe, request++,
                        static_cast<uint32_t>(std::toupper(*text)), 0)) {
            return 1;
        }
    }
    requestKey(pipe, request++, VK_SPACE, 0);

    tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::FocusOut, request++, {});
    CloseHandle(pipe);
    return 0;
}
