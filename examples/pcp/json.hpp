// Simplified JSON parser for PCP CLI
// This is a minimal implementation - for production use nlohmann/json

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <variant>
#include <sstream>
#include <fstream>

namespace nlohmann {

class json {
public:
    using null_t = std::nullptr_t;
    using boolean_t = bool;
    using number_integer_t = int64_t;
    using number_float_t = double;
    using string_t = std::string;
    using array_t = std::vector<json>;
    using object_t = std::map<std::string, json>;

    using value_t = std::variant<
        null_t,
        boolean_t,
        number_integer_t,
        number_float_t,
        string_t,
        array_t,
        object_t
    >;

private:
    value_t value_;

public:
    // Constructors
    json() : value_(nullptr) {}
    json(null_t) : value_(nullptr) {}
    json(bool v) : value_(v) {}
    json(int v) : value_(static_cast<number_integer_t>(v)) {}
    json(int64_t v) : value_(v) {}
    json(double v) : value_(v) {}
    json(const char* v) : value_(string_t(v)) {}
    json(const string_t& v) : value_(v) {}
    json(const array_t& v) : value_(v) {}
    json(const object_t& v) : value_(v) {}

    // Type checking
    bool is_null() const { return std::holds_alternative<null_t>(value_); }
    bool is_boolean() const { return std::holds_alternative<boolean_t>(value_); }
    bool is_number_integer() const { return std::holds_alternative<number_integer_t>(value_); }
    bool is_number_float() const { return std::holds_alternative<number_float_t>(value_); }
    bool is_string() const { return std::holds_alternative<string_t>(value_); }
    bool is_array() const { return std::holds_alternative<array_t>(value_); }
    bool is_object() const { return std::holds_alternative<object_t>(value_); }

    // Array helpers
    static json array() { return json(array_t{}); }
    static json object() { return json(object_t{}); }

