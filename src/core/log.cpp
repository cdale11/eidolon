#include "core/log.hpp"

#include <cstdio>
#include <ctime>

namespace eidolon {

bool EventLog::open(const std::string& path) {
  f_ = std::fopen(path.c_str(), "a");
  if (f_ == nullptr) return false;
  path_ = path;
  std::setvbuf(f_, nullptr, _IOLBF, 8192);
  return true;
}

void EventLog::close() {
  if (f_) {
    std::fclose(f_);
    f_ = nullptr;
  }
}

void EventLog::line(int64_t simTime, const char* kind, const char* fmt, ...) {
  if (!f_) return;
  std::fprintf(f_, "[t=%lld] %s: ", static_cast<long long>(simTime), kind);
  va_list args;
  va_start(args, fmt);
  std::vfprintf(f_, fmt, args);
  va_end(args);
  std::fprintf(f_, "\n");
}

void EventLog::flush() {
  if (f_) std::fflush(f_);
}

} // namespace eidolon
