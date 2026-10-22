#include <winsock2.h>
#include <ws2tcpip.h>

#include "clipboard_sync.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>

#include <bcrypt.h>
#include <winhttp.h>

#include "clipboard_image.h"

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

std::string toUtf8(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
        nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()), result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring sha256HexBytes(const uint8_t *data, size_t size) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD hashSize = 0;
    DWORD resultSize = 0;
    std::wstring result;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                    nullptr, 0) < 0) {
        return result;
    }
    BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                      reinterpret_cast<PUCHAR>(&objectSize),
                      sizeof(objectSize), &resultSize, 0);
    BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                      reinterpret_cast<PUCHAR>(&hashSize), sizeof(hashSize),
                      &resultSize, 0);
    std::vector<uint8_t> object(objectSize);
    std::vector<uint8_t> digest(hashSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize,
                         nullptr, 0, 0) >= 0) {
        BCryptHashData(hash, const_cast<PUCHAR>(data),
                       static_cast<ULONG>(size), 0);
        if (BCryptFinishHash(hash, digest.data(), hashSize, 0) >= 0) {
            static const wchar_t hex[] = L"0123456789ABCDEF";
            result.reserve(hashSize * 2);
            for (uint8_t byte : digest) {
                result.push_back(hex[byte >> 4]);
                result.push_back(hex[byte & 0x0f]);
            }
        }
    }
    if (hash) {
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return result;
}

std::wstring sha256Hex(const std::wstring &text) {
    const std::string utf8 = toUtf8(text);
    return sha256HexBytes(
        reinterpret_cast<const uint8_t *>(utf8.data()), utf8.size());
}

std::wstring fileHash(const std::wstring &name,
                      const std::vector<uint8_t> &data) {
    const std::wstring contentHash = sha256HexBytes(data.data(), data.size());
    const std::string combined = toUtf8(name) + "|" + toUtf8(contentHash);
    return sha256HexBytes(
        reinterpret_cast<const uint8_t *>(combined.data()), combined.size());
}

std::string base64Encode(const std::string &data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((data.size() + 2) / 3 * 4);
    for (size_t i = 0; i < data.size(); i += 3) {
        const uint32_t a = static_cast<uint8_t>(data[i]);
        const uint32_t b =
            i + 1 < data.size() ? static_cast<uint8_t>(data[i + 1]) : 0;
        const uint32_t c =
            i + 2 < data.size() ? static_cast<uint8_t>(data[i + 2]) : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        result.push_back(table[(value >> 18) & 0x3f]);
        result.push_back(table[(value >> 12) & 0x3f]);
        result.push_back(i + 1 < data.size() ? table[(value >> 6) & 0x3f]
                                             : '=');
        result.push_back(i + 2 < data.size() ? table[value & 0x3f] : '=');
    }
    return result;
}

std::string base64Decode(const std::string &text) {
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = i;
    }
    std::string result;
    uint32_t value = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (c == '=') {
            break;
        }
        const int digit = table[c];
        if (digit < 0) {
            continue;
        }
        value = (value << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((value >> bits) & 0xff));
        }
    }
    return result;
}

std::string jsonEscape(const std::string &text) {
    std::string result;
    result.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                result += buffer;
            } else {
                result.push_back(static_cast<char>(c));
            }
        }
    }
    return result;
}

