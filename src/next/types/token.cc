// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - TfToken implementation.

#include "token.hh"

#include <memory>
#include <unordered_map>
#include <vector>
#if defined(TINYUSDZ_ENABLE_THREAD)
#include <atomic>
#include <mutex>
#include <shared_mutex>
#endif

namespace tinyusdz {
namespace next {

namespace {
struct SVHash {
  using is_transparent = void;
  size_t operator()(std::string_view s) const noexcept {
    return std::hash<std::string_view>{}(s);
  }
};
struct SVEq {
  using is_transparent = void;
  bool operator()(std::string_view a, std::string_view b) const noexcept {
    return a == b;
  }
};
}  // namespace

// The interning table mirrors PropNameTable/TypeNameTable: owned strings via
// unique_ptr (stable addresses), a string_view->id map, and — under threads — an
// RCU-lite immutable snapshot republished on every insert so read HITS skip the
// rwlock.
struct TfTokenTable::Impl {
  std::vector<std::unique_ptr<std::string>> names_;
  std::unordered_map<std::string_view, uint32_t, SVHash, SVEq> name_to_id_;
#if defined(TINYUSDZ_ENABLE_THREAD)
  struct Snapshot {
    std::unordered_map<std::string_view, uint32_t, SVHash, SVEq> map;
    std::vector<const std::string*> by_id;
  };
  void publish_snapshot_locked() {
    auto snap = std::make_unique<Snapshot>();
    snap->map = name_to_id_;
    snap->by_id.reserve(names_.size());
    for (const auto& n : names_) snap->by_id.push_back(n.get());
    snapshot_.store(snap.get(), std::memory_order_release);
    retired_.push_back(std::move(snap));
  }
  mutable std::shared_mutex mu_;
  mutable std::atomic<const Snapshot*> snapshot_{nullptr};
  std::vector<std::unique_ptr<Snapshot>> retired_;
  std::atomic<bool> frozen_{false};
#endif

  uint32_t intern(std::string_view s) {
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
      auto sit = snap->map.find(s);
      if (sit != snap->map.end()) return sit->second;
    }
    if (frozen_.load(std::memory_order_acquire)) {
      auto it = name_to_id_.find(s);
      if (it != name_to_id_.end()) return it->second;
      frozen_.store(false, std::memory_order_release);
    }
    {
      std::shared_lock<std::shared_mutex> rlk(mu_);
      auto it = name_to_id_.find(s);
      if (it != name_to_id_.end()) return it->second;
    }
    std::unique_lock<std::shared_mutex> wlk(mu_);
    auto it = name_to_id_.find(s);
    if (it != name_to_id_.end()) return it->second;
    uint32_t id = static_cast<uint32_t>(names_.size());
    auto owned = std::make_unique<std::string>(s);
    const std::string_view key(*owned);
    names_.push_back(std::move(owned));
    name_to_id_.emplace(key, id);
    publish_snapshot_locked();
    return id;
#else
    auto it = name_to_id_.find(s);
    if (it != name_to_id_.end()) return it->second;
    uint32_t id = static_cast<uint32_t>(names_.size());
    auto owned = std::make_unique<std::string>(s);
    const std::string_view key(*owned);
    names_.push_back(std::move(owned));
    name_to_id_.emplace(key, id);
    return id;
#endif
  }

  const std::string& get(uint32_t id) const {
    static const std::string empty;
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
      if (id < snap->by_id.size()) return *snap->by_id[id];
    }
    if (!frozen_.load(std::memory_order_acquire)) {
      std::shared_lock<std::shared_mutex> rlk(mu_);
      if (id >= names_.size()) return empty;
      return *names_[id];
    }
#endif
    if (id >= names_.size()) return empty;
    return *names_[id];
  }

  bool contains(std::string_view s) const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    if (const Snapshot* snap = snapshot_.load(std::memory_order_acquire)) {
      if (snap->map.find(s) != snap->map.end()) return true;
    }
    if (!frozen_.load(std::memory_order_acquire)) {
      std::shared_lock<std::shared_mutex> rlk(mu_);
      return name_to_id_.find(s) != name_to_id_.end();
    }
#endif
    return name_to_id_.find(s) != name_to_id_.end();
  }

  size_t size() const {
#if defined(TINYUSDZ_ENABLE_THREAD)
    std::shared_lock<std::shared_mutex> rlk(mu_);
#endif
    return names_.size();
  }
};

TfTokenTable::TfTokenTable() : impl_(new Impl()) { register_common_tokens(); }
TfTokenTable::~TfTokenTable() { delete impl_; }

TfToken TfTokenTable::intern(std::string_view s) {
  return TfToken::FromId(impl_->intern(s));
}
const std::string& TfTokenTable::get(uint32_t id) const { return impl_->get(id); }
bool TfTokenTable::contains(std::string_view s) const {
  return impl_->contains(s);
}
size_t TfTokenTable::size() const { return impl_->size(); }

#if defined(TINYUSDZ_ENABLE_THREAD)
void TfTokenTable::freeze() {
  impl_->frozen_.store(true, std::memory_order_release);
}
void TfTokenTable::unfreeze() {
  impl_->frozen_.store(false, std::memory_order_release);
}
#else
void TfTokenTable::freeze() {}
void TfTokenTable::unfreeze() {}
#endif

void TfTokenTable::register_common_tokens() {
  // id 0 MUST be the empty string so a default-constructed TfToken is empty.
  intern("");

  // Model kind.
  intern("component");
  intern("group");
  intern("assembly");
  intern("subcomponent");
  // Purpose.
  intern("default");
  intern("render");
  intern("proxy");
  intern("guide");
  // Visibility.
  intern("inherited");
  intern("invisible");
  // Orientation.
  intern("rightHanded");
  intern("leftHanded");
  // Primvar interpolation.
  intern("constant");
  intern("uniform");
  intern("varying");
  intern("vertex");
  intern("faceVarying");
  // Subdivision scheme / boundary.
  intern("none");
  intern("catmullClark");
  intern("loop");
  intern("bilinear");
  intern("edgeAndCorner");
  intern("edgeOnly");
  // Axes.
  intern("X");
  intern("Y");
  intern("Z");
}

TfTokenTable& GetTfTokenTable() {
  static TfTokenTable g_table;
  return g_table;
}

// TfToken members that touch the global table.
TfToken::TfToken(std::string_view s) : id_(GetTfTokenTable().intern(s).id_) {}
TfToken::TfToken(const char* s)
    : id_(s ? GetTfTokenTable().intern(std::string_view(s)).id_ : 0) {}
TfToken::TfToken(const std::string& s)
    : id_(GetTfTokenTable().intern(std::string_view(s)).id_) {}

const std::string& TfToken::str() const { return GetTfTokenTable().get(id_); }

}  // namespace next
}  // namespace tinyusdz
