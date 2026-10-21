#include "tray.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <windows.h>
#include <windowsx.h>

#include "clipboard_sync.h"
#include "server.h"
#include "settings.h"

namespace tuxime {

namespace {

constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kStatusMessage = WM_APP + 11;
constexpr UINT kShowSettingsMessage = WM_APP + 20;

constexpr int kIdSettings = 1001;
constexpr int kIdSyncNow = 1002;
constexpr int kIdReload = 1003;
constexpr int kIdExit = 1004;

constexpr int kIdDefaultIm = 2001;
constexpr int kIdPageSize = 2002;
constexpr int kIdFuzzyZ = 2010;
constexpr int kIdFuzzyC = 2011;
constexpr int kIdFuzzyS = 2012;
constexpr int kIdFuzzyL = 2013;
constexpr int kIdFuzzyAn = 2014;
constexpr int kIdFuzzyEn = 2015;
constexpr int kIdFuzzyIn = 2016;
constexpr int kIdFuzzyF = 2017;
constexpr int kIdSyncEnabled = 2020;
constexpr int kIdSyncUrl = 2021;
constexpr int kIdSyncUser = 2022;
constexpr int kIdSyncPassword = 2023;
constexpr int kIdTest = 2030;
constexpr int kIdSyncNowButton = 2031;
constexpr int kIdApply = 2040;
constexpr int kIdClose = 2041;
constexpr int kIdStatus = 2050;

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

int systemDpi() {
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(nullptr, screen);
    return dpi > 0 ? dpi : 96;
}

} // namespace

struct TrayIcon::Impl {
    explicit Impl(Server &server) : server(server) {}

    Server &server;
    HWND hwnd = nullptr;
    HICON icon = nullptr;
    std::thread thread;
    std::atomic<bool> running{false};
    std::atomic<bool> pendingShowSettings{false};
    class SettingsWindow *settings = nullptr;

    void run();
    void showMenu();
    void showSettings();
    void updateStatus(const std::wstring &status);
    void destroyTray();

    static LRESULT CALLBACK trayProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam);
};

class SettingsWindow {
public:
    explicit SettingsWindow(TrayIcon::Impl &tray) : tray_(tray) {}
    ~SettingsWindow() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        if (font_) {
            DeleteObject(font_);
        }
    }

    bool create();
    void show();
    void setStatus(const std::wstring &status);

private:
    int s(int value) const { return MulDiv(value, dpi_, 96); }
    void createControls();
    void loadValues();
    PinyinSettings readPinyin() const;
    SyncConfig readSync() const;
    void updateSyncEnabled();
    void apply();
    void test();
    void syncNow();
    void setControlText(HWND control, const std::wstring &text) const;
    std::wstring controlText(HWND control) const;
    HWND make(const wchar_t *className, const wchar_t *text, DWORD style,
              int x, int y, int width, int height, int id,
              DWORD exStyle = 0);

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam,
                                 LPARAM lParam);

    TrayIcon::Impl &tray_;
    HWND hwnd_ = nullptr;
    int dpi_ = 96;
    HFONT font_ = nullptr;
    HWND defaultIm_ = nullptr;
    HWND pageSize_ = nullptr;
    HWND fuzzy_[8] = {};
    HWND syncEnabled_ = nullptr;
    HWND syncUrl_ = nullptr;
    HWND syncUser_ = nullptr;
    HWND syncPassword_ = nullptr;
    HWND status_ = nullptr;
};

void TrayIcon::Impl::run() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t trayClass[] = L"TuxImeTrayWindow";

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &TrayIcon::Impl::trayProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = trayClass;
    RegisterClassExW(&windowClass);

    hwnd = CreateWindowExW(0, trayClass, L"Tux IME", WS_OVERLAPPED, 0, 0, 0,
                           0, nullptr, nullptr, instance, this);
    if (!hwnd) {
        running = false;
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));

    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    const std::wstring iconPath =
        (std::filesystem::path(modulePath).parent_path() / "penguin.ico")
            .wstring();
    icon = static_cast<HICON>(
        LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, 16, 16,
                   LR_LOADFROMFILE));
    if (!icon) {
        icon = LoadIconW(nullptr, reinterpret_cast<LPCWSTR>(IDI_APPLICATION));
    }

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 1;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = kTrayMessage;
    data.hIcon = icon;
    wcscpy_s(data.szTip, L"Tux IME");
    Shell_NotifyIconW(NIM_ADD, &data);
    updateStatus(L"就绪");
    if (pendingShowSettings.exchange(false)) {
        showSettings();
    }

    MSG message{};
    while (running && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (settings) {
        delete settings;
        settings = nullptr;
    }
    destroyTray();
    if (icon) {
        DestroyIcon(icon);
        icon = nullptr;
    }
    if (hwnd) {
        DestroyWindow(hwnd);
        hwnd = nullptr;
    }
}

