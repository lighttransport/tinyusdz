// SPDX-License-Identifier: Apache-2.0
#include "browser.hh"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace tusdql {

namespace fs = std::filesystem;

namespace {

std::string ToLower(const std::string& s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string Extension(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  const size_t slash = path.find_last_of("/\\");
  if (slash != std::string::npos && dot < slash) return {};
  return ToLower(path.substr(dot));
}

// Case-insensitive, digit-aware-enough ordering so file lists read naturally.
bool NameLess(const std::string& a, const std::string& b) {
  const std::string la = ToLower(a);
  const std::string lb = ToLower(b);
  if (la != lb) return la < lb;
  return a < b;
}

}  // namespace

bool IsUsdPath(const std::string& path) {
  const std::string ext = Extension(path);
  return ext == ".usd" || ext == ".usda" || ext == ".usdc" || ext == ".usdz";
}

uint64_t ProjectMemoryForFile(const std::string& path, uint64_t file_size) {
  // Multipliers are deliberately coarse. They only need to be right enough to
  // reject the obviously hopeless before a single byte is allocated; the real
  // enforcement is the running MemBudget during load.
  double factor;
  const std::string ext = Extension(path);
  if (ext == ".usdc") {
    // Crate is compressed and composition fans out references/payloads.
    factor = 12.0;
  } else if (ext == ".usda") {
    // ASCII is verbose on disk, so the in-memory form is comparatively smaller,
    // but composition still multiplies it.
    factor = 5.0;
  } else if (ext == ".usdz") {
    // Archive of usdc/usda plus textures; textures dominate and we downsample
    // them, so the effective factor is lower than raw usdc.
    factor = 8.0;
  } else {
    factor = 8.0;
  }

  // Floor: even a tiny file needs the fixed cost of a stage, a BVH and the
  // framebuffers.
  const uint64_t kFixedOverhead = 24ull << 20;
  const double projected = static_cast<double>(file_size) * factor;
  const uint64_t capped =
      projected > 1.8e19 ? UINT64_MAX : static_cast<uint64_t>(projected);
  return kFixedOverhead + capped;
}

bool Browser::Open(const std::string& path, std::string* err) {
  std::error_code ec;
  const std::string in = path.empty() ? std::string(".") : path;

  const fs::path p(in);
  const fs::file_status st = fs::status(p, ec);
  if (ec || !fs::exists(st)) {
    if (err) *err = "no such file or directory: " + in;
    return false;
  }

  std::string want_selected;
  fs::path dir;
  if (fs::is_directory(st)) {
    dir = p;
  } else {
    dir = p.parent_path();
    if (dir.empty()) dir = fs::path(".");
    want_selected = fs::absolute(p, ec).lexically_normal().string();
  }

  if (!Scan(fs::absolute(dir, ec).lexically_normal().string(), err)) {
    return false;
  }

  if (!want_selected.empty()) {
    for (size_t i = 0; i < entries_.size(); i++) {
      if (entries_[i].path == want_selected) {
        selected_ = static_cast<int>(i);
        break;
      }
    }
  }
  return true;
}

bool Browser::Refresh(std::string* err) {
  const FileEntry* sel = SelectedEntry();
  const std::string keep = sel ? sel->path : std::string();
  if (!Scan(dir_, err)) return false;
  if (!keep.empty()) {
    for (size_t i = 0; i < entries_.size(); i++) {
      if (entries_[i].path == keep) {
        selected_ = static_cast<int>(i);
        break;
      }
    }
  }
  return true;
}

bool Browser::Scan(const std::string& dir, std::string* err) {
  std::error_code ec;
  const fs::path root(dir);

  std::vector<FileEntry> dirs;
  std::vector<FileEntry> files;

  auto add_file = [&](const fs::path& p, const std::string& label) {
    FileEntry e;
    e.name = label;
    e.path = p.lexically_normal().string();
    e.size = static_cast<uint64_t>(fs::file_size(p, ec));
    if (ec) {
      e.size = 0;
      ec.clear();
    }
    e.projected_bytes = ProjectMemoryForFile(e.path, e.size);
    e.over_budget = e.projected_bytes > budget_bytes_;
    files.push_back(std::move(e));
  };

  if (recursive_) {
    fs::recursive_directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
      if (err) *err = "cannot read directory: " + dir;
      return false;
    }
    for (const auto& de : it) {
      std::error_code ec2;
      if (!de.is_regular_file(ec2) || ec2) continue;
      const std::string p = de.path().string();
      if (!IsUsdPath(p)) continue;
      add_file(de.path(),
               fs::relative(de.path(), root, ec2).string());
    }
  } else {
    fs::directory_iterator it(
        root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
      if (err) *err = "cannot read directory: " + dir;
      return false;
    }
    for (const auto& de : it) {
      std::error_code ec2;
      if (de.is_directory(ec2) && !ec2) {
        FileEntry e;
        e.name = de.path().filename().string();
        e.path = de.path().lexically_normal().string();
        e.is_dir = true;
        dirs.push_back(std::move(e));
      } else if (de.is_regular_file(ec2) && !ec2) {
        const std::string p = de.path().string();
        if (!IsUsdPath(p)) continue;
        add_file(de.path(), de.path().filename().string());
      }
    }
  }