void appendUtf8(std::string &out, uint32_t code) {
    if (code <= 0x7f) {
        out.push_back(static_cast<char>(code));
    } else if (code <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else if (code <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (code >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (code >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

bool readHex4(const std::string &json, size_t &pos, uint32_t &code) {
    if (pos + 4 > json.size()) {
        return false;
    }
    code = 0;
    for (int i = 0; i < 4; ++i) {
        const char digit = json[pos++];
        code <<= 4;
        if (digit >= '0' && digit <= '9') {
            code |= digit - '0';
        } else if (digit >= 'a' && digit <= 'f') {
            code |= digit - 'a' + 10;
        } else if (digit >= 'A' && digit <= 'F') {
            code |= digit - 'A' + 10;
        } else {
            return false;
        }
    }
    return true;
}

bool jsonString(const std::string &json, const std::string &key,
                std::string &value) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        pos += needle.size();
        while (pos < json.size() &&
               std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] != ':') {
            continue;
        }
        ++pos;
        while (pos < json.size() &&
               std::isspace(static_cast<unsigned char>(json[pos]))) {
            ++pos;
        }
        if (pos >= json.size() || json[pos] != '"') {
            continue;
        }
        ++pos;
        value.clear();
        while (pos < json.size()) {
            const char c = json[pos++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                value.push_back(c);
                continue;
            }
            if (pos >= json.size()) {
                break;
            }
            const char escape = json[pos++];
            switch (escape) {
            case '"':
                value.push_back('"');
                break;
            case '\\':
                value.push_back('\\');
                break;
            case '/':
                value.push_back('/');
                break;
            case 'b':
                value.push_back('\b');
                break;
            case 'f':
                value.push_back('\f');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            case 'u': {
                uint32_t code = 0;
                if (!readHex4(json, pos, code)) {
                    return false;
                }
                if (code >= 0xd800 && code <= 0xdbff && pos + 6 <= json.size() &&
                    json[pos] == '\\' && json[pos + 1] == 'u') {
                    size_t next = pos + 2;
                    uint32_t low = 0;
                    if (readHex4(json, next, low) && low >= 0xdc00 &&
                        low <= 0xdfff) {
                        code = 0x10000 + ((code - 0xd800) << 10) +
                               (low - 0xdc00);
                        pos = next;
                    }
                }
                appendUtf8(value, code);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    return false;
}

std::string jsonUrl(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return value;
    };
    if (lower(url).ends_with("/syncclipboard.json")) {
        return url;
    }
    return url + "/SyncClipboard.json";
}

std::string baseUrl(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const std::string lower =
        [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) {
                               return std::tolower(c);
                           });
            return value;
        }(url);
    if (lower.ends_with("/syncclipboard.json")) {
        return url.substr(0, url.size() - std::string("SyncClipboard.json").size());
    }
    return url + "/";
}

std::string fileUrl(const std::string &serverUrl, const std::string &name) {
    return baseUrl(serverUrl) + "file/" + name;
}

std::string sanitizeFileName(std::string name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string::npos) {
        name = name.substr(slash + 1);
    }
    for (char &c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' &&
            c != '_' && c != '-') {
            c = '_';
        }
    }
    if (name.empty() || name == "." || name == "..") {
        return {};
    }
    return name;
}

bool jsonBool(const std::string &json, const std::string &key) {
    const std::string needle = "\"" + key + "\"";
    const auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return false;
    }
    const auto colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) {
        return false;
    }
    const auto value = json.find_first_not_of(" \t\r\n", colon + 1);
    return value != std::string::npos && json.compare(value, 4, "true") == 0;
}

bool isImageFileName(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower.ends_with(".png") || lower.ends_with(".jpg") ||
           lower.ends_with(".jpeg") || lower.ends_with(".bmp") ||
           lower.ends_with(".gif");
}

std::wstring httpStatusMessage(int status) {
    switch (status) {
    case 401:
        return L"认证失败：服务器拒绝了用户名或密码。WebDAV 可能需要单独的应用密码，"
               L"并请确认地址填到了具体工作目录。";
    case 403:
        return L"没有权限：请检查 WebDAV 账号权限和工作目录。";
    case 404:
        return L"路径不存在：WebDAV 地址需要填写到已存在的工作目录，"
               L"不要只填域名根目录。";
    case 405:
        return L"服务器不接受该请求：请确认地址是 SyncClipboard 或 WebDAV 工作目录。";
    default:
        return L"服务器返回 HTTP " + std::to_wstring(status);
    }
}