void TrayIcon::Impl::destroyTray() {
    if (!hwnd) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

void TrayIcon::Impl::updateStatus(const std::wstring &status) {
    if (!hwnd) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = 1;
    data.uFlags = NIF_TIP;
    wcsncpy_s(data.szTip, status.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &data);
    auto *copy = new std::wstring(status);
    PostMessageW(hwnd, kStatusMessage, 0,
                 reinterpret_cast<LPARAM>(copy));
}

void TrayIcon::Impl::showMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kIdSettings, L"设置...");
    AppendMenuW(menu, MF_STRING, kIdSyncNow, L"立即同步剪贴板");
    AppendMenuW(menu, MF_STRING, kIdReload, L"重新加载配置");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kIdExit, L"退出 Tux IME");

    POINT point{};
    GetCursorPos(&point);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
}

void TrayIcon::Impl::showSettings() {
    if (!settings) {
        settings = new SettingsWindow(*this);
        if (!settings->create()) {
            delete settings;
            settings = nullptr;
            return;
        }
    }
    settings->show();
}

LRESULT CALLBACK TrayIcon::Impl::trayProc(HWND hwnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<TrayIcon::Impl *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
    case kTrayMessage:
        if (LOWORD(lParam) == WM_RBUTTONUP ||
            LOWORD(lParam) == WM_CONTEXTMENU) {
            self->showMenu();
        } else if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
            self->showSettings();
        }
        return 0;
    case kStatusMessage: {
        auto *status = reinterpret_cast<std::wstring *>(lParam);
        if (status) {
            if (self->settings) {
                self->settings->setStatus(*status);
            }
            delete status;
        }
        return 0;
    }
    case kShowSettingsMessage:
        self->showSettings();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdSettings:
            self->showSettings();
            return 0;
        case kIdSyncNow:
            self->server.clipboardSync().trigger();
            self->updateStatus(L"已请求同步");
            return 0;
        case kIdReload:
            self->server.applySettings(SettingsStore::loadPinyin(),
                                       SettingsStore::loadSync());
            self->updateStatus(L"配置已重新加载");
            return 0;
        case kIdExit:
            self->server.requestExit();
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool SettingsWindow::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    constexpr wchar_t settingsClass[] = L"TuxImeSettingsWindow";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &SettingsWindow::proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(
            nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        windowClass.lpszClassName = settingsClass;
        windowClass.hbrBackground =
            reinterpret_cast<HBRUSH>(COLOR_3DFACE + 1);
        registered = RegisterClassExW(&windowClass) != 0 ||
                     GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, settingsClass, L"Tux IME 设置", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, s(660), s(470), nullptr, nullptr,
        instance, this);
    return hwnd_ != nullptr;
}

void SettingsWindow::show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

void SettingsWindow::setStatus(const std::wstring &status) {
    if (status_) {
        SetWindowTextW(status_, status.c_str());
    }
}

HWND SettingsWindow::make(const wchar_t *className, const wchar_t *text,
                          DWORD style, int x, int y, int width, int height,
                          int id, DWORD exStyle) {
    HWND control = CreateWindowExW(
        exStyle, className, text, WS_CHILD | WS_VISIBLE | style, s(x), s(y),
        s(width), s(height), hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr), nullptr);
    if (control && font_) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_),
                     TRUE);
    }
    return control;
}

