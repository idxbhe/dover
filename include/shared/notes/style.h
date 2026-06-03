#pragma once
#include "imgui.h"

namespace dover::shared::notes {

// Renders markdown content using the styled DoverMarkdownRenderer
void RenderMarkdown(const char* content, int zoom_idx);

} // namespace dover::shared::notes
