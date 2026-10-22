#include <cstdio>
#include <vector>
#include <windows.h>
#include <wincrypt.h>

#include "../src/clipboard_image.h"

int main() {
    const char *base64 =
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4"
        "z8DwHwAFgAI/ScL5WQAAAABJRU5ErkJggg==";
    DWORD size = 0;
    CryptStringToBinaryA(base64, 0, CRYPT_STRING_BASE64, nullptr, &size,
                         nullptr, nullptr);
    std::vector<uint8_t> png(size);
    if (!CryptStringToBinaryA(base64, 0, CRYPT_STRING_BASE64, png.data(),
                              &size, nullptr, nullptr)) {
        std::printf("decode failed\n");
        return 1;
    }
    png.resize(size);
    if (!tuxime::writeClipboardImage(nullptr, png)) {
        std::printf("write failed\n");
        return 1;
    }
    std::vector<uint8_t> readBack;
    if (!tuxime::readClipboardImage(nullptr, readBack)) {
        std::printf("read failed\n");
        return 1;
    }
    std::printf("png=%zu read=%zu hasImage=%d\n", png.size(), readBack.size(),
                tuxime::clipboardHasImage() ? 1 : 0);
    return readBack.empty() ? 1 : 0;
}
