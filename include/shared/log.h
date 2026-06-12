#pragma once

namespace dover::shared {

void InitializeLogging();
void LogInfo(const char* format, ...);
void LogWarning(const char* format, ...);
void LogError(const char* format, ...);
void LogDebug(const char* format, ...);

} // namespace dover::shared
