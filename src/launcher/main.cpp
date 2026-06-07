#include "shared/ipc.h"
#include "shared/storage.h"
#include "shared/log.h"
#include "shared/process.h"

#include "shared/notes/manager.h"
#include "shared/notes/layout.h"


#include "shared/settings/settings_window.h"
#include "shared/crosshair/crosshair_window.h"
#include "shared/input/input_window.h"
#include "shared/renderer.h"
#include "shared/assets/asset_storage.h"
#include "shared/theme.h"
#include "shared/icons.h"
#include "shared/game_storage.h"

#pragma comment(lib, "d3d11.lib")

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <commdlg.h>

namespace {
constexpr DWORD kOverlayReadyTimeoutMs = 10000;

std::filesystem::path ResolveWorkingDirectory(const std::wstring& application_path) {
  return std::filesystem::path(application_path).parent_path();
}

std::string WideToUTF8(const std::wstring& wstr);

struct GameConfig {
  std::wstring path;
  std::wstring name;
  std::string name_u8;
  std::string name_u8_sidebar;
};

std::vector<GameConfig> LoadSavedGames() {
  std::vector<GameConfig> games;
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return games;
  const auto config_path = root / L"launcher" / L"games.ini";
  if (!std::filesystem::exists(config_path)) return games;

  int count = GetPrivateProfileIntW(L"Games", L"Count", 0, config_path.c_str());
  for (int i = 0; i < count; ++i) {
    wchar_t key[32] = {};
    wsprintfW(key, L"Game%d", i);
    wchar_t path_buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Games", key, L"", path_buf, MAX_PATH, config_path.c_str());

    if (path_buf[0] != L'\0') {
      std::wstring path_str(path_buf);
      std::filesystem::path p(path_str);
      std::wstring wname = p.filename().wstring();
      std::string name_u8 = WideToUTF8(wname);
      std::string name_u8_sidebar = "  " + name_u8;
      games.push_back({path_str, wname, name_u8, name_u8_sidebar});
    }
  }
  return games;
}

void SaveGamePath(const std::wstring& path) {
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return;
  auto config_path = root / L"launcher" / L"games.ini";
  std::filesystem::create_directories(config_path.parent_path());

  auto games = LoadSavedGames();
  for (const auto& game : games) {
    if (game.path == path) return; // Already saved
  }

  int count = static_cast<int>(games.size());
  wchar_t key[32] = {};
  wsprintfW(key, L"Game%d", count);
  WritePrivateProfileStringW(L"Games", key, path.c_str(), config_path.c_str());
  WritePrivateProfileStringW(L"Games", L"Count", std::to_wstring(count + 1).c_str(), config_path.c_str());
}

HANDLE LaunchAndInject(const std::wstring& target_path, int argc, wchar_t** argv) {
  // Detect target process bitness
  DWORD binary_type = 0;
  bool is_64bit = true;
  if (GetBinaryTypeW(target_path.c_str(), &binary_type)) {
    is_64bit = (binary_type == SCS_64BIT_BINARY);
  }

  std::wstring command_line;
  if (argc >= 2) {
    command_line = dover::shared::BuildCommandLine(argc, argv);
  } else {
    command_line = L"\"" + target_path + L"\"";
  }

  if (!is_64bit) {
    const auto executable_dir = dover::shared::GetExecutableDirectory();
    const auto injector32_path = executable_dir / L"injector32.exe";

    if (!std::filesystem::exists(injector32_path)) {
      dover::shared::LogError("32-bit helper injector (injector32.exe) not found beside launcher.");
      return nullptr;
    }

    std::wstring full_command_line = L"\"" + injector32_path.wstring() + L"\" " + command_line;

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    dover::shared::LogInfo("32-bit target game detected. Spawning injector32.exe...");
    if (CreateProcessW(nullptr, full_command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      return pi.hProcess;
    }

    dover::shared::LogError("Failed to launch 32-bit helper injector.");
    return nullptr;
  }

  const auto overlay_path = dover::shared::GetOverlayDllPath();
  if (overlay_path.empty() || !std::filesystem::exists(overlay_path)) {
    dover::shared::LogError("64-bit Overlay DLL not found beside launcher.");
    return nullptr;
  }

  PROCESS_INFORMATION process_info{};
  const auto working_directory = ResolveWorkingDirectory(target_path);
  if (!dover::shared::StartSuspendedProcess(target_path, working_directory, command_line, process_info)) {
    DWORD err = GetLastError();
    if (err == ERROR_ELEVATION_REQUIRED) {
      dover::shared::LogError("Target game requires Administrator privileges. Restarting Launcher as Administrator...");
      
      wchar_t launcher_path[MAX_PATH];
      GetModuleFileNameW(nullptr, launcher_path, MAX_PATH);

      SHELLEXECUTEINFOW sei = { sizeof(sei) };
      sei.lpVerb = L"runas";
      sei.lpFile = launcher_path;
      std::wstring args = L"\"" + target_path + L"\"";
      sei.lpParameters = args.c_str();
      sei.nShow = SW_NORMAL;
      
      if (ShellExecuteExW(&sei)) {
        exit(0);
      } else {
        dover::shared::LogError("Failed to elevate Launcher privileges.");
      }
    } else {
      dover::shared::LogError("Failed to launch target process.");
    }
    return nullptr;
  }

  HANDLE ready_event = dover::shared::CreateOverlayReadyEvent(process_info.dwProcessId);
  if (!ready_event) {
    dover::shared::LogError("Failed to create overlay ready event.");
  }

  if (process_info.hProcess && process_info.hProcess != INVALID_HANDLE_VALUE) {
    if (!dover::shared::InjectDll(process_info.hProcess, overlay_path)) {
      dover::shared::LogError("DLL injection failed.");
      TerminateProcess(process_info.hProcess, 1);
      if (ready_event) {
        CloseHandle(ready_event);
      }
      if (process_info.hThread) {
        CloseHandle(process_info.hThread);
      }
      CloseHandle(process_info.hProcess);
      return nullptr;
    }
  } else {
    if (ready_event) {
      CloseHandle(ready_event);
    }
    return nullptr;
  }

  if (ready_event) {
    WaitForSingleObject(ready_event, kOverlayReadyTimeoutMs);
    CloseHandle(ready_event);
  }

  if (process_info.hThread && process_info.hThread != INVALID_HANDLE_VALUE) {
    ResumeThread(process_info.hThread);
    CloseHandle(process_info.hThread);
  }

  dover::shared::LogInfo("Launcher injected overlay and resumed target.");
  return process_info.hProcess;
}
} // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_GAME_EXITED = WM_APP + 2;
constexpr UINT ID_TRAY_ICON = 1;

namespace {
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
HWND g_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
bool g_has_tray_icon = false;

// Elite Frame Quota: Initial 5 frames to ensure UI renders correctly on startup
int g_FramesToRender = 5;

void CreateRenderTarget() {
  ID3D11Texture2D* pBackBuffer = nullptr;
  if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer))) && pBackBuffer) {
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
  }
}

