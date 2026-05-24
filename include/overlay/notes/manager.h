#pragma once
#include <string>
#include <vector>

namespace dover::overlay::notes {

struct NoteFile {
  std::string filename;  // "strategy.md"
  std::string title;     // "strategy"
  std::string content;   // Full file content
  bool is_dirty = false; // Unsaved changes flag
};

// Must be called once after ImGui context is created.
// localappdata_base: path to %LOCALAPPDATA%
// game_exe: name of the game process (e.g. "csgo.exe") — will be normalized
bool InitializeNotesManager(const std::wstring& localappdata_base,
                             const std::string& game_exe);

// Returns mutable reference to the in-memory notes list
std::vector<NoteFile>& GetNotes();

// Returns the normalized game folder name (e.g. "csgo")
const std::string& GetNotesGameName();

// Creates a note with a guaranteed unique auto-generated title.
// Returns the title of the created note, empty string on failure.
std::string CreateAutoNote();

// Creates a new empty note with the given title. Returns false if title exists.
bool CreateNote(const std::string& title);

// Deletes the note at index from disk and memory. Returns false on failure.
bool DeleteNote(size_t index);

// Saves a single note to disk. Returns false on failure.
bool SaveNote(size_t index);

// Saves all dirty notes (call on overlay close / DLL unload)
void AutoSaveAll();

// Triggers a debounced autosave check (call every frame when overlay is open)
void TickAutosave();

// Marks that a note was actively changed by the user (resets debounce timer)
void MarkNoteChanged();

// Returns true if the "Saved" status indicator should currently be visible in the UI
bool ShouldShowSavedStatus();

} // namespace dover::overlay::notes
