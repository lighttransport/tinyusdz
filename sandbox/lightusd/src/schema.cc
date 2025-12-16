// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Schema implementation

#include "lightusd/schema.hh"
#include "lightusd/prim.hh"
#include "lightusd/token.hh"
#include "lightusd/value.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace lightusd {
namespace v1 {

// ============================================================================
// ValidationResult implementation
// ============================================================================

void ValidationResult::add_error(const std::string& prop, const std::string& msg,
                                  const std::string& suggestion) {
    valid = false;
    issues.push_back({ValidationSeverity::Error, prop, msg, suggestion});
}

void ValidationResult::add_warning(const std::string& prop, const std::string& msg,
                                    const std::string& suggestion) {
    issues.push_back({ValidationSeverity::Warning, prop, msg, suggestion});
}

void ValidationResult::add_info(const std::string& prop, const std::string& msg) {
    issues.push_back({ValidationSeverity::Info, prop, msg, ""});
}

bool ValidationResult::has_errors() const {
    return std::any_of(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == ValidationSeverity::Error; });
}

bool ValidationResult::has_warnings() const {
    return std::any_of(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == ValidationSeverity::Warning; });
}

size_t ValidationResult::error_count() const {
    return std::count_if(issues.begin(), issues.end(),
        [](const ValidationIssue& i) { return i.severity == ValidationSeverity::Error; });
}

std::vector<std::string> ValidationResult::format_issues() const {
    std::vector<std::string> result;
    result.reserve(issues.size());
    for (const auto& issue : issues) {
        std::string prefix;
        switch (issue.severity) {
            case ValidationSeverity::Error:   prefix = "ERROR"; break;
            case ValidationSeverity::Warning: prefix = "WARNING"; break;
            case ValidationSeverity::Info:    prefix = "INFO"; break;
        }
        std::string formatted = "[" + prefix + "] ";
        if (!issue.property_name.empty()) {
            formatted += issue.property_name + ": ";
        }
        formatted += issue.message;
        if (!issue.suggestion.empty()) {
            formatted += " (" + issue.suggestion + ")";
        }
        result.push_back(std::move(formatted));
    }
    return result;
}

void ValidationResult::merge(const ValidationResult& other) {
    if (!other.valid) valid = false;
    issues.insert(issues.end(), other.issues.begin(), other.issues.end());
}

// ============================================================================
// JsonValue implementation
// ============================================================================

const JsonValue JsonValue::null_value_;
const std::string JsonValue::empty_string_;
const JsonValue::Array JsonValue::empty_array_;
const JsonValue::Object JsonValue::empty_object_;

bool JsonValue::as_bool(bool default_val) const {
    if (auto* v = std::get_if<bool>(&value_)) return *v;
    return default_val;
}

double JsonValue::as_number(double default_val) const {
    if (auto* v = std::get_if<double>(&value_)) return *v;
    return default_val;
}

int JsonValue::as_int(int default_val) const {
    if (auto* v = std::get_if<double>(&value_)) return static_cast<int>(*v);
    return default_val;
}

const std::string& JsonValue::as_string() const {
    if (auto* v = std::get_if<std::string>(&value_)) return *v;
    return empty_string_;
}

std::string JsonValue::as_string(const std::string& default_val) const {
    if (auto* v = std::get_if<std::string>(&value_)) return *v;
    return default_val;
}

const JsonValue::Array& JsonValue::as_array() const {
    if (auto* v = std::get_if<Array>(&value_)) return *v;
    return empty_array_;
}

const JsonValue::Object& JsonValue::as_object() const {
    if (auto* v = std::get_if<Object>(&value_)) return *v;
    return empty_object_;
}

bool JsonValue::has(const std::string& key) const {
    if (auto* obj = std::get_if<Object>(&value_)) {
        return obj->find(key) != obj->end();
    }
    return false;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (auto* obj = std::get_if<Object>(&value_)) {
        auto it = obj->find(key);
        if (it != obj->end()) return it->second;
    }
    return null_value_;
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (auto* arr = std::get_if<Array>(&value_)) {
        if (index < arr->size()) return (*arr)[index];
    }
    return null_value_;
}

