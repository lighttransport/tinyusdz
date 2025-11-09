#include <string>

namespace tinyusdz {

struct LayerMetas;
class Layer;
class Attribute;

namespace tydra {

bool RunJSScript(const std::string &js_code, std::string &err);

bool RunJSScriptWithLayerMetas(const std::string &js_code, const LayerMetas* layer_metas, std::string &err);

bool RunJSScriptWithAttribute(const std::string &js_code, const Attribute* attribute, std::string &err);

bool RunJSScriptWithLayer(const std::string &js_code, const class Layer* layer, std::string &err);


} // namespace tydra
} // namespace tinyusdz
