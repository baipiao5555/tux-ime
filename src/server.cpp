#include "server.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <memory>
#include <utility>

#include <fcitx-utils/environ.h>
#include <fcitx-utils/event.h>
#include <fcitx/addonmanager.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>

#include "clipboard_sync.h"
#include "common/pipe.h"
#include "tray.h"

namespace tuxime {

namespace {

fcitx::KeySym keySymFromVk(uint32_t vk) {
    switch (vk) {
    case VK_SPACE:
        return FcitxKey_space;
    case VK_RETURN:
        return FcitxKey_Return;
    case VK_BACK:
        return FcitxKey_BackSpace;
    case VK_ESCAPE:
        return FcitxKey_Escape;
    case VK_TAB:
        return FcitxKey_Tab;
    case VK_LEFT:
        return FcitxKey_Left;
    case VK_RIGHT:
        return FcitxKey_Right;
    case VK_UP:
        return FcitxKey_Up;
    case VK_DOWN:
        return FcitxKey_Down;
    case VK_DELETE:
        return FcitxKey_Delete;
    case VK_HOME:
        return FcitxKey_Home;
    case VK_END:
        return FcitxKey_End;
    case VK_PRIOR:
        return FcitxKey_Prior;
    case VK_NEXT:
        return FcitxKey_Next;
    case VK_OEM_1:
        return FcitxKey_semicolon;
    case VK_OEM_2:
        return FcitxKey_slash;
    case VK_OEM_3:
        return FcitxKey_grave;
    case VK_OEM_4:
        return FcitxKey_bracketleft;
    case VK_OEM_5:
        return FcitxKey_backslash;
    case VK_OEM_6:
        return FcitxKey_bracketright;
    case VK_OEM_7:
        return FcitxKey_apostrophe;
    case VK_OEM_COMMA:
        return FcitxKey_comma;
    case VK_OEM_MINUS:
        return FcitxKey_minus;
    case VK_OEM_PERIOD:
        return FcitxKey_period;
    case VK_OEM_PLUS:
        return FcitxKey_equal;
    default:
        break;
    }

    if (vk >= 'A' && vk <= 'Z') {
        return static_cast<fcitx::KeySym>('a' + (vk - 'A'));
    }
    if (vk >= '0' && vk <= '9') {
        return static_cast<fcitx::KeySym>('0' + (vk - '0'));
    }
    return FcitxKey_None;
}

fcitx::KeyStates keyStatesFromModifiers(uint32_t modifiers) {
    fcitx::KeyStates states = fcitx::KeyState::NoState;
    if (modifiers & ipc::Shift) {
        states |= fcitx::KeyState::Shift;
    }
    if (modifiers & ipc::Ctrl) {
        states |= fcitx::KeyState::Ctrl;
    }
    if (modifiers & ipc::Alt) {
        states |= fcitx::KeyState::Alt;
    }
    if (modifiers & ipc::Win) {
        states |= fcitx::KeyState::Super;
    }
    return states;
}

std::string narrow(const std::wstring &value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(),
                        static_cast<int>(value.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

} // namespace

ServerInputContext::ServerInputContext(Session &session,
                                       fcitx::InputContextManager &manager)
    : fcitx::InputContext(manager, "tuxime-server"), session_(session) {
    created();
    inputPanel().setCustomInputPanelCallback(
        [this](fcitx::InputContext *ic) {
            session_.capturePanel(ic->inputPanel());
        });
}

ServerInputContext::~ServerInputContext() { destroy(); }

void ServerInputContext::commitStringImpl(const std::string &text) {
    session_.appendCommit(text);
}

void ServerInputContext::forwardKeyImpl(const fcitx::ForwardKeyEvent &) {}

void ServerInputContext::deleteSurroundingTextImpl(int, unsigned int) {}

void ServerInputContext::updatePreeditImpl() {
    session_.capturePanel(inputPanel());
}

Session::Session(Server &server, uint32_t id, HANDLE pipe, uint32_t pid)
    : server_(server), id_(id), pid_(pid), pipe_(pipe) {}

Session::~Session() {
    alive_ = false;
    if (pipe_ != INVALID_HANDLE_VALUE && pipe_ != nullptr) {
        CloseHandle(pipe_);
        pipe_ = nullptr;
    }
    if (thread_.joinable()) {
        if (thread_.get_id() == std::this_thread::get_id()) {
            thread_.detach();
        } else {
            thread_.join();
        }
    }
}

void Session::start() {
    auto self = shared_from_this();
    thread_ = std::thread([self]() { self->run(); });
}

void Session::createContext() {
    if (ic_) {
        return;
    }
    ic_ = std::make_unique<ServerInputContext>(
        *this, server_.instance().inputContextManager());
    ic_->focusIn();
}

void Session::destroyContext() {
    if (!ic_) {
        return;
    }
    ic_->focusOut();
    ic_.reset();
}

void Session::focus(bool focused) {
    if (!ic_) {
        return;
    }
    if (focused) {
        ic_->focusIn();
    } else {
        ic_->focusOut();
    }
}

void Session::setCursor(const fcitx::Rect &rect) {
    if (ic_) {
        ic_->setCursorRect(rect);
    }
}

void Session::resetEngine() {
    if (ic_) {
        ic_->reset();
    }
}

void Session::appendCommit(const std::string &text) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    commit_ += text;
}

void Session::capturePanel(const fcitx::InputPanel &panel) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    preedit_ = panel.clientPreedit().toString();
    if (preedit_.empty()) {
        preedit_ = panel.preedit().toString();
    }
    aux_ = panel.auxUp().toString();
    candidates_.clear();
    selected_ = 0;
    auto candidateList = panel.candidateList();
    if (!candidateList) {
        return;
    }
    const int count = (std::min)(candidateList->size(), 50);
    selected_ = candidateList->cursorIndex();
    candidates_.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &candidate = candidateList->candidate(i);
        candidates_.push_back(
            {candidateList->label(i).toString(),
             candidate.text().toString(), candidate.comment().toString()});
    }
}

