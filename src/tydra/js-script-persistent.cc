#include "js-script.hh"
#include "value-to-json.hh"

#include "../tinyusdz.hh"
#include "../stage.hh"

#if defined(TINYUSDZ_WITH_QJS)

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "external/quickjs-ng/quickjs.h"
#include "external/quickjs-ng/quickjs-libc.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <sstream>
#include <functional>

#include "external/jsonhpp/nlohmann/json.hpp"

namespace tinyusdz {
namespace tydra {

namespace {

// ---------------------------------------------------------------------------
// Helpers: JSValue <-> JSON marshalling
// ---------------------------------------------------------------------------
JSValue JSONToJSValue(JSContext *ctx, const nlohmann::json &j) {
  std::string json_str = j.dump();
  return JS_ParseJSON(ctx, json_str.c_str(), json_str.length(), "<json>");
}

nlohmann::json JSValueToJSON(JSContext *ctx, JSValue val) {
  const char *json_str = JS_ToCString(ctx, val);
  if (!json_str) return nullptr;
  nlohmann::json j;
  try {
    j = nlohmann::json::parse(json_str);
  } catch (...) {
    j = nullptr;
  }
  JS_FreeCString(ctx, json_str);
  return j;
}

// ---------------------------------------------------------------------------
// Current context for JS function callbacks
// ---------------------------------------------------------------------------
// These global pointers are set before executing each script. Since
// QuickJS is single-threaded per runtime, this is safe within a session.
static Stage *g_js_stage = nullptr;

// ---------------------------------------------------------------------------
// JS function: tinyusdz.stage.info()
// ---------------------------------------------------------------------------
static JSValue js_stage_info(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int magic,
                             JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)argv; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }

  nlohmann::json info;
  const auto &metas = g_js_stage->metas();
  info["upAxis"] = tinyusdz::to_string(metas.upAxis.get_value());
  info["defaultPrim"] = metas.defaultPrim.str();
  info["metersPerUnit"] = metas.metersPerUnit.get_value();
  info["timeCodesPerSecond"] = metas.timeCodesPerSecond.get_value();
  info["framesPerSecond"] = metas.framesPerSecond.get_value();
  info["startTimeCode"] = metas.startTimeCode.get_value();

  double endTC = metas.endTimeCode.get_value();
  if (std::isinf(endTC)) {
    info["endTimeCode"] = nullptr;
  } else {
    info["endTimeCode"] = endTC;
  }
  info["rootPrimCount"] = g_js_stage->root_prims().size();

  return JSONToJSValue(ctx, info);
}

// ---------------------------------------------------------------------------
// Recursive prim tree to JSON helper
// ---------------------------------------------------------------------------
static void PrimToJSJSON(const Prim &prim, nlohmann::json &j, int max_depth) {
  j["name"] = prim.element_name();
  j["type"] = prim.prim_type_name();
  switch (prim.specifier()) {
    case Specifier::Def: j["specifier"] = "def"; break;
    case Specifier::Over: j["specifier"] = "over"; break;
    case Specifier::Class: j["specifier"] = "class"; break;
    default: j["specifier"] = "invalid"; break;
  }

  const auto &children = prim.children();
  if (max_depth != 0 && !children.empty()) {
    nlohmann::json child_arr = nlohmann::json::array();
    int next_depth = (max_depth > 0) ? max_depth - 1 : max_depth;
    for (const auto &child : children) {
      nlohmann::json c;
      PrimToJSJSON(child, c, next_depth);
      child_arr.push_back(c);
    }
    j["children"] = child_arr;
  } else if (!children.empty()) {
    j["childCount"] = children.size();
  }
}

// ---------------------------------------------------------------------------
// JS function: tinyusdz.prim.list(path, opts?)
// ---------------------------------------------------------------------------
static JSValue js_prim_list(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv, int magic,
                            JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }

  // Default: list root prims at "/"
  std::string path_str = "/";
  int max_depth = -1;

