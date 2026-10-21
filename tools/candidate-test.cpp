#include <chrono>
#include <thread>
#include <vector>
#include <windows.h>

#include "../win32/tsf/CandidateWindow.h"

int main() {
    fcitx::CandidateWindow window;
    std::vector<fcitx::ServerCandidate> candidates = {
        {L"1. ", L"你", L""},
        {L"2. ", L"拟", L""},
        {L"3. ", L"泥", L""},
        {L"4. ", L"逆", L""},
        {L"5. ", L"腻", L""},
        {L"6. ", L"呢", L""},
        {L"7. ", L"尼", L""},
    };
    RECT caret{200, 200, 202, 220};
    window.update(candidates, 0, caret);
    std::this_thread::sleep_for(std::chrono::seconds(15));
    window.hide();
    return 0;
}
