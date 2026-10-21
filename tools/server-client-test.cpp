#include <cctype>
#include <cstdio>
#include <windows.h>

#include "../win32/tsf/ServerClient.h"
#include "../win32/dll/register.h"

int main() {
    fcitx::dllInstance = GetModuleHandleW(nullptr);
    fcitx::ServerClient client;
    if (!client.connect()) {
        std::printf("connect failed\n");
        return 1;
    }
    client.focusIn();
    RECT caret{100, 100, 101, 120};
    client.setCursor(caret);

    const char *text = "nihao";
    for (const char *p = text; *p; ++p) {
        auto result = client.sendKey(
            static_cast<WPARAM>(toupper(static_cast<unsigned char>(*p))), 0,
            false);
        std::printf("key %c handled=%d preedit=%ls candidates=%zu\n", *p,
                    result.handled, result.preedit.c_str(),
                    result.candidates.size());
    }
    auto result = client.sendKey(VK_SPACE, 0, false);
    std::printf("space handled=%d commit=%ls\n", result.handled,
                result.commit.c_str());
    client.focusOut();
    return 0;
}
