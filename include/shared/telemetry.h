#pragma once
#include <cstdint>

namespace dover::shared::telemetry {
    struct TelemetryData {
        float fps = 0.0f;
        float cpu_usage = 0.0f;
        double ram_used_gb = 0.0f;
        double ram_total_gb = 0.0f;
    };

    void Update();
    const TelemetryData& GetData();
}