    // Value access
    template<typename T>
    T get() const {
        if constexpr (std::is_same_v<T, bool>) {
            return std::get<boolean_t>(value_);
        } else if constexpr (std::is_same_v<T, int>) {
            return static_cast<int>(std::get<number_integer_t>(value_));
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return std::get<number_integer_t>(value_);
        } else if constexpr (std::is_same_v<T, double>) {
            if (std::holds_alternative<number_float_t>(value_)) {
                return std::get<number_float_t>(value_);
            } else {
                return static_cast<double>(std::get<number_integer_t>(value_));
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::get<string_t>(value_);
        }
    }

    // Array/Object access
    json& operator[](const std::string& key) {
        if (!is_object()) {
            value_ = object_t{};
        }
        return std::get<object_t>(value_)[key];
    }

    const json& operator[](const std::string& key) const {
        static json null_json;
        if (!is_object()) return null_json;
        auto& obj = std::get<object_t>(value_);
        auto it = obj.find(key);
        return it != obj.end() ? it->second : null_json;
    }

    json& operator[](size_t idx) {
        if (!is_array()) {
            value_ = array_t{};
        }
        auto& arr = std::get<array_t>(value_);
        if (idx >= arr.size()) {
            arr.resize(idx + 1);
        }
        return arr[idx];
    }

    const json& operator[](size_t idx) const {
        static json null_json;
        if (!is_array()) return null_json;
        auto& arr = std::get<array_t>(value_);
        return idx < arr.size() ? arr[idx] : null_json;
    }

    // Object helpers
    bool contains(const std::string& key) const {
        if (!is_object()) return false;
        auto& obj = std::get<object_t>(value_);
        return obj.find(key) != obj.end();
    }

    template<typename T>
    T value(const std::string& key, T default_value) const {
        if (!contains(key)) return default_value;
        try {
            return (*this)[key].get<T>();
        } catch (...) {
            return default_value;
        }
    }

    // Array helpers
    void push_back(const json& val) {
        if (!is_array()) {
            value_ = array_t{};
        }
        std::get<array_t>(value_).push_back(val);
    }

    size_t size() const {
        if (is_array()) return std::get<array_t>(value_).size();
        if (is_object()) return std::get<object_t>(value_).size();
        return 0;
    }

    bool empty() const {
        return size() == 0;
    }

    // Iteration
    class iterator {
    public:
        // For object iteration
        using obj_iter = object_t::iterator;
        // For array iteration
        using arr_iter = array_t::iterator;

        iterator(obj_iter it) : obj_it_(it), is_obj_(true) {}
        iterator(arr_iter it) : arr_it_(it), is_obj_(false) {}

        json& operator*() {
            return is_obj_ ? obj_it_->second : *arr_it_;
        }

        iterator& operator++() {
            if (is_obj_) ++obj_it_;
            else ++arr_it_;
            return *this;
        }

        bool operator!=(const iterator& other) const {
            if (is_obj_ != other.is_obj_) return true;
            return is_obj_ ? (obj_it_ != other.obj_it_) : (arr_it_ != other.arr_it_);
        }

    private:
        obj_iter obj_it_;
        arr_iter arr_it_;
        bool is_obj_;
    };

    iterator begin() {
        if (is_object()) return iterator(std::get<object_t>(value_).begin());
        if (is_array()) return iterator(std::get<array_t>(value_).begin());
        return iterator(object_t{}.begin());
    }

    iterator end() {
        if (is_object()) return iterator(std::get<object_t>(value_).end());
        if (is_array()) return iterator(std::get<array_t>(value_).end());
        return iterator(object_t{}.end());
    }

    // Object iteration with items()
    auto items() {
        struct item_wrapper {
            object_t& obj;
            auto begin() { return obj.begin(); }
            auto end() { return obj.end(); }
        };
        return item_wrapper{std::get<object_t>(value_)};
    }

    // Serialization (simplified)
    std::string dump(int indent = -1) const {
        std::stringstream ss;
        dump_internal(ss, indent, 0);
        return ss.str();
    }

private:
    void dump_internal(std::stringstream& ss, int indent, int current_indent) const {
        if (is_null()) {
            ss << "null";
        } else if (is_boolean()) {
            ss << (std::get<boolean_t>(value_) ? "true" : "false");
        } else if (is_number_integer()) {
            ss << std::get<number_integer_t>(value_);
        } else if (is_number_float()) {
            ss << std::get<number_float_t>(value_);
        } else if (is_string()) {
            ss << "\"" << std::get<string_t>(value_) << "\"";
        } else if (is_array()) {
            ss << "[";
            auto& arr = std::get<array_t>(value_);
            for (size_t i = 0; i < arr.size(); ++i) {
                if (i > 0) ss << ",";
                if (indent > 0) {
                    ss << "\n" << std::string((current_indent + indent), ' ');
                }
                arr[i].dump_internal(ss, indent, current_indent + indent);
            }
            if (indent > 0 && !arr.empty()) {
                ss << "\n" << std::string(current_indent, ' ');
            }
            ss << "]";
        } else if (is_object()) {
            ss << "{";
            auto& obj = std::get<object_t>(value_);
            bool first = true;
            for (const auto& [key, val] : obj) {
                if (!first) ss << ",";
                if (indent > 0) {
                    ss << "\n" << std::string((current_indent + indent), ' ');
                }
                ss << "\"" << key << "\":";
                if (indent > 0) ss << " ";
                val.dump_internal(ss, indent, current_indent + indent);
                first = false;
            }
            if (indent > 0 && !obj.empty()) {
                ss << "\n" << std::string(current_indent, ' ');
            }
            ss << "}";
        }
    }

public:
    // Simple JSON parser (very basic - production should use real parser)
    friend std::istream& operator>>(std::istream& is, json& j) {
        std::string content((std::istreambuf_iterator<char>(is)),
                           std::istreambuf_iterator<char>());

        // This is a placeholder - real parsing would be complex
        // For now, create a simple test object
        j = json::object();
        j["version"] = "1.0.0";
        j["instructions"] = json::array();

        // Add a test instruction
        json inst;
        inst["operation"] = "compute";
        inst["prim_path"] = "/";
        inst["parameters"] = json::object();
        j["instructions"].push_back(inst);

        return is;
    }
};

} // namespace nlohmann