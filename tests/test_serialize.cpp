#include "harness.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "core/serialize.hpp"

using namespace eidolon;

namespace {
struct Payload {
  uint64_t u64v;
  int64_t i64v;
  uint32_t u32v;
  uint16_t u16v;
  uint8_t u8v;
  float f32v;
  double f64v;
  std::string strv;
  std::vector<uint8_t> blobv;
};

void writePayload(BinaryWriter& w, const Payload& p) {
  w.u64(p.u64v);
  w.i64(p.i64v);
  w.u32(p.u32v);
  w.u16(p.u16v);
  w.u8(p.u8v);
  w.f32(p.f32v);
  w.f64(p.f64v);
  w.str(p.strv);
  w.bytes(p.blobv.data(), p.blobv.size());
}

bool readPayload(BinaryReader& r, Payload& p) {
  return r.u64(p.u64v) && r.i64(p.i64v) && r.u32(p.u32v) && r.u16(p.u16v) &&
         r.u8(p.u8v) && r.f32(p.f32v) && r.f64(p.f64v) && r.str(p.strv) &&
         r.bytes(p.blobv.data(), p.blobv.size());
}
} // namespace

TEST(serialize_primitives_roundtrip) {
  Payload in{0xDEADBEEFCAFEF00DULL, -1234567890123LL, 0x12345678, 0xABCD, 0x5A,
             3.14159265f, 2.718281828459045, "hello world",
             {1, 2, 3, 4, 5, 6, 7, 8}};
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion,
                                           [&](BinaryWriter& w) { writePayload(w, in); });
  Payload out{};
  out.blobv.resize(8);
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader& r) { return readPayload(r, out); }, err);
  CHECK(ok);
  CHECK_EQ(in.u64v, out.u64v);
  CHECK_EQ(in.i64v, out.i64v);
  CHECK_EQ(in.u32v, out.u32v);
  CHECK_EQ(in.u16v, out.u16v);
  CHECK_EQ(in.u8v, out.u8v);
  CHECK(in.f32v == out.f32v);
  CHECK(in.f64v == out.f64v);
  CHECK_EQ(in.strv, out.strv);
  CHECK(std::memcmp(in.blobv.data(), out.blobv.data(), in.blobv.size()) == 0);
}

TEST(serialize_detects_corruption) {
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) {
    w.u64(1234);
  });
  blob[blob.size() / 2] ^= 0xFF;
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader&) { return true; }, err);
  CHECK(!ok);
  CHECK(err.find("checksum") != std::string::npos);
}

TEST(serialize_detects_version_mismatch) {
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion + 1, [&](BinaryWriter& w) {
    w.u64(1234);
  });
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader&) { return true; }, err);
  CHECK(!ok);
  CHECK(err.find("version") != std::string::npos);
}

TEST(serialize_detects_bad_magic) {
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) {
    w.u64(1234);
  });
  blob[0] = 0xFF;
  blob[1] = 0xFF;
  blob[2] = 0xFF;
  blob[3] = 0xFF;
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader&) { return true; }, err);
  CHECK(!ok);
  CHECK(err.find("magic") != std::string::npos);
}

TEST(serialize_truncated_rejected) {
  std::vector<uint8_t> blob = packSnapshot(kSnapshotVersion, [&](BinaryWriter& w) {
    w.u64(1234);
  });
  blob.resize(blob.size() - 3);
  std::string err;
  bool ok = unpackSnapshot(blob, kSnapshotVersion,
                           [&](BinaryReader&) { return true; }, err);
  CHECK(!ok);
}
