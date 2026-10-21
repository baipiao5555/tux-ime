/*
 * Minimal Notifications addon for Windows.
 *
 * Upstream's notifications module is DBus-only. Several input method addons
 * declare a dependency on it even though they only use it for optional tips,
 * so provide the same exported interface without a DBus connection.
 */
#include <cstdint>
#include <string>
#include <vector>
#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/instance.h>
#include "notifications_public.h"

namespace fcitx::notifications {

class Notifications final : public AddonInstance {
public:
    explicit Notifications(Instance *instance) : instance_(instance) {}

    uint32_t sendNotification(
        const std::string &appName, uint32_t replaceId,
        const std::string &appIcon, const std::string &summary,
        const std::string &body, const std::vector<std::string> &actions,
        int32_t timeout, NotificationActionCallback actionCallback,
        NotificationClosedCallback closedCallback) {
        FCITX_INFO() << "notification: " << appName << ": " << summary << " "
                     << body;
        return 0;
    }

    void showTip(const std::string &tipId, const std::string &appName,
                 const std::string &appIcon, const std::string &summary,
                 const std::string &body, int32_t timeout) {
        FCITX_INFO() << "tip: " << tipId << ": " << summary << " " << body;
    }

    void closeNotification(uint64_t internalId) {}

private:
    FCITX_ADDON_EXPORT_FUNCTION(Notifications, sendNotification);
    FCITX_ADDON_EXPORT_FUNCTION(Notifications, showTip);
    FCITX_ADDON_EXPORT_FUNCTION(Notifications, closeNotification);

    Instance *instance_;
};

class NotificationsFactory final : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        return new Notifications(manager->instance());
    }
};

} // namespace fcitx::notifications

FCITX_ADDON_FACTORY_V2(notifications,
                       fcitx::notifications::NotificationsFactory)
