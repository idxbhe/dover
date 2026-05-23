#pragma once

namespace dover::overlay::notes {

// Must be called once, after NotesManager is initialized.
void InitializeNotesUI();

// Renders the Notes floating window. Call every frame inside RenderImGuiUI.
void RenderNotesWindow(bool* p_open);

// Releases any cached render state
void ShutdownNotesUI();

} // namespace dover::overlay::notes