bool isPrivateAddress(const sockaddr *address) {
    if (address->sa_family == AF_INET) {
        const auto *v4 = reinterpret_cast<const sockaddr_in *>(address);
        const auto *bytes = reinterpret_cast<const uint8_t *>(&v4->sin_addr);
        return bytes[0] == 0 || bytes[0] == 10 || bytes[0] == 127 ||
               (bytes[0] == 100 && bytes[1] >= 64 && bytes[1] <= 127) ||
               (bytes[0] == 169 && bytes[1] == 254) ||
               (bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) ||
               (bytes[0] == 192 && bytes[1] == 168) ||
               (bytes[0] == 192 && bytes[1] == 0 && bytes[2] == 0) ||
               (bytes[0] == 192 && bytes[1] == 0 && bytes[2] == 2) ||
               (bytes[0] == 198 && (bytes[1] == 18 || bytes[1] == 19)) ||
               (bytes[0] == 198 && bytes[1] == 51 && bytes[2] == 100) ||
               (bytes[0] == 203 && bytes[1] == 0 && bytes[2] == 113) ||
               bytes[0] >= 224;
    }
    if (address->sa_family == AF_INET6) {
        const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(address);
        const auto *bytes = reinterpret_cast<const uint8_t *>(&v6->sin6_addr);
        if (IN6_IS_ADDR_LOOPBACK(&v6->sin6_addr) ||
            IN6_IS_ADDR_UNSPECIFIED(&v6->sin6_addr) ||
            IN6_IS_ADDR_LINKLOCAL(&v6->sin6_addr) ||
            IN6_IS_ADDR_SITELOCAL(&v6->sin6_addr) ||
            IN6_IS_ADDR_MULTICAST(&v6->sin6_addr)) {
            return true;
        }
        if ((bytes[0] & 0xfe) == 0xfc) {
            return true;
        }
        if (bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x0d &&
            bytes[3] == 0xb8) {
            return true;
        }
        const bool v4Mapped =
            bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 &&
            bytes[3] == 0 && bytes[4] == 0 && bytes[5] == 0 &&
            bytes[6] == 0 && bytes[7] == 0 && bytes[8] == 0 &&
            bytes[9] == 0 && bytes[10] == 0xff && bytes[11] == 0xff;
        if (v4Mapped) {
            sockaddr_in mapped{};
            mapped.sin_family = AF_INET;
            std::memcpy(&mapped.sin_addr, bytes + 12, 4);
            return isPrivateAddress(reinterpret_cast<sockaddr *>(&mapped));
        }
    }
    return false;
}

bool ensureWinsock() {
    static const bool initialized = []() {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

bool validateHost(const std::wstring &host, std::wstring &error) {
    if (host.empty()) {
        error = L"服务器地址缺少主机名。";
        return false;
    }
    if (!ensureWinsock()) {
        error = L"无法初始化网络。";
        return false;
    }
    std::wstring normalized = host;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    if (normalized == L"localhost" ||
        normalized.ends_with(L".localhost")) {
        error = L"不允许使用本机地址。";
        return false;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    const std::string narrowHost = toUtf8(host);
    if (getaddrinfo(narrowHost.c_str(), nullptr, &hints, &result) != 0 ||
        !result) {
        error = L"无法解析服务器主机名。";
        return false;
    }
    bool sawAddress = false;
    bool privateAddress = false;
    for (auto *entry = result; entry; entry = entry->ai_next) {
        if (entry->ai_family != AF_INET && entry->ai_family != AF_INET6) {
            continue;
        }
        sawAddress = true;
        if (isPrivateAddress(entry->ai_addr)) {
            privateAddress = true;
            break;
        }
    }
    freeaddrinfo(result);
    if (!sawAddress) {
        error = L"服务器主机名没有可用地址。";
        return false;
    }
    if (privateAddress) {
        error = L"默认拒绝本机、环回、私有和保留地址。";
        return false;
    }
    return true;
}

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string etag;
    std::wstring error;
};

HttpResponse performRequest(const std::string &method,
                            const std::string &url,
                            const std::string &body,
                            const std::string &etag,
                            const SyncConfig &config,
                            const std::string &contentType = {}) {
    HttpResponse response;
    std::wstring validationError;
    if (!validateSyncUrl(url, validationError)) {
        response.error = validationError;
        return response;
    }

    const std::wstring wideUrl = toWide(url);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) {
        response.error = L"服务器地址格式不正确。";
        return response;
    }

    const std::wstring host(components.lpszHostName,
                            components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(
        L"TuxIME/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        response.error = L"无法初始化 WinHTTP。";
        return response;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 10000);

    HINTERNET connection =
        WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connection) {
        response.error = L"无法连接服务器。";
        WinHttpCloseHandle(session);
        return response;
    }

    const std::wstring wideMethod = toWide(method);
    HINTERNET request = WinHttpOpenRequest(
        connection, wideMethod.c_str(), path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        response.error = L"无法创建请求。";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                     &redirectPolicy, sizeof(redirectPolicy));

    std::wstring headers;
    headers += L"User-Agent: TuxIME/0.1\r\n";
    if (method == "PUT") {
        const std::string type =
            contentType.empty() ? "application/json" : contentType;
        headers += L"Content-Type: " + toWide(type) + L"\r\n";
    }
    if (!etag.empty()) {
        headers += L"If-None-Match: " + toWide(etag) + L"\r\n";
    }
    if (!config.username.empty() || !config.password.empty()) {
        const std::string credentials = config.username + ":" +
                                        config.password;
        headers += L"Authorization: Basic " +
                   toWide(base64Encode(credentials)) + L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        request, headers.c_str(), static_cast<DWORD>(-1L),
        body.empty() ? WINHTTP_NO_REQUEST_DATA
                     : const_cast<char *>(body.data()),
        static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        response.error = L"请求失败：" + std::to_wstring(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

    DWORD statusSize = sizeof(response.status);
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &statusSize,
        WINHTTP_NO_HEADER_INDEX);

    DWORD etagSize = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"ETag", nullptr,
                        &etagSize, WINHTTP_NO_HEADER_INDEX);
    if (etagSize > 0) {
        std::vector<wchar_t> buffer(etagSize / sizeof(wchar_t) + 1);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"ETag",
                                buffer.data(), &etagSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            response.etag = toUtf8(buffer.data());
        }
    }

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
        response.body += chunk;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}

} // namespace

