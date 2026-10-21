#include <vector>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>

namespace fcitx {

class TuxKeyboardEngine final : public InputMethodEngine {
public:
    std::vector<InputMethodEntry> listInputMethods() override {
        std::vector<InputMethodEntry> methods;
        methods.emplace_back("tux-keyboard", "English (Tux)", "us",
                             "tuxkeyboard");
        return methods;
    }

    void keyEvent(const InputMethodEntry &, KeyEvent &) override {}
};

class TuxKeyboardFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *) override {
        return new TuxKeyboardEngine();
    }
};

} // namespace fcitx

FCITX_ADDON_FACTORY_V2(tuxkeyboard, fcitx::TuxKeyboardFactory)