size_t JsonValue::size() const {
    if (auto* arr = std::get_if<Array>(&value_)) return arr->size();
    if (auto* obj = std::get_if<Object>(&value_)) return obj->size();
    return 0;
}

// Simple JSON parser
namespace {

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input), pos_(0) {}

    Result<JsonValue> parse() {
        skip_whitespace();
        auto result = parse_value();
        if (!result) return result;
        skip_whitespace();
        if (pos_ < input_.size()) {
            return make_error("Unexpected characters after JSON value");
        }
        return result;
    }

private:
    std::string_view input_;
    size_t pos_;

    char peek() const {
        return pos_ < input_.size() ? input_[pos_] : '\0';
    }

    char consume() {
        return pos_ < input_.size() ? input_[pos_++] : '\0';
    }

    void skip_whitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    Result<JsonValue> parse_value() {
        skip_whitespace();
        char c = peek();

        if (c == '"') return parse_string();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();

        return make_error("Unexpected character: " + std::string(1, c));
    }

    Result<JsonValue> parse_string() {
        if (consume() != '"') return make_error("Expected '\"'");

        std::string result;
        while (pos_ < input_.size()) {
            char c = consume();
            if (c == '"') return JsonValue(std::move(result));
            if (c == '\\') {
                if (pos_ >= input_.size()) return make_error("Unexpected end of string");
                char escaped = consume();
                switch (escaped) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u': {
                        // Unicode escape (simplified - just skip for now)
                        if (pos_ + 4 > input_.size()) return make_error("Invalid unicode escape");
                        pos_ += 4;
                        result += '?';
                        break;
                    }
                    default: return make_error("Invalid escape sequence");
                }
            } else {
                result += c;
            }
        }
        return make_error("Unterminated string");
    }

    Result<JsonValue> parse_number() {
        size_t start = pos_;
        if (peek() == '-') ++pos_;

        while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }

        if (peek() == '.') {
            ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
        }

        std::string num_str(input_.substr(start, pos_ - start));
        double value = std::stod(num_str);
        return JsonValue(value);
    }

    Result<JsonValue> parse_bool() {
        if (input_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue(true);
        }
        if (input_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue(false);
        }
        return make_error("Expected 'true' or 'false'");
    }

    Result<JsonValue> parse_null() {
        if (input_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue(nullptr);
        }
        return make_error("Expected 'null'");
    }

    Result<JsonValue> parse_array() {
        if (consume() != '[') return make_error("Expected '['");

        JsonValue::Array arr;
        skip_whitespace();

        if (peek() == ']') {
            ++pos_;
            return JsonValue(std::move(arr));
        }

        while (true) {
            auto value = parse_value();
            if (!value) return value;
            arr.push_back(std::move(value).value());

            skip_whitespace();
            char c = consume();
            if (c == ']') return JsonValue(std::move(arr));
            if (c != ',') return make_error("Expected ',' or ']'");
            skip_whitespace();
        }
    }

    Result<JsonValue> parse_object() {
        if (consume() != '{') return make_error("Expected '{'");

        JsonValue::Object obj;
        skip_whitespace();

        if (peek() == '}') {
            ++pos_;
            return JsonValue(std::move(obj));
        }

        while (true) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) return key;

            skip_whitespace();
            if (consume() != ':') return make_error("Expected ':'");

            auto value = parse_value();
            if (!value) return value;

            obj[key.value().as_string()] = std::move(value).value();

            skip_whitespace();
            char c = consume();
            if (c == '}') return JsonValue(std::move(obj));
            if (c != ',') return make_error("Expected ',' or '}'");
        }
    }
};

} // anonymous namespace

Result<JsonValue> JsonValue::parse(std::string_view json) {
    JsonParser parser(json);
    return parser.parse();
}

