#include "update_check.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <windows.h>
#include <winhttp.h>

#include "common/version.h"

namespace tuxime {

namespace {

std::wstring toWide(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::string jsonString(const std::string &json, const std::string &key) {
    const std::string needle = "\"" + key + "\"";
    const auto keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return {};
    }
    auto pos = json.find(':', keyPos + needle.size());
    if (pos == std::string::npos) {
        return {};
    }
    ++pos;
    while (pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"') {
        return {};
    }
    ++pos;
    std::string result;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (c == '"') {
            break;
        }
        if (c == '\\' && pos < json.size()) {
            const char escaped = json[pos++];
            switch (escaped) {
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                result.push_back(escaped);
                break;
            }
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::vector<int> versionParts(const std::wstring &version) {
    std::vector<int> parts;
    size_t pos = 0;
    if (pos < version.size() && (version[pos] == L'v' || version[pos] == L'V')) {
        ++pos;
    }
    while (pos < version.size()) {
        while (pos < version.size() && !std::iswdigit(version[pos])) {
            ++pos;
        }
        int value = 0;
        bool found = false;
        while (pos < version.size() && std::iswdigit(version[pos])) {
            value = value * 10 + (version[pos] - L'0');
            ++pos;
            found = true;
        }
        if (found) {
            parts.push_back(value);
        } else {
            break;
        }
    }
    return parts;
}

bool isVersionNewer(const std::wstring &latest, const std::wstring &current) {
    const auto latestParts = versionParts(latest);
    const auto currentParts = versionParts(current);
    const size_t count = (std::max)(latestParts.size(), currentParts.size());
    for (size_t i = 0; i < count; ++i) {
        const int a = i < latestParts.size() ? latestParts[i] : 0;
        const int b = i < currentParts.size() ? currentParts[i] : 0;
        if (a != b) {
            return a > b;
        }
    }
    return false;
}

std::string readBody(HINTERNET request) {
    std::string body;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) ||
            available == 0) {
            break;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read) ||
            read == 0) {
            break;
        }
        chunk.resize(read);
        body += chunk;
    }
    return body;
}

} // namespace

UpdateCheckResult checkLatestRelease() {
    UpdateCheckResult result;
    HINTERNET session = WinHttpOpen(
        L"TuxIME/update-check", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        result.error = L"无法初始化网络。";
        return result;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

    HINTERNET connection =
        WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        result.error = L"无法连接 GitHub。";
        WinHttpCloseHandle(session);
        return result;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET",
        L"/repos/baipiao5555/tux-ime/releases?per_page=10", nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!request) {
        result.error = L"无法创建更新请求。";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirectPolicy, sizeof(redirectPolicy));
    const wchar_t *headers =
        L"User-Agent: TuxIME\r\nAccept: application/vnd.github+json\r\n";

    if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        result.error = L"检查更新请求失败。";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        result.error = L"GitHub 返回状态 " + std::to_wstring(status) + L"。";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }

    const std::string body = readBody(request);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (body.empty()) {
        result.error = L"更新信息为空。";
        return result;
    }

    const std::string tag = jsonString(body, "tag_name");
    const std::string page = jsonString(body, "html_url");
    if (tag.empty() || page.empty()) {
        result.error = L"无法解析更新信息。";
        return result;
    }
    if (page.rfind("https://github.com/", 0) != 0) {
        result.error = L"更新地址不是 GitHub HTTPS 地址。";
        return result;
    }

    std::string asset;
    const std::string assetKey = "\"browser_download_url\":\"";
    size_t search = 0;
    while ((search = body.find(assetKey, search)) != std::string::npos) {
        const size_t begin = search + assetKey.size();
        const size_t end = body.find('"', begin);
        if (end == std::string::npos) {
            break;
        }
        const std::string candidate = body.substr(begin, end - begin);
        if (candidate.rfind("https://github.com/", 0) == 0 &&
            candidate.find("TuxIME-Setup-") != std::string::npos &&
            candidate.ends_with(".exe")) {
            asset = candidate;
            break;
        }
        search = end;
    }

    const std::wstring latest = toWide(tag);
    result.version = latest;
    result.url = toWide(page);
    result.assetUrl = toWide(asset);
    result.hasUpdate =
        isVersionNewer(latest, toWide(TUX_IME_VERSION));
    result.ok = true;
    return result;
}

