#include "overlay/notes/manager.h"

#include <windows.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "psapi.lib")

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace dover::overlay::notes {

namespace {
std::vector<NoteFile> g_notes;
std::string g_game_name;
fs::path g_notes_dir;

// Autosave debounce: save 2s after last dirty change
Clock::time_point g_last_dirty_time{};
bool g_has_pending_save = false;

std::string NormalizeGameName(const std::string& exe_name) {
  std::string name = exe_name;
  // Strip .exe extension if present
  if (name.size() > 4 &&
      (name.substr(name.size() - 4) == ".exe" ||
       name.substr(name.size() - 4) == ".EXE")) {
    name = name.substr(0, name.size() - 4);
  }
  // Lowercase and replace spaces/special chars with underscore
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return static_cast<char>(::tolower(c)); });
  for (char& c : name) {
    if (!isalnum(c) && c != '_' && c != '-') c = '_';
  }
  return name;
}

std::string WstrToUtf8(const std::wstring& wstr) {
  if (wstr.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string result(size - 1, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, result.data(), size, nullptr, nullptr);
  return result;
}

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

bool InitializeNotesManager(const std::wstring& localappdata_base, const std::string& game_exe) {
  g_game_name = NormalizeGameName(game_exe);
  
  std::wstring base_dir = localappdata_base + L"\\dover\\notes\\" +
                          std::wstring(g_game_name.begin(), g_game_name.end());
  g_notes_dir = fs::path(base_dir);
  
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

const std::string& GetNotesGameName() {
  return g_game_name;
}

std::string CreateAutoNote() {
  const std::string base = "untitled";
  // Try "untitled" first, then "untitled_2", "untitled_3", ...
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
  // Normalize title to safe filename
  std::string safe_title = title;
  for (char& c : safe_title) {
    if (!isalnum(c) && c != '_' && c != '-' && c != ' ') c = '_';
  }
  // Check uniqueness
  for (const auto& n : g_notes) {
    if (n.title == safe_title) return false;
  }

  NoteFile note;
  note.title = safe_title;
  note.filename = safe_title + ".md";
  note.content = "# " + safe_title + "\n\nStart writing your notes here...\n";
  note.is_dirty = true;

  // Save immediately to disk
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
  if (ok) g_notes[index].is_dirty = false;
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
  bool any_dirty = false;
  for (const auto& n : g_notes) {
    if (n.is_dirty) { any_dirty = true; break; }
  }

  if (any_dirty) {
    auto now = Clock::now();
    if (!g_has_pending_save) {
      g_last_dirty_time = now;
      g_has_pending_save = true;
    } else {
      // Steam-like: save 500ms after last change
      auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_last_dirty_time).count();
      if (elapsed_ms >= 500) {
        AutoSaveAll();
      }
    }
  } else {
    g_has_pending_save = false;
  }
}

} // namespace dover::overlay::notes
