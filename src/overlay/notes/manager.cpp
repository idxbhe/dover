#include "overlay/notes/manager.h"
#include "overlay/notes/layout.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <array>
#include <cstring>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace dover::overlay::notes {

namespace {
std::array<NoteFile, MAX_NOTES> g_notes;
size_t g_active_notes_count = 0;
fs::path g_notes_dir;

// Autosave debounce: save 3s after last dirty change
Clock::time_point g_last_dirty_time{};
bool g_has_pending_save = false;
Clock::time_point g_save_status_show_until{};

NoteSortCriteria g_sort_criteria = NoteSortCriteria::Name;
bool g_sort_ascending = true;

void ReadFileContentInto(const fs::path& path, char* buffer, size_t buffer_size) {
  buffer[0] = '\0';
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) return;
  f.read(buffer, buffer_size - 1);
  size_t bytes_read = static_cast<size_t>(f.gcount());
  buffer[bytes_read] = '\0';

  // Normalize CRLF to LF in-place
  size_t w = 0;
  for (size_t r = 0; r < bytes_read; ++r) {
      if (buffer[r] == '\r') continue;
      buffer[w++] = buffer[r];
  }
  buffer[w] = '\0';
}

bool WriteFileContent(const fs::path& path, const char* content) {
  std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(content, strlen(content));
  return f.good();
}

void LoadNotesFromDisk() {
  g_active_notes_count = 0;
  if (!fs::exists(g_notes_dir)) {
    fs::create_directories(g_notes_dir);
    return;
  }
  for (const auto& entry : fs::directory_iterator(g_notes_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".md") continue;
    if (g_active_notes_count >= MAX_NOTES) break;

    NoteFile& note = g_notes[g_active_notes_count];
    
    std::string fname = entry.path().filename().string();
    std::string stem = entry.path().stem().string();
    
    // Safely amend string using strncpy_s
    strncpy_s(note.filename, sizeof(note.filename), fname.c_str(), _TRUNCATE);
    strncpy_s(note.title, sizeof(note.title), stem.c_str(), _TRUNCATE);
    
    ReadFileContentInto(entry.path(), note.content.get(), MAX_NOTE_SIZE);
    note.is_dirty = false;
    
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (GetFileAttributesExA(entry.path().string().c_str(), GetFileExInfoStandard, &file_info)) {
        note.date_created = (static_cast<uint64_t>(file_info.ftCreationTime.dwHighDateTime) << 32) | file_info.ftCreationTime.dwLowDateTime;
        note.date_modified = (static_cast<uint64_t>(file_info.ftLastWriteTime.dwHighDateTime) << 32) | file_info.ftLastWriteTime.dwLowDateTime;
    } else {
        note.date_created = 0;
        note.date_modified = 0;
    }
    
    g_active_notes_count++;
  }

  SortNotesArray();
}

} // namespace

#include "overlay/notes/layout.h"

NoteSortCriteria GetSortCriteria() { return g_sort_criteria; }
bool IsSortAscending() { return g_sort_ascending; }

void SortNotesArray() {
    if (g_active_notes_count == 0) return;
    std::sort(g_notes.begin(), g_notes.begin() + g_active_notes_count, [](const NoteFile& a, const NoteFile& b) {
        if (g_sort_criteria == NoteSortCriteria::Name) {
            int cmp = _stricmp(a.title, b.title);
            if (cmp == 0) cmp = strcmp(a.title, b.title);
            return g_sort_ascending ? (cmp < 0) : (cmp > 0);
        } else if (g_sort_criteria == NoteSortCriteria::DateCreated) {
            if (a.date_created == b.date_created) return strcmp(a.title, b.title) < 0;
            return g_sort_ascending ? (a.date_created < b.date_created) : (a.date_created > b.date_created);
        } else {
            if (a.date_modified == b.date_modified) return strcmp(a.title, b.title) < 0;
            return g_sort_ascending ? (a.date_modified < b.date_modified) : (a.date_modified > b.date_modified);
        }
    });
    GetNotesWindow().FixupIndicesAfterMutation();
}

void SetSortMode(NoteSortCriteria criteria, bool ascending) {
    g_sort_criteria = criteria;
    g_sort_ascending = ascending;
    SortNotesArray();
}

bool InitializeNotesManager(const fs::path& notes_dir) {
  g_notes_dir = notes_dir;
  
  // Pre-allocate all heap buffers once at startup
  for (size_t i = 0; i < MAX_NOTES; ++i) {
      if (!g_notes[i].content) {
          g_notes[i].content = std::make_unique<char[]>(MAX_NOTE_SIZE);
          g_notes[i].content[0] = '\0';
      }
  }

  LoadNotesFromDisk();

  // Create a default note if game has none
  if (g_active_notes_count == 0) {
    CreateNote("general");
  }
  return true;
}

std::span<NoteFile> GetNotes() {
  return std::span<NoteFile>(g_notes.data(), g_active_notes_count);
}

const fs::path& GetNotesDir() {
  return g_notes_dir;
}