std::string JsonValue::to_string(bool pretty, int indent) const {
    std::ostringstream oss;
    std::string indent_str(indent * 2, ' ');
    std::string next_indent(pretty ? (indent + 1) * 2 : 0, ' ');

    if (is_null()) {
        oss << "null";
    } else if (is_bool()) {
        oss << (as_bool() ? "true" : "false");
    } else if (is_number()) {
        double n = as_number();
        if (std::floor(n) == n && std::abs(n) < 1e15) {
            oss << static_cast<int64_t>(n);
        } else {
            oss << n;
        }
    } else if (is_string()) {
        oss << '"';
        for (char c : as_string()) {
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:   oss << c; break;
            }
        }
        oss << '"';
    } else if (is_array()) {
        const auto& arr = as_array();
        if (arr.empty()) {
            oss << "[]";
        } else {
            oss << '[';
            if (pretty) oss << '\n';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (pretty) oss << next_indent;
                oss << arr[i].to_string(pretty, indent + 1);
                if (i < arr.size() - 1) oss << ',';
                if (pretty) oss << '\n';
            }
            if (pretty) oss << indent_str;
            oss << ']';
        }
    } else if (is_object()) {
        const auto& obj = as_object();
        if (obj.empty()) {
            oss << "{}";
        } else {
            oss << '{';
            if (pretty) oss << '\n';
            size_t i = 0;
            for (const auto& [key, value] : obj) {
                if (pretty) oss << next_indent;
                oss << '"' << key << '"' << ':';
                if (pretty) oss << ' ';
                oss << value.to_string(pretty, indent + 1);
                if (i < obj.size() - 1) oss << ',';
                if (pretty) oss << '\n';
                ++i;
            }
            if (pretty) oss << indent_str;
            oss << '}';
        }
    }

    return oss.str();
}

// ============================================================================
// Type utilities
// ============================================================================

Result<std::pair<TypeId, bool>> parse_type_name(std::string_view type_name) {
    bool is_array = false;
    std::string base_type(type_name);

    // Check for array suffix
    if (base_type.size() >= 2 && base_type.back() == ']' && base_type[base_type.size()-2] == '[') {
        is_array = true;
        base_type = base_type.substr(0, base_type.size() - 2);
    }

    // Map type name to TypeId
    static const std::map<std::string, TypeId> type_map = {
        {"bool", TypeId::Bool},
        {"int", TypeId::Int32}, {"int32", TypeId::Int32},
        {"int64", TypeId::Int64},
        {"uint", TypeId::UInt32}, {"uint32", TypeId::UInt32},
        {"uint64", TypeId::UInt64},
        {"half", TypeId::Half},
        {"float", TypeId::Float},
        {"double", TypeId::Double},
        {"string", TypeId::String},
        {"token", TypeId::Token},
        {"asset", TypeId::AssetPath},
        {"int2", TypeId::Int2}, {"int3", TypeId::Int3}, {"int4", TypeId::Int4},
        {"float2", TypeId::Float2}, {"half2", TypeId::Half2}, {"double2", TypeId::Double2},
        {"float3", TypeId::Float3}, {"half3", TypeId::Half3}, {"double3", TypeId::Double3},
        {"float4", TypeId::Float4}, {"half4", TypeId::Half4}, {"double4", TypeId::Double4},
        {"point3f", TypeId::Point3f}, {"point3d", TypeId::Point3d}, {"point3h", TypeId::Point3h},
        {"vector3f", TypeId::Vector3f}, {"vector3d", TypeId::Vector3d}, {"vector3h", TypeId::Vector3h},
        {"normal3f", TypeId::Normal3f}, {"normal3d", TypeId::Normal3d}, {"normal3h", TypeId::Normal3h},
        {"color3f", TypeId::Color3f}, {"color3d", TypeId::Color3d}, {"color3h", TypeId::Color3h},
        {"color4f", TypeId::Color4f}, {"color4d", TypeId::Color4d}, {"color4h", TypeId::Color4h},
        {"texCoord2f", TypeId::TexCoord2f}, {"texCoord2d", TypeId::TexCoord2d}, {"texCoord2h", TypeId::TexCoord2h},
        {"texCoord3f", TypeId::TexCoord3f}, {"texCoord3d", TypeId::TexCoord3d}, {"texCoord3h", TypeId::TexCoord3h},
        {"matrix2d", TypeId::Matrix2d}, {"matrix3d", TypeId::Matrix3d}, {"matrix4d", TypeId::Matrix4d},
        {"quatf", TypeId::Quatf}, {"quatd", TypeId::Quatd}, {"quath", TypeId::Quath},
    };

    auto it = type_map.find(base_type);
    if (it != type_map.end()) {
        return std::make_pair(it->second, is_array);
    }

    return make_error("Unknown type: " + std::string(type_name));
}