bool validateSyncUrl(const std::string &url, std::wstring &error) {
    if (url.empty()) {
        error = L"请填写 SyncClipboard 服务器地址。";
        return false;
    }
    std::string normalized = url;
    if (normalized.find("://") == std::string::npos) {
        normalized = "http://" + normalized;
    }
    const std::wstring wide = toWide(normalized);
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &components) ||
        (components.nScheme != INTERNET_SCHEME_HTTP &&
         components.nScheme != INTERNET_SCHEME_HTTPS)) {
        error = L"服务器地址只允许 http 或 https。";
        return false;
    }
    const std::wstring host(components.lpszHostName,
                            components.dwHostNameLength);
    return validateHost(host, error);
}

ClipboardSync::ClipboardSync() = default;

ClipboardSync::~ClipboardSync() { stop(); }

SyncConfig ClipboardSync::config() const {
    std::lock_guard<std::mutex> lock(configMutex_);
    return config_;
}

bool ClipboardSync::start(const SyncConfig &config, StatusCallback callback) {
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        config_ = config;
        callback_ = std::move(callback);
    }
    if (thread_.joinable()) {
        return true;
    }
    loadHistory();
    stopping_ = false;
    running_ = true;
    thread_ = std::thread([this]() { run(); });
    return true;
}

void ClipboardSync::stop() {
    stopping_ = true;
    if (hwnd_) {
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    running_ = false;
}

void ClipboardSync::updateConfig(const SyncConfig &config) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP + 1, 0, 0);
    }
}

void ClipboardSync::trigger() {
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP + 2, 0, 0);
    }
}

void ClipboardSync::report(const std::wstring &status) {
    StatusCallback callback;
    {
        std::lock_guard<std::mutex> lock(configMutex_);
        callback = callback_;
    }
    if (callback) {
        callback(status);
    }
}

void ClipboardSync::setClipboardText(const std::wstring &text) {
    if (!OpenClipboard(hwnd_)) {
        return;
    }
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        auto *buffer = static_cast<wchar_t *>(GlobalLock(memory));
        if (buffer) {
            std::memcpy(buffer, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            lastAppliedHash_ = sha256Hex(text);
            memory = nullptr;
        }
    }
    if (memory) {
        GlobalFree(memory);
    }
    CloseClipboard();
}

std::wstring ClipboardSync::readClipboardText() const {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd_)) {
        return {};
    }
    std::wstring result;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const auto *buffer = static_cast<const wchar_t *>(GlobalLock(data));
        if (buffer) {
            result.assign(buffer);
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return result;
}

void ClipboardSync::addHistory(const std::wstring &text) {
    if (text.empty() || text.size() > 1000000) {
        return;
    }
    Entry entry{text, sha256Hex(text)};
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        if (!history_.empty() && history_.front().hash == entry.hash) {
            return;
        }
        std::erase_if(history_, [&entry](const Entry &existing) {
            return existing.hash == entry.hash;
        });
        history_.push_front(std::move(entry));
        while (history_.size() > 50) {
            history_.pop_back();
        }
    }
    saveHistory();
}

std::vector<ClipboardSync::Entry> ClipboardSync::history() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return {history_.begin(), history_.end()};
}

void ClipboardSync::clearHistory() {
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        history_.clear();
    }
    saveHistory();
}

void ClipboardSync::requestCopy(const std::wstring &text) {
    if (hwnd_) {
        PostMessageW(hwnd_, WM_APP + 3, 0,
                     reinterpret_cast<LPARAM>(new std::wstring(text)));
    }
}

