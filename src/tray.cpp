#include "tray.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "clipboard_sync.h"
#include "common/appearance.h"
#include "common/version.h"
#include "server.h"
#include "settings.h"
#include "update_check.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace tuxime {

namespace {

constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kStatusMessage = WM_APP + 11;
constexpr UINT kShowSettingsMessage = WM_APP + 20;

constexpr int kIdSettings = 1001;
constexpr int kIdSyncNow = 1002;
constexpr int kIdReload = 1003;
constexpr int kIdExit = 1004;
constexpr int kIdChinese = 1005;
constexpr int kIdEnglish = 1006;
constexpr int kIdTogglePunctuation = 1007;
constexpr int kIdToggleFullwidth = 1008;
constexpr int kIdToggleChttrans = 1009;
constexpr int kIdClipboardHistory = 1010;
constexpr int kIdInputMethodBase = 1100;

struct InputMethodOption {
    const char *id;
    const char *label;
};

constexpr InputMethodOption kInputMethodOptions[] = {
    {"pinyin", "中文（拼音）"},   {"shuangpin", "中文（双拼）"},
    {"wbx", "五笔"},              {"wbpy", "五笔拼音"},
    {"cangjie", "仓颉"},          {"erbi", "二笔"},
    {"zrm", "自然码"},            {"qxm", "冰蟾"},
    {"db", "电报码"},             {"wanfeng", "晚风"},
    {"tux-keyboard", "英文"},
};

const char *inputMethodLabel(const std::string &id) {
    for (const auto &option : kInputMethodOptions) {
        if (id == option.id) {
            return option.label;
        }
    }
    return "中文（拼音）";
}

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

void copyText(char *destination, size_t size, const std::string &text) {
    if (size == 0) {
        return;
    }
    const size_t length = (std::min)(text.size(), size - 1);
    std::memcpy(destination, text.data(), length);
    destination[length] = '\0';
}

void unpackColor(uint32_t color, float values[4]) {
    values[0] = static_cast<float>((color >> 16) & 0xff) / 255.0f;
    values[1] = static_cast<float>((color >> 8) & 0xff) / 255.0f;
    values[2] = static_cast<float>(color & 0xff) / 255.0f;
    values[3] = static_cast<float>((color >> 24) & 0xff) / 255.0f;
}

uint32_t packColor(const float values[4]) {
    auto channel = [](float value) {
        return static_cast<uint32_t>(
            (std::clamp)(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return (channel(values[3]) << 24) | (channel(values[0]) << 16) |
           (channel(values[1]) << 8) | channel(values[2]);
}

bool colorEdit(const char *label, uint32_t &color, float scale) {
    float values[4];
    unpackColor(color, values);
    ImGui::SetNextItemWidth(180.0f * scale);
    if (ImGui::ColorEdit4(label, values,
                          ImGuiColorEditFlags_AlphaBar |
                              ImGuiColorEditFlags_AlphaPreviewHalf)) {
        color = packColor(values);
        return true;
    }
    return false;
}

std::wstring chooseTextFile(bool save, const wchar_t *title) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = L"txt";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&dialog)
                         : GetOpenFileNameW(&dialog);
    return ok ? std::wstring(path) : std::wstring();
}

std::wstring chooseThemeFile(bool save, const wchar_t *title) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFilter =
        L"主题文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = L"ini";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER |
                   (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&dialog)
                         : GetOpenFileNameW(&dialog);
    return ok ? std::wstring(path) : std::wstring();
}

std::filesystem::path moduleDirectory() {
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(path).parent_path();
}

std::wstring quotePath(const std::filesystem::path &path) {
    return L"\"" + path.wstring() + L"\"";
}

bool runHiddenProcess(const std::wstring &commandLine, DWORD timeoutMs) {
    std::vector<wchar_t> command(commandLine.begin(), commandLine.end());
    command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                        &process)) {
        return false;
    }
    const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
    DWORD exitCode = 1;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(process.hProcess, &exitCode);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return wait == WAIT_OBJECT_0 && exitCode == 0;
}

int windowDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    static auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (getDpiForWindow && hwnd) {
        const UINT dpi = getDpiForWindow(hwnd);
        if (dpi) {
            return static_cast<int>(dpi);
        }
    }
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
    void showSettings(int page = 0);
    void updateStatus(const std::wstring &status);
    void destroyTray();

    static LRESULT CALLBACK trayProc(HWND hwnd, UINT message, WPARAM wParam,
                                      LPARAM lParam);
};

class SettingsWindow {
public:
    explicit SettingsWindow(TrayIcon::Impl &tray) : tray_(tray) {}
    ~SettingsWindow();

