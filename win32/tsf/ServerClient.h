#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace fcitx {

struct ServerCandidate {
    std::wstring label;
    std::wstring text;
    std::wstring comment;
};

struct ServerKeyResult {
    bool handled = false;
    bool ok = false;
    std::wstring preedit;
    std::wstring aux;
    std::wstring commit;
    std::wstring status;
    std::vector<ServerCandidate> candidates;
    int selected = 0;
};

class ServerClient {
public:
    ServerClient() = default;
    ~ServerClient();

    ServerClient(const ServerClient &) = delete;
    ServerClient &operator=(const ServerClient &) = delete;

    bool connect();
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }
    void disconnect();

    void focusIn();
    void focusOut();
    void setCursor(const RECT &rect);
    void reset();
    ServerKeyResult sendKey(WPARAM vk, LPARAM lParam, bool isUp);
    ServerKeyResult selectCandidate(int index);

private:
    bool startServer();
    bool sendHello();
    bool writeMessage(uint32_t type, uint64_t request,
                      const std::vector<uint8_t> &payload);
    bool readMessage(uint32_t &type, uint64_t &request,
                     std::vector<uint8_t> &payload);
    ServerKeyResult readKeyResult();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    uint64_t nextRequest_ = 1;
};

} // namespace fcitx