void Session::handleKey(const ipc::Key &msg, uint64_t request) {
    bool handled = false;
    if (ic_) {
        const auto sym = keySymFromVk(msg.vk);
        if (sym != FcitxKey_None) {
            const fcitx::Key key(sym,
                                 keyStatesFromModifiers(msg.modifiers));
            fcitx::KeyEvent event(ic_.get(), key, (msg.flags & ipc::KeyUp));
            handled = ic_->keyEvent(event) || event.accepted();
            capturePanel(ic_->inputPanel());
        }
    }
    sendKeyResult(request, handled);
}

void Session::sendKeyResult(uint64_t request, bool handled) {
    ipc::Writer writer;
    std::string preedit;
    std::string aux;
    std::string commit;
    std::vector<Candidate> candidates;
    int selected = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        preedit = preedit_;
        aux = aux_;
        commit = std::move(commit_);
        commit_.clear();
        candidates = candidates_;
        selected = selected_;
    }

    uint32_t flags = 0;
    if (!preedit.empty()) {
        flags |= ipc::HasPreedit;
    }
    if (!commit.empty()) {
        flags |= ipc::HasCommit;
    }
    if (!aux.empty()) {
        flags |= ipc::HasAux;
    }
    if (!candidates.empty()) {
        flags |= ipc::HasCandidates;
    }

    writer.u32(handled ? 1 : 0);
    writer.u32(flags);
    writer.i32(selected);
    writer.u32(static_cast<uint32_t>(candidates.size()));
    writer.str(preedit);
    writer.str(aux);
    writer.str(commit);
    for (const auto &candidate : candidates) {
        writer.str(candidate.label);
        writer.str(candidate.text);
        writer.str(candidate.comment);
    }
    send(ipc::Type::KeyResult, request, writer.take());
}

bool Session::readExact(void *data, size_t size) {
    auto *out = static_cast<uint8_t *>(data);
    size_t total = 0;
    while (total < size) {
        DWORD read = 0;
        if (!ReadFile(pipe_, out + total, static_cast<DWORD>(size - total),
                      &read, nullptr) ||
            read == 0) {
            return false;
        }
        total += read;
    }
    return true;
}

void Session::send(ipc::Type type, uint64_t request,
                   const std::vector<uint8_t> &payload) {
    const auto message = ipc::makeMessage(type, request, payload);
    std::lock_guard<std::mutex> lock(writeMutex_);
    DWORD written = 0;
    if (!WriteFile(pipe_, message.data(),
                   static_cast<DWORD>(message.size()), &written, nullptr) ||
        written != message.size()) {
        alive_ = false;
    }
}

