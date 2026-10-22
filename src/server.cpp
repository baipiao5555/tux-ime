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
#include <fcitx/action.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>

#include "clipboard_sync.h"
#include "common/pipe.h"
#include "tray.h"

namespace tuxime {

namespace {

constexpr const char *kInputMethods[] = {
    "tux-layout", "tux-keyboard", "pinyin", "shuangpin", "wbx",
    "wbpy",       "cangjie",      "erbi",   "zrm",       "qxm",
    "db",         "wanfeng",
};

bool knownInputMethod(const std::string &name) {
    for (const char *candidate : kInputMethods) {
        if (name == candidate) {
            return true;
        }
    }
    return false;
}

std::string inputMethodDisplayName(const std::string &name) {
    if (name == "pinyin") {
        return "拼音";
    }
    if (name == "shuangpin") {
        return "双拼";
    }
    if (name == "wbx") {
        return "五笔";
    }
    if (name == "wbpy") {
        return "五笔拼音";
    }
    if (name == "cangjie") {
        return "仓颉";
    }
    if (name == "erbi") {
        return "二笔";
    }
    if (name == "zrm") {
        return "自然码";
    }
    if (name == "qxm") {
        return "冰蟾";
    }
    if (name == "db") {
        return "电报码";
    }
    if (name == "wanfeng") {
        return "晚风";
    }
    if (name == "tux-keyboard") {
        return "英文";
    }
    if (name == "tux-layout") {
        return "键盘";
    }
    return name;
}

std::string statusLabel(const std::string &action,
                        const std::string &text) {
    if (action == "punctuation") {
        if (text.find("Full width") != std::string::npos ||
            text.find("full width") != std::string::npos) {
            return "中文标点";
        }
        return "英文标点";
    }
    if (action == "fullwidth") {
        if (text.find("Full width") != std::string::npos ||
            text.find("full width") != std::string::npos) {
            return "全角";
        }
        return "半角";
    }
    if (action == "chttrans") {
        if (text.find("Traditional") != std::string::npos ||
            text.find("traditional") != std::string::npos) {
            return "繁体";
        }
        return "简体";
    }
    return text;
}

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
    server_.instance().setCurrentInputMethod(
        server_.currentInputMethod());
    addStatusActions();
}

void Session::addStatusActions() {
    if (!ic_) {
        return;
    }
    const auto existing = ic_->statusArea().allActions();
    for (const char *name : {"punctuation", "fullwidth", "chttrans"}) {
        auto *action =
            server_.instance().userInterfaceManager().lookupAction(name);
        if (!action) {
            continue;
        }
        if (std::find(existing.begin(), existing.end(), action) ==
            existing.end()) {
            ic_->statusArea().addAction(fcitx::StatusGroup::AfterInputMethod,
                                        action);
        }
    }
}

void Session::captureStatus() {
    std::string status;
    if (ic_) {
        status = inputMethodDisplayName(
            server_.instance().currentInputMethod());
        for (auto *action : ic_->statusArea().allActions()) {
            if (!action || action->isSeparator()) {
                continue;
            }
            const std::string name = action->name();
            if (name != "punctuation" && name != "fullwidth" &&
                name != "chttrans") {
                continue;
            }
            const std::string text = action->shortText(ic_.get());
            if (text.empty()) {
                continue;
            }
            if (!status.empty()) {
                status += " · ";
            }
            status += statusLabel(name, text);
        }
        if (auto candidateList = ic_->inputPanel().candidateList()) {
            if (auto pageable = candidateList->toPageable()) {
                const int total = pageable->totalPages();
                const int current = pageable->currentPage();
                if (total > 1 && current >= 0) {
                    if (!status.empty()) {
                        status += " · ";
                    }
                    status += std::to_string(current + 1) + "/" +
                              std::to_string(total);
                }
            }
        }
    }
    std::lock_guard<std::mutex> lock(stateMutex_);
    status_ = std::move(status);
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
        addStatusActions();
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

void Session::activateAction(const std::string &actionName) {
    if (!ic_) {
        return;
    }
    if (auto *action =
            server_.instance().userInterfaceManager().lookupAction(
                actionName)) {
        action->activate(ic_.get());
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
            captureStatus();
        }
    }
    sendKeyResult(request, handled);
}

void Session::selectCandidate(int index, uint64_t request) {
    bool handled = false;
    if (ic_) {
        auto candidateList = ic_->inputPanel().candidateList();
        if (candidateList && index >= 0 &&
            index < candidateList->size()) {
            candidateList->candidate(index).select(ic_.get());
            handled = true;
            capturePanel(ic_->inputPanel());
            captureStatus();
        }
    }
    sendKeyResult(request, handled);
}

void Session::sendKeyResult(uint64_t request, bool handled) {
    ipc::Writer writer;
    std::string preedit;
    std::string aux;
    std::string commit;
    std::string status;
    std::vector<Candidate> candidates;
    int selected = 0;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        preedit = preedit_;
        aux = aux_;
        commit = std::move(commit_);
        commit_.clear();
        status = status_;
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
    if (!status.empty()) {
        flags |= ipc::HasStatus;
    }

    writer.u32(handled ? 1 : 0);
    writer.u32(flags);
    writer.i32(selected);
    writer.u32(static_cast<uint32_t>(candidates.size()));
    writer.str(preedit);
    writer.str(aux);
    writer.str(commit);
    if (!legacyProtocol_) {
        writer.str(status);
    }
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
        if (!reader.ok() ||
            (version != 1 && version != ipc::kProtocolVersion)) {
            alive_ = false;
            return;
        }
        pid_ = pid;
        protocolVersion_ = version;
        legacyProtocol_ = version == 1;
        server_.runOnFcitxThread([this]() { createContext(); });
        ipc::Writer writer;
        writer.u32(id_);
        writer.u32(version);
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
    case ipc::Type::ToggleAddon: {
        const std::string action = reader.str();
        if (reader.ok()) {
            server_.toggleAddon(action);
        }
        break;
    }
    case ipc::Type::SwitchInputMethod: {
        const std::string inputMethod = reader.str();
        if (reader.ok()) {
            server_.switchInputMethod(inputMethod);
        }
        break;
    }
    case ipc::Type::SelectCandidate: {
        const int index = reader.i32();
        if (reader.ok()) {
            server_.runOnFcitxThread([this, index, request]() {
                selectCandidate(index, request);
            });
        }
        break;
    }
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
    if (initialPipe_) {
        CloseHandle(initialPipe_);
        initialPipe_ = nullptr;
    }
    sessions_.clear();
}

void Server::setupEnvironment() {
    using SetDpiAwarenessContextFn = BOOL(WINAPI *)(HANDLE);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setContext = reinterpret_cast<SetDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            setContext(reinterpret_cast<HANDLE>(-4));
        } else {
            SetProcessDPIAware();
        }
    }

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
    auto &addonManager = instance_->addonManager();
    for (const char *addon : {"pinyin", "table", "tuxkeyboard"}) {
        try {
            addonManager.addon(addon, true);
        } catch (const std::exception &e) {
            FCITX_WARN() << "failed to load addon " << addon << ": "
                         << e.what();
        }
    }

    auto &manager = instance_->inputMethodManager();
    manager.load();
    std::string defaultInputMethod =
        SettingsStore::loadPinyin().defaultInputMethod;
    if (!knownInputMethod(defaultInputMethod) ||
        defaultInputMethod == "tux-layout") {
        defaultInputMethod = "pinyin";
    }
    currentInputMethod_ = defaultInputMethod;

    fcitx::InputMethodGroup group("Default");
    group.setDefaultLayout("us");
    for (const char *name : kInputMethods) {
        group.inputMethodList().emplace_back(name);
    }
    group.setDefaultInputMethod(defaultInputMethod);
    manager.setGroup(std::move(group));
    manager.setCurrentGroup("Default");
    manager.save();
}

