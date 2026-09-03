// SPDX-License-Identifier: Apache-2.0
//
// lusdquicklook — folder browsing and the file-list model.
//
// Pure data + filesystem access; no drawing and no lightui dependency, so it is
// unit-testable and usable from the headless path.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lusdql {

struct FileEntry {
  std::string name;   // display label (basename, or relative path when recursive)
  std::string path;   // full path to open
  uint64_t size = 0;  // bytes on disk (0 for directories)
  bool is_dir = false;
  bool is_parent = false;  // the ".." row

  // Set when the pre-open projection says this file cannot fit the budget.
  // Such rows are still listed and still clickable — the user gets an explicit
  // "too large" card rather than silently nothing.
  bool over_budget = false;

  // Projected peak resident bytes; shown as a hint on over-budget rows.
  uint64_t projected_bytes = 0;
};

// True for .usd/.usda/.usdc/.usdz (case-insensitive).
bool IsUsdPath(const std::string& path);

// Cheap pre-open projection of peak resident bytes, from file size and format.
// USDC is compressed and fans out hard on composition; USDA is verbose on disk
// and expands less; USDZ is a (usually uncompressed) archive of the above.
// Deliberately rough — it exists so that hopeless files never allocate at all.
uint64_t ProjectMemoryForFile(const std::string& path, uint64_t file_size);

class Browser {
 public:
  // `path` may be a directory (browse it) or a file (browse its parent and
  // preselect it). Returns false and fills `err` if the path does not exist.
  bool Open(const std::string& path, std::string* err);

  // Re-scan the current directory, preserving the selected path if it survives.
  bool Refresh(std::string* err);

  void SetRecursive(bool recursive) { recursive_ = recursive; }
  bool recursive() const { return recursive_; }

  // Files projected above this are flagged `over_budget`. Set before Open().
  void SetMemoryBudget(uint64_t bytes) { budget_bytes_ = bytes; }

  const std::string& dir() const { return dir_; }
  const std::vector<FileEntry>& entries() const { return entries_; }

  int selected() const { return selected_; }
  const FileEntry* SelectedEntry() const;

  // Clamps to a valid row; returns true when the selection actually changed.
  bool Select(int index);
  bool MoveSelection(int delta);

  // Enter the selected row if it is a directory. Returns true when the
  // directory changed (caller should re-render the list).
  bool DescendSelected(std::string* err);

  // Index of the first visible row.
  int scroll() const { return scroll_; }
  // Keeps `selected_` visible within a window of `visible_rows`.
  void EnsureSelectionVisible(int visible_rows);
  bool ScrollBy(int rows, int visible_rows);

  int RowCount() const { return static_cast<int>(entries_.size()); }

 private:
  bool Scan(const std::string& dir, std::string* err);

  std::string dir_;
  std::vector<FileEntry> entries_;
  int selected_ = -1;
  int scroll_ = 0;
  bool recursive_ = false;
  uint64_t budget_bytes_ = 512ull << 20;
};

}  // namespace lusdql
