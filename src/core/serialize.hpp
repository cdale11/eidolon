// Versioned binary serialization primitives + atomic snapshot save/load.
// No exceptions; failures return false and leave a message in `err`.
#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace eidolon {

constexpr uint32_t kSnapshotMagic = 0x4549444C; // "EIDL"
// v3: Phase 3 learning core (ValueNet/ThreatNet/policy/attention/neuromod/personality).
// v4: Phase 5 noise-field worldgen + scheduled-target fix.
// v5: Phase 5 wildlife (world + perception/learn layout + Flee action + attack stats).
// v6: Grid climate arrays (elevation/temperature/humidity) round-trip at full fidelity.
// v7: Phase 5 hazards (wounds/infection/immunity/exposure in body + hazard stats).
// v8: Phase 8 instruction learning + habit-biased action choice.
// v9: Directed-exploration state (exploreDir_/exploreTicks_) — organisms no longer
//      bounce in a corner to death when no food/water is in perception range.
// v10: Engine::lastAction_ persisted so the LLM bridge's CognitiveSnapshot has the
//      correct currentAction after a snapshot load (was always "active" placeholder).
// v11: Sleep architecture: Physiology::sleeping_ bool replaced by SleepStage (+ elapsed
//      time in stage) so drowsy/light/deep/REM stages persist across save/load.
constexpr uint32_t kSnapshotVersion = 11;

struct BinaryWriter {
public:
  void u8(uint8_t v) { buf_.push_back(v); }
  void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
  void u32(uint32_t v) { u16(static_cast<uint16_t>(v)); u16(static_cast<uint16_t>(v >> 16)); }
  void u64(uint64_t v) { u32(static_cast<uint32_t>(v)); u32(static_cast<uint32_t>(v >> 32)); }
  void i64(int64_t v) { u64(static_cast<uint64_t>(v)); }
  void f32(float v) { uint32_t u; std::memcpy(&u, &v, 4); u32(u); }
  void f64(double v) { uint64_t u; std::memcpy(&u, &v, 8); u64(u); }
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
  }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    bytes(s.data(), s.size());
  }
  size_t size() const { return buf_.size(); }
  const std::vector<uint8_t>& data() const { return buf_; }

private:
  std::vector<uint8_t> buf_;
};

struct BinaryReader {
public:
  BinaryReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
  BinaryReader(const std::vector<uint8_t>& v) : p_(v.data()), n_(v.size()) {}

  bool u8(uint8_t& v) { return take(v); }
  bool u16(uint16_t& v) {
    uint8_t a, b;
    if (!take(a) || !take(b)) return false;
    v = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8));
    return true;
  }
  bool u32(uint32_t& v) {
    uint16_t a, b;
    if (!u16(a) || !u16(b)) return false;
    v = static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 16);
    return true;
  }
  bool u64(uint64_t& v) {
    uint32_t a, b;
    if (!u32(a) || !u32(b)) return false;
    v = static_cast<uint64_t>(a) | (static_cast<uint64_t>(b) << 32);
    return true;
  }
  bool i64(int64_t& v) { return u64(reinterpret_cast<uint64_t&>(v)); }
  bool f32(float& v) {
    uint32_t u;
    if (!u32(u)) return false;
    std::memcpy(&v, &u, 4);
    return true;
  }
  bool f64(double& v) {
    uint64_t u;
    if (!u64(u)) return false;
    std::memcpy(&v, &u, 8);
    return true;
  }
  bool bytes(void* p, size_t n) {
    if (n_ < n) return false;
    std::memcpy(p, p_, n);
    p_ += n;
    n_ -= n;
    return true;
  }
  bool str(std::string& s) {
    uint32_t n;
    if (!u32(n) || n_ < n) return false;
    s.assign(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    n_ -= n;
    return true;
  }
  size_t remaining() const { return n_; }
  bool done() const { return n_ == 0; }

private:
  template <typename T>
  bool take(T& v) {
    if (n_ < sizeof(T)) return false;
    std::memcpy(&v, p_, sizeof(T));
    p_ += sizeof(T);
    n_ -= sizeof(T);
    return true;
  }
  const uint8_t* p_;
  size_t n_;
};

uint64_t fnv1a64(const uint8_t* p, size_t n) noexcept;

// Writes magic + version + payload, computes checksum over payload, appends it.
std::vector<uint8_t> packSnapshot(uint32_t version,
                                  const std::function<void(BinaryWriter&)>& payload);

// Validates magic/version/checksum; on success calls payload with the reader and returns
// true. On failure returns false and fills `err`.
bool unpackSnapshot(const std::vector<uint8_t>& blob, uint32_t expectedVersion,
                    const std::function<bool(BinaryReader&)>& payload, std::string& err);

// Atomic file snapshot: write temp file, fsync, rename over target.
bool saveSnapshotFile(const std::string& path, uint32_t version,
                      const std::function<void(BinaryWriter&)>& payload, std::string& err);
bool loadSnapshotFile(const std::string& path, uint32_t expectedVersion,
                      const std::function<bool(BinaryReader&)>& payload, std::string& err);

} // namespace eidolon