bool Server::claimPipe() {
    SECURITY_ATTRIBUTES attributes{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ipc::makePipeSecurity(attributes, descriptor)) {
        FCITX_ERROR() << "failed to create pipe security descriptor";
        return false;
    }
    const std::wstring name = ipc::pipeName();
    if (name.empty()) {
        LocalFree(descriptor);
        return false;
    }
    HANDLE pipe = CreateNamedPipeW(
        name.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED || error == ERROR_PIPE_BUSY) {
            FCITX_INFO() << "another tux-ime server is already running";
        } else {
            FCITX_ERROR() << "CreateNamedPipe failed: " << error;
        }
        return false;
    }
    initialPipe_ = pipe;
    return true;
}

bool Server::initialize() {
    setupEnvironment();
    if (!claimPipe()) {
        return false;
    }
    SettingsStore::ensureAddonDefaults();
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
    runOnFcitxThread([this, defaultIM = pinyin.defaultInputMethod]() {
        currentInputMethod_ = defaultIM;
        instance_->reloadConfig();
        auto &manager = instance_->inputMethodManager();
        manager.setDefaultInputMethod(defaultIM);
        manager.save();
        instance_->setCurrentInputMethod(defaultIM);
    });
}

void Server::switchInputMethod(bool chinese) {
    switchInputMethod(chinese ? "pinyin" : "tux-keyboard");
}

void Server::switchInputMethod(const std::string &inputMethod) {
    if (!knownInputMethod(inputMethod) || inputMethod == "tux-layout") {
        return;
    }
    runOnFcitxThread([this, inputMethod]() {
        currentInputMethod_ = inputMethod;
        instance_->setCurrentInputMethod(inputMethod);
    });
}

void Server::toggleAddon(const std::string &action) {
    runOnFcitxThread([this, action]() {
        std::vector<std::shared_ptr<Session>> sessions;
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            for (const auto &entry : sessions_) {
                sessions.push_back(entry.second);
            }
        }
        for (const auto &session : sessions) {
            session->activateAction(action);
        }
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
        HANDLE pipe = nullptr;
        if (initialPipe_) {
            // Keep the FILE_FLAG_FIRST_PIPE_INSTANCE handle created before
            // the Fcitx instance started, so a second UI-less server exits
            // instead of leaving an idle process and tray icon behind.
            pipe = initialPipe_;
            initialPipe_ = nullptr;
            first = false;
        } else {
            DWORD openMode = PIPE_ACCESS_DUPLEX;
            if (first) {
                openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
            }
            pipe = CreateNamedPipeW(
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
        }
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