void SettingsWindow::createControls() {
    dpi_ = systemDpi();
    font_ = CreateFontW(-MulDiv(9, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE,
                        FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                        DEFAULT_PITCH | FF_DONTCARE,
                        L"Microsoft YaHei UI");

    make(L"BUTTON", L"输入法", BS_GROUPBOX, 15, 10, 610, 170, 0);
    make(L"STATIC", L"默认输入法:", SS_LEFT, 30, 38, 95, 22, 0);
    defaultIm_ = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 130,
                      34, 180, 200, kIdDefaultIm);
    SendMessageW(defaultIm_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"中文（拼音）"));
    SendMessageW(defaultIm_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"英文"));
    SendMessageW(defaultIm_, CB_SETCURSEL, 0, 0);

    make(L"STATIC", L"每页候选:", SS_LEFT, 350, 38, 90, 22, 0);
    pageSize_ = make(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 445,
                     34, 80, 200, kIdPageSize);
    for (const wchar_t *size : {L"5", L"7", L"9"}) {
        SendMessageW(pageSize_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(size));
    }
    SendMessageW(pageSize_, CB_SETCURSEL, 1, 0);

    make(L"STATIC", L"模糊拼音:", SS_LEFT, 30, 68, 90, 22, 0);
    fuzzy_[0] = make(L"BUTTON", L"zh / z", BS_AUTOCHECKBOX, 30, 92, 120, 22,
                     kIdFuzzyZ);
    fuzzy_[1] = make(L"BUTTON", L"ch / c", BS_AUTOCHECKBOX, 220, 92, 120, 22,
                     kIdFuzzyC);
    fuzzy_[2] = make(L"BUTTON", L"sh / s", BS_AUTOCHECKBOX, 410, 92, 120, 22,
                     kIdFuzzyS);
    fuzzy_[3] = make(L"BUTTON", L"n / l", BS_AUTOCHECKBOX, 30, 118, 120, 22,
                     kIdFuzzyL);
    fuzzy_[4] = make(L"BUTTON", L"an / ang", BS_AUTOCHECKBOX, 220, 118, 120,
                     22, kIdFuzzyAn);
    fuzzy_[5] = make(L"BUTTON", L"en / eng", BS_AUTOCHECKBOX, 410, 118, 120,
                     22, kIdFuzzyEn);
    fuzzy_[6] = make(L"BUTTON", L"in / ing", BS_AUTOCHECKBOX, 30, 144, 120,
                     22, kIdFuzzyIn);
    fuzzy_[7] = make(L"BUTTON", L"f / h", BS_AUTOCHECKBOX, 220, 144, 120, 22,
                     kIdFuzzyF);

    make(L"BUTTON", L"剪贴板同步", BS_GROUPBOX, 15, 190, 610, 180, 0);
    syncEnabled_ = make(L"BUTTON", L"启用剪贴板同步", BS_AUTOCHECKBOX, 30, 215,
                        180, 22, kIdSyncEnabled);
    make(L"STATIC", L"服务器:", SS_LEFT, 30, 245, 70, 22, 0);
    syncUrl_ = make(L"EDIT", L"", ES_AUTOHSCROLL, 105, 241, 490, 24,
                    kIdSyncUrl, WS_EX_CLIENTEDGE);
    make(L"STATIC", L"用户名:", SS_LEFT, 30, 275, 70, 22, 0);
    syncUser_ = make(L"EDIT", L"", ES_AUTOHSCROLL, 105, 271, 180, 24,
                     kIdSyncUser, WS_EX_CLIENTEDGE);
    make(L"STATIC", L"密码:", SS_LEFT, 305, 275, 60, 22, 0);
    syncPassword_ = make(L"EDIT", L"", ES_PASSWORD | ES_AUTOHSCROLL, 365, 271,
                         230, 24, kIdSyncPassword, WS_EX_CLIENTEDGE);
    make(L"BUTTON", L"测试连接", BS_PUSHBUTTON, 30, 305, 100, 28, kIdTest);
    make(L"BUTTON", L"立即同步", BS_PUSHBUTTON, 145, 305, 100, 28,
         kIdSyncNowButton);
    status_ = make(L"STATIC", L"就绪", SS_LEFT, 30, 340, 560, 22, kIdStatus);

    make(L"BUTTON", L"应用", BS_PUSHBUTTON, 435, 390, 85, 30, kIdApply);
    make(L"BUTTON", L"关闭", BS_PUSHBUTTON, 530, 390, 85, 30, kIdClose);

    loadValues();
}

