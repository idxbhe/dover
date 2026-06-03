#pragma once
#include <filesystem>
#include <memory>
#include <span>

namespace dover::shared::notes {

constexpr size_t MAX_NOTES = 128;
constexpr size_t MAX_NOTE_SIZE = 65536; // 64KB, perfectly synchronized with m_edit_buffer

struct NoteFile {
  char filename[64];
  char title[64];
  std::unique_ptr<char[]> content; // Pre-allocated 64KB heap buffer, zero re-allocations
  bool is_dirty = false;
  uint64_t date_created = 0;
  uint64_t date_modified = 0;
};

enum class NoteSortCriteria {
  Name,
  DateCreated,
  DateModified
};

// Sort state getters
NoteSortCriteria GetSortCriteria();
bool IsSortAscending();

// Changes sort mode and sorts the array
void SetSortMode(NoteSortCriteria criteria, bool ascending);

// Sorts the array using current criteria (used internally and when new notes are added)
void SortNotesArray();

// Must be called once after ImGui context is created.
// notes_dir: full path to the per-game notes directory (from GameStorage)
bool InitializeNotesManager(const std::filesystem::path& notes_dir);

// cleanly shuts down background threads
void ShutdownNotesManager();

// Returns a span of the active in-memory notes (no copy, array backed)
std::span<NoteFile> GetNotes();

// Returns the notes directory path
const std::filesystem::path& GetNotesDir();

// Creates a note with a guaranteed unique auto-generated title.
// Returns a static buffer with the title, or empty string on failure.
const char* CreateAutoNote();

// Creates a new empty note with the given title. Returns false if title exists.
bool CreateNote(const char* title);

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

} // namespace dover::shared::notes
