#include "command-and-history.hh"

namespace tinyusdz {
namespace tydra {

bool HistoryQueue::Push(EditHistory &&hist) {
  if (history_queues.size() > kMaxHistories) {
    return false;
  }

  history_queues.emplace_back(std::move(hist));

  return true;
}

bool HistoryQueue::Undo() {
  if (history_queues.empty()) {
    return false;
  }

  // reuse maxHistories for undo buffer.
  if (undo_queues.size() > kMaxHistories) {
    return false;
  }

  undo_queues.emplace_back(history_queues.back());
  history_queues.pop_back();

  return true;
}

bool HistoryQueue::Redo() {
  if (undo_queues.empty()) {
    return false;
  }

  history_queues.emplace_back(undo_queues.back());
  undo_queues.pop_back();

  return true;
}

} // namespace tydra
} // namespace tinyusdz
