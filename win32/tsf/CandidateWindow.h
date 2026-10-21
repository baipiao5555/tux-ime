#pragma once

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
                const RECT &caret);
    void hide();
    bool visible() const { return hwnd_ != nullptr && IsWindowVisible(hwnd_); }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam);
    bool ensureWindow();
    void render(const std::vector<ServerCandidate> &candidates, int selected,
                const RECT &caret);
    int dpi() const;

    HWND hwnd_ = nullptr;
    ULONG_PTR gdiplusToken_ = 0;
};

} // namespace fcitx
