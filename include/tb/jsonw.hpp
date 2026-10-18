#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace tb {

inline std::string json_escape(const std::string& in) {
  std::string o;
  o.reserve(in.size() + 2);
  for (unsigned char c : in) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          o += b;
        } else {
          o += static_cast<char>(c);
        }
    }
  }
  return o;
}

// Minimal JSON object writer (flat or nested via raw()).
class Obj {
 public:
  Obj& s(const char* k, const std::string& v) { key(k); b_ += '"' + json_escape(v) + '"'; return *this; }
  Obj& i(const char* k, int64_t v) { key(k); b_ += std::to_string(v); return *this; }
  Obj& d(const char* k, double v) {
    key(k);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.3f", v);
    b_ += buf;
    return *this;
  }
  Obj& b(const char* k, bool v) { key(k); b_ += v ? "true" : "false"; return *this; }
  Obj& raw(const char* k, const std::string& v) { key(k); b_ += v; return *this; }
  std::string str() const { return b_ + "}"; }

 private:
  void key(const char* k) {
    if (!first_) b_ += ',';
    first_ = false;
    b_ += '"';
    b_ += k;
    b_ += "\":";
  }
  std::string b_ = "{";
  bool first_ = true;
};

}  // namespace tb