std::string type_id_to_name(TypeId id, bool is_array) {
    const TypeDescriptor* desc = get_type_descriptor(id);
    std::string name = desc ? desc->name : "unknown";
    if (is_array) name += "[]";
    return name;
}

bool value_matches_type(const Value& value, TypeId expected_type, bool expected_array) {
    if (value.is_array() != expected_array) return false;
    return value.type_id() == expected_type;
}

// ============================================================================
// PropertySchema implementation
// ============================================================================

Result<PropertySchema> PropertySchema::from_json(std::string_view json) {
    auto parsed = JsonValue::parse(json);
    if (!parsed) return make_error(parsed.error().message);

    const JsonValue& obj = parsed.value();
    if (!obj.is_object()) return make_error("Property schema must be an object");

    PropertySchema schema;
    schema.name = obj["name"].as_string("");
    schema.type_name = obj["type"].as_string("");
    schema.doc = obj["doc"].as_string("");
    schema.default_value_json = obj["default"].to_string();
    schema.interpolation = obj["interpolation"].as_string("");

    // Parse type
    if (!schema.type_name.empty()) {
        auto type_result = parse_type_name(schema.type_name);
        if (type_result) {
            schema.type_id = type_result.value().first;
            schema.is_array = type_result.value().second;
        }
    }

    // Requirement
    std::string req = obj["required"].as_string("optional");
    if (req == "true" || req == "required") {
        schema.requirement = PropertyRequirement::Required;
    } else if (req == "recommended") {
        schema.requirement = PropertyRequirement::Recommended;
    } else {
        schema.requirement = PropertyRequirement::Optional;
    }

    // Constraints
    if (obj.has("minArraySize")) {
        schema.min_array_size = static_cast<size_t>(obj["minArraySize"].as_int());
    }
    if (obj.has("maxArraySize")) {
        schema.max_array_size = static_cast<size_t>(obj["maxArraySize"].as_int());
    }
    if (obj.has("minValue")) {
        schema.min_value = obj["minValue"].as_number();
    }
    if (obj.has("maxValue")) {
        schema.max_value = obj["maxValue"].as_number();
    }

    // Allowed values
    if (obj.has("allowedValues")) {
        const auto& allowed = obj["allowedValues"].as_array();
        for (const auto& v : allowed) {
            schema.allowed_values.push_back(v.as_string());
        }
    }

    return schema;
}

std::string PropertySchema::to_json() const {
    JsonValue::Object obj;
    obj["name"] = name;
    obj["type"] = type_name;
    if (!doc.empty()) obj["doc"] = doc;

    switch (requirement) {
        case PropertyRequirement::Required: obj["required"] = "required"; break;
        case PropertyRequirement::Recommended: obj["required"] = "recommended"; break;
        case PropertyRequirement::Optional: obj["required"] = "optional"; break;
    }

    if (min_array_size) obj["minArraySize"] = static_cast<double>(*min_array_size);
    if (max_array_size) obj["maxArraySize"] = static_cast<double>(*max_array_size);
    if (min_value) obj["minValue"] = *min_value;
    if (max_value) obj["maxValue"] = *max_value;

    if (!allowed_values.empty()) {
        JsonValue::Array arr;
        for (const auto& v : allowed_values) arr.push_back(v);
        obj["allowedValues"] = std::move(arr);
    }

    if (!interpolation.empty()) obj["interpolation"] = interpolation;

    return JsonValue(std::move(obj)).to_string(true);
}

