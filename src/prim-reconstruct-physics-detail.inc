// SPDX-License-Identifier: Apache 2.0
// Shared physics-joint reconstruct helpers, #included INSIDE namespace tinyusdz::prim by
// prim-reconstruct-physics{,3,4}.cc (each instantiates them for its joint types; internal linkage).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
// Helper: check if any property starts with a given prefix
static bool HasPropertyPrefix(const PropertyMap &properties, const std::string &prefix) {
  for (const auto &prop : properties) {
    if (prop.first.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

// Helper: reconstruct PhysicsJointBase properties (shared by all joint types)
template <typename JointT>
static bool ReconstructJointBaseProperties(
    std::set<std::string> &table,
    PropertyMap &properties,
    JointT *joint,
    std::string *warn,
    std::string *err,
    const PrimReconstructOptions &options) {

  (void)warn;
  (void)err;
  (void)options;

  // Check for extension properties to auto-create typed API structs. Other
  // vendor namespaces (physxJoint:* / physxLimit:* / state:*) intentionally
  // remain as untyped properties and round-trip through the generic props map.
  if (!joint->mjcJoint.has_value() && HasPropertyPrefix(properties, "mjc:")) {
    joint->mjcJoint = MjcJointAPI();
  }
  if (!joint->newtonMimic.has_value() && HasPropertyPrefix(properties, "newton:")) {
    joint->newtonMimic = NewtonMimicAPI();
  }

  for (auto &prop : properties) {
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, "physics:body0", joint->body0)
    PARSE_SINGLE_TARGET_PATH_RELATION(table, prop, "physics:body1", joint->body1)
  }

  return true;
}
#pragma GCC diagnostic pop