void CleanupRenderTarget() {
  if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

bool CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;
  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
  if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
    return false;

  dover::shared::SetDx11Device(g_pd3dDevice);
  dover::shared::SetDx11Context(g_pd3dDeviceContext);
  dover::shared::assets::AssetStorage::Get().Initialize();

  CreateRenderTarget();
  return true;
}

void CleanupDeviceD3D() {
  CleanupRenderTarget();
  dover::shared::assets::AssetStorage::Get().Shutdown();
  dover::shared::SetDx11Context(nullptr);
  dover::shared::SetDx11Device(nullptr);
  if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
  if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
  if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void AddTrayIcon(HWND hwnd) {
  if (g_has_tray_icon) return;

  ZeroMemory(&g_nid, sizeof(g_nid));
  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = hwnd;
  g_nid.uID = ID_TRAY_ICON;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAYICON;
  g_nid.hIcon = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
  wcscpy_s(g_nid.szTip, L"Dover Launcher");

  Shell_NotifyIconW(NIM_ADD, &g_nid);
  g_has_tray_icon = true;
}

void RemoveTrayIcon() {
  if (!g_has_tray_icon) return;
  Shell_NotifyIconW(NIM_DELETE, &g_nid);
  g_has_tray_icon = false;
}

struct LauncherState {
  int x = 100;
  int y = 100;
  int width = 800;
  int height = 500;
  bool maximized = false;
};

LauncherState LoadLauncherState() {
  LauncherState state;
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return state;
  auto config_path = root / L"launcher" / L"games.ini";
  if (!std::filesystem::exists(config_path)) return state;

  state.x = GetPrivateProfileIntW(L"Launcher", L"x", 100, config_path.c_str());
  state.y = GetPrivateProfileIntW(L"Launcher", L"y", 100, config_path.c_str());
  state.width = GetPrivateProfileIntW(L"Launcher", L"width", 800, config_path.c_str());
  state.height = GetPrivateProfileIntW(L"Launcher", L"height", 500, config_path.c_str());
  state.maximized = GetPrivateProfileIntW(L"Launcher", L"maximized", 0, config_path.c_str()) != 0;

  return state;
}

void SaveLauncherState(HWND hwnd) {
  auto root = dover::shared::GetDoverRootDir();
  if (root.empty()) return;
  auto config_path = root / L"launcher" / L"games.ini";

  // Ensure directory exists for WritePrivateProfileString
  std::error_code ec;
  std::filesystem::create_directories(config_path.parent_path(), ec);

  WINDOWPLACEMENT wp = {};
  wp.length = sizeof(wp);
  if (GetWindowPlacement(hwnd, &wp)) {
    bool maximized = false;
    if (wp.showCmd == SW_SHOWMINIMIZED) {
      maximized = (wp.flags & WPF_RESTORETOMAXIMIZED) != 0;
    } else {
      maximized = (wp.showCmd == SW_SHOWMAXIMIZED);
    }

    int x = wp.rcNormalPosition.left;
    int y = wp.rcNormalPosition.top;
    int width = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
    int height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;

    WritePrivateProfileStringW(L"Launcher", L"x", std::to_wstring(x).c_str(), config_path.c_str());
    WritePrivateProfileStringW(L"Launcher", L"y", std::to_wstring(y).c_str(), config_path.c_str());
    WritePrivateProfileStringW(L"Launcher", L"width", std::to_wstring(width).c_str(), config_path.c_str());
    WritePrivateProfileStringW(L"Launcher", L"height", std::to_wstring(height).c_str(), config_path.c_str());
    WritePrivateProfileStringW(L"Launcher", L"maximized", std::to_wstring(maximized ? 1 : 0).c_str(), config_path.c_str());
  }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  // Let ImGui handle input first
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
    // We do NOT blindly reset g_FramesToRender here, because ImGui returns true
    // for many internal/noise messages that we don't care about when idle.
  }

  switch (msg) {
  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  case WM_MOUSEMOVE:
  case WM_MOUSEWHEEL:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_CHAR:
  case WM_SETFOCUS:
  case WM_KILLFOCUS:
  case WM_DISPLAYCHANGE:
  case WM_MOVE:
    g_FramesToRender = 5;
    break;

  case WM_PAINT:
    g_FramesToRender = 5;
    ::ValidateRect(hWnd, NULL); // Crucial: Stop OS from re-posting WM_PAINT
    break;

  case WM_NCCALCSIZE:
    return 0;

  case WM_GETMINMAXINFO: {
    MINMAXINFO* mmi = (MINMAXINFO*)lParam;
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(hMonitor, &mi)) {
      mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
      mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
      mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
      mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
    }
    return 0;
  }

  case WM_NCHITTEST: {
    POINT pt;
    pt.x = ((int)(short)LOWORD(lParam));
    pt.y = ((int)(short)HIWORD(lParam));
    RECT rc;
    GetWindowRect(hWnd, &rc);
    int x = pt.x - rc.left;
    int y = pt.y - rc.top;
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    bool is_maximized = IsZoomed(hWnd) != 0;
    if (!is_maximized) {
      constexpr int border_width = 8;

      bool left = x < border_width;
      bool right = x >= width - border_width;
      bool top = y < border_width;
      bool bottom = y >= height - border_width;

      if (top && left) return HTTOPLEFT;
      if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT;
      if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT;
      if (right) return HTRIGHT;
      if (top) return HTTOP;
      if (bottom) return HTBOTTOM;
    }

    if (y >= 0 && y < 35 && x < width - 110) {
      return HTCAPTION;
    }
    return HTCLIENT;
  }
  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) return 0;
    g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
    g_ResizeHeight = (UINT)HIWORD(lParam);
    g_FramesToRender = 5; // Force re-render on resize
    return 0;
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU) return 0; // Disable ALT application menu
    break;
  case WM_TRAYICON:
    if (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == WM_LBUTTONDOWN) {
      ShowWindow(hWnd, SW_SHOW);
      ShowWindow(hWnd, SW_RESTORE);
      SetForegroundWindow(hWnd);
      RemoveTrayIcon();
      g_FramesToRender = 5;
    }
    return 0;
  case WM_GAME_EXITED:
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
    RemoveTrayIcon();
    g_FramesToRender = 5;
    return 0;
  case WM_DESTROY:
    SaveLauncherState(hWnd);
    RemoveTrayIcon();
    ::PostQuitMessage(0);
    return 0;
  }
  return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

