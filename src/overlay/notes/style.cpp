#include "overlay/notes/style.h"
#include <imgui.h>
#include <imgui_md.h>
#include <vector>
#include <cstdio>

namespace dover::overlay {
  // Extern references to the global overlay fonts
  extern ImFont* g_fonts_editor[5];
  extern ImFont* g_fonts_preview[5];
  extern ImFont* g_fonts_preview_bold[5];
  extern ImFont* g_fonts_preview_italic[5];
  extern ImFont* g_fonts_preview_bold_italic[5];
  extern ImFont* g_fonts_preview_h1[5];
  extern ImFont* g_fonts_preview_h2[5];
  extern ImFont* g_fonts_preview_h3[5];
}

namespace dover::overlay::notes {

// ---- Color Palette (Neutral/Monochrome Obsidian-like) ----
namespace palette {
  // Heading colors: bright to subtle gradient
  static constexpr ImVec4 kH1     = {0.95f, 0.95f, 0.95f, 1.00f};
  static constexpr ImVec4 kH2     = {0.82f, 0.84f, 0.88f, 1.00f};
  static constexpr ImVec4 kH3     = {0.70f, 0.73f, 0.78f, 1.00f};
  static constexpr ImVec4 kH4Plus = {0.60f, 0.63f, 0.68f, 1.00f};

  // Heading separator
  static constexpr ImVec4 kHSep = {0.40f, 0.42f, 0.48f, 0.35f};

  // Code
  static constexpr ImVec4 kCodeText    = {0.84f, 0.62f, 0.55f, 1.00f}; // warm neutral
  static constexpr ImVec4 kCodeBlockBg = {0.10f, 0.11f, 0.14f, 0.90f};
  static constexpr ImVec4 kCodeBlockBd = {0.22f, 0.24f, 0.30f, 0.50f};

  // List bullets
  static constexpr ImVec4 kBullet = {0.50f, 0.55f, 0.65f, 0.85f};

  // Blockquote
  static constexpr ImVec4 kQuoteBorder = {0.45f, 0.50f, 0.60f, 0.50f};
  static constexpr ImVec4 kQuoteText   = {0.70f, 0.72f, 0.76f, 1.00f};

  // Horizontal rule
  static constexpr ImVec4 kHR = {0.40f, 0.42f, 0.50f, 0.40f};
}

// ---- Custom list tracking (base m_list_stack is private) ----
struct CustomListInfo {
  bool is_ol;
  unsigned cur_number;
};

struct DoverMarkdownRenderer : public imgui_md {
  int m_zoom_idx = 2;
  int m_list_level = 0;
  int m_li_indent_level = 0;

  // Code block state
  bool m_code_block_active = false;
  ImVec2 m_code_block_start = {0, 0};

  // Blockquote state
  int m_quote_depth = 0;
  float m_quote_start_x = 0.0f;

  // Custom list tracking
  std::vector<CustomListInfo> m_custom_list_stack;

  static constexpr float kListIndent = 18.0f;

  // ---- Font Selection ----
  ImFont* get_font() const override {
    if (m_is_code) return g_fonts_editor[m_zoom_idx];
    if (m_hlevel == 1) return g_fonts_preview_h1[m_zoom_idx];
    if (m_hlevel == 2) return g_fonts_preview_h2[m_zoom_idx];
    if (m_hlevel >= 3) return g_fonts_preview_h3[m_zoom_idx];
    if (m_is_strong && m_is_em) return g_fonts_preview_bold_italic[m_zoom_idx];
    if (m_is_strong) return g_fonts_preview_bold[m_zoom_idx];
    if (m_is_em) return g_fonts_preview_italic[m_zoom_idx];
    if (m_is_table_header) return g_fonts_preview_bold[m_zoom_idx];
    return g_fonts_preview[m_zoom_idx];
  }

  // ---- Color Selection ----
  ImVec4 get_color() const override {
    if (m_is_code) return palette::kCodeText;
    if (m_hlevel == 1) return palette::kH1;
    if (m_hlevel == 2) return palette::kH2;
    if (m_hlevel == 3) return palette::kH3;
    if (m_hlevel >= 4) return palette::kH4Plus;
    return imgui_md::get_color();
  }

