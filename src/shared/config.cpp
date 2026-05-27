#include "shared/config.h"

#include <windows.h>
#include <cstdio>

namespace dover::shared {

// Convert filesystem::path to wstring for Windows API calls
static std::wstring ToWide(const std::filesystem::path& path) {
    return path.wstring();
}

std::string ReadIniString(const std::filesystem::path& path, const char* section, const char* key, const char* default_val) {
    char buf[512] = {};
    std::wstring wide_path = ToWide(path);
    GetPrivateProfileStringA(section, key, default_val, buf, sizeof(buf), nullptr);
    // Windows API needs the file path as the last argument
    // Use WritePrivateProfileStringA with wide path via the W variant
    wchar_t wbuf[512] = {};
    wchar_t wsec[128], wkey[128], wdef[512];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsec, 128);
    MultiByteToWideChar(CP_UTF8, 0, key,     -1, wkey, 128);
    MultiByteToWideChar(CP_UTF8, 0, default_val, -1, wdef, 512);
    GetPrivateProfileStringW(wsec, wkey, wdef, wbuf, 512, wide_path.c_str());
    // Convert result back to UTF-8
    char result[512] = {};
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, result, 512, nullptr, nullptr);
    return result;
}

void ReadIniString(const std::filesystem::path& path, const char* section, const char* key, const char* default_val, char* out_buf, size_t out_buf_size) {
    if (!out_buf || out_buf_size == 0) return;
    std::wstring wide_path = ToWide(path);
    wchar_t wbuf[512] = {};
    wchar_t wsec[128], wkey[128], wdef[512];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsec, 128);
    MultiByteToWideChar(CP_UTF8, 0, key,     -1, wkey, 128);
    MultiByteToWideChar(CP_UTF8, 0, default_val, -1, wdef, 512);
    GetPrivateProfileStringW(wsec, wkey, wdef, wbuf, 512, wide_path.c_str());
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out_buf, static_cast<int>(out_buf_size), nullptr, nullptr);
    out_buf[out_buf_size - 1] = '\0';
}

void WriteIniString(const std::filesystem::path& path, const char* section, const char* key, const char* value) {
    std::wstring wide_path = ToWide(path);
    wchar_t wsec[128], wkey[128], wval[512];
    MultiByteToWideChar(CP_UTF8, 0, section, -1, wsec, 128);
    MultiByteToWideChar(CP_UTF8, 0, key,     -1, wkey, 128);
    MultiByteToWideChar(CP_UTF8, 0, value,   -1, wval, 512);
    WritePrivateProfileStringW(wsec, wkey, wval, wide_path.c_str());
}

bool ReadIniBool(const std::filesystem::path& path, const char* section, const char* key, bool default_val) {
    const char* def = default_val ? "1" : "0";
    std::string val = ReadIniString(path, section, key, def);
    return (val == "1" || val == "true" || val == "yes");
}

void WriteIniBool(const std::filesystem::path& path, const char* section, const char* key, bool value) {
    WriteIniString(path, section, key, value ? "1" : "0");
}

float ReadIniFloat(const std::filesystem::path& path, const char* section, const char* key, float default_val) {
    char def_buf[32];
    snprintf(def_buf, sizeof(def_buf), "%f", default_val);
    std::string val = ReadIniString(path, section, key, def_buf);
    try { return std::stof(val); } catch (...) { return default_val; }
}

void WriteIniFloat(const std::filesystem::path& path, const char* section, const char* key, float value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%f", value);
    WriteIniString(path, section, key, buf);
}

int ReadIniInt(const std::filesystem::path& path, const char* section, const char* key, int default_val) {
    char def_buf[32];
    snprintf(def_buf, sizeof(def_buf), "%d", default_val);
    std::string val = ReadIniString(path, section, key, def_buf);
    try { return std::stoi(val); } catch (...) { return default_val; }
}

void WriteIniInt(const std::filesystem::path& path, const char* section, const char* key, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    WriteIniString(path, section, key, buf);
}

} // namespace dover::shared
