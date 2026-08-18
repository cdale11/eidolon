// Minimal JSON parser/builder for the LLM bridge (no external deps). Supports the
// subset needed for OpenAI-compatible chat completions: objects, arrays, strings,
// numbers, booleans, null. No exceptions; malformed input returns false.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eidolon {

class JsonValue {
public:
  enum class Type : uint8_t { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  static JsonValue makeBool(bool b);
  static JsonValue makeNumber(double n);
  static JsonValue makeString(std::string s);
  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0.0) const;
  const std::string& asString() const { return str_; }
  const std::vector<JsonValue>& asArray() const { return arr_; }
  const std::map<std::string, JsonValue>& asObject() const { return obj_; }

  // Object access helpers.
  bool has(const std::string& key) const { return obj_.count(key) > 0; }
  const JsonValue* find(const std::string& key) const {
    const auto it = obj_.find(key);
    return it == obj_.end() ? nullptr : &it->second;
  }
  std::string str(const std::string& key, const std::string& fallback = "") const;
  double num(const std::string& key, double fallback = 0.0) const;
  bool boolean(const std::string& key, bool fallback = false) const;

  void set(const std::string& key, JsonValue v) { obj_[key] = std::move(v); }
  void push(JsonValue v) { arr_.push_back(std::move(v)); }
  void setString(const std::string& key, const std::string& v) {
    obj_[key] = makeString(v);
  }
  void setNumber(const std::string& key, double v) { obj_[key] = makeNumber(v); }
  void setBool(const std::string& key, bool v) { obj_[key] = makeBool(v); }

  std::string dump() const;

private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0.0;
  std::string str_;
  std::vector<JsonValue> arr_;
  std::map<std::string, JsonValue> obj_;
};

// Parses a JSON document; returns false on malformed input. `out` is reset first.
bool jsonParse(const std::string& text, JsonValue& out);

} // namespace eidolon