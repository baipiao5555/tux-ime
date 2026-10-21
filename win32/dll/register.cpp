#include "register.h"
#include "util.h"
#include <filesystem>
#include <msctf.h>

namespace fs = std::filesystem;

#define FCITX5 "Tux IME"
#define THREADING_MODEL "ThreadingModel"
#define APARTMENT "Apartment"

namespace fcitx {
HINSTANCE dllInstance; // Set by DllMain.

namespace {

bool setStringValue(HKEY key, const char *name, const char *value,
                    DWORD size) {
    return RegSetValueExA(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE *>(value),
                          size) == ERROR_SUCCESS;
}

} // namespace

/*
HKEY_CLASSES_ROOT\CLSID\{8D7031FD-4D2D-4CCE-BB41-05F54D7AC612}: Tux IME
  - InprocServer32: <dll path>
    ThreadingModel: Apartment
*/
BOOL RegisterServer() {
    DWORD disposition = 0;
    HKEY clsidKey = nullptr;
    HKEY inprocKey = nullptr;
    const auto clsidPath = "CLSID\\" + guidToString(FCITX_CLSID);
    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, clsidPath.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &clsidKey, &disposition) != ERROR_SUCCESS) {
        return FALSE;
    }
    bool ok = setStringValue(clsidKey, nullptr, FCITX5, sizeof FCITX5);
    if (RegCreateKeyExA(clsidKey, "InprocServer32", 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &inprocKey, &disposition) != ERROR_SUCCESS) {
        RegCloseKey(clsidKey);
        return FALSE;
    }

    WCHAR dllPath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(dllInstance, dllPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        ok = false;
    } else {
        ok = ok && RegSetValueExW(
                       inprocKey, nullptr, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(dllPath),
                       (length + 1) * sizeof(WCHAR)) == ERROR_SUCCESS;
    }
    ok = ok && setStringValue(inprocKey, THREADING_MODEL, APARTMENT,
                              sizeof APARTMENT);

    RegCloseKey(inprocKey);
    RegCloseKey(clsidKey);
    return ok ? TRUE : FALSE;
}

void UnregisterServer() {
    const auto clsidPath = "CLSID\\" + guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_CLASSES_ROOT, clsidPath.c_str());
}

/*
HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\CTF\TIP\{...}
  - LanguageProfile
    - 0x00000804
      - {PROFILE_GUID}
        Description: Tux IME
        Enable: 0x00000001
        IconFile: <install path>\bin\penguin.ico
*/
BOOL RegisterProfiles() {
    const std::wstring description = stringToWString(FCITX5, CP_UTF8);
    WCHAR dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(dllInstance, dllPath, MAX_PATH) == 0) {
        return FALSE;
    }
    fs::path iconPath = dllPath;
    iconPath = iconPath.remove_filename().append("penguin.ico");

    ITfInputProcessorProfileMgr *manager = nullptr;
    const HRESULT createResult = CoCreateInstance(
        CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_ALL,
        IID_ITfInputProcessorProfileMgr,
        reinterpret_cast<void **>(&manager));
    if (FAILED(createResult) || !manager) {
        return FALSE;
    }

    const HRESULT result = manager->RegisterProfile(
        FCITX_CLSID, TEXTSERVICE_LANGID_HANS, PROFILE_GUID,
        description.c_str(), description.size() * sizeof(WCHAR),
        iconPath.c_str(), iconPath.wstring().size() * sizeof(WCHAR), 0,
        nullptr, 0, 1, 0);
    manager->Release();
    return result == S_OK;
}

// No documentation about what they mean.
const GUID Categories[] = {GUID_TFCAT_CATEGORY_OF_TIP,
                           GUID_TFCAT_TIP_KEYBOARD,
                           GUID_TFCAT_TIPCAP_SECUREMODE,
                           GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
                           GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
                           GUID_TFCAT_TIPCAP_COMLESS,
                           GUID_TFCAT_TIPCAP_WOW16,
                           GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
                           GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
                           GUID_TFCAT_PROP_AUDIODATA,
                           GUID_TFCAT_PROP_INKDATA,
                           GUID_TFCAT_PROPSTYLE_CUSTOM,
                           GUID_TFCAT_PROPSTYLE_STATIC,
                           GUID_TFCAT_PROPSTYLE_STATICCOMPACT,
                           GUID_TFCAT_DISPLAYATTRIBUTEPROVIDER,
                           GUID_TFCAT_DISPLAYATTRIBUTEPROPERTY};

BOOL RegisterCategories() {
    ITfCategoryMgr *manager = nullptr;
    if (FAILED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                CLSCTX_INPROC_SERVER, IID_ITfCategoryMgr,
                                reinterpret_cast<void **>(&manager))) ||
        !manager) {
        return FALSE;
    }
    HRESULT result = S_OK;
    for (const auto &guid : Categories) {
        result |= manager->RegisterCategory(FCITX_CLSID, guid, FCITX_CLSID);
    }
    manager->Release();
    return result == S_OK;
}

void UnregisterCategoriesAndProfiles() {
    const auto key = "SOFTWARE\\Microsoft\\CTF\\TIP\\" +
                     guidToString(FCITX_CLSID);
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, key.c_str());
}
} // namespace fcitx
