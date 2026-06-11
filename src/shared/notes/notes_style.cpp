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
    float base_size = dover::shared::kPreviewSizes[std::clamp(m_zoom_idx, 0, 4)];
    if (m_is_code) return dover::shared::kEditorSizes[std::clamp(m_zoom_idx, 0, 4)];
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

  // ---- Lists (geometric indent, custom bullets) ----
  void BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) override {
    if (e) {
      m_list_level++;
      if (m_list_level == 1) {
        m_first_list_item = true;
      }
      ImGui::Indent(15.0f);
      if (m_custom_list_depth < 32) {
        m_custom_list_stack[m_custom_list_depth++] = {false, 0};
      }
    } else {
      m_list_level--;
      ImGui::Unindent(15.0f);
      if (m_custom_list_depth > 0) m_custom_list_depth--;
    }
    imgui_md::BLOCK_UL(d, e);
  }

  void BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) override {
    if (e) {
      m_list_level++;
      if (m_list_level == 1) {
        m_first_list_item = true;
      }
      ImGui::Indent(15.0f);
      if (m_custom_list_depth < 32) {
        m_custom_list_stack[m_custom_list_depth++] = {true, d->start};
      }
    } else {
      m_list_level--;
      ImGui::Unindent(15.0f);
      if (m_custom_list_depth > 0) m_custom_list_depth--;
    }
    imgui_md::BLOCK_OL(d, e);
  }

  void BLOCK_LI(const MD_BLOCK_LI_DETAIL* d, bool e) override {
    if (e) {
      if (!m_first_list_item) {
        ImGui::NewLine();
      }
      m_first_list_item = false;

      if (d->is_task) {
        bool is_checked = (d->task_mark == 'x' || d->task_mark == 'X');
        
        ImGui::PushID((int)d->task_mark_offset);
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float line_height = ImGui::GetTextLineHeight();
        
        // Compact premium size (14.0f pixels)
        float size = 14.0f;
        float cy = pos.y + (line_height - size) * 0.5f + 1.0f; // Vertically center with absolute precision
        float cx = pos.x - 4.0f; // Shift to the left to avoid text crowding
        
        ImVec2 box_min(cx, cy);
        ImVec2 box_max(cx + size, cy + size);
        
        // Bounding space in ImGui's layout matching the visual size
        ImGui::Dummy(ImVec2(size, size));
        
        bool hovered = ImGui::IsItemHovered();
        bool clicked = hovered && ImGui::IsMouseClicked(0);
        
        if (clicked) {
            // Instant memory mutation in Read Mode!
            if (m_markdown_source) {
                m_markdown_source[d->task_mark_offset] = is_checked ? ' ' : 'x';
                is_checked = !is_checked; // Update state for current frame render

                // Locate and mark the note as dirty for auto-saving
                auto notes = GetNotes();
                for (size_t i = 0; i < notes.size(); ++i) {
                    if (notes[i].content.get() == m_markdown_source) {
                        notes[i].is_dirty = true;
                        GetNotesWindow().SyncEditBufferFromNote(static_cast<int>(i));
                        break;
                    }
                }
                MarkNoteChanged();
            }
        }
        
        // Render crisp high-performance custom checkbox geometry
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (is_checked) {
            // Solid premium green with subtle hover highlight
            ImU32 bg_col = hovered 
                           ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.85f, 0.25f, 1.00f))
                           : ImGui::ColorConvertFloat4ToU32(ImVec4(0.20f, 0.80f, 0.20f, 1.00f));
            dl->AddRectFilled(box_min, box_max, bg_col, 2.5f);

            // Ultra-precise checked tick mark lines
            ImU32 check_col = IM_COL32(255, 255, 255, 255);
            dl->AddLine(ImVec2(cx + 3.0f, cy + size * 0.5f), ImVec2(cx + 6.0f, cy + size - 3.0f), check_col, 2.0f);
            dl->AddLine(ImVec2(cx + 6.0f, cy + size - 3.0f), ImVec2(cx + size - 3.0f, cy + 3.0f), check_col, 2.0f);
        } else {
            // Premium Obsidian dark hollow square
            ImU32 border_col = hovered
                               ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.60f, 0.65f, 0.75f, 0.95f))
                               : ImGui::ColorConvertFloat4ToU32(ImVec4(0.40f, 0.45f, 0.55f, 0.75f));
            dl->AddRect(box_min, box_max, border_col, 2.5f, 1.2f, 0);
        }
        
        ImGui::PopID();
        
        // Push subsequent list text to a clean distance
        ImGui::SameLine(0.0f, 12.0f);
      } else if (m_custom_list_depth > 0 && m_custom_list_stack[m_custom_list_depth - 1].is_ol) {
        // Ordered list: render number
        auto& info = m_custom_list_stack[m_custom_list_depth - 1];
        char buf[16];
        snprintf(buf, sizeof(buf), "%u.", info.cur_number++);
        ImGui::PushStyleColor(ImGuiCol_Text, palette::kBullet);
        ImGui::Text("%s", buf);
        ImGui::PopStyleColor();
        ImGui::SameLine();
      } else {
        // Unordered list: custom bullet based on nesting depth
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float font_size = ImGui::GetFontSize();
        ImU32 col = ImGui::GetColorU32(palette::kBullet);

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
    } else {
      ImGui::Unindent(kListIndent);
    }
  }

  // ---- Blockquote (left border + indent + dimmed text) ----
  void BLOCK_QUOTE(bool e) override {
    if (e) {
      m_quote_depth++;
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
      if (m_quote_depth <= 32) {
        m_quote_x_stack[m_quote_depth - 1] = ImGui::GetCursorScreenPos().x;
        m_quote_y_stack[m_quote_depth - 1] = ImGui::GetCursorScreenPos().y;
      }
      ImGui::Indent(16.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, palette::kQuoteText);
    } else {
      ImGui::PopStyleColor();
      ImGui::Unindent(16.0f);
      if (m_quote_depth > 0 && m_quote_depth <= 32) {
        float start_x = m_quote_x_stack[m_quote_depth - 1];
        float start_y = m_quote_y_stack[m_quote_depth - 1];
        float end_y = ImGui::GetCursorScreenPos().y;

        // Draw an Obsidian-like solid left vertical bar in the indented gap area
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(
          ImVec2(start_x + 4.0f, start_y), 
          ImVec2(start_x + 7.0f, end_y), 
          ImGui::GetColorU32(palette::kQuoteBorder), 
          1.5f
        );
      }
      m_quote_depth--;
      ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
  }

  // ---- Paragraph Spacing ----
  void BLOCK_P(bool e) override {
    if (m_list_level > 0) {
      m_last_block_was_heading = false;
      return;
    }
    if (e) {
      if (!m_last_block_was_heading) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
      }
      m_last_block_was_heading = false;
    } else {
      ImGui::NewLine();
    }
  }

  // ---- Horizontal Rule (custom rendered) ----
  void BLOCK_HR(bool e) override {
    if (!e) {
      ImGui::Dummy(ImVec2(0.0f, 4.0f));

      ImVec2 pos = ImGui::GetCursorScreenPos();
      float width = ImGui::GetContentRegionAvail().x;

      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImU32 col = ImGui::GetColorU32(palette::kHR);
      dl->AddLine(pos, ImVec2(pos.x + width, pos.y), col, 1.0f);

      ImGui::Dummy(ImVec2(0.0f, 4.0f));
    }
  }

  // ---- Soft Break (newline without double-space) ----
  void soft_break() override {
    ImGui::NewLine();
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

  // Reset table accumulation for the current frame
  for (size_t i = 0; i < renderer.kMaxTableCols; ++i) {
    renderer.m_table_col_current_width[i] = 0.0f;
  }

  ImGui::PushFont(dover::shared::g_font_preview, dover::shared::kPreviewSizes[std::clamp(zoom_idx, 0, 4)]);
  renderer.print(content, content + strlen(content));
  ImGui::PopFont();

  // After rendering, commit the accumulated widths to the max_width cache for the next frame
  for (size_t i = 0; i < renderer.kMaxTableCols; ++i) {
    // Apply a simple 1-frame delayed auto-width expansion
    if (renderer.m_table_col_current_width[i] > 0.0f) {
      renderer.m_table_col_max_width[i] = renderer.m_table_col_current_width[i];
    }
  }
}

} // namespace dover::shared::notes
