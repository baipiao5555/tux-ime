#include <cstdio>
#include <windows.h>
#include <msctf.h>
#include <objbase.h>

#ifndef TF_IPPMF_FORSESSION
#define TF_IPPMF_FORSESSION 0x20000000
#endif

int main(int argc, char **argv) {
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

    if (argc > 1) {
        const CLSID clsid = {0x8d7031fd,
                             0x4d2d,
                             0x4cce,
                             {0xbb, 0x41, 0x05, 0xf5, 0x4d, 0x7a, 0xc6,
                              0x12}};
        const GUID profile = {0x76ae52e2,
                              0x7f68,
                              0x4a6d,
                              {0x81, 0xd0, 0xa4, 0x8d, 0x4a, 0xc7, 0x3e,
                               0x36}};
        const HRESULT result = manager->ActivateProfile(
            TF_PROFILETYPE_INPUTPROCESSOR, 0x0804, clsid, profile, nullptr,
            TF_IPPMF_FORSESSION);
        std::printf("activate=0x%08lx\n", static_cast<unsigned long>(result));
    }

    IEnumTfInputProcessorProfiles *enumerator = nullptr;
    if (FAILED(manager->EnumProfiles(0x0804, &enumerator)) || !enumerator) {
        std::printf("enum failed\n");
        manager->Release();
        return 1;
    }
    TF_INPUTPROCESSORPROFILE item{};
    ULONG fetched = 0;
    while (enumerator->Next(1, &item, &fetched) == S_OK && fetched) {
        wchar_t clsidText[64]{};
        wchar_t profileText[64]{};
        StringFromGUID2(item.clsid, clsidText, 64);
        StringFromGUID2(item.guidProfile, profileText, 64);
        std::printf("type=%lu lang=%04x clsid=%ls profile=%ls flags=%lu\n",
                    item.dwProfileType, item.langid, clsidText, profileText,
                    item.dwFlags);
    }
    enumerator->Release();
    manager->Release();
    CoUninitialize();
    return 0;
}
