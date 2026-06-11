#include "shared/notes/style.h"
#include "shared/notes/manager.h" // Added for GetNotes() and MarkNoteChanged()
#include "shared/notes/layout.h" // Added for GetNotesWindow() and SyncEditBufferFromNote()
#include "shared/icons.h"
#include "shared/theme.h"
#include <imgui.h>
#define private public
#include <imgui_md.h>
#undef private
#include <cstdio>
#include <cstring>
#include <iterator>

namespace dover::overlay {
  // Extern references to the global overlay fonts

}

namespace dover::shared::notes {

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
  static constexpr ImVec4 kHR = {0.60f, 0.63f, 0.72f, 0.60f};
}

// ---- Custom list tracking (base m_list_stack is private) ----
struct CustomListInfo {
  bool is_ol;
  unsigned cur_number;
};

struct DoverMarkdownRenderer : public imgui_md {
  int (*m_original_text_callback)(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*) = nullptr;

  DoverMarkdownRenderer() {
    m_original_text_callback = m_md.text;
    m_md.text = [](MD_TEXTTYPE t, const MD_CHAR* text, MD_SIZE size, void* u) {
      auto* r = static_cast<DoverMarkdownRenderer*>(u);
      return r->custom_text_callback(t, text, size);
    };
  }

  int m_zoom_idx = 2;
  int m_list_level = 0;
  char* m_markdown_source = nullptr;

  // Code block state
  bool m_code_block_active = false;
  ImVec2 m_code_block_start = {0, 0};

  // Blockquote state
  int m_quote_depth = 0;
  float m_quote_x_stack[32] = {};
  float m_quote_y_stack[32] = {};

  // Custom list tracking (Fixed static array, no vector allocations)
  CustomListInfo m_custom_list_stack[32];
  size_t m_custom_list_depth = 0;
  
  bool m_last_block_was_heading = false;
  bool m_first_list_item = false;

  // Table cell alignment & auto-expansion state
  MD_ALIGN m_cell_align = MD_ALIGN_DEFAULT;
  bool m_cell_text_aligned = false;
  float m_cell_total_text_width = 0.0f;
  
  static constexpr size_t kMaxTableCols = 64;
  float m_table_col_max_width[kMaxTableCols] = {};
  float m_table_col_current_width[kMaxTableCols] = {};

  static constexpr float kListIndent = 15.0f;

  // ---- Font Selection ----
  ImFont* get_font() const override {
    if (m_is_code) return dover::shared::g_font_editor;
    if (m_is_strong && m_is_em) return dover::shared::g_font_preview_bold_italic;
    if (m_is_strong || m_is_table_header || m_hlevel > 0) return dover::shared::g_font_preview_bold;
    if (m_is_em) return dover::shared::g_font_preview_italic;
    return dover::shared::g_font_preview;
  }

