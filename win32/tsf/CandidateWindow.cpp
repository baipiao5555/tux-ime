#include "CandidateWindow.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

namespace fcitx {

namespace {

constexpr wchar_t kWindowClass[] = L"TuxImeCandidateWindow";

int systemDpi() {
    using GetDpiForSystemFn = UINT(WINAPI *)();
    static auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
    if (getDpiForSystem) {
        return static_cast<int>(getDpiForSystem());
    }
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi : 96;
}

void addRoundedRect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rect,
                    float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter,
                270.0f, 90.0f);
    path.AddArc(rect.X + rect.Width - diameter,
                rect.Y + rect.Height - diameter, diameter, diameter, 0.0f,
                90.0f);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter,
                90.0f, 90.0f);
    path.CloseFigure();
}

std::wstring displayLabel(const ServerCandidate &candidate, size_t index) {
    if (!candidate.label.empty()) {
        return candidate.label;
    }
    return std::to_wstring(index + 1) + L".";
}

} // namespace

CandidateWindow::CandidateWindow() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &input, nullptr) !=
        Gdiplus::Ok) {
        gdiplusToken_ = 0;
    }
}

CandidateWindow::~CandidateWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

int CandidateWindow::dpi() const {
    if (hwnd_) {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        static auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"),
                           "GetDpiForWindow"));
        if (getDpiForWindow) {
            const UINT value = getDpiForWindow(hwnd_);
            if (value) {
                return static_cast<int>(value);
            }
        }
    }
    return systemDpi();
}