namespace {

bool allowedDownloadHost(const std::wstring &host) {
    return host == L"github.com" ||
           host == L"objects.githubusercontent.com" ||
           host == L"release-assets.githubusercontent.com";
}

std::wstring locationHeader(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Location",
                        nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (size == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Location",
                             buffer.data(), &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    return buffer.data();
}

bool writeDownload(const std::wstring &url, const std::wstring &path,
                   std::wstring &error) {
    std::wstring current = url;
    for (int redirect = 0; redirect < 6; ++redirect) {
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = static_cast<DWORD>(-1);
        components.dwHostNameLength = static_cast<DWORD>(-1);
        components.dwUrlPathLength = static_cast<DWORD>(-1);
        components.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(current.c_str(), 0, 0, &components) ||
            components.nScheme != INTERNET_SCHEME_HTTPS) {
            error = L"下载地址不是 HTTPS。";
            return false;
        }
        const std::wstring host(components.lpszHostName,
                                components.dwHostNameLength);
        if (!allowedDownloadHost(host)) {
            error = L"下载地址主机不在允许列表。";
            return false;
        }

        std::wstring target(components.lpszUrlPath,
                            components.dwUrlPathLength);
        if (components.lpszExtraInfo && components.dwExtraInfoLength) {
            target.append(components.lpszExtraInfo,
                          components.dwExtraInfoLength);
        }
        if (target.empty()) {
            target = L"/";
        }

        HINTERNET session = WinHttpOpen(
            L"TuxIME/updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) {
            error = L"无法初始化下载网络。";
            return false;
        }
        WinHttpSetTimeouts(session, 10000, 10000, 15000, 60000);
        HINTERNET connection = WinHttpConnect(
            session, host.c_str(),
            components.nPort ? components.nPort : INTERNET_DEFAULT_HTTPS_PORT,
            0);
        if (!connection) {
            WinHttpCloseHandle(session);
            error = L"无法连接下载服务器。";
            return false;
        }
        HINTERNET request = WinHttpOpenRequest(
            connection, L"GET", target.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);
        if (!request) {
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            error = L"无法创建下载请求。";
            return false;
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirectPolicy, sizeof(redirectPolicy));
        const wchar_t *headers =
            L"User-Agent: TuxIME\r\nAccept: application/octet-stream\r\n";
        if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr)) {
            error = L"下载请求失败。";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX);
        if (status == 301 || status == 302 || status == 303 ||
            status == 307 || status == 308) {
            const std::wstring location = locationHeader(request);
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            if (location.empty()) {
                error = L"下载重定向缺少地址。";
                return false;
            }
            current = location;
            continue;
        }
        if (status != 200) {
            error = L"下载失败，HTTP " + std::to_wstring(status) + L"。";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }

        std::ofstream out(std::filesystem::path(path),
                          std::ios::binary | std::ios::trunc);
        if (!out) {
            error = L"无法创建安装包文件。";
            WinHttpCloseHandle(request);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return false;
        }
        size_t total = 0;
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) ||
                available == 0) {
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read) ||
                read == 0) {
                break;
            }
            total += read;
            if (total > 200ull * 1024 * 1024) {
                error = L"安装包超过 200 MB，已中止。";
                out.close();
                std::filesystem::remove(std::filesystem::path(path));
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
                WinHttpCloseHandle(session);
                return false;
            }
            out.write(chunk.data(), read);
        }
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (total == 0) {
            error = L"下载内容为空。";
            std::filesystem::remove(std::filesystem::path(path));
            return false;
        }
        std::ifstream check(std::filesystem::path(path), std::ios::binary);
        char magic[2] = {};
        check.read(magic, 2);
        if (magic[0] != 'M' || magic[1] != 'Z') {
            error = L"下载文件不是有效的 Windows 安装包。";
            check.close();
            std::filesystem::remove(std::filesystem::path(path));
            return false;
        }
        return true;
    }
    error = L"下载重定向次数过多。";
    return false;
}

} // namespace

UpdateDownloadResult downloadAndVerify(const std::wstring &url,
                                       const std::wstring &version) {
    UpdateDownloadResult result;
    if (url.rfind(L"https://github.com/", 0) != 0) {
        result.error = L"下载地址必须是 GitHub HTTPS 地址。";
        return result;
    }

    wchar_t temp[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, temp) == 0) {
        result.error = L"无法取得临时目录。";
        return result;
    }
    std::wstring safeVersion;
    for (wchar_t c : version) {
        if (std::iswdigit(c) || c == L'.') {
            safeVersion.push_back(c);
        }
    }
    if (safeVersion.empty()) {
        safeVersion = L"latest";
    }
    const auto directory = std::filesystem::path(temp) / "TuxIME";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        result.error = L"无法创建临时目录。";
        return result;
    }
    const auto path =
        directory / (L"TuxIME-Setup-" + safeVersion + L".exe");
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path, ec);
    }
    if (!writeDownload(url, path.wstring(), result.error)) {
        result.error = L"下载安装包失败：" + result.error;
        return result;
    }
    result.ok = true;
    result.path = path.wstring();
    return result;
}

} // namespace tuxime