void ClipboardSync::loadHistory() {
    std::ifstream in(SettingsStore::appDataDir() / "clipboard_history.txt",
                     std::ios::binary);
    std::string line;
    std::deque<Entry> entries;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::string utf8 = base64Decode(line);
        if (utf8.empty()) {
            continue;
        }
        std::wstring text = toWide(utf8);
        if (text.empty()) {
            continue;
        }
        entries.push_back({std::move(text), {}});
        if (entries.size() >= 50) {
            break;
        }
    }
    for (auto &entry : entries) {
        entry.hash = sha256Hex(entry.text);
    }
    std::lock_guard<std::mutex> lock(historyMutex_);
    history_ = std::move(entries);
}

void ClipboardSync::saveHistory() {
    std::deque<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        entries = history_;
    }
    const auto path = SettingsStore::appDataDir() / "clipboard_history.txt";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (const auto &entry : entries) {
        out << base64Encode(toUtf8(entry.text)) << '\n';
    }
}

bool ClipboardSync::pushText(const std::wstring &text) {
    const SyncConfig current = config();
    if (!current.enabled || text.empty() || text.size() > 1000000) {
        return false;
    }
    const std::wstring hash = sha256Hex(text);
    const std::string payload =
        "{\"type\":\"Text\",\"text\":\"" + jsonEscape(toUtf8(text)) +
        "\",\"hash\":\"" + toUtf8(hash) +
        "\",\"hasData\":false,\"dataName\":\"\",\"size\":" +
        std::to_string(toUtf8(text).size()) + "}";
    const HttpResponse response = performRequest(
        "PUT", jsonUrl(current.serverUrl), payload, "", current);
    if (!response.error.empty()) {
        report(response.error);
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        report(httpStatusMessage(response.status));
        return false;
    }
    lastHash_ = hash;
    report(L"已同步到服务器");
    return true;
}

bool ClipboardSync::pushImage(const std::vector<uint8_t> &png) {
    const SyncConfig current = config();
    if (!current.enabled || png.empty()) {
        return false;
    }
    const std::wstring contentHash = sha256HexBytes(png.data(), png.size());
    std::string name = "tuxime-" + toUtf8(contentHash).substr(0, 16) + ".png";
    name = sanitizeFileName(name);
    if (name.empty()) {
        return false;
    }
    const std::wstring nameWide = toWide(name);
    const std::wstring hash = fileHash(nameWide, png);
    if (hash == lastHash_ || hash == lastAppliedHash_) {
        return false;
    }

    const std::string fileEndpoint = fileUrl(current.serverUrl, name);
    const std::string fileBody(png.begin(), png.end());
    auto uploadFile = [&]() {
        return performRequest("PUT", fileEndpoint, fileBody, "", current,
                              "application/octet-stream");
    };
    HttpResponse upload = uploadFile();
    if (upload.status == 404 || upload.status == 409) {
        performRequest("MKCOL", baseUrl(current.serverUrl) + "file/", "",
                       "", current);
        upload = uploadFile();
    }
    if (!upload.error.empty()) {
        report(upload.error);
        return false;
    }
    if (upload.status < 200 || upload.status >= 300) {
        report(httpStatusMessage(upload.status));
        return false;
    }

    const std::string payload =
        "{\"type\":\"File\",\"text\":\"" + jsonEscape(name) +
        "\",\"hash\":\"" + toUtf8(hash) +
        "\",\"hasData\":true,\"dataName\":\"" + jsonEscape(name) +
        "\",\"size\":" + std::to_string(png.size()) + "}";
    const HttpResponse response =
        performRequest("PUT", jsonUrl(current.serverUrl), payload, "",
                       current);
    if (!response.error.empty()) {
        report(response.error);
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        report(httpStatusMessage(response.status));
        return false;
    }
    lastHash_ = hash;
    report(L"图片已同步到服务器");
    return true;
}

void ClipboardSync::syncImage(const std::vector<uint8_t> &png) {
    pushImage(png);
}

void ClipboardSync::onClipboardUpdate() {
    const SyncConfig current = config();
    if (clipboardHasImage()) {
        std::vector<uint8_t> png;
        if (readClipboardImage(hwnd_, png) && !png.empty()) {
            syncImage(png);
            return;
        }
    }
    const std::wstring text = readClipboardText();
    if (text.empty()) {
        return;
    }
    addHistory(text);
    if (!current.enabled) {
        return;
    }
    const std::wstring hash = sha256Hex(text);
    if (hash == lastHash_ || hash == lastAppliedHash_) {
        return;
    }
    pushText(text);
}