    bool create();
    void show(int page = -1);
    void setStatus(const std::wstring &status);
private:
    bool createDevice();
    void destroyDevice();
    void createRenderTarget();
    void destroyRenderTarget();
    void loadValues();
    void drawUi();
    void apply();
    void test();
    void syncNow();
    void render();
    void hide();

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wParam,
                                 LPARAM lParam);

    TrayIcon::Impl &tray_;
    HWND hwnd_ = nullptr;
    ID3D11Device *device_ = nullptr;
    ID3D11DeviceContext *context_ = nullptr;
    IDXGISwapChain *swapChain_ = nullptr;
    ID3D11RenderTargetView *renderTarget_ = nullptr;
    bool occluded_ = false;
    ImGuiContext *imgui_ = nullptr;

    PinyinSettings pinyin_;
    SyncConfig sync_;
    char url_[2048] = {};
    char username_[256] = {};
    char password_[256] = {};
    char phraseCode_[64] = {};
    char phraseText_[512] = {};
    std::vector<CustomPhrase> phrases_;
    int phraseSelected_ = -1;
    tuxime::Appearance appearance_;
    int page_ = 0;
    std::wstring status_;
    std::wstring updateVersion_;
    std::wstring updateUrl_;
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
    HMENU inputMethodMenu = CreatePopupMenu();
    for (size_t i = 0; i < std::size(kInputMethodOptions); ++i) {
        const std::wstring label = toWide(kInputMethodOptions[i].label);
        AppendMenuW(inputMethodMenu, MF_STRING,
                    kIdInputMethodBase + static_cast<int>(i),
                    label.c_str());
    }
    AppendMenuW(menu, MF_POPUP,
                reinterpret_cast<UINT_PTR>(inputMethodMenu), L"输入法");
    AppendMenuW(menu, MF_STRING, kIdTogglePunctuation, L"切换中文标点");
    AppendMenuW(menu, MF_STRING, kIdToggleFullwidth, L"切换全角/半角");
    AppendMenuW(menu, MF_STRING, kIdToggleChttrans, L"切换简体/繁体");
    AppendMenuW(menu, MF_STRING, kIdClipboardHistory, L"剪贴板历史...");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
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

void TrayIcon::Impl::showSettings(int page) {
    if (!settings) {
        settings = new SettingsWindow(*this);
        if (!settings->create()) {
            delete settings;
            settings = nullptr;
            return;
        }
    }
    settings->show(page);
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
        if (LOWORD(wParam) >= kIdInputMethodBase &&
            LOWORD(wParam) <
                kIdInputMethodBase +
                    static_cast<int>(std::size(kInputMethodOptions))) {
            const auto index =
                LOWORD(wParam) - kIdInputMethodBase;
            self->server.switchInputMethod(
                kInputMethodOptions[index].id);
            self->updateStatus(toWide(kInputMethodOptions[index].label));
            return 0;
        }
        switch (LOWORD(wParam)) {
        case kIdChinese:
            self->server.switchInputMethod("pinyin");
            self->updateStatus(L"已切换到中文（拼音）");
            return 0;
        case kIdEnglish:
            self->server.switchInputMethod("tux-keyboard");
            self->updateStatus(L"已切换到英文");
            return 0;
        case kIdSettings:
            self->showSettings();
            return 0;
        case kIdTogglePunctuation:
            self->server.toggleAddon("punctuation");
            self->updateStatus(L"已切换中文标点");
            return 0;
        case kIdToggleFullwidth:
            self->server.toggleAddon("fullwidth");
            self->updateStatus(L"已切换全角/半角");
            return 0;
        case kIdToggleChttrans:
            self->server.toggleAddon("chttrans");
            self->updateStatus(L"已切换简体/繁体");
            return 0;
        case kIdClipboardHistory:
            self->showSettings(1);
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

SettingsWindow::~SettingsWindow() {
    hide();
    if (imgui_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(imgui_);
        imgui_ = nullptr;
    }
    destroyDevice();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool SettingsWindow::create() {
    ImGui_ImplWin32_EnableDpiAwareness();

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
        registered = RegisterClassExW(&windowClass) != 0 ||
                     GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered) {
        return false;
    }

    hwnd_ = CreateWindowExW(
        0, settingsClass, L"Tux IME 设置", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 820, 740, nullptr, nullptr, instance,
        this);
    if (!hwnd_) {
        return false;
    }
    if (!createDevice()) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }

    imgui_ = ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const float scale = static_cast<float>(windowDpi(hwnd_)) / 96.0f;
    ImGui::StyleColorsLight();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 10.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.12f, 0.13f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.95f, 0.96f, 0.97f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.85f, 0.87f, 0.90f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.92f, 0.94f, 0.97f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.86f, 0.90f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.80f, 0.86f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.82f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.25f, 0.52f, 0.90f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] =
        ImVec4(0.16f, 0.38f, 0.72f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.82f, 0.18f);
    style.Colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.20f, 0.45f, 0.82f, 0.28f);
    style.Colors[ImGuiCol_HeaderActive] =
        ImVec4(0.20f, 0.45f, 0.82f, 0.38f);
    style.Colors[ImGuiCol_TextSelectedBg] =
        ImVec4(0.20f, 0.45f, 0.82f, 0.30f);
    style.ScaleAllSizes(scale);

    ImFont *font = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc", 17.0f * scale, nullptr,
        io.Fonts->GetGlyphRangesChineseFull());
    if (!font) {
        io.Fonts->AddFontDefault();
    }

    ImGui_ImplWin32_Init(hwnd_);
    ImGui_ImplDX11_Init(device_, context_);

    loadValues();
    SetTimer(hwnd_, 1, 16, nullptr);
    return true;
}

