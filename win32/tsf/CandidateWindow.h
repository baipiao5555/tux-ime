#pragma once

#include <functional>
#include <utility>
#include <vector>
#include <windows.h>

#include "ServerClient.h"

namespace fcitx {

class CandidateWindow {
public:
    CandidateWindow();
    ~CandidateWindow();

    CandidateWindow(const CandidateWindow &) = delete;
    CandidateWindow &operator=(const CandidateWindow &) = delete;

    void update(const std::vector<ServerCandidate> &candidates, int selected,
                const RECT &caret, const std::wstring &status = L"");
    void setSelectCallback(std::function<void(int)> callback) {
        selectCallback_ = std::move(callback);
    }
    void setPageCallback(std::function<void(int)> callback) {
        pageCallback_ = std::move(callback);
    }
    void hide();
    bool visible() const { return hwnd_ != nullptr && IsWindowVisible(hwnd_); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);
    bool ensureWindow();
    void render(const std::vector<ServerCandidate> &candidates, int selected,
                const RECT &caret, const std::wstring &status);
    int dpi() const;

    HWND hwnd_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
    std::function<void(int)> selectCallback_;
    std::function<void(int)> pageCallback_;
    struct HitRange {
        float left;
        float right;
        float top;
        float bottom;
        int pageDelta = 0;
    };
    std::vector<HitRange> hitRanges_;
};

} // namespace fcitx