  if (argc > 0 && JS_IsString(argv[0])) {
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s) { path_str = s; JS_FreeCString(ctx, s); }
  }
  if (argc > 1 && JS_IsObject(argv[1])) {
    nlohmann::json opts = JSValueToJSON(ctx, argv[1]);
    if (opts.contains("maxDepth") && opts["maxDepth"].is_number()) {
      max_depth = opts["maxDepth"].get<int>();
    }
  }

  if (path_str == "/") {
    nlohmann::json result = nlohmann::json::array();
    for (const auto &prim : g_js_stage->root_prims()) {
      nlohmann::json j;
      PrimToJSJSON(prim, j, max_depth);
      result.push_back(j);
    }
    nlohmann::json wrap;
    wrap["prims"] = result;
    wrap["count"] = result.size();
    return JSONToJSValue(ctx, wrap);
  }

  tinyusdz::Path path(path_str);
  if (!path.is_valid()) {
    return JS_ThrowTypeError(ctx, "Invalid path: %s", path_str.c_str());
  }

  const Prim *prim = nullptr;
  std::string err;
  if (!g_js_stage->find_prim_at_path(path, &prim, &err)) {
    return JS_ThrowTypeError(ctx, "Prim not found: %s", path_str.c_str());
  }

  nlohmann::json j;
  PrimToJSJSON(*prim, j, max_depth);
  nlohmann::json wrap;
  wrap["prim"] = j;
  return JSONToJSValue(ctx, wrap);
}

// ---------------------------------------------------------------------------
// JS function: tinyusdz.prim.get(path)
// ---------------------------------------------------------------------------
static JSValue js_prim_get(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv, int magic,
                           JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }
  if (argc < 1 || !JS_IsString(argv[0])) {
    return JS_ThrowTypeError(ctx, "prim.get() requires a path string");
  }

  const char *s = JS_ToCString(ctx, argv[0]);
  if (!s) return JS_EXCEPTION;
  std::string path_str(s);
  JS_FreeCString(ctx, s);

  tinyusdz::Path path(path_str);
  if (!path.is_valid()) {
    return JS_ThrowTypeError(ctx, "Invalid path: %s", path_str.c_str());
  }

  const Prim *prim = nullptr;
  std::string err;
  if (!g_js_stage->find_prim_at_path(path, &prim, &err)) {
    return JS_ThrowTypeError(ctx, "Prim not found: %s", path_str.c_str());
  }

  nlohmann::json j;
  j["name"] = prim->element_name();
  j["path"] = prim->absolute_path().full_path();
  j["type"] = prim->prim_type_name();
  switch (prim->specifier()) {
    case Specifier::Def: j["specifier"] = "def"; break;
    case Specifier::Over: j["specifier"] = "over"; break;
    case Specifier::Class: j["specifier"] = "class"; break;
    default: j["specifier"] = "invalid"; break;
  }

  // Children
  nlohmann::json children = nlohmann::json::array();
  for (const auto &c : prim->children()) {
    nlohmann::json cc;
    cc["name"] = c.element_name();
    cc["type"] = c.prim_type_name();
    children.push_back(cc);
  }
  j["children"] = children;
  j["childCount"] = prim->children().size();

  // Metadata
  j["metadata"] = PrimMetaToJSON(prim->metas());

  return JSONToJSValue(ctx, j);
}

// ---------------------------------------------------------------------------
// JS function: tinyusdz.stage.exportToString()
// ---------------------------------------------------------------------------
static JSValue js_stage_export(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv, int magic,
                               JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)argv; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }

  std::string usda = g_js_stage->ExportToString();
  return JS_NewString(ctx, usda.c_str());
}

// ---------------------------------------------------------------------------
// JS function: tinyusdz.prim.listChildren(path)
// ---------------------------------------------------------------------------
static JSValue js_prim_listChildren(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv, int magic,
                                     JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }

  std::string path_str = "/";
  if (argc > 0 && JS_IsString(argv[0])) {
    const char *s = JS_ToCString(ctx, argv[0]);
    if (s) { path_str = s; JS_FreeCString(ctx, s); }
  }

  const Prim *prim = nullptr;
  if (path_str == "/") {
    nlohmann::json result = nlohmann::json::array();
    for (const auto &p : g_js_stage->root_prims()) {
      nlohmann::json j;
      j["name"] = p.element_name();
      j["type"] = p.prim_type_name();
      j["childCount"] = p.children().size();
      result.push_back(j);
    }
    nlohmann::json wrap;
    wrap["prims"] = result;
    wrap["count"] = result.size();
    return JSONToJSValue(ctx, wrap);
  }

  tinyusdz::Path path(path_str);
  if (!path.is_valid()) {
    return JS_ThrowTypeError(ctx, "Invalid path: %s", path_str.c_str());
  }

  std::string err;
  if (!g_js_stage->find_prim_at_path(path, &prim, &err)) {
    return JS_ThrowTypeError(ctx, "Prim not found: %s", path_str.c_str());
  }

  nlohmann::json result = nlohmann::json::array();
  for (const auto &c : prim->children()) {
    nlohmann::json j;
    j["name"] = c.element_name();
    j["type"] = c.prim_type_name();
    j["childCount"] = c.children().size();
    result.push_back(j);
  }

  nlohmann::json wrap;
  wrap["prims"] = result;
  wrap["count"] = result.size();
  return JSONToJSValue(ctx, wrap);
}