bool SettingsWindow::createDevice() {
    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = hwnd_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &description, &swapChain_, &device_, &level,
        &context_);
    if (FAILED(result)) {
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, &description, &swapChain_, &device_, &level,
            &context_);
    }
    if (FAILED(result)) {
        return false;
    }
    createRenderTarget();
    return renderTarget_ != nullptr;
}

void SettingsWindow::destroyDevice() {
    destroyRenderTarget();
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (context_) {
        context_->Release();
        context_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
}

void SettingsWindow::createRenderTarget() {
    ID3D11Texture2D *backBuffer = nullptr;
    if (SUCCEEDED(swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void **>(&backBuffer))) &&
        backBuffer) {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTarget_);
        backBuffer->Release();
    }
}

void SettingsWindow::destroyRenderTarget() {
    if (renderTarget_) {
        renderTarget_->Release();
        renderTarget_ = nullptr;
    }
}

void SettingsWindow::loadValues() {
    pinyin_ = SettingsStore::loadPinyin();
    sync_ = SettingsStore::loadSync();
    copyText(url_, sizeof(url_), sync_.serverUrl);
    copyText(username_, sizeof(username_), sync_.username);
    copyText(password_, sizeof(password_), sync_.password);
    phrases_ = SettingsStore::loadPhrases();
    appearance_ = tuxime::loadAppearance();
    status_ = L"就绪";
}

void SettingsWindow::show(int page) {
    if (page >= 0) {
        page_ = page;
    }
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetTimer(hwnd_, 1, 16, nullptr);
}

