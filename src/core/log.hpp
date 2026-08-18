// Event log: text lines with sim-time prefixes, append-only.
// The log is the observable trace of a run and must be deterministic under a fixed
// seed (never write wall-clock time or addresses into it).
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eidolon {

class EventLog {
public:
  EventLog() = default;
  EventLog(const EventLog&) = delete;
  EventLog& operator=(const EventLog&) = delete;

  // Opens/creates the log file (append mode if it already exists).
  bool open(const std::string& path);
  bool isOpen() const { return f_ != nullptr; }
  void close();

  // Line with sim-time prefix: "[t=12345] kind: message".
  void line(int64_t simTime, const char* kind, const char* fmt, ...)
      __attribute__((format(printf, 4, 5)));

  void flush();

private:
  FILE* f_ = nullptr;
  std::string path_;
};

} // namespace eidolon