  std::sort(dirs.begin(), dirs.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return NameLess(a.name, b.name);
            });
  std::sort(files.begin(), files.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return NameLess(a.name, b.name);
            });

  entries_.clear();
  entries_.reserve(dirs.size() + files.size() + 1);

  if (root.has_parent_path() && root.parent_path() != root) {
    FileEntry up;
    up.name = "..";
    up.path = root.parent_path().lexically_normal().string();
    up.is_dir = true;
    up.is_parent = true;
    entries_.push_back(std::move(up));
  }
  for (auto& d : dirs) entries_.push_back(std::move(d));
  for (auto& f : files) entries_.push_back(std::move(f));

  dir_ = root.string();
  scroll_ = 0;
  // Preselect the first previewable file, if there is one.
  selected_ = -1;
  for (size_t i = 0; i < entries_.size(); i++) {
    if (!entries_[i].is_dir) {
      selected_ = static_cast<int>(i);
      break;
    }
  }
  return true;
}

const FileEntry* Browser::SelectedEntry() const {
  if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) {
    return nullptr;
  }
  return &entries_[static_cast<size_t>(selected_)];
}

bool Browser::Select(int index) {
  if (entries_.empty()) return false;
  const int clamped =
      std::max(0, std::min(index, static_cast<int>(entries_.size()) - 1));
  if (clamped == selected_) return false;
  selected_ = clamped;
  return true;
}

bool Browser::MoveSelection(int delta) {
  if (entries_.empty()) return false;
  const int base = (selected_ < 0) ? 0 : selected_ + delta;
  return Select(base);
}

bool Browser::DescendSelected(std::string* err) {
  const FileEntry* sel = SelectedEntry();
  if (!sel || !sel->is_dir) return false;
  const std::string target = sel->path;
  const std::string from = dir_;
  if (!Scan(target, err)) return false;

  // Coming back up: land on the directory we just left so repeated
  // ".."-navigation keeps its place.
  for (size_t i = 0; i < entries_.size(); i++) {
    if (entries_[i].is_dir && !entries_[i].is_parent &&
        entries_[i].path == from) {
      selected_ = static_cast<int>(i);
      break;
    }
  }
  return true;
}

void Browser::EnsureSelectionVisible(int visible_rows) {
  if (selected_ < 0 || visible_rows <= 0) return;
  if (selected_ < scroll_) {
    scroll_ = selected_;
  } else if (selected_ >= scroll_ + visible_rows) {
    scroll_ = selected_ - visible_rows + 1;
  }
  const int max_scroll =
      std::max(0, static_cast<int>(entries_.size()) - visible_rows);
  scroll_ = std::max(0, std::min(scroll_, max_scroll));
}

bool Browser::ScrollBy(int rows, int visible_rows) {
  const int max_scroll =
      std::max(0, static_cast<int>(entries_.size()) - std::max(1, visible_rows));
  const int next = std::max(0, std::min(scroll_ + rows, max_scroll));
  if (next == scroll_) return false;
  scroll_ = next;
  return true;
}

}  // namespace tusdql