bool CandidateWindow::ensureWindow() {
    if (hwnd_) {
        return true;
    }
    if (!gdiplusToken_) {
        return false;
    }

    static bool registered = false;
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &CandidateWindow::windowProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(
            nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        windowClass.lpszClassName = kWindowClass;
        registered = RegisterClassExW(&windowClass) != 0 ||
                     GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
            WS_EX_TOPMOST,
        kWindowClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance,
        nullptr);
    if (!hwnd_) {
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    return true;
}

void CandidateWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void CandidateWindow::update(const std::vector<ServerCandidate> &candidates,
                             int selected, const RECT &caret) {
    if (candidates.empty()) {
        hide();
        return;
    }
    if (!ensureWindow()) {
        return;
    }
    render(candidates, selected, caret);
}

void CandidateWindow::render(const std::vector<ServerCandidate> &candidates,
                             int selected, const RECT &caret) {
    const float scale = static_cast<float>(dpi()) / 96.0f;
    const float padding = 8.0f * scale;
    const float itemGap = 14.0f * scale;
    const float innerGap = 6.0f * scale;
    const float height = 36.0f * scale;
    const float radius = 8.0f * scale;
    const float itemRadius = 5.0f * scale;
    const float textSize = 16.0f * scale;
    const float labelSize = 12.0f * scale;
    const float commentSize = 11.0f * scale;

    Gdiplus::Font textFont(L"Microsoft YaHei UI", textSize,
                           Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font labelFont(L"Segoe UI", labelSize,
                            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font commentFont(L"Microsoft YaHei UI", commentSize,
                              Gdiplus::FontStyleRegular,
                              Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetFormatFlags(Gdiplus::StringFormatFlagsMeasureTrailingSpaces);

    HDC screen = GetDC(nullptr);
    Gdiplus::Graphics measure(screen);
    measure.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

    struct ItemLayout {
        float labelWidth;
        float textWidth;
        float commentWidth;
        float width;
        std::wstring label;
    };
    std::vector<ItemLayout> layout;
    layout.reserve(candidates.size());
    float totalWidth = padding * 2.0f;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto label = displayLabel(candidates[i], i);
        Gdiplus::RectF labelBounds;
        Gdiplus::RectF textBounds;
        Gdiplus::RectF commentBounds;
        measure.MeasureString(label.c_str(), -1, &labelFont,
                              Gdiplus::PointF(0, 0), &format, &labelBounds);
        measure.MeasureString(candidates[i].text.c_str(), -1, &textFont,
                              Gdiplus::PointF(0, 0), &format, &textBounds);
        if (!candidates[i].comment.empty()) {
            measure.MeasureString(candidates[i].comment.c_str(), -1,
                                  &commentFont, Gdiplus::PointF(0, 0),
                                  &format, &commentBounds);
        }
        ItemLayout item{labelBounds.Width, textBounds.Width,
                        commentBounds.Width, 0.0f, label};
        item.width = item.labelWidth + innerGap + item.textWidth;
        if (item.commentWidth > 0.0f) {
            item.width += innerGap + item.commentWidth;
        }
        item.width = (std::max)(item.width, 20.0f * scale);
        totalWidth += item.width;
        if (i > 0) {
            totalWidth += itemGap;
        }
        layout.push_back(item);
    }
    ReleaseDC(nullptr, screen);

    totalWidth = (std::max)(totalWidth, 80.0f * scale);
    const int windowWidth = static_cast<int>(std::ceil(totalWidth));
    const int windowHeight = static_cast<int>(std::ceil(height));

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    const POINT anchorPoint{caret.left, caret.bottom};
    const HMONITOR monitor =
        MonitorFromPoint(anchorPoint, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(monitor, &monitorInfo);

    int x = caret.left;
    int y = caret.bottom + static_cast<int>(4.0f * scale);
    if (x + windowWidth > monitorInfo.rcWork.right) {
        x = monitorInfo.rcWork.right - windowWidth;
    }
    if (x < monitorInfo.rcWork.left) {
        x = monitorInfo.rcWork.left;
    }
    if (y + windowHeight > monitorInfo.rcWork.bottom) {
        y = caret.top - windowHeight - static_cast<int>(4.0f * scale);
    }
    if (y < monitorInfo.rcWork.top) {
        y = monitorInfo.rcWork.top;
    }

    Gdiplus::Bitmap canvas(windowWidth, windowHeight,
                           PixelFormat32bppPARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) {
        return;
    }
    {
        Gdiplus::Graphics graphics(&canvas);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(
            Gdiplus::TextRenderingHintAntiAliasGridFit);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

        Gdiplus::GraphicsPath backgroundPath;
        addRoundedRect(backgroundPath,
                       Gdiplus::RectF(0.0f, 0.0f,
                                      static_cast<float>(windowWidth),
                                      static_cast<float>(windowHeight)),
                       radius);
        Gdiplus::SolidBrush background(
            Gdiplus::Color(246, 250, 250, 250));
        graphics.FillPath(&background, &backgroundPath);
        Gdiplus::Pen border(Gdiplus::Color(220, 190, 195, 205),
                            1.0f * scale);
        graphics.DrawPath(&border, &backgroundPath);

        float itemX = padding;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto &item = layout[i];
            const bool isSelected = static_cast<int>(i) == selected;
            const float itemLeft = itemX - innerGap * 0.5f;
            const float itemWidth = item.width + innerGap;
            if (isSelected) {
                Gdiplus::GraphicsPath selectedPath;
                addRoundedRect(
                    selectedPath,
                    Gdiplus::RectF(itemLeft, 4.0f * scale, itemWidth,
                                   height - 8.0f * scale),
                    itemRadius);
                Gdiplus::SolidBrush selectedBrush(
                    Gdiplus::Color(255, 214, 232, 255));
                graphics.FillPath(&selectedBrush, &selectedPath);
            }

            Gdiplus::SolidBrush labelBrush(
                isSelected ? Gdiplus::Color(255, 70, 110, 160)
                           : Gdiplus::Color(255, 130, 135, 145));
            Gdiplus::SolidBrush textBrush(
                Gdiplus::Color(255, 28, 30, 34));
            Gdiplus::SolidBrush commentBrush(
                Gdiplus::Color(255, 120, 125, 135));

            const float textY =
                (height - textSize) / 2.0f - 1.0f * scale;
            const float labelY =
                (height - labelSize) / 2.0f - 0.5f * scale;
            graphics.DrawString(item.label.c_str(), -1, &labelFont,
                                Gdiplus::PointF(itemX, labelY), &labelBrush);
            const float textX = itemX + item.labelWidth + innerGap;
            graphics.DrawString(candidates[i].text.c_str(), -1, &textFont,
                                Gdiplus::PointF(textX, textY), &textBrush);
            if (item.commentWidth > 0.0f) {
                const float commentX = textX + item.textWidth + innerGap;
                const float commentY =
                    (height - commentSize) / 2.0f - 1.0f * scale;
                graphics.DrawString(candidates[i].comment.c_str(), -1,
                                    &commentFont,
                                    Gdiplus::PointF(commentX, commentY),
                                    &commentBrush);
            }
            itemX += item.width + itemGap;
        }
    }

    Gdiplus::BitmapData bitmapData{};
    Gdiplus::Rect bitmapRect(0, 0, windowWidth, windowHeight);
    if (canvas.LockBits(&bitmapRect, Gdiplus::ImageLockModeRead,
                        PixelFormat32bppPARGB,
                        &bitmapData) != Gdiplus::Ok) {
        return;
    }

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = windowWidth;
    bitmapInfo.bmiHeader.biHeight = -windowHeight;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HDC memoryDc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(memoryDc, &bitmapInfo, DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    if (!dib) {
        canvas.UnlockBits(&bitmapData);
        DeleteDC(memoryDc);
        return;
    }
    const HGDIOBJ oldBitmap = SelectObject(memoryDc, dib);

    const auto *sourcePixels =
        static_cast<const uint8_t *>(bitmapData.Scan0);
    auto *destinationPixels = static_cast<uint8_t *>(bits);
    const size_t rowBytes = static_cast<size_t>(windowWidth) * 4;
    for (int row = 0; row < windowHeight; ++row) {
        std::memcpy(destinationPixels + row * rowBytes,
                    sourcePixels + row * bitmapData.Stride, rowBytes);
    }
    canvas.UnlockBits(&bitmapData);

    HDC screenDc = GetDC(nullptr);
    POINT destination{x, y};
    SIZE size{windowWidth, windowHeight};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screenDc, &destination, &size, memoryDc,
                        &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screenDc);

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memoryDc);

    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, windowWidth, windowHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

LRESULT CALLBACK CandidateWindow::windowProc(HWND hwnd, UINT message,
                                              WPARAM wParam,
                                              LPARAM lParam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace fcitx