// ---------------------------------------------------------------------------
// JS function: tinyusdz.query.findAllByType(typeName)
// ---------------------------------------------------------------------------
static void CollectPrimsByType(const Prim &prim, const std::string &type_name,
                               std::vector<std::string> &results) {
  if (prim.prim_type_name() == type_name) {
    results.push_back(prim.absolute_path().full_path());
  }
  for (const auto &child : prim.children()) {
    CollectPrimsByType(child, type_name, results);
  }
}

static JSValue js_query_findAllByType(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv, int magic,
                                       JSValueConst *func_data) {
  (void)this_val; (void)argc; (void)magic; (void)func_data;
  if (!g_js_stage) {
    return JS_ThrowTypeError(ctx, "No stage loaded");
  }
  if (argc < 1 || !JS_IsString(argv[0])) {
    return JS_ThrowTypeError(ctx, "findAllByType() requires a type name string");
  }

  const char *s = JS_ToCString(ctx, argv[0]);
  if (!s) return JS_EXCEPTION;
  std::string type_name(s);
  JS_FreeCString(ctx, s);

  std::vector<std::string> found;
  for (const auto &prim : g_js_stage->root_prims()) {
    CollectPrimsByType(prim, type_name, found);
  }

  nlohmann::json result = nlohmann::json::array();
  for (const auto &p : found) {
    result.push_back(p);
  }

  nlohmann::json wrap;
  wrap["paths"] = result;
  wrap["count"] = found.size();
  return JSONToJSValue(ctx, wrap);
}

// ---------------------------------------------------------------------------
// Module initialisation helper: register a function on an object
// ---------------------------------------------------------------------------
static void RegisterFunction(JSContext *ctx, JSValue obj, const char *name,
                             JSCFunctionData *func, int length) {
  JSValue f = JS_NewCFunctionData(ctx, func, length, 0, 0, nullptr);
  JS_SetPropertyStr(ctx, obj, name, f);
}

static void RegisterObject(JSContext *ctx, JSValue parent, const char *name,
                           JSValue obj) {
  JS_SetPropertyStr(ctx, parent, name, obj);
}

} // namespace

// ===========================================================================
// Public: InitJSEngine
// ===========================================================================
bool InitJSEngine(JSEngineState &engine, std::string &err) {
  if (engine.initialized) {
    return true; // already initialized
  }

  JSRuntime *rt = JS_NewRuntime();
  if (!rt) {
    err = "Failed to create JavaScript runtime";
    return false;
  }

  // Set memory limits (64MB stack, 256MB total)
  JS_SetMemoryLimit(rt, 256 * 1024 * 1024);
  JS_SetMaxStackSize(rt, 64 * 1024 * 1024);

  JSContext *ctx = JS_NewContext(rt);
  if (!ctx) {
    err = "Failed to create JavaScript context";
    JS_FreeRuntime(rt);
    return false;
  }

  // Initialize standard library (console.log, etc.)
  js_std_add_helpers(ctx, 0, nullptr);

  engine.runtime = rt;
  engine.context = ctx;
  engine.initialized = true;
  return true;
}

// ===========================================================================
// Public: DestroyJSEngine
// ===========================================================================
bool DestroyJSEngine(JSEngineState &engine) {
  if (!engine.initialized) return true;

  JSContext *ctx = static_cast<JSContext *>(engine.context);
  JSRuntime *rt = static_cast<JSRuntime *>(engine.runtime);

  if (ctx) {
    JS_FreeContext(ctx);
  }
  if (rt) {
    JS_FreeRuntime(rt);
  }

  engine.runtime = nullptr;
  engine.context = nullptr;
  engine.initialized = false;
  return true;
}

