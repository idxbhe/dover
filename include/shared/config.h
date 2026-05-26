#pragma once

#include <filesystem>
#include <string>

namespace dover::shared {

// ---- Generic INI read/write via Windows API (no extra dependencies) ----
// All functions take a wide-string path so filesystem::path.wstring() works directly.

std::string  ReadIniString(const std::filesystem::path& path, const char* section, const char* key, const char* default_val);
void         WriteIniString(const std::filesystem::path& path, const char* section, const char* key, const char* value);

bool         ReadIniBool(const std::filesystem::path& path, const char* section, const char* key, bool default_val);
void         WriteIniBool(const std::filesystem::path& path, const char* section, const char* key, bool value);

float        ReadIniFloat(const std::filesystem::path& path, const char* section, const char* key, float default_val);
void         WriteIniFloat(const std::filesystem::path& path, const char* section, const char* key, float value);

int          ReadIniInt(const std::filesystem::path& path, const char* section, const char* key, int default_val);
void         WriteIniInt(const std::filesystem::path& path, const char* section, const char* key, int value);

} // namespace dover::shared
