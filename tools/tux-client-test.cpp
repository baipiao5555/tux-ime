#include <cctype>
#include <cstdio>
#include <cstring>
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
    const std::string status = reader.str();
    std::printf("handled=%u flags=%u selected=%d candidates=%u\n",
                result.handled, result.flags, result.selected,
                result.candidateCount);
    if (!status.empty()) {
        std::printf("  status: %s\n", status.c_str());
    }
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

int main(int argc, char **argv) {
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

    if (argc > 2 && std::strcmp(argv[1], "--toggle") == 0) {
        tuxime::ipc::Writer writer;
        writer.str(argv[2]);
        tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::ToggleAddon, 2,
                                  writer.take());
        CloseHandle(pipe);
        return 0;
    }

    if (argc > 2 && std::strcmp(argv[1], "--im") == 0) {
        tuxime::ipc::Writer writer;
        writer.str(argv[2]);
        tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::SwitchInputMethod,
                                  2, writer.take());
        Sleep(200);
        CloseHandle(pipe);
        return 0;
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
    const char *keys = argc > 1 ? argv[1] : "nihao";
    for (const char *text = keys; *text; ++text) {
        uint32_t vk = static_cast<uint32_t>(std::toupper(*text));
        if (*text == ',') {
            vk = VK_OEM_COMMA;
        } else if (*text == '.') {
            vk = VK_OEM_PERIOD;
        } else if (*text == ';') {
            vk = VK_OEM_1;
        } else if (*text == '=') {
            vk = VK_OEM_PLUS;
        } else if (*text == '-') {
            vk = VK_OEM_MINUS;
        }
        if (!requestKey(pipe, request++, vk, 0)) {
            return 1;
        }
    }
    requestKey(pipe, request++, VK_SPACE, 0);

    tuxime::ipc::writeMessage(pipe, tuxime::ipc::Type::FocusOut, request++, {});
    CloseHandle(pipe);
    return 0;
}