void ClipboardSync::poll() {
    const SyncConfig current = config();
    if (!current.enabled || current.serverUrl.empty()) {
        return;
    }
    const std::string url = jsonUrl(current.serverUrl);
    const HttpResponse response =
        performRequest("GET", url, "", lastEtag_, current);
    if (!response.error.empty()) {
        report(response.error);
        return;
    }
    if (response.status == 304) {
        return;
    }
    if (response.status < 200 || response.status >= 300) {
        report(httpStatusMessage(response.status));
        return;
    }
    lastEtag_ = response.etag;

    std::string type;
    std::string textUtf8;
    std::string hashUtf8;
    jsonString(response.body, "type", type);
    jsonString(response.body, "hash", hashUtf8);
    if (type == "Text" && jsonString(response.body, "text", textUtf8)) {
        const std::wstring text = toWide(textUtf8);
        const std::wstring hash =
            hashUtf8.empty() ? sha256Hex(text) : toWide(hashUtf8);
        if (hash == lastHash_ || hash == lastAppliedHash_) {
            return;
        }
        lastHash_ = hash;
        setClipboardText(text);
        report(L"收到远端剪贴板");
        return;
    }

    if (type != "File" && type != "Image") {
        return;
    }
    std::string dataName;
    if (!jsonBool(response.body, "hasData") ||
        !jsonString(response.body, "dataName", dataName)) {
        return;
    }
    dataName = sanitizeFileName(dataName);
    if (dataName.empty()) {
        return;
    }
    const std::wstring hash = toWide(hashUtf8);
    if (!hash.empty() &&
        (hash == lastHash_ || hash == lastAppliedHash_)) {
        return;
    }
    const HttpResponse file = performRequest(
        "GET", fileUrl(current.serverUrl, dataName), "", "", current);
    if (!file.error.empty()) {
        report(file.error);
        return;
    }
    if (file.status < 200 || file.status >= 300) {
        report(httpStatusMessage(file.status));
        return;
    }
    if (!isImageFileName(dataName)) {
        report(L"收到文件，当前版本只支持图片同步");
        return;
    }
    const std::vector<uint8_t> image(file.body.begin(), file.body.end());
    if (!writeClipboardImage(hwnd_, image)) {
        report(L"图片写入剪贴板失败");
        return;
    }
    if (!hash.empty()) {
        lastHash_ = hash;
        lastAppliedHash_ = hash;
    }
    report(L"收到远端图片");
}

void ClipboardSync::run() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t className[] = L"TuxImeClipboardWindow";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &ClipboardSync::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = className;
    RegisterClassExW(&windowClass);

    hwnd_ = CreateWindowExW(0, className, L"", 0, 0, 0, 0, 0,
                            HWND_MESSAGE, nullptr, instance, this);
    if (!hwnd_) {
        running_ = false;
        return;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
    AddClipboardFormatListener(hwnd_);
    SetTimer(hwnd_, 1, 3000, nullptr);

    onClipboardUpdate();
    poll();

    if (stopping_) {
        PostQuitMessage(0);
    }

    MSG message{};
    while (!stopping_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    KillTimer(hwnd_, 1);
    RemoveClipboardFormatListener(hwnd_);
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
}

LRESULT CALLBACK ClipboardSync::windowProc(HWND hwnd, UINT message,
                                            WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<ClipboardSync *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
    case WM_CLIPBOARDUPDATE:
        self->onClipboardUpdate();
        return 0;
    case WM_TIMER:
        self->poll();
        return 0;
    case WM_APP + 1:
        self->poll();
        return 0;
    case WM_APP + 2:
        self->onClipboardUpdate();
        return 0;
    case WM_APP + 3: {
        auto *text = reinterpret_cast<std::wstring *>(lParam);
        if (text) {
            self->setClipboardText(*text);
            delete text;
        }
        return 0;
    }
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

std::wstring ClipboardSync::testConnection(const SyncConfig &config) const {
    std::wstring error;
    if (!validateSyncUrl(config.serverUrl, error)) {
        return error;
    }
    const HttpResponse response =
        performRequest("GET", jsonUrl(config.serverUrl), "", "", config);
    if (!response.error.empty()) {
        return response.error;
    }
    if (response.status >= 200 && response.status < 300) {
        return L"连接成功。";
    }
    return httpStatusMessage(response.status);
}

} // namespace tuxime
