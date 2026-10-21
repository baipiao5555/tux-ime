#pragma once

#include <memory>
#include <string>

namespace tuxime {

class Server;

class TrayIcon {
public:
    struct Impl;

    explicit TrayIcon(Server &server);
    ~TrayIcon();

    TrayIcon(const TrayIcon &) = delete;
    TrayIcon &operator=(const TrayIcon &) = delete;

    void start();
    void stop();
    void setStatus(const std::wstring &status);
    void showSettings();

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace tuxime
