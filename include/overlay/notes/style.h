#pragma once
#include "imgui.h"
#include <string>

namespace dover::overlay::notes {

// Renders markdown content using the styled DoverMarkdownRenderer
void RenderMarkdown(const std::string& content, int zoom_idx);

} // namespace dover::overlay::notes
