#include "shared/ui/components/base_window.h"
#include "shared/icons.h"
#include "shared/theme.h"
#include "shared/game_storage.h"
#include <imgui.h>
#include <imgui_internal.h> // ImGui::ClearActiveID()

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace dover::shared::ui {

namespace {
    constexpr float kNavBarHeight = 42.0f;
}

void BaseWindow::Render(bool interactive) {
    // If not interactive and not pinned, completely hide the window
    if (!interactive && !m_is_pinned) {
        return;
    }

    if (!m_is_open) {
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display_size = io.DisplaySize;

    ImGuiWindowFlags win_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    
    if (!interactive) {
        win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    }
    
    // If the window is initialized and has a saved position/size, but m_prev_pos/m_prev_size is still default (0,0),
    // grab the current/saved values from ImGui's internal window registry.
    if (m_prev_pos.x == 0.0f && m_prev_pos.y == 0.0f && m_prev_size.x == 0.0f && m_prev_size.y == 0.0f) {
        ImGuiWindow* window = ImGui::FindWindowByName(m_window_name);
        if (window) {
            m_prev_pos = window->Pos;
            m_prev_size = window->Size;
        }
    }

    if (m_is_fullscreen) {
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(display_size, ImGuiCond_Always);
        win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    } else if (m_is_maximized) {
        if (m_ctx == RenderContext::Overlay) {
            ImGui::SetNextWindowPos(ImVec2(0.0f, kNavBarHeight), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(display_size.x, display_size.y - kNavBarHeight), ImGuiCond_Always);
        } else {
#ifdef _WIN32
            // Use Win32 Monitor API to find the monitor containing the window's last position
            POINT pt = { static_cast<LONG>(m_prev_pos.x + m_prev_size.x * 0.5f), static_cast<LONG>(m_prev_pos.y + m_prev_size.y * 0.5f) };
            HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            if (hMonitor && GetMonitorInfoW(hMonitor, &mi)) {
                ImGui::SetNextWindowPos(ImVec2(static_cast<float>(mi.rcWork.left), static_cast<float>(mi.rcWork.top)), ImGuiCond_Always);
                ImGui::SetNextWindowSize(ImVec2(static_cast<float>(mi.rcWork.right - mi.rcWork.left), static_cast<float>(mi.rcWork.bottom - mi.rcWork.top)), ImGuiCond_Always);
            } else {
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(display_size, ImGuiCond_Always);
            }
#else
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(display_size, ImGuiCond_Always);
#endif
        }
        win_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    } else {
        if (m_was_maximized || m_was_fullscreen) {
            ImVec2 target_size = m_prev_size;
            if (target_size.x <= 0.0f || target_size.y <= 0.0f) {
                target_size = m_default_size;
            }
            ImVec2 target_pos = m_prev_pos;
            if (target_pos.x == 0.0f && target_pos.y == 0.0f) {
                const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
                if (main_viewport) {
                    target_pos.x = main_viewport->Pos.x + (main_viewport->Size.x - target_size.x) * 0.5f;
                    target_pos.y = main_viewport->Pos.y + (main_viewport->Size.y - target_size.y) * 0.5f;
                }
            }
            ImGui::SetNextWindowPos(target_pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(target_size, ImGuiCond_Always);
            m_was_maximized = false;
            m_was_fullscreen = false;
        } else {
            ImGui::SetNextWindowSize(m_default_size, ImGuiCond_FirstUseEver);
        }
    }

    bool no_border = m_is_fullscreen || m_is_maximized || !interactive;
    ImGui::SetNextWindowSizeConstraints(ImVec2(150.0f, 150.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, m_is_maximized ? 0.0f : 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    if (no_border) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.12f, 0.15f, 0.22f, 1.00f));
    }

    ImGui::SetNextWindowBgAlpha(m_bg_alpha);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    bool is_open_local = m_is_open.load();
    bool begin_ok = ImGui::Begin(m_window_name, &is_open_local, win_flags);
    if (is_open_local != m_is_open.load()) m_is_open.store(is_open_local);
    ImGui::PopStyleColor();

    if (no_border) {
        ImGui::PopStyleVar(3);
    } else {
        ImGui::PopStyleColor(1);
        ImGui::PopStyleVar(3);
    }

    if (begin_ok) {
        m_is_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        
        // Handle Esc key to close the focused window
        if (interactive && m_is_focused && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            Close();
        }
        
        // Track the correct parent window size and position while windowed
        if (!m_is_maximized && !m_is_fullscreen) {
            m_prev_pos = ImGui::GetWindowPos();
            m_prev_size = ImGui::GetWindowSize();
        }
        
        // Draw Premium Balanced Dark Slate-Blue gradient background base
        ImVec2 min_p = ImGui::GetWindowPos();
        ImVec2 max_p = ImVec2(min_p.x + ImGui::GetWindowSize().x, min_p.y + ImGui::GetWindowSize().y);
        ImU32 col_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.082f, 0.094f, 0.125f, m_bg_alpha)); // Top-Left: Dark Slate-Blue (#151820)
        ImU32 col_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(0.067f, 0.075f, 0.102f, m_bg_alpha)); // Top-Right: Deep Obsidian Core (#11131a)
        ImU32 col_br = ImGui::ColorConvertFloat4ToU32(ImVec4(0.051f, 0.055f, 0.078f, m_bg_alpha)); // Bottom-Right: Dark Velvet Onyx (#0d0e14)
        ImU32 col_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(0.071f, 0.082f, 0.110f, m_bg_alpha)); // Bottom-Left: Balanced Charcoal Blue (#12151c)
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(min_p, max_p, col_tl, col_tr, col_br, col_bl);

        // Pre-render hooks (like internal backgrounds/shadows)
        PreRender(interactive);

        // Standardized Header / Toolbar
        if (interactive) {
            if (strcmp(m_window_name, "Settings") != 0 && strcmp(m_window_name, "Input Mapper") != 0) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f); // Move decorations & toolbar 3px lower
                RenderToolbar(interactive);
                
                float avail_x = ImGui::GetContentRegionAvail().x;
                float right_boundary = ImGui::GetCursorPosX() + avail_x;
                
                RenderWindowDecorations(interactive, right_boundary);

                // Separator removed per design request
            }
        }

        // Main content area
        RenderContent(interactive);

        // Floating buttons or popups
        PostRender(interactive);
    }

    if (!begin_ok) {
        ImGui::End();
        return;
    }

    ImGui::End();
}

