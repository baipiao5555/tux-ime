#pragma once

#include <cstdint>
#include <vector>
#include <windows.h>

namespace tuxime {

bool clipboardHasImage();
bool readClipboardImage(HWND owner, std::vector<uint8_t> &png);
bool writeClipboardImage(HWND owner, const std::vector<uint8_t> &png);

} // namespace tuxime
