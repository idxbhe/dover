#include "overlay/notes/style.h"
#include <imgui.h>
#include <imgui_md.h>

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

struct DoverMarkdownRenderer : public imgui_md {
  int m_zoom_idx = 2;
  int m_list_level = 0;

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
  
  ImVec4 get_color() const override {
    if (m_is_code) {
      return ImVec4(0.85f, 0.40f, 0.40f, 1.00f); // Soft reddish-orange for code text
    }
    return imgui_md::get_color();
  }

  void BLOCK_CODE(const MD_BLOCK_CODE_DETAIL* d, bool e) override {
    m_is_code = e;
    if (e) {
      ImGui::PushFont(get_font());
      ImGui::PushStyleColor(ImGuiCol_Text, get_color());
    } else {
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
  }

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

  void BLOCK_H(const MD_BLOCK_H_DETAIL* d, bool e) override {
    if (e) {
      m_hlevel = d->level;
      ImGui::Dummy(ImVec2(0.0f, 10.0f)); // Small gap before heading
    } else {
      m_hlevel = 0;
      ImGui::Dummy(ImVec2(0.0f, 4.0f)); // Tiny gap after heading
    }
    if (e) {
      ImGui::PushFont(get_font());
    } else {
      ImGui::PopFont();
    }
  }

  void BLOCK_UL(const MD_BLOCK_UL_DETAIL* d, bool e) override {
    if (e) m_list_level++; else m_list_level--;
    imgui_md::BLOCK_UL(d, e);
  }
  
  void BLOCK_OL(const MD_BLOCK_OL_DETAIL* d, bool e) override {
    if (e) m_list_level++; else m_list_level--;
    imgui_md::BLOCK_OL(d, e);
  }

  void BLOCK_P(bool e) override {
    if (m_list_level > 0) return;
    if (e) {
      ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }
  }

  void open_url() const override {}
  bool get_image(image_info&) const override { return false; }
};

void RenderMarkdown(const std::string& content, int zoom_idx) {
  static DoverMarkdownRenderer renderer;
  renderer.m_zoom_idx = zoom_idx;
  renderer.print(content.c_str(), content.c_str() + content.size());
}

} // namespace dover::overlay::notes
