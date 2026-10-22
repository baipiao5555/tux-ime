#include "clipboard_image.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

#include <gdiplus.h>
#include <objidl.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")

namespace tuxime {

namespace {

ULONG_PTR gdiplusToken() {
    static ULONG_PTR token = 0;
    static std::once_flag once;
    std::call_once(once, []() {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
    });
    return token;
}

bool encodePng(Gdiplus::Bitmap &bitmap, std::vector<uint8_t> &png) {
    IStream *stream = nullptr;
    if (!SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream))) {
        return false;
    }
    CLSID encoder = {0x557cf406,
                     0x1a04,
                     0x11d3,
                     {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    const bool saved = bitmap.Save(stream, &encoder, nullptr) == Gdiplus::Ok;
    if (!saved) {
        stream->Release();
        return false;
    }
    STATSTG stat{};
    if (FAILED(stream->Stat(&stat, STATFLAG_NONAME)) ||
        stat.cbSize.QuadPart == 0 ||
        stat.cbSize.QuadPart > 64 * 1024 * 1024) {
        stream->Release();
        return false;
    }
    HGLOBAL memory = nullptr;
    if (FAILED(GetHGlobalFromStream(stream, &memory)) || !memory) {
        stream->Release();
        return false;
    }
    const auto *data = static_cast<const uint8_t *>(GlobalLock(memory));
    if (!data) {
        stream->Release();
        return false;
    }
    png.assign(data, data + stat.cbSize.QuadPart);
    GlobalUnlock(memory);
    stream->Release();
    return !png.empty();
}

bool readPngFormat(std::vector<uint8_t> &png) {
    const UINT format = RegisterClipboardFormatW(L"PNG");
    if (format == 0 || !IsClipboardFormatAvailable(format)) {
        return false;
    }
    HANDLE handle = GetClipboardData(format);
    if (!handle) {
        return false;
    }
    const SIZE_T size = GlobalSize(handle);
    const auto *data = static_cast<const uint8_t *>(GlobalLock(handle));
    if (!data || size == 0 || size > 64 * 1024 * 1024) {
        if (data) {
            GlobalUnlock(handle);
        }
        return false;
    }
    png.assign(data, data + size);
    GlobalUnlock(handle);
    return true;
}

bool readDibFormat(std::vector<uint8_t> &png) {
    for (const UINT format : {static_cast<UINT>(CF_DIBV5),
                              static_cast<UINT>(CF_DIB)}) {
        if (!IsClipboardFormatAvailable(format)) {
            continue;
        }
        HANDLE handle = GetClipboardData(format);
        if (!handle) {
            continue;
        }
        const SIZE_T size = GlobalSize(handle);
        const auto *data = static_cast<const uint8_t *>(GlobalLock(handle));
        if (!data || size < sizeof(BITMAPINFOHEADER)) {
            if (data) {
                GlobalUnlock(handle);
            }
            continue;
        }
        const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(data);
        const int width = header->biWidth;
        const int height = std::abs(header->biHeight);
        if (width <= 0 || height <= 0 || width > 10000 || height > 10000) {
            GlobalUnlock(handle);
            continue;
        }
        Gdiplus::Bitmap bitmap(reinterpret_cast<const BITMAPINFO *>(data),
                               const_cast<void *>(
                                   static_cast<const void *>(data + header->biSize)));
        const bool ok = bitmap.GetLastStatus() == Gdiplus::Ok &&
                        encodePng(bitmap, png);
        GlobalUnlock(handle);
        if (ok) {
            return true;
        }
    }
    return false;
}

} // namespace

bool clipboardHasImage() {
    const UINT png = RegisterClipboardFormatW(L"PNG");
    return (png != 0 && IsClipboardFormatAvailable(png)) ||
           IsClipboardFormatAvailable(CF_DIBV5) ||
           IsClipboardFormatAvailable(CF_DIB);
}

bool readClipboardImage(HWND owner, std::vector<uint8_t> &png) {
    if (!gdiplusToken() || !OpenClipboard(owner)) {
        return false;
    }
    bool result = readPngFormat(png);
    if (!result) {
        result = readDibFormat(png);
    }
    CloseClipboard();
    return result;
}

bool writeClipboardImage(HWND owner, const std::vector<uint8_t> &png) {
    if (png.empty() || !gdiplusToken()) {
        return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, png.size());
    if (!memory) {
        return false;
    }
    auto *bytes = static_cast<uint8_t *>(GlobalLock(memory));
    if (!bytes) {
        GlobalFree(memory);
        return false;
    }
    std::memcpy(bytes, png.data(), png.size());
    GlobalUnlock(memory);

    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream) {
        GlobalFree(memory);
        return false;
    }

    Gdiplus::Bitmap bitmap(stream, FALSE);
    HBITMAP hbitmap = nullptr;
    const bool decoded =
        bitmap.GetLastStatus() == Gdiplus::Ok &&
        bitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255), &hbitmap) ==
            Gdiplus::Ok &&
        hbitmap != nullptr;
    stream->Release();
    if (!decoded) {
        return false;
    }

    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (!OpenClipboard(owner)) {
        DeleteObject(hbitmap);
        return false;
    }
    EmptyClipboard();
    bool success = SetClipboardData(CF_BITMAP, hbitmap) != nullptr;
    if (!success) {
        DeleteObject(hbitmap);
    }
    if (pngFormat != 0) {
        HGLOBAL pngMemory = GlobalAlloc(GMEM_MOVEABLE, png.size());
        if (pngMemory) {
            auto *target = static_cast<uint8_t *>(GlobalLock(pngMemory));
            if (target) {
                std::memcpy(target, png.data(), png.size());
                GlobalUnlock(pngMemory);
                if (!SetClipboardData(pngFormat, pngMemory)) {
                    GlobalFree(pngMemory);
                }
            } else {
                GlobalFree(pngMemory);
            }
        }
    }
    CloseClipboard();
    return success;
}

} // namespace tuxime
