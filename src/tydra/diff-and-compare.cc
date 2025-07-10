#include "diff-and-compare.hh"

namespace tinyusdz {
namespace tydra {

namespace detail {

#if 1
// return true when there is a diff.
static bool ComputeDiffImpl(
  uint32_t depth,
  const std::string &root, const PrimSpec &lhs, const PrimSpec &rhs,
  std::unordered_map<std::string, PrimSpecDiff> &psDiffs,
  std::unordered_map<std::string, PropDiff> &propDiffs) {

  if (depth > (1024*1024)) {
    return false;
  }

  // TODO
  (void)root;
  (void)psDiffs;
  (void)propDiffs;


  for (const auto &child : lhs.children()) {
    for (const auto &r_child : rhs.children()) {
      // TODO
      (void)r_child;
      (void)child;
      
    }
  }

  return true;

}
#endif


} // namespace

void Diff(const Layer &lhs, const Layer &rhs,
  std::unordered_map<std::string, PrimSpecDiff> &psDiffs,
  std::unordered_map<std::string, PropDiff> &propDiffs) {

  std::set<std::string> l_names;
  for (const auto &child : lhs.primspecs()) {
    l_names.insert(child.first);
  }

  std::set<std::string> r_names;
  for (const auto &child : rhs.primspecs()) {
    r_names.insert(child.first);
  }

  std::set<std::string> addedNames;
  std::set<std::string> deletedNames;
  std::set<std::string> modifiedNames;

  for (const auto &l_name : l_names) {
    if (!r_names.count(l_name)) {
      deletedNames.insert(l_name);
    }
  }

  for (const auto &r_name : r_names) {
    if (!l_names.count(r_name)) {
      addedNames.insert(r_name);
    }
  }

  for (const auto &child : lhs.primspecs()) {
    for (const auto &r_child : rhs.primspecs()) {

      if (child.first == r_child.first) {
        if (detail::ComputeDiffImpl(0, "/", child.second, r_child.second, psDiffs, propDiffs)) {
          modifiedNames.insert(child.first);
        }
      } else {
        
        modifiedNames.insert(child.first);
      }
    }
  }

  // TODO
  PrimSpecDiff psd;
  //psd.addedPS = addedNames;
  //psd.deletedPS = deletedNames;
  //psd.modifiedPS = modifiedNames;
  
}

} // namespace tydra
} // namespace tinyusdz
