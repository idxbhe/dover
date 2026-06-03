#include "shared/ipc.h"
#include "shared/storage.h"
#include "shared/log.h"
#include "shared/process.h"

#include "shared/notes/manager.h"
#include "shared/notes/layout.h"
#include "shared/settings/settings_window.h"
#include "shared/theme.h"
#include "shared/game_storage.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
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

struct GameConfig {
  std::wstring path;
  std::wstring name;
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
      games.push_back({path_str, p.filename().wstring()});
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

  if (!dover::shared::InjectDll(process_info.hProcess, overlay_path)) {
    dover::shared::LogError("DLL injection failed.");
    TerminateProcess(process_info.hProcess, 1);
    if (ready_event) {
      CloseHandle(ready_event);
    }
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return nullptr;
  }

  if (ready_event) {
    WaitForSingleObject(ready_event, kOverlayReadyTimeoutMs);
    CloseHandle(ready_event);
  }

  ResumeThread(process_info.hThread);
  CloseHandle(process_info.hThread);

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

void CreateRenderTarget() {
  ID3D11Texture2D* pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
  pBackBuffer->Release();
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

  CreateRenderTarget();
  return true;
}

void CleanupDeviceD3D() {
  CleanupRenderTarget();
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

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;

  switch (msg) {
  case WM_NCHITTEST: {
    LRESULT result = ::DefWindowProcW(hWnd, msg, wParam, lParam);
    if (result == HTCLIENT) {
      POINT pt;
      pt.x = ((int)(short)LOWORD(lParam));
      pt.y = ((int)(short)HIWORD(lParam));
      RECT rc;
      GetWindowRect(hWnd, &rc);
      pt.x -= rc.left;
      pt.y -= rc.top;
      int width = rc.right - rc.left;
      if (pt.y >= 0 && pt.y < 35 && pt.x < width - 80) {
        return HTCAPTION;
      }
    }
    return result;
  }
  case WM_SIZE:
    if (wParam == SIZE_MINIMIZED) return 0;
    g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
    g_ResizeHeight = (UINT)HIWORD(lParam);
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
    }
    return 0;
  case WM_GAME_EXITED:
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
    RemoveTrayIcon();
    return 0;
  case WM_DESTROY:
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

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR /*pCmdLine*/, int /*nCmdShow*/) {
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
  HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dover Launcher", WS_POPUP, 100, 100, 600, 400, nullptr, nullptr, wc.hInstance, nullptr);
  g_hwnd = hwnd;

  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ::ShowWindow(hwnd, SW_SHOWDEFAULT);
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
  enum class ActiveWindow { None, Notes, Settings };
  ActiveWindow active_win = ActiveWindow::None;

  bool done = false;
  auto games = LoadSavedGames();
  char input_buffer[MAX_PATH] = {};

  while (!done) {
    MSG msg;
    while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT) done = true;
    }
    if (done) break;

    if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
      CleanupRenderTarget();
      g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
      g_ResizeWidth = g_ResizeHeight = 0;
      CreateRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Launcher UI", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.13f, 0.15f, 1.00f));
    ImGui::BeginChild("TitleBar", ImVec2(0, 35), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(10.0f, 10.0f));
    ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.00f, 1.00f), "DOVER LAUNCHER");

    float button_width = 40.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - button_width * 2);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
    ImGui::SetCursorPosY(0);
    if (ImGui::Button("-", ImVec2(button_width, 35))) { ShowWindow(g_hwnd, SW_MINIMIZE); }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.1f, 0.1f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("X", ImVec2(button_width, 35))) { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    ImGui::BeginChild("Content", ImVec2(0, 0), false, ImGuiWindowFlags_None);

        ImGui::Text("Saved Games:");
        for (size_t i = 0; i < games.size(); ++i) {
          ImGui::PushID(static_cast<int>(i));
          ImGui::BeginDisabled(active_game_idx != -1 && active_game_idx != static_cast<int>(i));

          std::string name_u8 = WideToUTF8(games[i].name);
          if (ImGui::Button(name_u8.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 230.0f, 30.0f))) {
            HANDLE hProcess = LaunchAndInject(games[i].path, 0, nullptr);
            if (hProcess) {
              ShowWindow(hwnd, SW_HIDE);
              AddTrayIcon(hwnd);

              struct WaitThreadData {
                HWND hwnd;
                HANDLE hProcess;
              };
              WaitThreadData* data = new WaitThreadData{ hwnd, hProcess };
              CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                WaitThreadData* d = static_cast<WaitThreadData*>(lpParam);
                WaitForSingleObject(d->hProcess, INFINITE);
                CloseHandle(d->hProcess);
                PostMessageW(d->hwnd, WM_GAME_EXITED, 0, 0);
                delete d;
                return 0;
              }, data, 0, nullptr);
            }
          }
          ImGui::SameLine();
          if (ImGui::Button("Notes", ImVec2(70.0f, 30.0f))) {
            if (active_game_idx == static_cast<int>(i) && active_win == ActiveWindow::Notes) {
                // already open
            } else {
                if (active_win == ActiveWindow::Notes) dover::shared::notes::ShutdownNotesManager();
                dover::shared::GameStorage::Get().Initialize(games[i].name);
                dover::shared::notes::InitializeNotesManager(dover::shared::GameStorage::Get().GetNotesDir());
                auto& notes_win = dover::shared::notes::GetNotesWindow();
                notes_win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                notes_win.SetOpenDirect(true);
                active_game_idx = static_cast<int>(i);
                active_win = ActiveWindow::Notes;
            }
          }
          ImGui::SameLine();
          if (ImGui::Button("Settings", ImVec2(75.0f, 30.0f))) {
            if (active_game_idx == static_cast<int>(i) && active_win == ActiveWindow::Settings) {
                // already open
            } else {
                if (active_win == ActiveWindow::Notes) dover::shared::notes::ShutdownNotesManager();
                dover::shared::GameStorage::Get().Initialize(games[i].name);
                auto& settings_win = dover::shared::settings::GetSettingsWindow();
                settings_win.SetRenderContext(dover::shared::ui::RenderContext::Launcher);
                settings_win.SetOpenDirect(true);
                active_game_idx = static_cast<int>(i);
                active_win = ActiveWindow::Settings;
            }
          }
          ImGui::SameLine();
          if (ImGui::Button("Remove", ImVec2(70.0f, 30.0f))) {
            // Implement removal logic later, simplified for now
          }
          ImGui::EndDisabled();
          ImGui::PopID();
        }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    ImGui::Text("Add New Game (Absolute Path):");
    ImGui::InputText("##GamePath", input_buffer, IM_ARRAYSIZE(input_buffer));
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
      std::wstring selected_path = BrowseForExecutable(hwnd);
      if (!selected_path.empty()) {
        std::string selected_path_u8 = WideToUTF8(selected_path);
        strcpy_s(input_buffer, IM_ARRAYSIZE(input_buffer), selected_path_u8.c_str());
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add")) {
      std::string path_u8(input_buffer);
      std::wstring path_w = UTF8ToWide(path_u8);
      if (!path_w.empty() && std::filesystem::exists(path_w)) {
        SaveGamePath(path_w);
        games = LoadSavedGames(); // Reload list
        input_buffer[0] = '\0';
      }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

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

    g_pSwapChain->Present(1, 0); // Present with vsync
  }

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  CleanupDeviceD3D();
  ::DestroyWindow(hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  return 0;
}