  float get_font_size() const {
    static constexpr float preview_sizes[5] = { 13.0f, 15.0f, 18.0f, 22.0f, 26.0f };
    static constexpr float editor_sizes[5] = { 12.0f, 14.0f, 17.0f, 21.0f, 25.0f };
    float base_size = preview_sizes[std::clamp(m_zoom_idx, 0, 4)];
    if (m_is_code) return editor_sizes[std::clamp(m_zoom_idx, 0, 4)];
    if (m_hlevel == 1) return base_size * 2.00f;
    if (m_hlevel == 2) return base_size * 1.65f;
    if (m_hlevel == 3) return base_size * 1.35f;
    if (m_hlevel == 4) return base_size * 1.15f;
    return base_size;
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

      // Spacing before code block (Outer Top Gap of 3px)
      ImGui::Dummy(ImVec2(0.0f, 3.0f));

      // Record start position for background rect
      m_code_block_start = ImGui::GetCursorScreenPos();

      // Top inner padding
      ImGui::Dummy(ImVec2(0, 6.0f));
      ImGui::Indent(12.0f);

      // Channel split: 0 = background (drawn first), 1 = text (drawn on top)
      dl->ChannelsSplit(2);
      dl->ChannelsSetCurrent(1);

      ImGui::PushFont(get_font(), get_font_size());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Unindent(12.0f);

      // Bottom inner padding
      ImGui::Dummy(ImVec2(0, 6.0f));

      // Calculate rect covering the entire code block
      ImVec2 end_pos = ImGui::GetCursorScreenPos();
      float left  = m_code_block_start.x;
      float right = left + ImGui::GetContentRegionAvail().x;

      ImVec2 min_r(left, m_code_block_start.y);
      ImVec2 max_r(right, end_pos.y);

      // Draw background on channel 0 (behind text) with reduced rounding
      dl->ChannelsSetCurrent(0);
      dl->AddRectFilled(min_r, max_r, ImGui::GetColorU32(palette::kCodeBlockBg), 3.0f);
      dl->AddRect(min_r, max_r, ImGui::GetColorU32(palette::kCodeBlockBd), 3.0f, 1.0f, 0);
      dl->ChannelsMerge();

      // Spacing after code block (Outer Bottom Gap of 3px)
      ImGui::Dummy(ImVec2(0.0f, 3.0f));

      m_is_code = false;
      m_code_block_active = false;
      m_last_block_was_heading = true; // Prevents subsequent paragraph from inserting a double top-margin of 8px
    }
  }

  // ---- Inline Code ----
  void SPAN_CODE(bool e) override {
    m_is_code = e;
    if (e) {
      ImGui::PushFont(get_font(), get_font_size());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

  // ---- Headings (clean, no artificial spacing/lines) ----
  void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) override {
    if (e) {
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      m_hlevel = d->level;
      ImGui::PushFont(get_font(), get_font_size());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::NewLine();
      ImGui::Dummy(ImVec2(0.0f, 3.0f));
      m_hlevel = 0;
      m_last_block_was_heading = true;
    }
  }

  void SPAN_EM(bool e) override {
      m_is_em = e;
      if (e) ImGui::PushFont(get_font(), get_font_size());
      else ImGui::PopFont();
  }

  void SPAN_STRONG(bool e) override {
      m_is_strong = e;
      if (e) ImGui::PushFont(get_font(), get_font_size());
      else ImGui::PopFont();
  }

  void BLOCK_TH(const MD_BLOCK_TD_DETAIL* d, bool e) override {
      imgui_md::BLOCK_TH(d, e);
      if (e) {
          m_cell_align = d->align;
          m_cell_text_aligned = false;
          m_cell_total_text_width = 0.0f;
          ImGui::PushFont(get_font(), get_font_size());
      } else {
          ImGui::PopFont();
      }
  }

  void BLOCK_TD(const MD_BLOCK_TD_DETAIL* d, bool e) override {
    imgui_md::BLOCK_TD(d, e);
    if (e) { m_cell_align = d->align; m_cell_text_aligned = false; m_cell_total_text_width = 0.0f; }
  }

  int custom_text_callback(MD_TEXTTYPE type, const char* str, MD_SIZE size) {
    bool is_text = (type == MD_TEXT_NORMAL || type == MD_TEXT_CODE);
    
    if ((m_is_table_header || m_is_table_body) && is_text) {
      float text_width = ImGui::CalcTextSize(str, str + size).x;
      size_t col_idx = m_table_next_column > 0 ? m_table_next_column - 1 : 0;
      float col_left = m_table_col_pos.size() > col_idx ? m_table_col_pos[col_idx] : ImGui::GetCursorPosX();

      // Accumulate intrinsic physical width for auto-expansion (ignore alignment shifts)
      m_cell_total_text_width += text_width;
      float required_width = m_cell_total_text_width + 16.0f; // 16px aesthetic padding
      if (col_idx < kMaxTableCols && required_width > m_table_col_current_width[col_idx]) {
        m_table_col_current_width[col_idx] = required_width;
      }

      // Determine the maximum available column width from the cross-frame cache
      float col_width = col_idx < kMaxTableCols ? m_table_col_max_width[col_idx] : text_width;
      if (col_width == 0.0f) col_width = text_width; // Frame 1 fallback

      // Handle horizontal alignment
      if (m_cell_align != MD_ALIGN_DEFAULT && !m_cell_text_aligned && col_width > text_width) {
        m_cell_text_aligned = true; // Only shift the first inline span
        float extra_space = col_width - text_width;
        if (m_cell_align == MD_ALIGN_CENTER) {
          ImGui::SetCursorPosX(col_left + extra_space * 0.5f);
        } else if (m_cell_align == MD_ALIGN_RIGHT) {
          ImGui::SetCursorPosX(col_left + extra_space - 4.0f);
        }
      }

      // Render the actual text
      int res = m_original_text_callback(type, str, size, this);

      // Force imgui_md to use our maximum tracked width for the column boundaries
      if (m_is_table_header) {
        float target_end_x = col_left + (col_idx < kMaxTableCols ? m_table_col_max_width[col_idx] : text_width);
        if (m_table_last_pos.x < target_end_x) {
          m_table_last_pos.x = target_end_x;
        }
      }

      return res;
    }

    return m_original_text_callback(type, str, size, this);
  }

  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};

void RenderMarkdown(const char* content, int zoom_idx) {
  if (!content || content[0] == '\0') return;
  static DoverMarkdownRenderer renderer;
  
  // Enable task list flags in base class's public m_md parser
  static bool flags_set = false;
  if (!flags_set) {
    renderer.m_md.flags |= MD_FLAG_TASKLISTS;
    flags_set = true;
  }
  renderer.m_zoom_idx = zoom_idx;
  renderer.m_markdown_source = const_cast<char*>(content);

  // Reset transient state per frame
  renderer.m_list_level = 0;
  renderer.m_code_block_active = false;
  renderer.m_quote_depth = 0;
  renderer.m_custom_list_depth = 0; // Ensures safe bound check
  renderer.m_last_block_was_heading = false;
  renderer.m_first_list_item = false;

  static constexpr float preview_sizes[5] = { 13.0f, 15.0f, 18.0f, 22.0f, 26.0f };
  ImGui::PushFont(dover::shared::g_font_preview, preview_sizes[std::clamp(zoom_idx, 0, 4)]);
  renderer.print(content, content + strlen(content));
  ImGui::PopFont();
}

} // namespace dover::shared::notes