ValidationResult PropertySchema::validate(const Value& value) const {
    ValidationResult result;

    // Type check
    if (type_id != TypeId::Invalid) {
        if (!value_matches_type(value, type_id, is_array)) {
            result.add_error(name, "Type mismatch: expected " + type_name +
                ", got " + type_id_to_name(value.type_id(), value.is_array()));
        }
    }

    // Array size constraints
    if (value.is_array()) {
        size_t size = value.array_size();
        if (min_array_size && size < *min_array_size) {
            result.add_error(name, "Array too small: " + std::to_string(size) +
                " < " + std::to_string(*min_array_size));
        }
        if (max_array_size && size > *max_array_size) {
            result.add_error(name, "Array too large: " + std::to_string(size) +
                " > " + std::to_string(*max_array_size));
        }
    }

    // Token allowed values
    if (!allowed_values.empty() && value.type_id() == TypeId::Token) {
        const Token* token = value.as_token();
        if (token) {
            bool found = std::find(allowed_values.begin(), allowed_values.end(),
                                   token->str()) != allowed_values.end();
            if (!found) {
                result.add_error(name, "Invalid value '" + token->str() +
                    "'. Allowed: " + [this]() {
                        std::string s;
                        for (size_t i = 0; i < allowed_values.size(); ++i) {
                            if (i > 0) s += ", ";
                            s += allowed_values[i];
                        }
                        return s;
                    }());
            }
        }
    }

    // Numeric range constraints
    if (min_value || max_value) {
        double num = 0;
        bool has_num = false;
        if (auto* f = value.as_float()) { num = *f; has_num = true; }
        else if (auto* d = value.as_double()) { num = *d; has_num = true; }
        else if (auto* i = value.as_int32()) { num = *i; has_num = true; }

        if (has_num) {
            if (min_value && num < *min_value) {
                result.add_error(name, "Value " + std::to_string(num) +
                    " below minimum " + std::to_string(*min_value));
            }
            if (max_value && num > *max_value) {
                result.add_error(name, "Value " + std::to_string(num) +
                    " above maximum " + std::to_string(*max_value));
            }
        }
    }

    return result;
}

// ============================================================================
// PrimSchema implementation
// ============================================================================

Result<PrimSchema> PrimSchema::from_json(std::string_view json) {
    auto parsed = JsonValue::parse(json);
    if (!parsed) return make_error(parsed.error().message);

    const JsonValue& obj = parsed.value();
    if (!obj.is_object()) return make_error("Prim schema must be an object");

    PrimSchema schema;
    schema.type_name = obj["typeName"].as_string("");
    schema.doc = obj["doc"].as_string("");
    schema.inherits_from = obj["inherits"].as_string("");
    schema.allow_additional_properties = obj["allowAdditionalProperties"].as_bool(true);
    schema.version = obj["version"].as_string("1.0");

    // Properties
    if (obj.has("properties")) {
        const auto& props = obj["properties"].as_array();
        for (const auto& prop : props) {
            auto prop_result = PropertySchema::from_json(prop.to_string());
            if (prop_result) {
                schema.properties.push_back(std::move(prop_result).value());
            }
        }
    }

    // Required properties
    if (obj.has("required")) {
        const auto& required = obj["required"].as_array();
        for (const auto& r : required) {
            schema.required_properties.push_back(r.as_string());
        }
    }

    // API schemas
    if (obj.has("apiSchemas")) {
        const auto& apis = obj["apiSchemas"].as_array();
        for (const auto& api : apis) {
            schema.api_schemas.push_back(api.as_string());
        }
    }

    // Allowed child types
    if (obj.has("allowedChildTypes")) {
        const auto& children = obj["allowedChildTypes"].as_array();
        for (const auto& child : children) {
            schema.allowed_child_types.push_back(child.as_string());
        }
    }

    return schema;
}

