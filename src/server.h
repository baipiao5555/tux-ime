#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <windows.h>

#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

#include "common/ipc.h"
#include "settings.h"

namespace fcitx {
class EventDispatcher;
class InputContextManager;
class KeyEvent;
class InputPanel;
} // namespace fcitx

namespace tuxime {

class ClipboardSync;
class TrayIcon;
class Server;
class Session;

class ServerInputContext final : public fcitx::InputContext {
public:
    ServerInputContext(Session &session, fcitx::InputContextManager &manager);
    ~ServerInputContext() override;

    const char *frontend() const override { return "tuxime"; }

    void commitStringImpl(const std::string &text) override;
    void forwardKeyImpl(const fcitx::ForwardKeyEvent &key) override;
    void deleteSurroundingTextImpl(int offset, unsigned int size) override;
    void updatePreeditImpl() override;

private:
    Session &session_;
};

struct Candidate {
    std::string label;
    std::string text;
    std::string comment;
};

class Session final : public std::enable_shared_from_this<Session> {
public:
    Session(Server &server, uint32_t id, HANDLE pipe, uint32_t pid);
    ~Session();

    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    void start();
    uint32_t id() const { return id_; }
    uint32_t pid() const { return pid_; }

    void createContext();
    void destroyContext();
    void focus(bool focused);
    void setCursor(const fcitx::Rect &rect);
    void resetEngine();
    void handleKey(const ipc::Key &key, uint64_t request);
    void selectCandidate(int index, uint64_t request);
    void activateAction(const std::string &action);
    void addStatusActions();
    void captureStatus();
    void appendCommit(const std::string &text);
    void capturePanel(const fcitx::InputPanel &panel);

private:
    void run();
    void dispatch(ipc::Type type, uint64_t request,
                  ipc::Reader &reader);
    void send(ipc::Type type, uint64_t request,
              const std::vector<uint8_t> &payload);
    void sendKeyResult(uint64_t request, bool handled);
    bool readExact(void *data, size_t size);

    Server &server_;
    uint32_t id_;
    uint32_t pid_;
    uint32_t protocolVersion_ = ipc::kProtocolVersion;
    bool legacyProtocol_ = false;
    HANDLE pipe_;
    std::atomic<bool> alive_{true};
    std::thread thread_;

    std::mutex writeMutex_;
    std::mutex stateMutex_;
    std::unique_ptr<ServerInputContext> ic_;
    std::string preedit_;
    std::string aux_;
    std::string commit_;
    std::string status_;
    std::vector<Candidate> candidates_;
    int selected_ = 0;
};

class Server {
public:
    Server();
    ~Server();

    bool initialize();
    int run(bool showSettings = false);
    void showSettings();

    fcitx::Instance &instance() { return *instance_; }
    ClipboardSync &clipboardSync() { return *clipboardSync_; }
    const std::string &currentInputMethod() const {
        return currentInputMethod_;
    }

    void schedule(std::function<void()> func);
    void runOnFcitxThread(std::function<void()> func);
    void removeSession(uint32_t id);
    void applySettings(const PinyinSettings &pinyin,
                       const SyncConfig &sync);
    void switchInputMethod(bool chinese);
    void switchInputMethod(const std::string &inputMethod);
    void toggleAddon(const std::string &action);
    void requestExit();

private:
    void acceptLoop();
    bool claimPipe();
    void stopAccepting();
    void setupEnvironment();
    void registerDefaultGroup();

    std::unique_ptr<fcitx::Instance> instance_;
    std::unique_ptr<fcitx::EventDispatcher> dispatcher_;
    std::atomic<bool> running_{true};
    std::atomic<HANDLE> listenPipe_{nullptr};
    HANDLE initialPipe_ = nullptr;
    std::thread pipeThread_;
    std::mutex sessionsMutex_;
    std::unordered_map<uint32_t, std::shared_ptr<Session>> sessions_;
    uint32_t nextSessionId_ = 1;
    std::unique_ptr<ClipboardSync> clipboardSync_;
    std::unique_ptr<TrayIcon> tray_;
    std::string currentInputMethod_ = "pinyin";
};

} // namespace tuxime
