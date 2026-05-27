#pragma once
#include "imgui.h"

namespace dover::overlay::notes {

// Renders markdown content using the styled DoverMarkdownRenderer
void RenderMarkdown(const char* content, int zoom_idx);

} // namespace dover::overlay::notes