void BaseWindow::RenderWindowDecorations(bool interactive, float right_boundary, float custom_y_pos) {
    if (!interactive) return;

    auto DrawCustomButton = [&](const char* icon, float same_line_pos, const char* tooltip, std::atomic<bool>* toggle_state = nullptr) -> bool {
        ImGui::SameLine();
        ImGui::SetCursorPosX(same_line_pos);
        if (custom_y_pos >= 0.0f) {
            ImGui::SetCursorPosY(custom_y_pos);
        } else {
            if (ImGui::GetCursorPosY() < 4.0f) {
                ImGui::SetCursorPosY(4.0f);
            }
        }
        
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));
        bool is_toggled = (toggle_state && toggle_state->load());
        
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
                border_color = ImVec4(0.210f, 0.280f, 0.380f, 1.00f);
            } else if (hovered) {
                bg_color = ImVec4(0.850f, 0.150f, 0.150f, 1.00f);
                border_color = ImVec4(0.170f, 0.220f, 0.300f, 1.00f);
            } else {
                bg_color = ImVec4(0.750f, 0.150f, 0.150f, 0.40f);
                border_color = ImVec4(0.120f, 0.141f, 0.174f, 1.00f);
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
        
        ImU32 text_col32;
        if (is_toggled) {
            text_col32 = ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.80f, 0.20f, 1.00f));
        } else {
            text_col32 = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]);
        }
        
        ImGui::GetWindowDrawList()->AddRectFilled(min_p, max_p, bg_col32, 2.0f);
        
        ImVec2 mid_p = ImVec2(max_p.x, min_p.y + (max_p.y - min_p.y) * 0.5f);
        ImU32 half_hl_col = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.03f));
        ImGui::GetWindowDrawList()->AddRectFilled(min_p, mid_p, half_hl_col, 2.0f, ImDrawFlags_RoundCornersTop);
        
        ImGui::GetWindowDrawList()->AddRect(min_p, max_p, border_col32, 2.0f, 1.0f, 0);
        
        if (dover::shared::g_font_gui) ImGui::PushFont(dover::shared::g_font_gui, 18.0f);
        ImVec2 text_size = ImGui::CalcTextSize(icon);
        ImVec2 text_pos = ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f);
        ImGui::GetWindowDrawList()->AddText(text_pos, text_col32, icon);
        if (dover::shared::g_font_gui) ImGui::PopFont();
        
        if (hovered && tooltip) {
            ImGui::SetTooltip(tooltip);
        }
        
        if (clicked && toggle_state) {
            toggle_state->store(!toggle_state->load());
        }
        
        return clicked;
    };

    float offset = 34.0f;
    if (HasFeature(m_features, WindowFeature::Close)) {
        if (DrawCustomButton(ICON_WINDOW_CLOSE, right_boundary - offset, "Close")) {
            Close();
        }
        offset += 26.0f;
    }

    if (HasFeature(m_features, WindowFeature::Maximize)) {
        bool is_max = m_is_maximized.load();
        const char* icon = is_max ? ICON_WINDOW_WINDOWED : ICON_WINDOW_MAXIMIZE;
        const char* tooltip = is_max ? "Restore Window Size" : "Maximize Window";
        if (DrawCustomButton(icon, right_boundary - offset, tooltip)) {
            if (is_max) {
                m_was_maximized = true;
            }
            m_is_maximized.store(!is_max);
            dover::shared::GameStorage::Get().SaveConfig();
        }
        offset += 26.0f;
    }

    if (HasFeature(m_features, WindowFeature::Fullscreen)) {
        if (m_ctx == RenderContext::Overlay) {
            bool is_full = m_is_fullscreen.load();
            if (DrawCustomButton(ICON_WINDOW_FULLSCREEN, right_boundary - offset, is_full ? "Exit Fullscreen" : "Fullscreen")) {
                if (is_full) {
                    m_was_fullscreen = true;
                }
                m_is_fullscreen.store(!is_full);
                dover::shared::GameStorage::Get().SaveConfig();
            }
            offset += 26.0f;
        }
    }

    if (HasFeature(m_features, WindowFeature::Pin)) {
        if (m_ctx == RenderContext::Overlay) {
            if (DrawCustomButton(ICON_WINDOW_PINNED, right_boundary - offset, m_is_pinned ? "Unpin from screen" : "Pin to screen", &m_is_pinned)) {
                dover::shared::GameStorage::Get().SaveConfig();
            }
            offset += 26.0f;
        }
    }
}

void BaseWindow::Open() {
    m_is_open = true;
    dover::shared::GameStorage::Get().SaveState();
}

void BaseWindow::Close() {
    // Force-flush any active InputText widget before closing.
    // ImGui holds typed characters in an internal buffer until the widget loses focus.
    // Without this, the last keystrokes typed before close are silently lost.
    ImGui::ClearActiveID();
    m_is_open = false;
    dover::shared::GameStorage::Get().SaveState();
}

void BaseWindow::ToggleOpen() {
    m_is_open.store(!m_is_open.load());
    dover::shared::GameStorage::Get().SaveState();
}

} // namespace dover::shared::ui
