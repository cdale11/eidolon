#include "core/json.hpp"

#include <cstdio>
#include <cstdlib>

namespace eidolon {

JsonValue JsonValue::makeBool(bool b) {
  JsonValue v;
  v.type_ = Type::Bool;
  v.bool_ = b;
  return v;
}
JsonValue JsonValue::makeNumber(double n) {
  JsonValue v;
  v.type_ = Type::Number;
  v.num_ = n;
  return v;
}
JsonValue JsonValue::makeString(std::string s) {
  JsonValue v;
  v.type_ = Type::String;
  v.str_ = std::move(s);
  return v;
}
JsonValue JsonValue::makeArray() {
  JsonValue v;
  v.type_ = Type::Array;
  return v;
}
JsonValue JsonValue::makeObject() {
  JsonValue v;
  v.type_ = Type::Object;
  return v;
}

bool JsonValue::asBool(bool fallback) const {
  return type_ == Type::Bool ? bool_ : fallback;
}
double JsonValue::asNumber(double fallback) const {
  return type_ == Type::Number ? num_ : fallback;
}
std::string JsonValue::str(const std::string& key, const std::string& fallback) const {
  const JsonValue* v = find(key);
  return v && v->type_ == Type::String ? v->str_ : fallback;
}
double JsonValue::num(const std::string& key, double fallback) const {
  const JsonValue* v = find(key);
  return v && v->type_ == Type::Number ? v->num_ : fallback;
}
bool JsonValue::boolean(const std::string& key, bool fallback) const {
  const JsonValue* v = find(key);
  return v && v->type_ == Type::Bool ? v->bool_ : fallback;
}

std::string JsonValue::dump() const {
  std::string out;
  switch (type_) {
    case Type::Null: out = "null"; break;
    case Type::Bool: out = bool_ ? "true" : "false"; break;
    case Type::Number: {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%g", num_);
      out = buf;
      break;
    }
    case Type::String: {
      out = "\"";
      for (const char c : str_) {
        switch (c) {
          case '"': out += "\\\""; break;
          case '\\': out += "\\\\"; break;
          case '\n': out += "\\n"; break;
          case '\r': out += "\\r"; break;
          case '\t': out += "\\t"; break;
          default:
            if (static_cast<unsigned char>(c) < 0x20) {
              char esc[8];
              std::snprintf(esc, sizeof(esc), "\\u%04x", c);
              out += esc;
            } else {
              out += c;
            }
        }
      }
      out += "\"";
      break;
    }
    case Type::Array: {
      out = "[";
      bool first = true;
      for (const JsonValue& v : arr_) {
        if (!first) out += ",";
        first = false;
        out += v.dump();
      }
      out += "]";
      break;
    }
    case Type::Object: {
      out = "{";
      bool first = true;
      for (const auto& kv : obj_) {
        if (!first) out += ",";
        first = false;
        JsonValue key = makeString(kv.first);
        out += key.dump();
        out += ":";
        out += kv.second.dump();
      }
      out += "}";
      break;
    }
  }
  return out;
}

namespace {

struct Parser {
  const char* p;
  const char* end;

  void skipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  bool eof() const { return p >= end; }
  char peek() const { return eof() ? '\0' : *p; }
  bool consume(char c) {
    if (peek() == c) {
      ++p;
      return true;
    }
    return false;
  }

  bool parse(JsonValue& out) {
    skipWs();
    if (eof()) return false;
    switch (peek()) {
      case '{': return parseObject(out);
      case '[': return parseArray(out);
      case '"': {
        std::string s;
        if (!parseString(s)) return false;
        out = JsonValue::makeString(std::move(s));
        return true;
      }
      case 't':
        if (end - p >= 4 && std::string(p, 4) == "true") {
          p += 4;
          out = JsonValue::makeBool(true);
          return true;
        }
        return false;
      case 'f':
        if (end - p >= 5 && std::string(p, 5) == "false") {
          p += 5;
          out = JsonValue::makeBool(false);
          return true;
        }
        return false;
      case 'n':
        if (end - p >= 4 && std::string(p, 4) == "null") {
          p += 4;
          out = JsonValue();
          return true;
        }
        return false;
      default: return parseNumber(out);
    }
  }

  bool parseString(std::string& out) {
    if (!consume('"')) return false;
    out.clear();
    while (!eof()) {
      const char c = *p++;
      if (c == '"') return true;
      if (c == '\\') {
        if (eof()) return false;
        const char esc = *p++;
        switch (esc) {
          case '"': out += '"'; break;
          case '\\': out += '\\'; break;
          case '/': out += '/'; break;
          case 'b': out += '\b'; break;
          case 'f': out += '\f'; break;
          case 'n': out += '\n'; break;
          case 'r': out += '\r'; break;
          case 't': out += '\t'; break;
          case 'u': {
            if (end - p < 4) return false;
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              const char h = *p++;
              code <<= 4;
              if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
              else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
              else return false;
            }
            if (code < 0x80) out += static_cast<char>(code);
            else if (code < 0x800) {
              out += static_cast<char>(0xC0 | (code >> 6));
              out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
              out += static_cast<char>(0xE0 | (code >> 12));
              out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
              out += static_cast<char>(0x80 | (code & 0x3F));
            }
            break;
          }
          default: return false;
        }
      } else if (static_cast<unsigned char>(c) < 0x20) {
        return false;
      } else {
        out += c;
      }
    }
    return false;
  }

  bool parseNumber(JsonValue& out) {
    const char* start = p;
    if (consume('-')) { /* ok */ }
    while (!eof() && (*p >= '0' && *p <= '9')) ++p;
    if (consume('.')) {
      while (!eof() && (*p >= '0' && *p <= '9')) ++p;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++p;
      if (peek() == '+' || peek() == '-') ++p;
      while (!eof() && (*p >= '0' && *p <= '9')) ++p;
    }
    if (p == start || (p - start == 1 && *start == '-')) return false;
    out = JsonValue::makeNumber(std::strtod(start, nullptr));
    return true;
  }

  bool parseArray(JsonValue& out) {
    if (!consume('[')) return false;
    out = JsonValue::makeArray();
    skipWs();
    if (consume(']')) return true;
    for (;;) {
      skipWs();
      JsonValue v;
      if (!parse(v)) return false;
      out.push(std::move(v));
      skipWs();
      if (consume(']')) return true;
      if (!consume(',')) return false;
    }
  }

  bool parseObject(JsonValue& out) {
    if (!consume('{')) return false;
    out = JsonValue::makeObject();
    skipWs();
    if (consume('}')) return true;
    for (;;) {
      skipWs();
      std::string key;
      if (!parseString(key)) return false;
      skipWs();
      if (!consume(':')) return false;
      skipWs();
      JsonValue v;
      if (!parse(v)) return false;
      out.set(key, std::move(v));
      skipWs();
      if (consume('}')) return true;
      if (!consume(',')) return false;
    }
  }
};

} // namespace

bool jsonParse(const std::string& text, JsonValue& out) {
  Parser parser{text.data(), text.data() + text.size()};
  if (!parser.parse(out)) return false;
  parser.skipWs();
  return parser.eof();
}

} // namespace eidolon