std::string PrimSchema::to_json() const {
    JsonValue::Object obj;
    obj["typeName"] = type_name;
    if (!doc.empty()) obj["doc"] = doc;
    if (!inherits_from.empty()) obj["inherits"] = inherits_from;
    obj["allowAdditionalProperties"] = allow_additional_properties;
    if (!version.empty()) obj["version"] = version;

    // Properties
    if (!properties.empty()) {
        JsonValue::Array props;
        for (const auto& prop : properties) {
            auto prop_json = JsonValue::parse(prop.to_json());
            if (prop_json) props.push_back(std::move(prop_json).value());
        }
        obj["properties"] = std::move(props);
    }

    // Required
    if (!required_properties.empty()) {
        JsonValue::Array req;
        for (const auto& r : required_properties) req.push_back(r);
        obj["required"] = std::move(req);
    }

    // API schemas
    if (!api_schemas.empty()) {
        JsonValue::Array apis;
        for (const auto& api : api_schemas) apis.push_back(api);
        obj["apiSchemas"] = std::move(apis);
    }

    // Allowed child types
    if (!allowed_child_types.empty()) {
        JsonValue::Array children;
        for (const auto& child : allowed_child_types) children.push_back(child);
        obj["allowedChildTypes"] = std::move(children);
    }

    return JsonValue(std::move(obj)).to_string(true);
}

const PropertySchema* PrimSchema::get_property(std::string_view name) const {
    for (const auto& prop : properties) {
        if (prop.name == name) return &prop;
    }
    return nullptr;
}

bool PrimSchema::is_required(std::string_view name) const {
    return std::find(required_properties.begin(), required_properties.end(),
                     name) != required_properties.end();
}

ValidationResult PrimSchema::validate(const Prim& prim) const {
    ValidationResult result;

    // Check required properties
    for (const auto& req_name : required_properties) {
        const Attribute* attr = prim.get_attribute(req_name);
        if (!attr || !attr->is_authored()) {
            result.add_error(req_name, "Required property is missing");
        }
    }

    // Validate existing properties against schema
    for (const auto& prop_name : prim.property_names()) {
        const PropertySchema* prop_schema = get_property(prop_name);

        if (!prop_schema) {
            if (!allow_additional_properties) {
                result.add_warning(prop_name, "Property not defined in schema for type " + type_name);
            }
            continue;
        }

        const Attribute* attr = prim.get_attribute(prop_name);
        if (attr && attr->is_authored()) {
            auto value_result = attr->get_default();
            if (value_result) {
                auto prop_result = prop_schema->validate(value_result.value());
                result.merge(prop_result);
            }
        }
    }

    // Check for recommended properties
    for (const auto& prop : properties) {
        if (prop.requirement == PropertyRequirement::Recommended) {
            const Attribute* attr = prim.get_attribute(prop.name);
            if (!attr || !attr->is_authored()) {
                result.add_info(prop.name, "Recommended property is not present");
            }
        }
    }

    // Check child types if constrained
    if (!allowed_child_types.empty()) {
        for (size_t i = 0; i < prim.child_count(); ++i) {
            const Prim* child = prim.child(i);
            if (!child) continue;

            const std::string& child_type = child->type_name();
            if (!child_type.empty()) {
                bool allowed = std::find(allowed_child_types.begin(), allowed_child_types.end(),
                                         child_type) != allowed_child_types.end();
                if (!allowed) {
                    result.add_warning("", "Child '" + child->name() + "' has type '" + child_type +
                        "' which is not in allowed types");
                }
            }
        }
    }

    return result;
}

void PrimSchema::merge_parent(const PrimSchema& parent) {
    // Add parent properties that don't exist in this schema
    for (const auto& parent_prop : parent.properties) {
        if (!get_property(parent_prop.name)) {
            properties.push_back(parent_prop);
        }
    }

    // Merge required properties
    for (const auto& req : parent.required_properties) {
        if (std::find(required_properties.begin(), required_properties.end(), req)
            == required_properties.end()) {
            required_properties.push_back(req);
        }
    }

    // Merge API schemas
    for (const auto& api : parent.api_schemas) {
        if (std::find(api_schemas.begin(), api_schemas.end(), api) == api_schemas.end()) {
            api_schemas.push_back(api);
        }
    }
}

} // namespace v1
} // namespace lightusd