void Session::dispatch(ipc::Type type, uint64_t request,
                       ipc::Reader &reader) {
    switch (type) {
    case ipc::Type::Hello: {
        const uint32_t pid = reader.u32();
        const uint32_t version = reader.u32();
        if (!reader.ok() || version != ipc::kProtocolVersion) {
            alive_ = false;
            return;
        }
        pid_ = pid;
        server_.runOnFcitxThread([this]() { createContext(); });
        ipc::Writer writer;
        writer.u32(id_);
        writer.u32(ipc::kProtocolVersion);
        send(ipc::Type::HelloAck, request, writer.take());
        break;
    }
    case ipc::Type::FocusIn:
        server_.runOnFcitxThread([this]() { focus(true); });
        break;
    case ipc::Type::FocusOut:
        server_.runOnFcitxThread([this]() { focus(false); });
        break;
    case ipc::Type::Cursor: {
        const int32_t x = reader.i32();
        const int32_t y = reader.i32();
        const int32_t width = reader.i32();
        const int32_t height = reader.i32();
        if (!reader.ok()) {
            alive_ = false;
            return;
        }
        server_.runOnFcitxThread([this, x, y, width, height]() {
            setCursor(fcitx::Rect(x, y, x + width, y + height));
        });
        break;
    }
    case ipc::Type::Key: {
        ipc::Key key{};
        key.vk = reader.u32();
        key.scan = reader.u32();
        key.modifiers = reader.u32();
        key.flags = reader.u32();
        if (!reader.ok()) {
            alive_ = false;
            return;
        }
        server_.runOnFcitxThread([this, key, request]() {
            handleKey(key, request);
        });
        break;
    }
    case ipc::Type::Reset:
        server_.runOnFcitxThread([this]() { resetEngine(); });
        break;
    case ipc::Type::ShowSettings:
        server_.showSettings();
        break;
    default:
        break;
    }
}

void Session::run() {
    auto self = shared_from_this();
    while (alive_) {
        ipc::Header header{};
        if (!readExact(&header, sizeof(header))) {
            break;
        }
        if (header.size < sizeof(ipc::Header) ||
            header.size > 1024 * 1024) {
            break;
        }
        std::vector<uint8_t> payload(header.size - sizeof(ipc::Header));
        if (!payload.empty() &&
            !readExact(payload.data(), payload.size())) {
            break;
        }
        ipc::Reader reader(payload.data(), payload.size());
        dispatch(static_cast<ipc::Type>(header.type), header.request, reader);
    }
    alive_ = false;
    server_.runOnFcitxThread([this]() { destroyContext(); });
    server_.schedule([this, self]() { server_.removeSession(id_); });
}

Server::Server() = default;

Server::~Server() {
    running_ = false;
    if (clipboardSync_) {
        clipboardSync_->stop();
    }
    if (tray_) {
        tray_->stop();
    }
    if (dispatcher_) {
        dispatcher_->schedule(nullptr);
    }
    stopAccepting();
    if (pipeThread_.joinable()) {
        pipeThread_.join();
    }
    sessions_.clear();
}

void Server::setupEnvironment() {
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const auto root =
        std::filesystem::path(modulePath).parent_path().parent_path();
    const auto addonDir = root / "lib" / "fcitx5";
    const auto dataDir = root / "data" / "fcitx5";
    fcitx::setEnvironment("FCITX_ADDON_DIRS", narrow(addonDir.wstring()).c_str());
    fcitx::setEnvironment("FCITX_DATA_DIRS", narrow(dataDir.wstring()).c_str());
    fcitx::setEnvironment("XDG_DATA_DIRS",
                          narrow((root / "data").wstring()).c_str());
}

void Server::registerDefaultGroup() {
    auto &manager = instance_->inputMethodManager();
    manager.load();
    const bool keyboardDefault = SettingsStore::loadPinyin().keyboardDefault;
    fcitx::InputMethodGroup group("Default");
    group.setDefaultLayout("us");
    group.inputMethodList().emplace_back("pinyin");
    group.inputMethodList().emplace_back("tux-keyboard");
    group.setDefaultInputMethod(keyboardDefault ? "tux-keyboard" : "pinyin");
    manager.setGroup(std::move(group));
    manager.setCurrentGroup("Default");
    manager.save();
}

