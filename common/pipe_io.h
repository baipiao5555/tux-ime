#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

#include "ipc.h"

namespace tuxime::ipc {

inline bool readExact(HANDLE pipe, void *data, size_t size) {
    auto *out = static_cast<uint8_t *>(data);
    size_t total = 0;
    while (total < size) {
        DWORD read = 0;
        if (!ReadFile(pipe, out + total, static_cast<DWORD>(size - total),
                      &read, nullptr) ||
            read == 0) {
            return false;
        }
        total += read;
    }
    return true;
}

inline bool readMessage(HANDLE pipe, Header &header,
                        std::vector<uint8_t> &payload) {
    if (!readExact(pipe, &header, sizeof(header))) {
        return false;
    }
    if (header.size < sizeof(Header) || header.size > 1024 * 1024) {
        return false;
    }
    payload.resize(header.size - sizeof(Header));
    return payload.empty() ||
           readExact(pipe, payload.data(), payload.size());
}

inline bool writeMessage(HANDLE pipe, Type type, uint64_t request,
                         const std::vector<uint8_t> &payload) {
    const auto message = makeMessage(type, request, payload);
    DWORD written = 0;
    return WriteFile(pipe, message.data(),
                     static_cast<DWORD>(message.size()), &written,
                     nullptr) &&
           written == message.size();
}

inline HANDLE connectPipe(const std::wstring &name, DWORD timeoutMs) {
    const DWORD start = GetTickCount();
    for (;;) {
        HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            return pipe;
        }
        if (GetTickCount() - start >= timeoutMs) {
            return INVALID_HANDLE_VALUE;
        }
        Sleep(25);
    }
}

} // namespace tuxime::ipc