  // ---- Code Block (with background rect via channel splitting) ----
  void BLOCK_CODE(const MD_BLOCK_CODE_DETAIL*, bool e) override {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (e) {
      m_is_code = true;
      m_code_block_active = true;

      // Spacing before code block
      ImGui::NewLine();
      ImGui::Dummy(ImVec2(0, 4.0f));

      // Record start position for background rect
      m_code_block_start = ImGui::GetCursorScreenPos();

      // Top inner padding
      ImGui::Dummy(ImVec2(0, 6.0f));
      ImGui::Indent(12.0f);

      // Channel split: 0 = background (drawn first), 1 = text (drawn on top)
      dl->ChannelsSplit(2);
      dl->ChannelsSetCurrent(1);

      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Unindent(12.0f);

      // Bottom inner padding
      ImGui::Dummy(ImVec2(0, 6.0f));

      // Calculate rect covering the entire code block
      ImVec2 end_pos = ImGui::GetCursorScreenPos();
      float left  = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
      float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;

      ImVec2 min_r(left, m_code_block_start.y);
      ImVec2 max_r(right, end_pos.y);

      // Draw background on channel 0 (behind text)
      dl->ChannelsSetCurrent(0);
      dl->AddRectFilled(min_r, max_r, ImGui::GetColorU32(palette::kCodeBlockBg), 6.0f);
      dl->AddRect(min_r, max_r, ImGui::GetColorU32(palette::kCodeBlockBd), 6.0f, 0, 1.0f);
      dl->ChannelsMerge();

      // Spacing after code block
      ImGui::Dummy(ImVec2(0, 4.0f));

      m_is_code = false;
      m_code_block_active = false;
    }
  }

  // ---- Inline Code ----
  void SPAN_CODE(bool e) override {
    m_is_code = e;
    if (e) {
      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

  // ---- Headings (per-level color, symmetric spacing, separator for H1/H2) ----
  void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) override {
    if (e) {
      m_hlevel = d->level;

      // Proportional top spacing per heading level
      float top_gap = 18.0f;
      if (d->level == 2) top_gap = 14.0f;
      else if (d->level == 3) top_gap = 10.0f;
      else if (d->level >= 4) top_gap = 8.0f;
      ImGui::Dummy(ImVec2(0.0f, top_gap));

      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();

      // Bottom spacing
      float bot_gap = 10.0f;
      if (d->level == 2) bot_gap = 8.0f;
      else if (d->level == 3) bot_gap = 6.0f;
      else if (d->level >= 4) bot_gap = 4.0f;

      // Custom separator line for H1/H2 (subtle, not ImGui::Separator)
      if (d->level <= 2) {
        ImGui::NewLine();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float left  = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float y = pos.y + 2.0f;
        ImGui::GetWindowDrawList()->AddLine(
          ImVec2(left, y), ImVec2(right, y),
          ImGui::GetColorU32(palette::kHSep), 1.0f);
      }

      ImGui::Dummy(ImVec2(0.0f, bot_gap));
      m_hlevel = 0;
    }
  }

  // ---- Lists (geometric indent, custom bullets) ----
  void BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) override {
    if (e) {
      m_list_level++;
      m_custom_list_stack.push_back({false, 0});
    } else {
      m_list_level--;
      if (!m_custom_list_stack.empty()) m_custom_list_stack.pop_back();
    }
    imgui_md::BLOCK_UL(d, e);
  }

  void BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) override {
    if (e) {
      m_list_level++;
      m_custom_list_stack.push_back({true, d->start});
    } else {
      m_list_level--;
      if (!m_custom_list_stack.empty()) m_custom_list_stack.pop_back();
    }
    imgui_md::BLOCK_OL(d, e);
  }

