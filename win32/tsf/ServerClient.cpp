#include "ServerClient.h"

#include <filesystem>
#include <string>
#include <vector>

#include "../dll/register.h"
#include "common/ipc.h"
#include "common/pipe.h"
#include "common/pipe_io.h"

namespace fcitx {

namespace {

std::wstring fromUtf8(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring serverExecutablePath() {
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(dllInstance, modulePath, MAX_PATH) == 0) {
        return {};
    }
    const auto dllDir =
        std::filesystem::path(modulePath).parent_path();
    return (dllDir / "tuxime-server.exe").wstring();
}

} // namespace

ServerClient::~ServerClient() { disconnect(); }

bool ServerClient::connect() {
    if (connected()) {
        return true;
    }
    const std::wstring name = tuxime::ipc::pipeName();
    if (name.empty()) {
        return false;
    }
    pipe_ = tuxime::ipc::connectPipe(name, 1000);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        if (!startServer()) {
            return false;
        }
        pipe_ = tuxime::ipc::connectPipe(name, 10000);
    }
    if (pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!sendHello()) {
        disconnect();
        return false;
    }
    return true;
}

bool ServerClient::startServer() {
    const std::wstring serverPath = serverExecutablePath();
    if (serverPath.empty() ||
        GetFileAttributesW(serverPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    std::wstring commandLine = L"\"" + serverPath + L"\"";
    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    const auto parent =
        std::filesystem::path(serverPath).parent_path().wstring();
    if (!CreateProcessW(serverPath.c_str(), commandLine.data(), nullptr,
                        nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        parent.c_str(), &startupInfo, &processInfo)) {
        return false;
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

void ServerClient::disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool ServerClient::writeMessage(uint32_t type, uint64_t request,
                                const std::vector<uint8_t> &payload) {
    if (!connected()) {
        return false;
    }
    return tuxime::ipc::writeMessage(
        pipe_, static_cast<tuxime::ipc::Type>(type), request, payload);
}

bool ServerClient::readMessage(uint32_t &type, uint64_t &request,
                               std::vector<uint8_t> &payload) {
    if (!connected()) {
        return false;
    }
    tuxime::ipc::Header header{};
    if (!tuxime::ipc::readMessage(pipe_, header, payload)) {
        disconnect();
        return false;
    }
    type = header.type;
    request = header.request;
    return true;
}

bool ServerClient::sendHello() {
    tuxime::ipc::Writer writer;
    writer.u32(GetCurrentProcessId());
    writer.u32(tuxime::ipc::kProtocolVersion);
    if (!writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::Hello), 0,
                      writer.take())) {
        return false;
    }

    uint32_t type = 0;
    uint64_t request = 0;
    std::vector<uint8_t> payload;
    if (!readMessage(type, request, payload) ||
        type != static_cast<uint32_t>(tuxime::ipc::Type::HelloAck)) {
        return false;
    }
    tuxime::ipc::Reader reader(payload.data(), payload.size());
    reader.u32();
    const uint32_t version = reader.u32();
    return reader.ok() && version == tuxime::ipc::kProtocolVersion;
}

void ServerClient::focusIn() {
    if (connected()) {
        writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::FocusIn),
                     nextRequest_++, {});
    }
}

void ServerClient::focusOut() {
    if (connected()) {
        writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::FocusOut),
                     nextRequest_++, {});
    }
}

void ServerClient::setCursor(const RECT &rect) {
    if (!connected()) {
        return;
    }
    tuxime::ipc::Writer writer;
    writer.i32(rect.left);
    writer.i32(rect.top);
    writer.i32(rect.right - rect.left);
    writer.i32(rect.bottom - rect.top);
    writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::Cursor),
                 nextRequest_++, writer.take());
}

void ServerClient::reset() {
    if (connected()) {
        writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::Reset),
                     nextRequest_++, {});
    }
}

ServerKeyResult ServerClient::sendKey(WPARAM vk, LPARAM lParam, bool isUp) {
    ServerKeyResult result;
    if (!connect()) {
        return result;
    }

    const uint64_t request = nextRequest_++;
    tuxime::ipc::Writer writer;
    writer.u32(static_cast<uint32_t>(vk));
    writer.u32(static_cast<uint32_t>((lParam >> 16) & 0xff));
    uint32_t modifiers = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) {
        modifiers |= tuxime::ipc::Shift;
    }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        modifiers |= tuxime::ipc::Ctrl;
    }
    if (GetKeyState(VK_MENU) & 0x8000) {
        modifiers |= tuxime::ipc::Alt;
    }
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) {
        modifiers |= tuxime::ipc::Win;
    }
    writer.u32(modifiers);
    writer.u32(isUp ? tuxime::ipc::KeyUp : 0);
    if (!writeMessage(static_cast<uint32_t>(tuxime::ipc::Type::Key),
                      request, writer.take())) {
        return result;
    }
    return readKeyResult();
}

ServerKeyResult ServerClient::selectCandidate(int index) {
    ServerKeyResult result;
    if (!connect()) {
        return result;
    }
    const uint64_t request = nextRequest_++;
    tuxime::ipc::Writer writer;
    writer.i32(index);
    if (!writeMessage(
            static_cast<uint32_t>(tuxime::ipc::Type::SelectCandidate),
            request, writer.take())) {
        return result;
    }
    return readKeyResult();
}

ServerKeyResult ServerClient::readKeyResult() {
    ServerKeyResult result;
    uint32_t type = 0;
    uint64_t response = 0;
    std::vector<uint8_t> payload;
    if (!readMessage(type, response, payload) ||
        type != static_cast<uint32_t>(tuxime::ipc::Type::KeyResult)) {
        return result;
    }
    tuxime::ipc::Reader reader(payload.data(), payload.size());
    tuxime::ipc::KeyResult keyResult{};
    keyResult.handled = reader.u32();
    keyResult.flags = reader.u32();
    keyResult.selected = reader.i32();
    keyResult.candidateCount = reader.u32();
    result.handled = keyResult.handled != 0;
    result.selected = keyResult.selected;
    result.preedit = fromUtf8(reader.str());
    result.aux = fromUtf8(reader.str());
    result.commit = fromUtf8(reader.str());
    result.status = fromUtf8(reader.str());
    for (uint32_t i = 0; i < keyResult.candidateCount; ++i) {
        ServerCandidate candidate;
        candidate.label = fromUtf8(reader.str());
        candidate.text = fromUtf8(reader.str());
        candidate.comment = fromUtf8(reader.str());
        result.candidates.push_back(std::move(candidate));
    }
    result.ok = reader.ok();
    return result;
}

} // namespace fcitx
