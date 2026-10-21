#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <windows.h>

#include <fcitx-utils/environ.h>
#include <fcitx-utils/key.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/userinterface.h>

namespace fs = std::filesystem;

namespace fcitx {
std::unique_ptr<Instance> instance;

void setenvLocal(const char *name, const std::string &value) {
    setEnvironment(name, value.c_str());
}

void setupEnv() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    auto rootPath = std::filesystem::path(path).parent_path().parent_path();
    setenvLocal("FCITX_ADDON_DIRS", (rootPath / "lib" / "fcitx5").string());
    setenvLocal("FCITX_DATA_DIRS", (rootPath / "data" / "fcitx5").string());
}

class TestInputContext final : public InputContext {
public:
    TestInputContext(InputContextManager &manager, std::string program)
        : InputContext(manager, std::move(program)) {
        created();
    }
    ~TestInputContext() override { destroy(); }

    const char *frontend() const override { return "tux-test"; }

    void commitStringImpl(const std::string &text) override {
        std::printf("COMMIT %s\n", text.c_str());
        std::fflush(stdout);
    }
    void forwardKeyImpl(const ForwardKeyEvent &key) override {
        std::printf("FORWARD %s\n", key.key().toString().c_str());
        std::fflush(stdout);
    }
    void deleteSurroundingTextImpl(int offset, unsigned int size) override {
        std::printf("DELETE %d %u\n", offset, size);
        std::fflush(stdout);
    }
    void updatePreeditImpl() override {
        std::printf("PREEDIT %s\n",
                    inputPanel().clientPreedit().toString().c_str());
        std::fflush(stdout);
    }
};

class TestUI final : public UserInterface {
public:
    void update(UserInterfaceComponent component,
                InputContext *inputContext) override {
        if (component != UserInterfaceComponent::InputPanel) {
            return;
        }
        const auto &panel = inputContext->inputPanel();
        std::printf("UI preedit=[%s] aux=[%s]\n",
                    panel.clientPreedit().toString().c_str(),
                    panel.auxUp().toString().c_str());
        const auto &candidateList = *panel.candidateList();
        for (int i = 0; i < candidateList.size(); ++i) {
            const auto &candidate = candidateList.candidate(i);
            std::printf("  %d: %s %s\n", i,
                        candidate.text().toString().c_str(),
                        candidate.comment().toString().c_str());
        }
        std::fflush(stdout);
    }
    bool available() override { return true; }
    void suspend() override {}
    void resume() override {}
};

} // namespace fcitx

int main() {
    fcitx::setupEnv();
    fcitx::Log::setLogRule("*=4");

    fcitx::instance = std::make_unique<fcitx::Instance>(0, nullptr);
    auto &addonManager = fcitx::instance->addonManager();
    addonManager.registerDefaultLoader(nullptr);
    fcitx::instance->initialize();

    auto &inputMethodManager = fcitx::instance->inputMethodManager();
    inputMethodManager.load();
    std::printf("LOADED GROUPS %d\n", inputMethodManager.groupCount());
    fcitx::InputMethodGroup group("Default");
    group.setDefaultLayout("us");
    group.inputMethodList().emplace_back("pinyin");
    group.setDefaultInputMethod("pinyin");
    inputMethodManager.setGroup(std::move(group));
    std::printf("SET GROUP\n");
    std::printf("CURRENT GROUP %s\n",
                inputMethodManager.currentGroup().name().c_str());
    for (const auto &item : inputMethodManager.currentGroup().inputMethodList()) {
        std::printf("IM %s\n", item.name().c_str());
    }

    fcitx::TestInputContext ic(fcitx::instance->inputContextManager(), "tux-test");
    ic.setCursorRect(fcitx::Rect(100, 100, 1, 20));
    ic.focusIn();

    const char *keys = "nihao";
    for (const char *p = keys; *p; ++p) {
        fcitx::Key key(static_cast<fcitx::KeySym>(*p));
        fcitx::KeyEvent event(&ic, key, false);
        bool handled = ic.keyEvent(event);
        std::printf("KEY %c handled=%d accepted=%d\n", *p, handled,
                    event.accepted());
    }
    fcitx::Key space(static_cast<fcitx::KeySym>(' '));
    fcitx::KeyEvent spaceEvent(&ic, space, false);
    std::printf("SPACE handled=%d\n", ic.keyEvent(spaceEvent));
    std::printf("DONE\n");
    return 0;
}