bool Server::initialize() {
    setupEnvironment();
    fcitx::Log::setLogRule("*=4");

    instance_ = std::make_unique<fcitx::Instance>(0, nullptr);
    dispatcher_ = std::make_unique<fcitx::EventDispatcher>();
    dispatcher_->attach(&instance_->eventLoop());

    auto &addonManager = instance_->addonManager();
    addonManager.registerDefaultLoader(nullptr);
    instance_->initialize();
    registerDefaultGroup();
    return instance_->initialized();
}

int Server::run(bool showSettingsRequested) {
    clipboardSync_ = std::make_unique<ClipboardSync>();
    tray_ = std::make_unique<TrayIcon>(*this);
    tray_->start();
    if (showSettingsRequested) {
        tray_->showSettings();
    }
    clipboardSync_->start(SettingsStore::loadSync(),
                          [this](const std::wstring &status) {
                              if (tray_) {
                                  tray_->setStatus(status);
                              }
                          });

    pipeThread_ = std::thread([this]() { acceptLoop(); });
    instance_->eventLoop().exec();
    running_ = false;
    stopAccepting();
    if (clipboardSync_) {
        clipboardSync_->stop();
    }
    if (tray_) {
        tray_->stop();
    }
    return 0;
}

void Server::stopAccepting() {
    HANDLE pipe = listenPipe_.exchange(nullptr);
    if (pipe) {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

void Server::showSettings() {
    if (tray_) {
        tray_->showSettings();
    }
}

void Server::requestExit() {
    running_ = false;
    stopAccepting();
    schedule([this]() { instance_->exit(); });
}

void Server::applySettings(const PinyinSettings &pinyin,
                           const SyncConfig &sync) {
    if (clipboardSync_) {
        clipboardSync_->updateConfig(sync);
    }
    runOnFcitxThread([this, keyboardDefault = pinyin.keyboardDefault]() {
        instance_->reloadConfig();
        auto &manager = instance_->inputMethodManager();
        manager.setDefaultInputMethod(keyboardDefault ? "tux-keyboard"
                                                       : "pinyin");
        manager.save();
    });
}

void Server::schedule(std::function<void()> func) {
    if (!dispatcher_) {
        return;
    }
    dispatcher_->schedule(std::move(func));
}

void Server::runOnFcitxThread(std::function<void()> func) {
    if (!dispatcher_) {
        return;
    }
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    dispatcher_->schedule([func = std::move(func), promise]() mutable {
        try {
            func();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    if (future.wait_for(std::chrono::seconds(10)) !=
        std::future_status::ready) {
        FCITX_WARN() << "timed out waiting for the fcitx thread";
        return;
    }
    try {
        future.get();
    } catch (const std::exception &e) {
        FCITX_ERROR() << "fcitx thread operation failed: " << e.what();
    }
}

void Server::removeSession(uint32_t id) {
    std::shared_ptr<Session> removed;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto iter = sessions_.find(id);
        if (iter == sessions_.end()) {
            return;
        }
        removed = std::move(iter->second);
        sessions_.erase(iter);
    }
    if (removed) {
        removed->destroyContext();
    }
}

void Server::acceptLoop() {
    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ipc::makePipeSecurity(attributes, descriptor)) {
        FCITX_ERROR() << "failed to create pipe security descriptor";
        return;
    }
    const std::wstring name = ipc::pipeName();
    if (name.empty()) {
        LocalFree(descriptor);
        return;
    }

    bool first = true;
    while (running_) {
        DWORD openMode = PIPE_ACCESS_DUPLEX;
        if (first) {
            openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        }
        HANDLE pipe = CreateNamedPipeW(
            name.c_str(), openMode,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0,
            &attributes);
        if (pipe == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            if (first &&
                (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY)) {
                FCITX_INFO() << "another tux-ime server is already running";
                break;
            }
            FCITX_ERROR() << "CreateNamedPipe failed: " << error;
            Sleep(100);
            continue;
        }
        first = false;
        listenPipe_ = pipe;

        const BOOL connected =
            ConnectNamedPipe(pipe, nullptr)
                ? TRUE
                : GetLastError() == ERROR_PIPE_CONNECTED;
        listenPipe_ = nullptr;
        if (!connected) {
            CloseHandle(pipe);
            continue;
        }

        uint32_t id = 0;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            id = nextSessionId_++;
        }
        auto session = std::make_shared<Session>(*this, id, pipe, 0);
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_[id] = session;
        }
        session->start();
    }
    LocalFree(descriptor);
}

} // namespace tuxime
