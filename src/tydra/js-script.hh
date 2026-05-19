#pragma once

#include <string>
#include <memory>

#include "../core/prim.hh"
#include "../layer.hh"
#include "../stage.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json_fwd.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

struct LayerMetas;
class Attribute;
class Layer;

namespace tydra {

// ---------------------------------------------------------------------------
// JavaScript Engine State
// ---------------------------------------------------------------------------
// Opaque state for a persistent QuickJS engine attached to an MCP session.
// Lifetime managed by InitJSEngine / DestroyJSEngine.
struct JSEngineState {
  void *runtime{nullptr};   // JSRuntime*
  void *context{nullptr};   // JSContext*
  bool initialized{false};
};

// ---------------------------------------------------------------------------
// Engine lifecycle (per MCP session)
// ---------------------------------------------------------------------------
bool InitJSEngine(JSEngineState &engine, std::string &err);
bool DestroyJSEngine(JSEngineState &engine);

// ---------------------------------------------------------------------------
// Register the tinyusdz.* module against a Stage reference.
// After this, JS scripts can use:
//   tinyusdz.stage.info()
//   tinyusdz.prim.list(path)
//   tinyusdz.prim.get(path)
//   tinyusdz.prim.create(path, typeName)
//   tinyusdz.attr.list(path)
//   tinyusdz.attr.get(path, name)
//   tinyusdz.composition.referenceAdd(path, assetPath, ...)
//   tinyusdz.query.findAllByType(typeName)
//   tinyusdz.value.create(typeName, data)
// ---------------------------------------------------------------------------
bool RegisterUSDModule(JSEngineState &engine, Stage *stage, std::string &err);

// ---------------------------------------------------------------------------
// Execute JavaScript and capture the return value as JSON.
// The script runs in the engine's JS context and can access the registered
// tinyusdz.* module.
//
// Returns:
//   - ok=true, return_value contains the script's return value as JSON
//   - ok=false, err contains the JavaScript error message
// ---------------------------------------------------------------------------
bool RunJSScript(JSEngineState &engine, const std::string &js_code,
                 nlohmann::json &return_value, std::string &err);

// ---------------------------------------------------------------------------
// Legacy API (kept for backward compatibility)
// ---------------------------------------------------------------------------
bool RunJSScript(const std::string &js_code, std::string &err);
bool RunJSScriptWithLayerMetas(const std::string &js_code,
                               const LayerMetas *layer_metas,
                               std::string &err);
bool RunJSScriptWithAttribute(const std::string &js_code,
                              const Attribute *attribute, std::string &err);
bool RunJSScriptWithLayer(const std::string &js_code, const class Layer *layer,
                          std::string &err);

} // namespace tydra
} // namespace tinyusdz
