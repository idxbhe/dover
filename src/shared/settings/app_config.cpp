#include "shared/settings/app_config.h"

namespace dover::shared {

AppConfig& GetAppConfig() {
    static AppConfig config;
    return config;
}

} // namespace dover::shared
