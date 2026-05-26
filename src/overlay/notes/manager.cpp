#include "overlay/notes/manager.h"
#include "overlay/notes/layout.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace dover::overlay::notes {

namespace {
std::vector<NoteFile> g_notes;
fs::path g_notes_dir;

// Autosave debounce: save 3s after last dirty change
Clock::time_point g_last_dirty_time{};
bool g_has_pending_save = false;
Clock::time_point g_save_status_show_until{};

std::string ReadFileContent(const fs::path& path) {
  std::ifstream f(path, std::ios::in | std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool WriteFileContent(const fs::path& path, const std::string& content) {
  std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(content.data(), static_cast<std::streamsize>(content.size()));
  return f.good();
}

void LoadNotesFromDisk() {
  g_notes.clear();
  if (!fs::exists(g_notes_dir)) {
    fs::create_directories(g_notes_dir);
    return;
  }
  for (const auto& entry : fs::directory_iterator(g_notes_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".md") continue;
    NoteFile note;
    note.filename = entry.path().filename().string();
    note.title = entry.path().stem().string();
    note.content = ReadFileContent(entry.path());
    note.is_dirty = false;
    g_notes.push_back(std::move(note));
  }
  // Sort alphabetically by title
  std::sort(g_notes.begin(), g_notes.end(), [](const NoteFile& a, const NoteFile& b) {
    return a.title < b.title;
  });
}

} // namespace

bool InitializeNotesManager(const fs::path& notes_dir) {
  g_notes_dir = notes_dir;
  LoadNotesFromDisk();

  // Create a default note if game has none
  if (g_notes.empty()) {
    CreateNote("general");
  }
  return true;
}

std::vector<NoteFile>& GetNotes() {
  return g_notes;
}

const fs::path& GetNotesDir() {
  return g_notes_dir;
}

std::string CreateAutoNote() {
  const std::string base = "untitled";
  std::string candidate = base;
  int suffix = 2;
  while (true) {
    bool conflict = false;
    for (const auto& n : g_notes) {
      if (n.title == candidate) { conflict = true; break; }
    }
    if (!conflict) break;
    candidate = base + "_" + std::to_string(suffix++);
  }
  if (CreateNote(candidate)) return candidate;
  return {};
}

bool CreateNote(const std::string& title) {
  std::string safe_title = title;
  for (char& c : safe_title) {
    if (!isalnum(c) && c != '_' && c != '-' && c != ' ') c = '_';
  }
  for (const auto& n : g_notes) {
    if (n.title == safe_title) return false;
  }

  NoteFile note;
  note.title = safe_title;
  note.filename = safe_title + ".md";
  note.content = "# " + safe_title + "\n\nStart writing your notes here...\n";
  note.is_dirty = true;

  fs::create_directories(g_notes_dir);
  WriteFileContent(g_notes_dir / note.filename, note.content);
  note.is_dirty = false;

  g_notes.push_back(std::move(note));
  std::sort(g_notes.begin(), g_notes.end(), [](const NoteFile& a, const NoteFile& b) {
    return a.title < b.title;
  });
  return true;
}

bool DeleteNote(size_t index) {
  if (index >= g_notes.size()) return false;
  fs::path file_path = g_notes_dir / g_notes[index].filename;
  if (fs::exists(file_path)) {
    std::error_code ec;
    fs::remove(file_path, ec);
    if (ec) return false;
  }
  g_notes.erase(g_notes.begin() + static_cast<ptrdiff_t>(index));
  return true;
}

bool SaveNote(size_t index) {
  if (index >= g_notes.size()) return false;
  fs::create_directories(g_notes_dir);
  bool ok = WriteFileContent(g_notes_dir / g_notes[index].filename, g_notes[index].content);
  if (ok) {
    g_notes[index].is_dirty = false;
    g_save_status_show_until = Clock::now() + std::chrono::seconds(3);
  }
  return ok;
}

void AutoSaveAll() {
  for (size_t i = 0; i < g_notes.size(); ++i) {
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