// ===========================================================================
// Public: RegisterUSDModule
// ===========================================================================
bool RegisterUSDModule(JSEngineState &engine, Stage *stage,
                       std::string &err) {
  if (!engine.initialized) {
    err = "JS engine not initialized";
    return false;
  }

  JSContext *ctx = static_cast<JSContext *>(engine.context);
  g_js_stage = stage;

  // Create the tinyusdz namespace
  JSValue global = JS_GetGlobalObject(ctx);
  JSValue tinyusdz = JS_NewObject(ctx);

  // --- stage sub-module ---
  JSValue stage_mod = JS_NewObject(ctx);
  RegisterFunction(ctx, stage_mod, "info", js_stage_info, 0);
  RegisterFunction(ctx, stage_mod, "exportToString", js_stage_export, 0);
  RegisterObject(ctx, tinyusdz, "stage", stage_mod);

  // --- prim sub-module ---
  JSValue prim_mod = JS_NewObject(ctx);
  RegisterFunction(ctx, prim_mod, "list", js_prim_list, 2);
  RegisterFunction(ctx, prim_mod, "get", js_prim_get, 1);
  RegisterFunction(ctx, prim_mod, "listChildren", js_prim_listChildren, 1);
  RegisterObject(ctx, tinyusdz, "prim", prim_mod);

  // --- query sub-module ---
  JSValue query_mod = JS_NewObject(ctx);
  RegisterFunction(ctx, query_mod, "findAllByType", js_query_findAllByType, 1);
  RegisterObject(ctx, tinyusdz, "query", query_mod);

  // --- value helper ---
  JSValue value_mod = JS_NewObject(ctx);
  // value.create will be a JS function that wraps values
  RegisterObject(ctx, tinyusdz, "value", value_mod);

  // Set the module
  JS_SetPropertyStr(ctx, global, "tinyusdz", tinyusdz);
  JS_FreeValue(ctx, global);

  return true;
}

// ===========================================================================
// Public: RunJSScript (persistent engine)
// ===========================================================================
bool RunJSScript(JSEngineState &engine, const std::string &js_code,
                 nlohmann::json &return_value, std::string &err) {
  if (!engine.initialized) {
    err = "JS engine not initialized";
    return false;
  }

  JSContext *ctx = static_cast<JSContext *>(engine.context);

  // Evaluate
  JSValue result = JS_Eval(ctx, js_code.c_str(), js_code.length(),
                           "<eval>", JS_EVAL_TYPE_GLOBAL);

  if (JS_IsException(result)) {
    JSValue exception = JS_GetException(ctx);
    const char *error_str = JS_ToCString(ctx, exception);
    if (error_str) {
      err = "JavaScript error: ";
      err += error_str;
      JS_FreeCString(ctx, error_str);
    } else {
      err = "JavaScript error: unknown";
    }
    JS_FreeValue(ctx, exception);
    return_value = nullptr;
    return false;
  }

  // Capture return value
  return_value = JSValueToJSON(ctx, result);
  JS_FreeValue(ctx, result);
  return true;
}

} // namespace tydra
} // namespace tinyusdz

#else // !TINYUSDZ_WITH_QJS

namespace tinyusdz {
namespace tydra {

bool InitJSEngine(JSEngineState &engine, std::string &err) {
  (void)engine;
  err = "JavaScript (QuickJS) not enabled. Build with -DTINYUSDZ_WITH_QJS=ON";
  return false;
}

bool DestroyJSEngine(JSEngineState &engine) {
  (void)engine;
  return true;
}

bool RegisterUSDModule(JSEngineState &engine, Stage *stage,
                       std::string &err) {
  (void)engine; (void)stage;
  err = "JavaScript (QuickJS) not enabled.";
  return false;
}

bool RunJSScript(JSEngineState &engine, const std::string &js_code,
                 nlohmann::json &return_value, std::string &err) {
  (void)engine; (void)js_code; (void)return_value;
  err = "JavaScript (QuickJS) not enabled.";
  return false;
}

} // namespace tydra
} // namespace tinyusdz

#endif // TINYUSDZ_WITH_QJS
