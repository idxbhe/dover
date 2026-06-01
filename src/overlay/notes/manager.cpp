#include "overlay/notes/manager.h"
#include "overlay/notes/layout.h"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <array>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace dover::overlay::notes {

namespace {
std::array<NoteFile, MAX_NOTES> g_notes;
size_t g_active_notes_count = 0;
fs::path g_notes_dir;

struct PendingNoteData {
    char filename[64];
    char title[64];
    std::string content;
    uint64_t date_created;
    uint64_t date_modified;
};
std::vector<PendingNoteData> g_pending_load_notes;
std::atomic<bool> g_pending_load_ready{false};

// Autosave debounce: save 3s after last dirty change
Clock::time_point g_last_dirty_time{};
bool g_has_pending_save = false;
Clock::time_point g_save_status_show_until{};

NoteSortCriteria g_sort_criteria = NoteSortCriteria::Name;
bool g_sort_ascending = true;

enum class IoCommandType {
    None,
    Save,
    Delete
};

struct IoCommand {
    IoCommandType type = IoCommandType::None;
    char filename[64];
    std::unique_ptr<char[]> content;
};

constexpr size_t IO_QUEUE_SIZE = 8;
std::array<IoCommand, IO_QUEUE_SIZE> g_io_queue;
std::atomic<size_t> g_io_head{0};
std::atomic<size_t> g_io_tail{0};

std::thread g_io_thread;
std::atomic<bool> g_io_thread_running{false};
std::mutex g_io_mutex;
std::condition_variable g_io_cv;

void IoWorkerRoutine() {
    std::vector<PendingNoteData> loaded;
    if (fs::exists(g_notes_dir)) {
        for (const auto& entry : fs::directory_iterator(g_notes_dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
            if (loaded.size() >= MAX_NOTES) break;
            
            PendingNoteData tn{};
            std::string fname = entry.path().filename().string();
            std::string stem = entry.path().stem().string();
            strncpy_s(tn.filename, sizeof(tn.filename), fname.c_str(), _TRUNCATE);
            strncpy_s(tn.title, sizeof(tn.title), stem.c_str(), _TRUNCATE);
            
            std::ifstream f(entry.path(), std::ios::in | std::ios::binary);
            if (f) {
               f.seekg(0, std::ios::end);
               size_t size = static_cast<size_t>(f.tellg());
               f.seekg(0, std::ios::beg);
               if (size > MAX_NOTE_SIZE - 1) size = MAX_NOTE_SIZE - 1;
               tn.content.resize(size);
               f.read(&tn.content[0], size);
               tn.content.erase(std::remove(tn.content.begin(), tn.content.end(), '\r'), tn.content.end());
            }
            
            WIN32_FILE_ATTRIBUTE_DATA file_info;
            if (GetFileAttributesExA(entry.path().string().c_str(), GetFileExInfoStandard, &file_info)) {
                tn.date_created = (static_cast<uint64_t>(file_info.ftCreationTime.dwHighDateTime) << 32) | file_info.ftCreationTime.dwLowDateTime;
                tn.date_modified = (static_cast<uint64_t>(file_info.ftLastWriteTime.dwHighDateTime) << 32) | file_info.ftLastWriteTime.dwLowDateTime;
            }
            loaded.push_back(std::move(tn));
        }
    }
    
    if (loaded.empty()) {
        PendingNoteData tn{};
        strncpy_s(tn.title, sizeof(tn.title), "general", _TRUNCATE);
        strncpy_s(tn.filename, sizeof(tn.filename), "general.md", _TRUNCATE);
        tn.content = "# general\n\nStart writing your notes here...\n";
        
        fs::create_directories(g_notes_dir);
        std::ofstream f(g_notes_dir / tn.filename, std::ios::out | std::ios::binary | std::ios::trunc);
        if (f) f.write(tn.content.c_str(), tn.content.length());
        
        WIN32_FILE_ATTRIBUTE_DATA file_info;
        if (GetFileAttributesExA((g_notes_dir / tn.filename).string().c_str(), GetFileExInfoStandard, &file_info)) {
            tn.date_created = (static_cast<uint64_t>(file_info.ftCreationTime.dwHighDateTime) << 32) | file_info.ftCreationTime.dwLowDateTime;
            tn.date_modified = (static_cast<uint64_t>(file_info.ftLastWriteTime.dwHighDateTime) << 32) | file_info.ftLastWriteTime.dwLowDateTime;
        }
        loaded.push_back(std::move(tn));
    }
    
    g_pending_load_notes = std::move(loaded);
    g_pending_load_ready.store(true);

    while (g_io_thread_running.load(std::memory_order_relaxed)) {
        size_t tail = g_io_tail.load(std::memory_order_acquire);
        size_t head = g_io_head.load(std::memory_order_acquire);
        
        if (tail == head) {
            std::unique_lock<std::mutex> lock(g_io_mutex);
            g_io_cv.wait(lock, []() {
                return g_io_head.load(std::memory_order_acquire) != g_io_tail.load(std::memory_order_acquire) || !g_io_thread_running.load(std::memory_order_relaxed);
            });
            continue;
        }

        IoCommand& cmd = g_io_queue[tail];
        if (cmd.type == IoCommandType::Save) {
            fs::create_directories(g_notes_dir);
            std::ofstream f(g_notes_dir / cmd.filename, std::ios::out | std::ios::binary | std::ios::trunc);
            if (f) f.write(cmd.content.get(), strlen(cmd.content.get()));
        } else if (cmd.type == IoCommandType::Delete) {
            fs::path file_path = g_notes_dir / cmd.filename;
            if (fs::exists(file_path)) {
                std::error_code ec;
                fs::remove(file_path, ec);
            }
        }
        
        cmd.type = IoCommandType::None;
        g_io_tail.store((tail + 1) % IO_QUEUE_SIZE, std::memory_order_release);
    }
}

void PushIoCommand(IoCommandType type, const char* filename, const char* content) {
    size_t head = g_io_head.load(std::memory_order_relaxed);
    size_t next_head = (head + 1) % IO_QUEUE_SIZE;
    
    if (next_head == g_io_tail.load(std::memory_order_acquire)) {
        return; // drop on full
    }
    
    IoCommand& cmd = g_io_queue[head];
    cmd.type = type;
    strncpy_s(cmd.filename, sizeof(cmd.filename), filename, _TRUNCATE);
    if (content && cmd.content) {
        strncpy_s(cmd.content.get(), MAX_NOTE_SIZE, content, _TRUNCATE);
    }
    
    g_io_head.store(next_head, std::memory_order_release);
    g_io_cv.notify_one();
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
  // Pre-allocate IO queue buffers
  for (size_t i = 0; i < IO_QUEUE_SIZE; ++i) {
      if (!g_io_queue[i].content) {
          g_io_queue[i].content = std::make_unique<char[]>(MAX_NOTE_SIZE);
      }
  }

  g_io_thread_running.store(true, std::memory_order_relaxed);
  g_io_thread = std::thread(IoWorkerRoutine);
  
  return true;
}

void ShutdownNotesManager() {
    if (g_io_thread_running.load()) {
        g_io_thread_running.store(false, std::memory_order_relaxed);
        g_io_cv.notify_one();
        if (g_io_thread.joinable()) {
            g_io_thread.join();
        }
    }
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
    PushIoCommand(IoCommandType::Save, note.filename, note.content.get());

  note.is_dirty = false;
  note.date_created = Clock::now().time_since_epoch().count();
  note.date_modified = note.date_created;

  g_active_notes_count++;
  
  SortNotesArray();
  return true;
}

bool DeleteNote(size_t index) {
  if (index >= g_active_notes_count) return false;
  PushIoCommand(IoCommandType::Delete, g_notes[index].filename, nullptr);
  
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
  PushIoCommand(IoCommandType::Save, g_notes[index].filename, g_notes[index].content.get());
  
  g_notes[index].is_dirty = false;
  g_save_status_show_until = Clock::now() + std::chrono::seconds(3);
  g_notes[index].date_modified = Clock::now().time_since_epoch().count();
  return true;
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
  if (g_pending_load_ready.load()) {
      g_active_notes_count = 0;
      for (auto& tn : g_pending_load_notes) {
          if (g_active_notes_count >= MAX_NOTES) break;
          NoteFile& note = g_notes[g_active_notes_count];
          strncpy_s(note.filename, sizeof(note.filename), tn.filename, _TRUNCATE);
          strncpy_s(note.title, sizeof(note.title), tn.title, _TRUNCATE);
          strncpy_s(note.content.get(), MAX_NOTE_SIZE, tn.content.c_str(), _TRUNCATE);
          note.date_created = tn.date_created;
          note.date_modified = tn.date_modified;
          note.is_dirty = false;
          g_active_notes_count++;
      }
      g_pending_load_notes.clear();
      g_pending_load_ready.store(false);
      SortNotesArray();
  }

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