void SettingsWindow::loadValues() {
    const PinyinSettings pinyin = SettingsStore::loadPinyin();
    SendMessageW(defaultIm_, CB_SETCURSEL, pinyin.keyboardDefault ? 1 : 0,
                 0);

    int pageIndex = SendMessageW(pageSize_, CB_FINDSTRINGEXACT, -1,
                                 reinterpret_cast<LPARAM>(
                                     std::to_wstring(pinyin.pageSize).c_str()));
    if (pageIndex == CB_ERR) {
        const std::wstring value = std::to_wstring(pinyin.pageSize);
        pageIndex = static_cast<int>(SendMessageW(
            pageSize_, CB_ADDSTRING, 0,
            reinterpret_cast<LPARAM>(value.c_str())));
    }
    SendMessageW(pageSize_, CB_SETCURSEL, pageIndex, 0);

    const BOOL checks[] = {
        pinyin.fuzzyZ, pinyin.fuzzyC, pinyin.fuzzyS, pinyin.fuzzyL,
        pinyin.fuzzyAn, pinyin.fuzzyEn, pinyin.fuzzyIn, pinyin.fuzzyF};
    for (int i = 0; i < 8; ++i) {
        SendMessageW(fuzzy_[i], BM_SETCHECK,
                     checks[i] ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    const SyncConfig sync = SettingsStore::loadSync();
    SendMessageW(syncEnabled_, BM_SETCHECK,
                 sync.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    setControlText(syncUrl_, toWide(sync.serverUrl));
    setControlText(syncUser_, toWide(sync.username));
    setControlText(syncPassword_, toWide(sync.password));
    updateSyncEnabled();
}

PinyinSettings SettingsWindow::readPinyin() const {
    PinyinSettings settings;
    settings.keyboardDefault =
        SendMessageW(defaultIm_, CB_GETCURSEL, 0, 0) == 1;
    const int pageIndex =
        static_cast<int>(SendMessageW(pageSize_, CB_GETCURSEL, 0, 0));
    wchar_t buffer[16] = {};
    SendMessageW(pageSize_, CB_GETLBTEXT, pageIndex,
                 reinterpret_cast<LPARAM>(buffer));
    settings.pageSize = std::clamp(_wtoi(buffer), 3, 10);
    settings.fuzzyZ =
        SendMessageW(fuzzy_[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyC =
        SendMessageW(fuzzy_[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyS =
        SendMessageW(fuzzy_[2], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyL =
        SendMessageW(fuzzy_[3], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyAn =
        SendMessageW(fuzzy_[4], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyEn =
        SendMessageW(fuzzy_[5], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyIn =
        SendMessageW(fuzzy_[6], BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings.fuzzyF =
        SendMessageW(fuzzy_[7], BM_GETCHECK, 0, 0) == BST_CHECKED;
    return settings;
}

SyncConfig SettingsWindow::readSync() const {
    SyncConfig config;
    config.enabled =
        SendMessageW(syncEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    config.serverUrl = toUtf8(controlText(syncUrl_));
    config.username = toUtf8(controlText(syncUser_));
    config.password = toUtf8(controlText(syncPassword_));
    return config;
}

void SettingsWindow::updateSyncEnabled() {
    const bool enabled =
        SendMessageW(syncEnabled_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableWindow(syncUrl_, enabled);
    EnableWindow(syncUser_, enabled);
    EnableWindow(syncPassword_, enabled);
}

void SettingsWindow::apply() {
    const PinyinSettings pinyin = readPinyin();
    const SyncConfig sync = readSync();
    SettingsStore::savePinyin(pinyin);
    SettingsStore::saveSync(sync);
    tray_.server.applySettings(pinyin, sync);
    setStatus(L"设置已保存");
}

void SettingsWindow::test() {
    setStatus(L"正在测试连接...");
    UpdateWindow(hwnd_);
    const SyncConfig sync = readSync();
    setStatus(tray_.server.clipboardSync().testConnection(sync));
}

void SettingsWindow::syncNow() {
    const SyncConfig sync = readSync();
    SettingsStore::saveSync(sync);
    tray_.server.clipboardSync().updateConfig(sync);
    if (sync.enabled) {
        tray_.server.clipboardSync().trigger();
        setStatus(L"已请求同步");
    } else {
        setStatus(L"请先启用剪贴板同步");
    }
}

void SettingsWindow::setControlText(HWND control,
                                    const std::wstring &text) const {
    if (control) {
        SetWindowTextW(control, text.c_str());
    }
}

std::wstring SettingsWindow::controlText(HWND control) const {
    wchar_t buffer[4096] = {};
    if (control) {
        GetWindowTextW(control, buffer, static_cast<int>(std::size(buffer)));
    }
    return buffer;
}

LRESULT CALLBACK SettingsWindow::proc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<SettingsWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_CREATE) {
        self = reinterpret_cast<SettingsWindow *>(
            reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->createControls();
        return 0;
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kIdApply:
            self->apply();
            return 0;
        case kIdClose:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case kIdTest:
            self->test();
            return 0;
        case kIdSyncNowButton:
            self->syncNow();
            return 0;
        case kIdSyncEnabled:
            self->updateSyncEnabled();
            return 0;
        default:
            break;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
        return reinterpret_cast<LRESULT>(
            GetSysColorBrush(COLOR_3DFACE));
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

TrayIcon::TrayIcon(Server &server) : impl_(std::make_unique<Impl>(server)) {}

TrayIcon::~TrayIcon() { stop(); }

void TrayIcon::start() {
    if (impl_->thread.joinable()) {
        return;
    }
    impl_->running = true;
    impl_->thread = std::thread([this]() { impl_->run(); });
}

void TrayIcon::stop() {
    impl_->running = false;
    if (impl_->hwnd) {
        PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

void TrayIcon::setStatus(const std::wstring &status) {
    impl_->updateStatus(status);
}

void TrayIcon::showSettings() {
    if (impl_->hwnd) {
        PostMessageW(impl_->hwnd, kShowSettingsMessage, 0, 0);
    } else {
        impl_->pendingShowSettings = true;
    }
}

} // namespace tuxime