std::string WideToUTF8(const std::wstring& wstr) {
  if (wstr.empty()) return std::string();
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
  std::string strTo(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
  return strTo;
}

std::wstring UTF8ToWide(const std::string& str) {
  if (str.empty()) return std::wstring();
  int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
  std::wstring wstrTo(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
  return wstrTo;
}

std::wstring BrowseForExecutable(HWND owner_hwnd) {
  wchar_t file_name[MAX_PATH] = {};
  OPENFILENAMEW ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = owner_hwnd;
  ofn.lpstrFilter = L"Executable Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = file_name;
  ofn.nMaxFile = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

  if (GetOpenFileNameW(&ofn)) {
    return std::wstring(file_name);
  }
  return L"";
}

} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
  (void)hPrevInstance;
  (void)lpCmdLine;
  (void)nShowCmd;
  auto dover_root = dover::shared::GetDoverRootDir();
  if (dover_root.empty()) {
    dover::shared::LogError("Failed to locate Documents/Dover directory.");
    return 1;
  }
  std::filesystem::create_directories(dover_root / L"launcher");

  int argc;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 2) {
    std::wstring target_path = argv[1];
    SaveGamePath(target_path);
    HANDLE hProcess = LaunchAndInject(target_path, argc, argv);
    bool success = (hProcess != nullptr);
    if (hProcess) {
      WaitForSingleObject(hProcess, INFINITE);
      CloseHandle(hProcess);
    }
    LocalFree(argv);
    return success ? 0 : 1;
  }
  LocalFree(argv);

  WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, nullptr, nullptr, nullptr, nullptr, L"DoverLauncherClass", nullptr };
  ::RegisterClassExW(&wc);

  auto state = LoadLauncherState();
  
  // Ensure the window position is visible on at least one monitor
  POINT pt = { state.x + state.width / 2, state.y + state.height / 2 };
  HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
  if (!hMonitor) {
    state.x = 100;
    state.y = 100;
    state.width = 800;
    state.height = 500;
  }

  HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dover Launcher", WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU,
                              state.x, state.y, state.width, state.height, nullptr, nullptr, wc.hInstance, nullptr);
  g_hwnd = hwnd;

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ::ShowWindow(hwnd, state.maximized ? SW_SHOWMAXIMIZED : SW_SHOWDEFAULT);
  ::UpdateWindow(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport
  io.IniFilename = nullptr; // Disable ini generation for launcher

  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.00f);

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  dover::shared::SetupImGuiTheme();

  int active_game_idx = -1;
  enum class ActiveWindow {
    None,
    Notes,
    Settings,
    Crosshair,
    Controller
  };
  ActiveWindow active_win = ActiveWindow::None;
  int selected_game_idx = 0;

  bool done = false;
  auto games = LoadSavedGames();

  while (!done) {
    MSG msg;
    // 1. Block and wait for ANY message (including viewport spam)
    if (::GetMessageW(&msg, nullptr, 0U, 0U)) {
      ::TranslateMessage(&msg);
      ::DispatchMessageW(&msg);
      
      // Drain remaining messages in queue
      while (::PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            done = true;
            break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
      }
    } else {
      done = true;
    }

    if (done) break;

    // =========================================================================
    // THE ULTIMATE IDLE GATE: 0.00% CPU
    // If no quota left, we do ABSOLUTELY NOTHING.
    // =========================================================================
    if (g_FramesToRender <= 0) {
      continue;
    }

    // Skip entire render if window is hidden (only happens during Tray cleanup frames)
    if (!::IsWindowVisible(hwnd)) {
        // Still decrement quota to eventually hit the gate above
        g_FramesToRender--;
        
        // Handle ImGui lifecycle even if hidden for cleanup (e.g. viewports)
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::EndFrame(); // No actual UI drawing
        ImGui::Render();
        
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        
        ::Sleep(16); // Don't burn CPU during background cleanup
        continue;
    }

    // Consume 1 quota for this frame
    g_FramesToRender--;

    if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
      g_ResizeWidth = g_ResizeHeight = 0;
      CreateRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(main_viewport->Pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(main_viewport->Size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Launcher UI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    // Side panel
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.07f, 0.09f, 1.0f));
    ImGui::BeginChild("Sidebar", ImVec2(220, 0), false, ImGuiWindowFlags_NoScrollbar);
    
    // Logo / Title area
    ImGui::SetCursorPos(ImVec2(20.0f, 20.0f));
    ImGui::PushFont(dover::shared::g_fonts_preview_h3[1]);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "DOVER");
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(20.0f, 40.0f));
    ImGui::PushFont(dover::shared::g_font_panel);
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "LAUNCHER");
    ImGui::PopFont();
    ImGui::SetCursorPosY(80.0f);

    // List of games
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    for (size_t i = 0; i < games.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        bool is_selected = (selected_game_idx == static_cast<int>(i));
        if (is_selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        }
        
        ImGui::SetCursorPosX(10.0f);
        
        ImVec2 button_pos = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::Button("##game_btn", ImVec2(200.0f, 40.0f));
        
        // Render Icon and Text over the button
        ImGui::SetCursorScreenPos(ImVec2(button_pos.x + 12.0f, button_pos.y + 8.0f));
        void* icon_srv = dover::shared::assets::AssetStorage::Get().GetIconForGame(games[i].path);
        if (icon_srv) {
            ImGui::Image((ImTextureID)icon_srv, ImVec2(24.0f, 24.0f));
            ImGui::SameLine(48.0f);
        } else {
            // Fallback padding if icon extraction fails
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 36.0f);
        }
        
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
        ImGui::Text("%s", games[i].name_u8.c_str());

        if (clicked) {
            selected_game_idx = static_cast<int>(i);
        }
        
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    // Add new game at the bottom
    ImGui::SetCursorPos(ImVec2(10.0f, ImGui::GetWindowHeight() - 50.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    if (ImGui::Button("+ ADD NEW GAME", ImVec2(200.0f, 40.0f))) {
      std::wstring selected_path = BrowseForExecutable(hwnd);
      if (!selected_path.empty()) {
        SaveGamePath(selected_path);
        games = LoadSavedGames(); // Reload list
        selected_game_idx = static_cast<int>(games.size()) - 1;
      }
    }
    ImGui::PopStyleColor(3);

    ImGui::EndChild();
    ImGui::PopStyleColor(); // End Sidebar

    ImGui::SameLine(0, 0);

    // Main Content Panel
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.11f, 0.13f, 1.0f));
    ImGui::BeginChild("MainContent", ImVec2(0, 0), false, ImGuiWindowFlags_None);

    // Window controls at top right (styled like overlay-settings window decoration)
    auto DrawTitlebarButton = [&](const char* icon, float same_line_pos, const char* tooltip) -> bool {
      ImGui::SameLine();
      ImGui::SetCursorPosX(same_line_pos);
      ImGui::SetCursorPosY(4.0f); // 4px padding from top to fit inside the 35px titlebar
      
      ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));
      
      bool clicked = ImGui::Button(icon);
      
      ImGui::PopStyleColor(4);
      
      ImVec2 min_p = ImGui::GetItemRectMin();
      ImVec2 max_p = ImGui::GetItemRectMax();
      ImVec2 center = ImVec2(min_p.x + (max_p.x - min_p.x) * 0.5f, min_p.y + (max_p.y - min_p.y) * 0.5f);
      
      bool hovered = ImGui::IsItemHovered();
      bool active = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
      bool is_close = (strcmp(icon, ICON_WINDOW_CLOSE) == 0);
      
      ImVec4 bg_color;
      ImVec4 border_color;
      
      if (is_close) {
          if (active) {
              bg_color = ImVec4(0.650f, 0.100f, 0.100f, 1.00f);
              border_color = ImVec4(0.500f, 0.050f, 0.050f, 1.00f);
          } else if (hovered) {
              bg_color = ImVec4(0.850f, 0.150f, 0.150f, 1.00f);
              border_color = ImVec4(0.700f, 0.100f, 0.100f, 1.00f);
          } else {
              bg_color = ImVec4(0.750f, 0.150f, 0.150f, 0.40f);
              border_color = ImVec4(0.600f, 0.100f, 0.100f, 0.50f);
          }
      } else {
          if (active) {
              bg_color = ImVec4(0.280f, 0.370f, 0.500f, 1.00f);
              border_color = ImVec4(0.210f, 0.280f, 0.380f, 1.00f);
          } else if (hovered) {
              bg_color = ImVec4(0.230f, 0.300f, 0.410f, 1.00f);
              border_color = ImVec4(0.170f, 0.220f, 0.300f, 1.00f);
          } else {
              bg_color = ImVec4(0.170f, 0.200f, 0.246f, 1.00f);
              border_color = ImVec4(0.120f, 0.141f, 0.174f, 1.00f);
          }
      }
      
      ImU32 bg_col32 = ImGui::ColorConvertFloat4ToU32(bg_color);
      ImU32 border_col32 = ImGui::ColorConvertFloat4ToU32(border_color);
      ImU32 text_col32 = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
      
      ImGui::GetWindowDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f);
      
      ImVec2 mid_p = ImVec2(max_p.x, min_p.y + (max_p.y - min_p.y) * 0.5f);
      ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
      ImGui::GetWindowDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
      
      ImGui::GetWindowDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 0, 1.0f);
      
      if (dover::shared::g_font_gui) ImGui::PushFont(dover::shared::g_font_gui);
      ImVec2 text_size = ImGui::CalcTextSize(icon);
      ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
      ImGui::GetWindowDrawList()->AddText(text_pos, text_col32, icon);
      if (dover::shared::g_font_gui) ImGui::PopFont();
      
      if (hovered && tooltip) {
          ImGui::SetTooltip(tooltip);
      }
      
      return clicked;
    };

    float right_boundary = ImGui::GetWindowWidth();
    if (DrawTitlebarButton(ICON_WINDOW_CLOSE, right_boundary - 34.0f, "Close")) {
      PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }

    bool is_max = IsZoomed(g_hwnd) != 0;
    const char* max_icon = is_max ? ICON_WINDOW_WINDOWED : ICON_WINDOW_MAXIMIZE;
    const char* max_tooltip = is_max ? "Restore" : "Maximize";
    if (DrawTitlebarButton(max_icon, right_boundary - 60.0f, max_tooltip)) {
      if (is_max) {
        ShowWindow(g_hwnd, SW_RESTORE);
      } else {
        ShowWindow(g_hwnd, SW_MAXIMIZE);
      }
    }

    if (DrawTitlebarButton(ICON_WINDOW_MINIMIZE, right_boundary - 86.0f, "Minimize")) {
      ShowWindow(g_hwnd, SW_MINIMIZE);
    }

    if (games.empty() || selected_game_idx < 0 || selected_game_idx >= static_cast<int>(games.size())) {
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() / 2 - 100, ImGui::GetWindowHeight() / 2 - 20));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No game selected. Add a game to begin.");
    } else {
        auto& game = games[selected_game_idx];
        
        ImGui::SetCursorPos(ImVec2(40.0f, 40.0f));
        ImGui::PushFont(dover::shared::g_fonts_preview_h2[1]);
        ImGui::Text("%s", game.name_u8.c_str());
        ImGui::PopFont();
        
        ImGui::SetCursorPos(ImVec2(40.0f, 85.0f));
        ImVec2 launch_p = ImGui::GetCursorScreenPos();
        ImVec2 launch_size = ImVec2(200.0f, 50.0f);
        
        bool is_active_game = (active_game_idx == selected_game_idx);
        
        // Custom Styled Button with Gradient
        ImGui::PushID("launch_btn_grad");
        bool launch_clicked = ImGui::Button("##launch", launch_size);
        bool launch_hovered = ImGui::IsItemHovered();
        bool launch_active = ImGui::IsItemActive();
        
        ImVec4 col_grad_top = launch_active ? ImVec4(0.12f, 0.55f, 0.32f, 1.0f) : (launch_hovered ? ImVec4(0.20f, 0.75f, 0.45f, 1.0f) : ImVec4(0.18f, 0.68f, 0.38f, 1.0f));
        ImVec4 col_grad_bot = launch_active ? ImVec4(0.08f, 0.45f, 0.25f, 1.0f) : (launch_hovered ? ImVec4(0.15f, 0.65f, 0.38f, 1.0f) : ImVec4(0.12f, 0.58f, 0.32f, 1.0f));
        
        ImU32 c_top = ImGui::ColorConvertFloat4ToU32(col_grad_top);
        ImU32 c_bot = ImGui::ColorConvertFloat4ToU32(col_grad_bot);
        
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(launch_p, ImVec2(launch_p.x + launch_size.x, launch_p.y + launch_size.y), c_top, c_top, c_bot, c_bot);
        dl->AddRect(launch_p, ImVec2(launch_p.x + launch_size.x, launch_p.y + launch_size.y), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,0.05f)), 0.0f);
        
        // Glossy highlight
        dl->AddRectFilled(launch_p, ImVec2(launch_p.x + launch_size.x, launch_p.y + launch_size.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(1,1,1,0.03f)));

        ImGui::PushFont(dover::shared::g_fonts_preview_bold[1]);
        const char* launch_label = is_active_game ? "RUNNING" : "LAUNCH GAME";
        ImVec2 l_text_size = ImGui::CalcTextSize(launch_label);
        dl->AddText(ImVec2(launch_p.x + (launch_size.x - l_text_size.x) * 0.5f, launch_p.y + (launch_size.y - l_text_size.y) * 0.5f), IM_COL32_WHITE, launch_label);
        ImGui::PopFont();
        ImGui::PopID();

        if (launch_clicked && !is_active_game) {
            HANDLE hProcess = LaunchAndInject(game.path, 0, nullptr);
            if (hProcess) {
              // Graceful Viewport Destruction:
              // Cleanly close active modules so ImGui drops their windows.
              if (active_win == ActiveWindow::Notes) dover::shared::notes::ShutdownNotesManager();
              active_win = ActiveWindow::None;
              active_game_idx = -1;
              
              ShowWindow(hwnd, SW_HIDE);
              AddTrayIcon(hwnd);
              g_FramesToRender = 5; // Final frames to naturally destroy all viewports

              std::thread([hwnd, hProcess]() {
                WaitForSingleObject(hProcess, INFINITE);
                CloseHandle(hProcess);
                PostMessageW(hwnd, WM_GAME_EXITED, 0, 0);
              }).detach();
            }
        }

        ImGui::SetCursorPos(ImVec2(40.0f, 160.0f));
        ImGui::PushFont(dover::shared::g_fonts_preview_bold[0]);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "OVERLAY MODULES");
        ImGui::PopFont();
        
        ImGui::SetCursorPos(ImVec2(40.0f, 190.0f));
        
        // Modules Grid
        float card_width = 160.0f;
        float card_height = 100.0f;
        
        auto draw_module_card = [&](const char* id, const char* title, const char* icon, ActiveWindow win_type) {
            bool is_open = (active_game_idx == selected_game_idx && active_win == win_type);
            ImGui::PushID(id);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, is_open ? ImVec4(0.15f, 0.2f, 0.3f, 1.0f) : ImVec4(0.15f, 0.16f, 0.18f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
            ImGui::BeginChild("Card", ImVec2(card_width, card_height), true, ImGuiWindowFlags_NoScrollbar);
            
            ImGui::SetCursorPos(ImVec2(15.0f, 15.0f));
            ImGui::PushFont(dover::shared::g_font_panel); // panel font has icons
            ImGui::TextColored(is_open ? ImVec4(0.5f, 0.8f, 1.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", icon);
            ImGui::PopFont();
            
            ImGui::SetCursorPos(ImVec2(15.0f, 45.0f));
            ImGui::PushFont(dover::shared::g_fonts_preview_bold[0]);
            ImGui::Text("%s", title);
            ImGui::PopFont();
            
            ImGui::SetCursorPos(ImVec2(15.0f, 65.0f));
            ImGui::TextColored(is_open ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f) : ImVec4(0.5f, 0.5f, 0.5f, 1.0f), is_open ? "Configuring" : "Configure");
            
            // Invisible button to capture clicks over the whole card
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.05f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.1f));
            if (ImGui::Button("##card_btn", ImVec2(card_width, card_height))) {
                if (is_open) {
                    // Focus or close? Let's just do nothing.
                } else {
                    if (active_win == ActiveWindow::Notes) dover::shared::notes::ShutdownNotesManager();
                    dover::shared::GameStorage::Get().Initialize(game.name);
                    
                    if (win_type == ActiveWindow::Notes) {
                        dover::shared::notes::InitializeNotesManager(dover::shared::GameStorage::Get().GetNotesDir());
                        auto& win = dover::shared::notes::GetNotesWindow();
                        win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                        win.SetOpenDirect(true);
                    } else if (win_type == ActiveWindow::Settings) {
                        auto& win = dover::shared::settings::GetSettingsWindow();
                        win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                        win.SetOpenDirect(true);
                    } else if (win_type == ActiveWindow::Crosshair) {
                        auto& win = dover::shared::crosshair::GetCrosshairWindow();
                        win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                        win.SetOpenDirect(true);
                    } else if (win_type == ActiveWindow::Controller) {
                        auto& win = dover::shared::input::GetInputWindow();
                        win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                        win.SetOpenDirect(true);
                    }
                    
                    active_game_idx = selected_game_idx;
                    active_win = win_type;
                }
            }
            ImGui::PopStyleColor(3);
            
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
        };

        draw_module_card("Notes", "Notes Editor", "\xef\x84\xa9", ActiveWindow::Notes);
        ImGui::SameLine(0, 15.0f);
        draw_module_card("Crosshair", "Smart Reticle", "\xef\x84\xaa", ActiveWindow::Crosshair);
        ImGui::SameLine(0, 15.0f);
        draw_module_card("Controller", "Input Map", "\xef\x84\xa8", ActiveWindow::Controller);
        
        ImGui::SetCursorPos(ImVec2(40.0f, 305.0f));
        draw_module_card("Settings", "App Config", "\xef\x84\xab", ActiveWindow::Settings);
        
        // Remove button
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 100.0f, ImGui::GetWindowHeight() - 40.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.2f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.1f, 0.1f, 0.6f));
        if (ImGui::Button("Remove", ImVec2(80.0f, 25.0f))) {
            // Remove from config by fully rebuilding the list
            auto root = dover::shared::GetDoverRootDir();
            if (!root.empty()) {
                auto config_path = root / L"launcher" / L"games.ini";

                // Clear the entire [Games] section in the ini file to remove holes
                WritePrivateProfileStringW(L"Games", nullptr, nullptr, config_path.c_str());

                // Remove game from local list
                games.erase(games.begin() + selected_game_idx);

                // Rewrite the contiguous list
                for (size_t i = 0; i < games.size(); ++i) {
                    wchar_t key[32] = {};
                    wsprintfW(key, L"Game%zu", i);
                    WritePrivateProfileStringW(L"Games", key, games[i].path.c_str(), config_path.c_str());
                }
                WritePrivateProfileStringW(L"Games", L"Count", std::to_wstring(games.size()).c_str(), config_path.c_str());

                // Fix up active_game_idx so UI remains correct
                if (active_game_idx == selected_game_idx) {
                    active_game_idx = -1;
                    active_win = ActiveWindow::None;
                } else if (active_game_idx > selected_game_idx) {
                    active_game_idx--;
                }

                selected_game_idx = 0;
            }
        }
        ImGui::PopStyleColor(3);
    }
    
    ImGui::EndChild();
    ImGui::PopStyleColor();
    
    ImGui::End();

    if (active_win == ActiveWindow::Notes) {
        auto& notes_win = dover::shared::notes::GetNotesWindow();
        if (notes_win.IsOpen()) {
            notes_win.Render(true);
        } else {
            dover::shared::notes::ShutdownNotesManager();
            active_win = ActiveWindow::None;
            active_game_idx = -1;
        }
    } else if (active_win == ActiveWindow::Settings) {
        auto& settings_win = dover::shared::settings::GetSettingsWindow();
        if (settings_win.IsOpen()) {
            settings_win.Render(true);
        } else {
            active_win = ActiveWindow::None;
            active_game_idx = -1;
        }
    } else if (active_win == ActiveWindow::Crosshair) {
        auto& crosshair_win = dover::shared::crosshair::GetCrosshairWindow();
        if (crosshair_win.IsOpen()) {
            crosshair_win.Render(true);
        } else {
            active_win = ActiveWindow::None;
            active_game_idx = -1;
        }
    } else if (active_win == ActiveWindow::Controller) {
        auto& controller_win = dover::shared::input::GetInputWindow();
        if (controller_win.IsOpen()) {
            controller_win.Render(true);
        } else {
            active_win = ActiveWindow::None;
            active_game_idx = -1;
        }
    }

    ImGui::Render();
    const float clear_color_with_alpha[4] = { 0.08f, 0.09f, 0.11f, 1.00f };
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    // Only present if the main window is visible to avoid DXGI overhead
    if (::IsWindowVisible(hwnd) && !::IsIconic(hwnd)) {
        g_pSwapChain->Present(1, 0); // Present with vsync
    } else {
        ::Sleep(16); // Throttle fast-forward frames when hidden
    }
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  CleanupDeviceD3D();
  ::DestroyWindow(hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  return 0;
}
