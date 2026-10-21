#include "util.h"
#include <format>

namespace fcitx {
// {8D7031FD-4D2D-4CCE-BB41-05F54D7AC612}
const GUID FCITX_CLSID = {0x8d7031fd,
                          0x4d2d,
                          0x4cce,
                          {0xbb, 0x41, 0x05, 0xf5, 0x4d, 0x7a, 0xc6, 0x12}};

// {76AE52E2-7F68-4A6D-81D0-A48D4AC73E36}
const GUID PROFILE_GUID{0x76ae52e2,
                        0x7f68,
                        0x4a6d,
                        {0x81, 0xd0, 0xa4, 0x8d, 0x4a, 0xc7, 0x3e, 0x36}};

std::string guidToString(REFGUID guid) {
    const auto *p = reinterpret_cast<const unsigned char *>(&guid);
    return std::format("{{{:02X}{:02X}{:02X}{:02X}-{:02X}{:02X}-{:02X}{:02X}-{:"
                       "02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}",
                       p[3], p[2], p[1], p[0], p[5], p[4], p[7], p[6], p[8],
                       p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

std::wstring stringToWString(const std::string &str, int codePage) {
    int len =
        MultiByteToWideChar(codePage, 0, str.c_str(), str.size(), nullptr, 0);
    WCHAR *buf = new WCHAR[len + 1];
    MultiByteToWideChar(codePage, 0, str.c_str(), str.size(), buf, len);
    buf[len] = '\0';
    std::wstring wstr;
    wstr.append(buf);
    delete[] buf;
    return wstr;
}
} // namespace fcitx