  void BLOCK_LI(const MD_BLOCK_LI_DETAIL*, bool e) override {
    if (e) {
      ImGui::NewLine();

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      float font_size = ImGui::GetFontSize();
      ImU32 col = ImGui::GetColorU32(palette::kBullet);

      if (!m_custom_list_stack.empty() && m_custom_list_stack.back().is_ol) {
        // Ordered list: render number
        auto& info = m_custom_list_stack.back();
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.", info.cur_number++);
        ImGui::PushStyleColor(ImGuiCol_Text, palette::kBullet);
        ImGui::Text("%s", buf);
        ImGui::PopStyleColor();
        ImGui::SameLine();
      } else {
        // Unordered list: custom bullet based on nesting depth
        float cx = pos.x + 5.0f;
        float cy = pos.y + font_size * 0.5f;

        if (m_list_level <= 1) {
          // Level 1: Filled circle
          dl->AddCircleFilled(ImVec2(cx, cy), 2.8f, col, 12);
        } else if (m_list_level == 2) {
          // Level 2: Hollow circle (ring)
          dl->AddCircle(ImVec2(cx, cy), 2.5f, col, 12, 1.3f);
        } else {
          // Level 3+: Dash
          dl->AddRectFilled(
            ImVec2(cx - 3.0f, cy - 0.7f),
            ImVec2(cx + 3.0f, cy + 0.7f), col);
        }

        // Reserve horizontal space for bullet, keep on same line
        ImGui::Dummy(ImVec2(12.0f, 0.01f));
        ImGui::SameLine(0, 0);
      }

      ImGui::Indent(kListIndent);
      m_li_indent_level++;
    } else {
      if (m_li_indent_level > 0) {
        ImGui::Unindent(kListIndent);
        m_li_indent_level--;
      }
    }
  }

  // ---- Blockquote (left border + indent + dimmed text) ----
  void BLOCK_QUOTE(bool e) override {
    if (e) {
      m_quote_depth++;
      ImGui::NewLine();
      m_quote_start_x = ImGui::GetCursorScreenPos().x;
      ImGui::Indent(16.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, palette::kQuoteText);
    } else {
      ImGui::PopStyleColor();
      ImGui::Unindent(16.0f);
      m_quote_depth--;
    }
  }

  // ---- Paragraph Spacing ----
  void BLOCK_P(bool e) override {
    if (m_list_level > 0) return;
    if (e) {
      ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }
  }

  // ---- Horizontal Rule (custom rendered) ----
  void BLOCK_HR(bool e) override {
    if (!e) {
      ImGui::NewLine();
      ImGui::Dummy(ImVec2(0, 6.0f));

      ImVec2 pos = ImGui::GetCursorScreenPos();
      float left  = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMin().x;
      float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      float center = (left + right) * 0.5f;
      float half_w = (right - left) * 0.5f;

      ImDrawList* dl = ImGui::GetWindowDrawList();
      // Gradient: fade from center outward
      ImU32 col_center = ImGui::GetColorU32(palette::kHR);
      ImU32 col_edge   = ImGui::GetColorU32(ImVec4(palette::kHR.x, palette::kHR.y, palette::kHR.z, 0.05f));
      dl->AddLine(ImVec2(left, pos.y), ImVec2(center, pos.y), col_edge, 1.0f);
      dl->AddLine(ImVec2(left + half_w * 0.3f, pos.y), ImVec2(right - half_w * 0.3f, pos.y), col_center, 1.0f);
      dl->AddLine(ImVec2(center, pos.y), ImVec2(right, pos.y), col_edge, 1.0f);

      ImGui::Dummy(ImVec2(0, 6.0f));
    }
  }

  // ---- Soft Break (newline without double-space) ----
  void soft_break() override {
    if (m_li_indent_level > 0) {
      ImGui::Unindent(kListIndent);
      m_li_indent_level--;
    }
    ImGui::NewLine();
  }

  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};

void RenderMarkdown(const std::string& content, int zoom_idx) {
  static DoverMarkdownRenderer renderer;
  renderer.m_zoom_idx = zoom_idx;

  // Reset transient state per frame
  renderer.m_list_level = 0;
  renderer.m_li_indent_level = 0;
  renderer.m_code_block_active = false;
  renderer.m_quote_depth = 0;
  renderer.m_custom_list_stack.clear();

  ImGui::PushFont(g_fonts_preview[zoom_idx]);
  renderer.print(content.c_str(), content.c_str() + content.size());
  ImGui::PopFont();
}

} // namespace dover::overlay::notes