void SettingsWindow::hide() {
    if (hwnd_) {
        KillTimer(hwnd_, 1);
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void SettingsWindow::setStatus(const std::wstring &status) {
    status_ = status;
}

void SettingsWindow::apply() {
    sync_.serverUrl = url_;
    sync_.username = username_;
    sync_.password = password_;
    SettingsStore::savePinyin(pinyin_);
    SettingsStore::saveSync(sync_);
    tray_.server.applySettings(pinyin_, sync_);
    status_ = L"设置已保存";
}

void SettingsWindow::test() {
    SyncConfig config;
    config.enabled = sync_.enabled;
    config.serverUrl = url_;
    config.username = username_;
    config.password = password_;
    status_ = L"正在测试连接...";
    status_ = tray_.server.clipboardSync().testConnection(config);
}

void SettingsWindow::syncNow() {
    sync_.serverUrl = url_;
    sync_.username = username_;
    sync_.password = password_;
    SettingsStore::saveSync(sync_);
    tray_.server.clipboardSync().updateConfig(sync_);
    if (!sync_.enabled) {
        status_ = L"请先启用剪贴板同步";
        return;
    }
    tray_.server.clipboardSync().trigger();
    status_ = L"已请求同步";
}

void SettingsWindow::drawUi() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##tux-settings", nullptr, flags);
    ImGui::PopStyleVar();

    const float scale = static_cast<float>(windowDpi(hwnd_)) / 96.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(0.12f, 0.34f, 0.62f, 1.0f));
    ImGui::BeginChild("##header", ImVec2(0, 76.0f * scale), false);
    ImGui::SetCursorPos(ImVec2(24.0f * scale, 16.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::TextUnformatted("Tux IME");
    ImGui::SetCursorPos(ImVec2(24.0f * scale, 42.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.82f, 0.89f, 1.0f, 1.0f));
    ImGui::TextUnformatted("Fcitx5 输入法 / 设置");
    ImGui::PopStyleColor(2);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::BeginChild("##body", ImVec2(0, 0), false);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImVec4(0.93f, 0.95f, 0.98f, 1.0f));
    ImGui::BeginChild("##nav", ImVec2(180.0f * scale, 0), false);
    ImGui::Dummy(ImVec2(0, 10.0f * scale));
    const char *tabs[] = {"输入法", "剪贴板", "短语", "词典", "外观",
                          "关于"};
    for (int i = 0; i < 6; ++i) {
        const bool selected = page_ == i;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Header,
                                  ImVec4(0.20f, 0.45f, 0.82f, 0.22f));
        }
        ImGui::SetCursorPosX(10.0f * scale);
        if (ImGui::Selectable(tabs[i], selected, 0,
                              ImVec2(150.0f * scale, 34.0f * scale))) {
            page_ = i;
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::SameLine();

    ImGui::BeginChild("##right", ImVec2(0, 0), false);
    const float footerHeight = 72.0f * scale;
    ImGui::BeginChild("##content", ImVec2(0, -footerHeight), false);
    ImGui::Dummy(ImVec2(0, 6.0f * scale));
    if (page_ == 0) {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f),
                           "输入法");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4.0f * scale));
        ImGui::TextUnformatted("默认输入法");
        ImGui::SetNextItemWidth(260.0f * scale);
        const char *currentLabel =
            inputMethodLabel(pinyin_.defaultInputMethod);
        if (ImGui::BeginCombo("##default-im", currentLabel)) {
            for (const auto &option : kInputMethodOptions) {
                const bool selected =
                    pinyin_.defaultInputMethod == option.id;
                if (ImGui::Selectable(option.label, selected)) {
                    pinyin_.defaultInputMethod = option.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(
            "支持拼音、双拼、五笔、仓颉等；选择后点“应用”。");
        ImGui::Dummy(ImVec2(0, 6.0f * scale));

        ImGui::TextUnformatted("双拼方案");
        ImGui::SetNextItemWidth(260.0f * scale);
        const struct {
            const char *id;
            const char *label;
        } shuangpinOptions[] = {
            {"Ziranma", "自然码"},       {"MS", "微软双拼"},
            {"Ziguang", "紫光双拼"},     {"ABC", "智能 ABC"},
            {"Zhongwenzhixing", "中文之星"},
            {"PinyinJiajia", "拼音加加"},
            {"Xiaohe", "小鹤双拼"},      {"GB", "国标双拼"},
        };
        const char *shuangpinLabel = "自然码";
        for (const auto &option : shuangpinOptions) {
            if (pinyin_.shuangpinProfile == option.id) {
                shuangpinLabel = option.label;
            }
        }
        if (ImGui::BeginCombo("##shuangpin-profile", shuangpinLabel)) {
            for (const auto &option : shuangpinOptions) {
                const bool selected =
                    pinyin_.shuangpinProfile == option.id;
                if (ImGui::Selectable(option.label, selected)) {
                    pinyin_.shuangpinProfile = option.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("仅在切换到双拼输入法时生效。");
        ImGui::Dummy(ImVec2(0, 8.0f * scale));

        ImGui::TextUnformatted("每页候选数");
        ImGui::SetNextItemWidth(180.0f * scale);
        ImGui::SliderInt("##page-size", &pinyin_.pageSize, 3, 10);

        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextUnformatted("模糊拼音");
        ImGui::TextDisabled("选中后，相近读音会一起参与匹配。");
        if (ImGui::BeginTable("##fuzzy", 3,
                              ImGuiTableFlags_SizingStretchSame)) {
            const struct {
                const char *label;
                bool *value;
            } options[] = {
                {"zh / z", &pinyin_.fuzzyZ},
                {"ch / c", &pinyin_.fuzzyC},
                {"sh / s", &pinyin_.fuzzyS},
                {"n / l", &pinyin_.fuzzyL},
                {"an / ang", &pinyin_.fuzzyAn},
                {"en / eng", &pinyin_.fuzzyEn},
                {"in / ing", &pinyin_.fuzzyIn},
                {"f / h", &pinyin_.fuzzyF},
            };
            for (const auto &option : options) {
                ImGui::TableNextColumn();
                ImGui::Checkbox(option.label, option.value);
            }
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextUnformatted("候选项与预编辑");
        if (ImGui::BeginTable("##pinyin-options", 2,
                              ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::Checkbox("英文候选", &pinyin_.spellEnabled);
            ImGui::TableNextColumn();
            ImGui::Checkbox("符号候选", &pinyin_.symbolsEnabled);
            ImGui::TableNextColumn();
            ImGui::Checkbox("拆字候选", &pinyin_.chaiziEnabled);
            ImGui::TableNextColumn();
            ImGui::Checkbox("扩展 B 区汉字", &pinyin_.extBEnabled);
            ImGui::TableNextColumn();
            ImGui::Checkbox("词语预测", &pinyin_.predictionEnabled);
            ImGui::TableNextColumn();
            ImGui::Checkbox("预编辑显示完整拼音",
                            &pinyin_.pinyinInPreedit);
            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextUnformatted("功能开关");
        if (ImGui::Button("中文标点")) {
            tray_.server.toggleAddon("punctuation");
            status_ = L"已切换中文标点";
        }
        ImGui::SameLine();
        if (ImGui::Button("全角/半角")) {
            tray_.server.toggleAddon("fullwidth");
            status_ = L"已切换全角/半角";
        }
        ImGui::SameLine();
        if (ImGui::Button("简体/繁体")) {
            tray_.server.toggleAddon("chttrans");
            status_ = L"已切换简体/繁体";
        }
        ImGui::TextDisabled(
            "快捷键：Ctrl+. 标点，Shift+Space 全角，Ctrl+Shift+F 简繁。");
    } else if (page_ == 1) {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f),
                           "剪贴板同步");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4.0f * scale));
        ImGui::Checkbox("启用剪贴板同步", &sync_.enabled);
        ImGui::TextDisabled(
            "使用 SyncClipboard HTTP 协议同步文本和图片；密码用 Windows DPAPI 保存。");
        ImGui::Dummy(ImVec2(0, 6.0f * scale));

        ImGui::TextUnformatted("最近复制");
        ImGui::BeginChild("##clipboard-history",
                          ImVec2(0, 160.0f * scale), true);
        const auto history = tray_.server.clipboardSync().history();
        if (history.empty()) {
            ImGui::TextDisabled("暂无历史");
        }
        for (size_t i = 0; i < history.size(); ++i) {
            std::string display = toUtf8(history[i].text);
            std::replace(display.begin(), display.end(), '\n', ' ');
            std::replace(display.begin(), display.end(), '\r', ' ');
            if (display.size() > 100) {
                display.resize(100);
                display += "...";
            }
            display += "##history";
            display += std::to_string(i);
            if (ImGui::Selectable(display.c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                tray_.server.clipboardSync().requestCopy(history[i].text);
                status_ = L"已复制到剪贴板";
            }
        }
        ImGui::EndChild();
        if (ImGui::Button("清空历史")) {
            tray_.server.clipboardSync().clearHistory();
            status_ = L"剪贴板历史已清空";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("双击条目复制");
        ImGui::Dummy(ImVec2(0, 6.0f * scale));

        ImGui::TextUnformatted("服务器地址");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##sync-url", "https://example.com",
                                 url_, sizeof(url_));
        ImGui::TextDisabled(
            "只允许 http/https，拒绝本机、环回、私有和保留地址。");
        ImGui::TextDisabled(
            "WebDAV 请填写到具体工作目录，例如 https://host/dav/clipboard；"
            "不要只填域名根目录。");
        ImGui::Dummy(ImVec2(0, 6.0f * scale));

        ImGui::TextUnformatted("用户名");
        ImGui::SetNextItemWidth(260.0f * scale);
        ImGui::InputText("##sync-user", username_, sizeof(username_));
        ImGui::SameLine();
        ImGui::TextUnformatted("密码");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f * scale);
        ImGui::InputText("##sync-pass", password_, sizeof(password_),
                         ImGuiInputTextFlags_Password);

        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        if (ImGui::Button("测试连接")) {
            test();
        }
        ImGui::SameLine();
        if (ImGui::Button("立即同步")) {
            syncNow();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("建议使用 HTTPS");
    } else if (page_ == 2) {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f),
                           "自定义短语");
        ImGui::Separator();
        ImGui::TextDisabled("拼音编码 + 输出文本，例如 nihao → 你好呀。");
        ImGui::BeginChild("##phrase-list", ImVec2(0, 210.0f * scale), true);
        for (size_t i = 0; i < phrases_.size(); ++i) {
            std::string display =
                phrases_[i].code + "   " + phrases_[i].text;
            if (display.size() > 100) {
                display.resize(100);
                display += "...";
            }
            display += "##phrase";
            display += std::to_string(i);
            if (ImGui::Selectable(display.c_str(),
                                  phraseSelected_ == static_cast<int>(i))) {
                phraseSelected_ = static_cast<int>(i);
                copyText(phraseCode_, sizeof(phraseCode_),
                         phrases_[i].code);
                copyText(phraseText_, sizeof(phraseText_),
                         phrases_[i].text);
            }
        }
        ImGui::EndChild();
        ImGui::SetNextItemWidth(180.0f * scale);
        ImGui::InputTextWithHint("##phrase-code", "编码",
                                 phraseCode_, sizeof(phraseCode_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f * scale);
        ImGui::InputTextWithHint("##phrase-text", "短语",
                                 phraseText_, sizeof(phraseText_));
        if (ImGui::Button("添加/更新")) {
            std::string code = phraseCode_;
            std::string text = phraseText_;
            std::transform(code.begin(), code.end(), code.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            if (code.empty() || text.empty()) {
                status_ = L"编码和短语不能为空";
            } else {
                if (phraseSelected_ >= 0 &&
                    phraseSelected_ < static_cast<int>(phrases_.size())) {
                    phrases_[phraseSelected_].code = code;
                    phrases_[phraseSelected_].text = text;
                } else {
                    phrases_.push_back({code, text});
                    phraseSelected_ =
                        static_cast<int>(phrases_.size()) - 1;
                }
                SettingsStore::savePhrases(phrases_);
                tray_.server.applySettings(pinyin_, sync_);
                status_ = L"短语已保存并生效";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("删除选中")) {
            if (phraseSelected_ >= 0 &&
                phraseSelected_ < static_cast<int>(phrases_.size())) {
                phrases_.erase(phrases_.begin() + phraseSelected_);
                phraseSelected_ = -1;
                phraseCode_[0] = '\0';
                phraseText_[0] = '\0';
                SettingsStore::savePhrases(phrases_);
                tray_.server.applySettings(pinyin_, sync_);
                status_ = L"短语已删除";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("导入短语")) {
            const std::wstring path =
                chooseTextFile(false, L"导入自定义短语");
            if (!path.empty()) {
                std::ifstream in(std::filesystem::path(path),
                                 std::ios::binary);
                std::string line;
                size_t imported = 0;
                while (std::getline(in, line)) {
                    if (!line.empty() && line.back() == '\r') {
                        line.pop_back();
                    }
                    const auto tab = line.find('\t');
                    if (tab == std::string::npos) {
                        continue;
                    }
                    std::string code = line.substr(0, tab);
                    const std::string text = line.substr(tab + 1);
                    std::transform(code.begin(), code.end(), code.begin(),
                                   [](unsigned char c) {
                                       return static_cast<char>(
                                           std::tolower(c));
                                   });
                    if (code.empty() || text.empty()) {
                        continue;
                    }
                    const auto existing = std::find_if(
                        phrases_.begin(), phrases_.end(),
                        [&code](const CustomPhrase &phrase) {
                            return phrase.code == code;
                        });
                    if (existing != phrases_.end()) {
                        existing->text = text;
                    } else {
                        phrases_.push_back({code, text});
                    }
                    ++imported;
                }
                SettingsStore::savePhrases(phrases_);
                tray_.server.applySettings(pinyin_, sync_);
                status_ = L"已导入 " + std::to_wstring(imported) +
                          L" 条短语";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("导出短语")) {
            const std::wstring path =
                chooseTextFile(true, L"导出自定义短语");
            if (!path.empty()) {
                std::ofstream out(std::filesystem::path(path),
                                  std::ios::binary | std::ios::trunc);
                for (const auto &phrase : phrases_) {
                    out << phrase.code << '\t' << phrase.text << '\n';
                }
                status_ = out ? L"短语已导出" : L"短语导出失败";
            }
        }
    } else if (page_ == 3) {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f),
                           "拼音用户词典");
        ImGui::Separator();
        ImGui::TextDisabled(
            "导入/导出 libime 用户词典文本；导入后会自动重启输入法。");
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        const auto userDict = SettingsStore::pinyinUserDictPath();
        const auto tool = moduleDirectory() / "libime_pinyindict.exe";
        if (ImGui::Button("导出用户词典")) {
            if (!std::filesystem::exists(userDict)) {
                status_ = L"用户词典为空";
            } else if (!std::filesystem::exists(tool)) {
                status_ = L"缺少 libime_pinyindict.exe";
            } else {
                const std::wstring path =
                    chooseTextFile(true, L"导出拼音用户词典");
                if (!path.empty()) {
                    const std::wstring command =
                        quotePath(tool) + L" -d " + quotePath(userDict) +
                        L" " + quotePath(std::filesystem::path(path));
                    status_ = runHiddenProcess(command, 60000)
                                  ? L"用户词典已导出"
                                  : L"用户词典导出失败";
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("导入用户词典")) {
            if (!std::filesystem::exists(tool)) {
                status_ = L"缺少 libime_pinyindict.exe";
            } else {
                const std::wstring path =
                    chooseTextFile(false, L"导入拼音用户词典");
                if (!path.empty()) {
                    const auto tempDir =
                        std::filesystem::temp_directory_path() / "TuxIME";
                    std::error_code error;
                    std::filesystem::create_directories(tempDir, error);
                    const auto imported = tempDir / "dict-import.dict";
                    const std::wstring command =
                        quotePath(tool) + L" " +
                        quotePath(std::filesystem::path(path)) + L" " +
                        quotePath(imported);
                    if (!runHiddenProcess(command, 120000)) {
                        status_ = L"词典文本转换失败";
                    } else {
                        const auto script = tempDir / "apply-dict.ps1";
                        const auto serverExe =
                            moduleDirectory() / "tuxime-server.exe";
                        auto psQuote = [](const std::filesystem::path &value) {
                            std::string text = toUtf8(value.wstring());
                            size_t pos = 0;
                            while ((pos = text.find('\'', pos)) !=
                                   std::string::npos) {
                                text.insert(pos, 1, '\'');
                                pos += 2;
                            }
                            return "'" + text + "'";
                        };
                        std::ofstream out(script, std::ios::binary |
                                                     std::ios::trunc);
                        out.write("\xEF\xBB\xBF", 3);
                        out << "param([int]$ServerPid)\n"
                            << "$deadline = (Get-Date).AddSeconds(25)\n"
                            << "$p = Get-Process -Id $ServerPid "
                               "-ErrorAction SilentlyContinue\n"
                            << "while ($p -and (Get-Date) -lt $deadline) { "
                               "Start-Sleep -Milliseconds 200; "
                               "$p = Get-Process -Id $ServerPid "
                               "-ErrorAction SilentlyContinue }\n"
                            << "if ($p) { exit 1 }\n"
                            << "if (Test-Path "
                            << psQuote(userDict)
                            << ") { Copy-Item -LiteralPath "
                            << psQuote(userDict) << " -Destination "
                            << psQuote(std::filesystem::path(
                                   userDict.wstring() + L".bak"))
                            << " -Force }\n"
                            << "Copy-Item -LiteralPath "
                            << psQuote(imported) << " -Destination "
                            << psQuote(userDict) << " -Force\n"
                            << "Start-Process -FilePath "
                            << psQuote(serverExe) << " -WorkingDirectory "
                            << psQuote(moduleDirectory()) << "\n";
                        out.close();
                        const std::wstring windir = [] {
                            wchar_t buffer[MAX_PATH] = {};
                            GetEnvironmentVariableW(L"WINDIR", buffer,
                                                    MAX_PATH);
                            return std::wstring(buffer);
                        }();
                        const std::wstring powershell =
                            windir +
                            L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
                        std::wstring helperCommand =
                            quotePath(std::filesystem::path(powershell)) +
                            L" -NoProfile -ExecutionPolicy Bypass "
                            L"-WindowStyle Hidden -File " +
                            quotePath(script) + L" -ServerPid " +
                            std::to_wstring(GetCurrentProcessId());
                        std::vector<wchar_t> commandLine(
                            helperCommand.begin(), helperCommand.end());
                        commandLine.push_back(L'\0');
                        STARTUPINFOW startup{};
                        startup.cb = sizeof(startup);
                        PROCESS_INFORMATION process{};
                        if (!CreateProcessW(
                                nullptr, commandLine.data(), nullptr, nullptr,
                                FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                                &startup, &process)) {
                            status_ = L"无法启动词典应用进程";
                        } else {
                            CloseHandle(process.hThread);
                            CloseHandle(process.hProcess);
                            status_ = L"正在应用词典并重启输入法...";
                            tray_.server.requestExit();
                        }
                    }
                }
            }
        }
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextWrapped(
            "文本格式：每行“词 拼音 频率”，例如“你好 ni'hao 0”。"
            "导入会替换当前用户词典，并自动备份为 user.dict.bak。");
    } else if (page_ == 4) {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f), "外观");
        ImGui::Separator();
        ImGui::TextDisabled("候选窗设置；保存后下一次按键生效。");
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextUnformatted("候选窗字号");
        ImGui::SetNextItemWidth(240.0f * scale);
        ImGui::SliderInt("##font-size", &appearance_.fontSize, 12, 24);
        ImGui::Checkbox("深色候选窗", &appearance_.dark);
        ImGui::Checkbox("纵向候选列表", &appearance_.vertical);
        ImGui::Checkbox("显示输入法/标点/简繁状态", &appearance_.showStatus);
        ImGui::Checkbox("自定义候选窗颜色", &appearance_.customColors);
        if (appearance_.customColors) {
            if (ImGui::Button("恢复默认配色")) {
                appearance_.customColors = false;
                tuxime::setDefaultColors(appearance_);
            }
            colorEdit("背景色##candidate-bg", appearance_.backgroundColor,
                      scale);
            colorEdit("文字色##candidate-text", appearance_.textColor, scale);
            colorEdit("选中背景色##candidate-selected-bg",
                      appearance_.selectedBackgroundColor, scale);
            colorEdit("选中文字色##candidate-selected-text",
                      appearance_.selectedTextColor, scale);
            colorEdit("边框色##candidate-border", appearance_.borderColor,
                      scale);
            colorEdit("状态栏文字色##candidate-status",
                      appearance_.statusColor, scale);
        }
        ImGui::Dummy(ImVec2(0, 10.0f * scale));
        if (ImGui::Button("保存外观")) {
            tuxime::saveAppearance(appearance_);
            status_ = L"外观已保存";
        }
        ImGui::SameLine();
        if (ImGui::Button("导出主题")) {
            const std::wstring path =
                chooseThemeFile(true, L"导出候选窗主题");
            if (!path.empty()) {
                tuxime::saveAppearance(appearance_);
                std::error_code error;
                std::filesystem::copy_file(
                    std::filesystem::path(tuxime::appearancePath()),
                    std::filesystem::path(path),
                    std::filesystem::copy_options::overwrite_existing,
                    error);
                status_ = error ? L"主题导出失败" : L"主题已导出";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("导入主题")) {
            const std::wstring path =
                chooseThemeFile(false, L"导入候选窗主题");
            if (!path.empty()) {
                std::error_code error;
                std::filesystem::copy_file(
                    std::filesystem::path(path),
                    std::filesystem::path(tuxime::appearancePath()),
                    std::filesystem::copy_options::overwrite_existing,
                    error);
                if (error) {
                    status_ = L"主题导入失败";
                } else {
                    appearance_ = tuxime::loadAppearance();
                    status_ = L"主题已导入";
                }
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.12f, 0.34f, 0.62f, 1.0f), "关于");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6.0f * scale));
        ImGui::TextUnformatted("Tux IME " TUX_IME_VERSION);
        ImGui::TextDisabled("Fcitx5 5.1.22 / libime 1.1.17 / "
                            "fcitx5-chinese-addons 5.1.15");
        ImGui::Dummy(ImVec2(0, 6.0f * scale));
        ImGui::TextUnformatted(
            "独立 TSF 输入法，不与小狼毫共用 CLSID、DLL 或管道。");
        ImGui::TextUnformatted(
            "项目地址：https://github.com/baipiao5555/tux-ime");
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        if (ImGui::Button("检查更新")) {
            const auto result = tuxime::checkLatestRelease();
            if (!result.ok) {
                status_ = result.error;
                updateVersion_.clear();
                updateUrl_.clear();
            } else if (result.hasUpdate) {
                updateVersion_ = result.version;
                updateUrl_ = result.url;
                status_ = L"发现新版本 " + result.version;
            } else {
                updateVersion_.clear();
                updateUrl_.clear();
                status_ = L"已经是最新版本";
            }
        }
        if (!updateUrl_.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("打开下载页")) {
                ShellExecuteW(nullptr, L"open", updateUrl_.c_str(), nullptr,
                              nullptr, SW_SHOWNORMAL);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", toUtf8(updateVersion_).c_str());
        }
    }

    ImGui::EndChild();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.35f, 0.40f, 0.48f, 1.0f), "%s",
                       toUtf8(status_).c_str());
    ImGui::SameLine();
    const float buttonWidth = 92.0f * scale;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth * 2.0f -
                         42.0f * scale);
    if (ImGui::Button("应用", ImVec2(buttonWidth, 34.0f * scale))) {
        apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("关闭", ImVec2(buttonWidth, 34.0f * scale))) {
        hide();
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::End();
}

void SettingsWindow::render() {
    if (!hwnd_ || !imgui_ || !device_ || !context_ || !swapChain_ ||
        !renderTarget_) {
        return;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    drawUi();
    ImGui::Render();

    const float clear[4] = {0.95f, 0.96f, 0.97f, 1.0f};
    context_->OMSetRenderTargets(1, &renderTarget_, nullptr);
    context_->ClearRenderTargetView(renderTarget_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT result = swapChain_->Present(1, 0);
    occluded_ = result == DXGI_STATUS_OCCLUDED;
}

LRESULT CALLBACK SettingsWindow::proc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
    auto *self = reinterpret_cast<SettingsWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam);
    }
    if (message == WM_CREATE) {
        self = reinterpret_cast<SettingsWindow *>(
            reinterpret_cast<CREATESTRUCTW *>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        return 0;
    }
    if (!self) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
    case WM_SIZE:
        if (self->device_ && self->swapChain_ && wParam != SIZE_MINIMIZED) {
            self->destroyRenderTarget();
            self->swapChain_->ResizeBuffers(
                0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            self->createRenderTarget();
        }
        return 0;
    case WM_TIMER:
        if (IsWindowVisible(hwnd)) {
            self->render();
        } else {
            KillTimer(hwnd, 1);
        }
        return 0;
    case WM_PAINT:
        ValidateRect(hwnd, nullptr);
        return 0;
    case WM_CLOSE:
        self->hide();
        return 0;
    case WM_DESTROY:
        self->hwnd_ = nullptr;
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
