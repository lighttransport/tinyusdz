// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
// Simple USD Layer edit commands and linked-list of USD Layer history

#include <string>
#include <memory>
#include <vector>
#include <deque>

#include "prim-types.hh"

namespace tinyusdz {
namespace tydra {

enum class EditCommand
{
  Create,
  Modify,
  Delete,
};

enum class EditOp
{
  NewLayer,
  LoadLayer,
  SaveLayer,
  XformTranslation,
  XformRotation,
  XformScale,
  XformTransform,
  XformPivotTranslation,
  XformPivotRotation,
  XformPivotScale,
};


// Only support the move operation for the efficiency.
struct EditHistory
{
  EditCommand cmd;
  EditOp op;
  std::string arg;
  uint64_t id;

  tinyusdz::Layer layer;
};

// We only support queues for now(no history graph)
class HistoryQueue
{
 public:
  constexpr static uint32_t kMaxHistories = 1024ul * 1024ul;

  bool push(EditHistory &&hist);

  bool undo();
  bool redo();

 private:
  std::deque<EditHistory> history_queues;
  std::deque<EditHistory> undo_queues;
  
};

} // namespace tydra
} // namespace tinyusdz
