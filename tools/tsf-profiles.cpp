#include <cstdio>
#include <windows.h>
#include <msctf.h>
#include <objbase.h>

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ITfInputProcessorProfileMgr *manager = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_ITfInputProcessorProfileMgr,
                                reinterpret_cast<void **>(&manager))) ||
        !manager) {
        std::printf("no profile manager\n");
        return 1;
    }
    IEnumTfInputProcessorProfiles *enumerator = nullptr;
    if (FAILED(manager->EnumProfiles(0x0804, &enumerator)) || !enumerator) {
        std::printf("enum failed\n");
        manager->Release();
        return 1;
    }
    TF_INPUTPROCESSORPROFILE profile{};
    ULONG fetched = 0;
    while (enumerator->Next(1, &profile, &fetched) == S_OK && fetched) {
        wchar_t clsid[64]{};
        wchar_t guid[64]{};
        StringFromGUID2(profile.clsid, clsid, 64);
        StringFromGUID2(profile.guidProfile, guid, 64);
        std::printf("type=%lu lang=%04x clsid=%ls profile=%ls flags=%lu "
                    "caps=%lu hkl=%p\n",
                    profile.dwProfileType, profile.langid, clsid, guid,
                    profile.dwFlags, profile.dwCaps,
                    reinterpret_cast<void *>(profile.hkl));
    }
    enumerator->Release();
    manager->Release();
    CoUninitialize();
    return 0;
}