const char* CreateAutoNote() {
  static char candidate[64];
  const char* base = "untitled";
  strncpy_s(candidate, sizeof(candidate), base, _TRUNCATE);
  
  int suffix = 2;
  while (true) {
    bool conflict = false;
    for (size_t i = 0; i < g_active_notes_count; ++i) {
      if (strcmp(g_notes[i].title, candidate) == 0) { conflict = true; break; }
    }
    if (!conflict) break;
    // Safely format the name
    snprintf(candidate, sizeof(candidate), "%s_%d", base, suffix++);
  }
  if (CreateNote(candidate)) return candidate;
  return "";
}

bool CreateNote(const char* title) {
  if (g_active_notes_count >= MAX_NOTES) return false;

  char safe_title[64];
  strncpy_s(safe_title, sizeof(safe_title), title, _TRUNCATE);
  for (char* c = safe_title; *c != '\0'; ++c) {
    if (!isalnum(*c) && *c != '_' && *c != '-' && *c != ' ') *c = '_';
  }
  
  for (size_t i = 0; i < g_active_notes_count; ++i) {
    if (strcmp(g_notes[i].title, safe_title) == 0) return false;
  }

  NoteFile& note = g_notes[g_active_notes_count];
  strncpy_s(note.title, sizeof(note.title), safe_title, _TRUNCATE);
  snprintf(note.filename, sizeof(note.filename), "%s.md", safe_title);
  
  snprintf(note.content.get(), MAX_NOTE_SIZE, "# %s\n\nStart writing your notes here...\n", safe_title);
  note.is_dirty = true;

  fs::create_directories(g_notes_dir);
  WriteFileContent(g_notes_dir / note.filename, note.content.get());
  note.is_dirty = false;

  WIN32_FILE_ATTRIBUTE_DATA file_info;
  if (GetFileAttributesExA((g_notes_dir / note.filename).string().c_str(), GetFileExInfoStandard, &file_info)) {
      note.date_created = (static_cast<uint64_t>(file_info.ftCreationTime.dwHighDateTime) << 32) | file_info.ftCreationTime.dwLowDateTime;
      note.date_modified = (static_cast<uint64_t>(file_info.ftLastWriteTime.dwHighDateTime) << 32) | file_info.ftLastWriteTime.dwLowDateTime;
  } else {
      note.date_created = 0;
      note.date_modified = 0;
  }

  g_active_notes_count++;
  
  SortNotesArray();
  return true;
}

bool DeleteNote(size_t index) {
  if (index >= g_active_notes_count) return false;
  fs::path file_path = g_notes_dir / g_notes[index].filename;
  if (fs::exists(file_path)) {
    std::error_code ec;
    fs::remove(file_path, ec);
    if (ec) return false;
  }
  
  // Shift elements down
  for (size_t i = index; i < g_active_notes_count - 1; ++i) {
      std::swap(g_notes[i].content, g_notes[i + 1].content);
      strncpy_s(g_notes[i].title, sizeof(g_notes[i].title), g_notes[i + 1].title, _TRUNCATE);
      strncpy_s(g_notes[i].filename, sizeof(g_notes[i].filename), g_notes[i + 1].filename, _TRUNCATE);
      g_notes[i].is_dirty = g_notes[i + 1].is_dirty;
      g_notes[i].date_created = g_notes[i + 1].date_created;
      g_notes[i].date_modified = g_notes[i + 1].date_modified;
  }
  g_active_notes_count--;
  GetNotesWindow().FixupIndicesAfterMutation();
  return true;
}

bool SaveNote(size_t index) {
  if (index >= g_active_notes_count) return false;
  fs::create_directories(g_notes_dir);
  bool ok = WriteFileContent(g_notes_dir / g_notes[index].filename, g_notes[index].content.get());
  if (ok) {
    g_notes[index].is_dirty = false;
    g_save_status_show_until = Clock::now() + std::chrono::seconds(3);
    
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (GetFileAttributesExA((g_notes_dir / g_notes[index].filename).string().c_str(), GetFileExInfoStandard, &file_info)) {
        g_notes[index].date_modified = (static_cast<uint64_t>(file_info.ftLastWriteTime.dwHighDateTime) << 32) | file_info.ftLastWriteTime.dwLowDateTime;
    }
  }
  return ok;
}

void AutoSaveAll() {
  for (size_t i = 0; i < g_active_notes_count; ++i) {
    if (g_notes[i].is_dirty) {
      SaveNote(i);
    }
  }
  g_has_pending_save = false;
}

void TickAutosave() {
  if (g_has_pending_save) {
    auto now = Clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_dirty_time).count();
    if (elapsed_ms >= 3000) {
      GetNotesWindow().FlushEditBuffer();
      AutoSaveAll();
    }
  }
}

void MarkNoteChanged() {
  g_last_dirty_time = Clock::now();
  g_has_pending_save = true;
}

bool ShouldShowSavedStatus() {
  return Clock::now() < g_save_status_show_until;
}

} // namespace dover::overlay::notes
