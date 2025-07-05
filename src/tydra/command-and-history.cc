#include <string>

#include "command-and-history.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif


namespace tinyusdz {
namespace tydra {

bool HistoryQueue::push(EditHistory &&hist) {
  if (history_queues.size() > kMaxHistories) {
    return false;
  }

  history_queues.emplace_back(std::move(hist));

  return true;
}

bool HistoryQueue::undo() {
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

bool HistoryQueue::redo() {
  if (undo_queues.empty()) {
    return false;
  }

  history_queues.emplace_back(undo_queues.back());
  undo_queues.pop_back();

  return true;
}

std::string HistoryQueue::to_json_string() const {
  nlohmann::json j;
  j["history"] = nlohmann::json::array();

  for (const auto &hist : history_queues) {
    nlohmann::json hist_json;
    hist_json["cmd"] = static_cast<int>(hist.cmd);
    hist_json["op"] = static_cast<int>(hist.op);
    hist_json["arg"] = hist.arg;
    hist_json["id"] = hist.id;
    // Serialize layer if needed
    // hist_json["layer"] = ...; // Implement layer serialization if necessary

    j["history"].push_back(hist_json);
  }

  return j.dump();  
}

} // namespace tydra
} // namespace tinyusdz
