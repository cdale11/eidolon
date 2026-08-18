#include "core/serialize.hpp"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace eidolon {

uint64_t fnv1a64(const uint8_t* p, size_t n) noexcept {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ULL;
  }
  return h;
}

std::vector<uint8_t> packSnapshot(uint32_t version,
                                  const std::function<void(BinaryWriter&)>& payload) {
  BinaryWriter w;
  payload(w);
  std::vector<uint8_t> out;
  out.reserve(12 + w.size());
  BinaryWriter h;
  h.u32(kSnapshotMagic);
  h.u32(version);
  h.u64(fnv1a64(w.data().data(), w.size()));
  out.insert(out.end(), h.data().begin(), h.data().end());
  out.insert(out.end(), w.data().begin(), w.data().end());
  return out;
}

bool unpackSnapshot(const std::vector<uint8_t>& blob, uint32_t expectedVersion,
                    const std::function<bool(BinaryReader&)>& payload, std::string& err) {
  BinaryReader r(blob);
  uint32_t magic, version;
  uint64_t checksum;
  if (!r.u32(magic) || !r.u32(version) || !r.u64(checksum)) {
    err = "snapshot too small";
    return false;
  }
  if (magic != kSnapshotMagic) {
    err = "not an eidolon snapshot (bad magic)";
    return false;
  }
  if (version != expectedVersion) {
    err = "snapshot version " + std::to_string(version) + " != expected " +
          std::to_string(expectedVersion);
    return false;
  }
  const size_t payloadSize = r.remaining();
  const uint8_t* payloadBegin = blob.data() + blob.size() - payloadSize;
  if (fnv1a64(payloadBegin, payloadSize) != checksum) {
    err = "snapshot checksum mismatch (corrupt)";
    return false;
  }
  return payload(r);
}

bool saveSnapshotFile(const std::string& path, uint32_t version,
                      const std::function<void(BinaryWriter&)>& payload, std::string& err) {
  const std::vector<uint8_t> blob = packSnapshot(version, payload);
  const std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) {
    err = "cannot open " + tmp;
    return false;
  }
  const size_t n = std::fwrite(blob.data(), 1, blob.size(), f);
  if (n != blob.size() || std::fflush(f) != 0 || fsync(fileno(f)) != 0) {
    std::fclose(f);
    std::remove(tmp.c_str());
    err = "write failed for " + tmp;
    return false;
  }
  std::fclose(f);
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    err = "rename failed for " + path;
    return false;
  }
  return true;
}

bool loadSnapshotFile(const std::string& path, uint32_t expectedVersion,
                      const std::function<bool(BinaryReader&)>& payload, std::string& err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    err = "cannot open " + path;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz < 0) {
    std::fclose(f);
    err = "cannot stat " + path;
    return false;
  }
  std::vector<uint8_t> blob(static_cast<size_t>(sz));
  const size_t n = std::fread(blob.data(), 1, blob.size(), f);
  std::fclose(f);
  if (n != blob.size()) {
    err = "short read on " + path;
    return false;
  }
  return unpackSnapshot(blob, expectedVersion, payload, err);
}

} // namespace eidolon
