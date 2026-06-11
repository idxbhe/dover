#include "shared/engine_quirks.h"
#include <windows.h>
#include <algorithm>
#include <vector>
#include <string_view>

namespace dover::shared {

static EngineQuirks g_quirks;

static std::wstring GetProcessName() {
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) return L"";
    
    std::wstring full_path(path);
    size_t last_slash = full_path.find_last_of(L"\\/");
    if (last_slash == std::wstring::npos) return full_path;
    
    return full_path.substr(last_slash + 1);
}

void InitializeEngineQuirks() {
    std::wstring process_name = GetProcessName();
    if (process_name.empty()) return;

    // Convert to lowercase for robust matching
    std::transform(process_name.begin(), process_name.end(), process_name.begin(), ::towlower);

    // Static lookup table for known problematic engines
    if (process_name == L"tesv.exe") {
        // Skyrim LE (DirectX 9) - Requires explicit backbuffer target to avoid double rendering/blur
        g_quirks.dx9_force_backbuffer_render = true;
    }
    else if (process_name == L"skyrim.exe" || process_name == L"skyrimse.exe") {
        // Skyrim SE/AE (DirectX 11) - DX11 hook already handles swapchain dimensions, 
        // but we keep this here for future DX11 specific quirks.
    }
}

const EngineQuirks& GetEngineQuirks() {
    return g_quirks;
}

} // namespace dover::shared
