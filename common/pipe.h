#pragma once

#include <string>
#include <vector>
#include <windows.h>
#include <sddl.h>

#include "ipc.h"

namespace tuxime::ipc {

inline std::wstring currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return {};
    }
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    std::wstring result;
    if (GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        auto *user = reinterpret_cast<TOKEN_USER *>(buffer.data());
        LPWSTR sidString = nullptr;
        if (ConvertSidToStringSidW(user->User.Sid, &sidString)) {
            result = sidString;
            LocalFree(sidString);
        }
    }
    CloseHandle(token);
    return result;
}

inline std::wstring pipeName() {
    const std::wstring sid = currentUserSid();
    if (sid.empty()) {
        return {};
    }
    DWORD sessionId = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sessionId);
    return std::wstring(kPipePrefix) + sid + L"-" + std::to_wstring(sessionId);
}

inline bool makePipeSecurity(SECURITY_ATTRIBUTES &attributes,
                             PSECURITY_DESCRIPTOR &descriptor) {
    const std::wstring sid = currentUserSid();
    if (sid.empty()) {
        return false;
    }
    const std::wstring sddl =
        L"D:(A;;GA;;;" + sid + L")(A;;GA;;;SY)(A;;GA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
        return false;
    }
    attributes.nLength = sizeof(attributes);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
    return true;
}

} // namespace tuxime::ipc
