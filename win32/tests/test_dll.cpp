#include "../dll/util.h"
#include <Windows.h>
#include <cassert>

using namespace fcitx;

int main() {
    assert(guidToString(FCITX_CLSID) ==
           "{8D7031FD-4D2D-4CCE-BB41-05F54D7AC612}");
    assert(stringToWString("中A", CP_UTF8) == L"中A");
    return 0;
}
