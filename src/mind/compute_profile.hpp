#ifndef EIDOLON_COMPUTE_PROFILE_HPP
#define EIDOLON_COMPUTE_PROFILE_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "core/serialize.hpp"

namespace eidolon {

// Capability detection and backend selection for client-first compute architecture
// Phase 11: Portable WASM client compute (DESIGN §17)

// SIMD support levels
enum class SimdLevel : uint8_t {
  None = 0,
  SSE2 = 1,
  SSE4 = 2,
  AVX = 3,
  AVX2 = 4,
  WASM_SIMD128 = 5,
};

// Threading support
enum class ThreadSupport : uint8_t {
  None = 0,
  SingleThread = 1,
  WebWorkers = 2,
  SharedArrayBuffer = 3,
  NativePthreads = 4,
};

// GPU acceleration
enum class GpuBackend : uint8_t {
  None = 0,
  WebGL = 1,
  WebGPU = 2,
  CUDA = 3,
  Vulkan = 4,
  Metal = 5,
  ROCm = 6,
};

// Compute profile describing client capabilities
struct ComputeProfile {
  // SIMD
  SimdLevel simdLevel = SimdLevel::None;
  bool hasWasmSimd128 = false;
  
  // Threading
  ThreadSupport threadSupport = ThreadSupport::None;
  uint32_t maxWorkers = 1;
  bool hasSharedArrayBuffer = false;
  
  // GPU
  GpuBackend gpuBackend = GpuBackend::None;
  bool hasWebGPU = false;
  bool hasWebGL = false;
  
  // Memory
  uint64_t maxMemoryBytes = 64 * 1024 * 1024; // 64 MB default
  uint64_t preferredMemoryBytes = 256 * 1024 * 1024; // 256 MB default
  
  // Performance hints
  double estimatedSimStepsPerSec = 1000.0;
  double estimatedInferencesPerSec = 100.0;
  
  // Platform identification
  std::string userAgent;
  std::string platform;
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
  // Human-readable summary
  std::string toString() const;
};

// Backend selection result
enum class BackendType : uint8_t {
  ServerFallback = 0,
  WasmPlain = 1,
  WasmSimd = 2,
  WasmSimdMt = 3,
  WebGPU = 4,
  Native = 5,
};

struct BackendSelection {
  BackendType type = BackendType::ServerFallback;
  std::string reason;
  ComputeProfile profile;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

// Capability detector (client-side JavaScript would populate this)
// C++ side provides the logic for backend selection
class ComputeProfileDetector {
public:
  // Create profile from JavaScript-provided capabilities
  static ComputeProfile fromJsCapabilities(
      bool wasmSimd128,
      bool sharedArrayBuffer,
      uint32_t hardwareConcurrency,
      bool webgpu,
      bool webgl,
      uint64_t maxMemory,
      const std::string& userAgent);
  
  // Select best backend for given profile
  BackendSelection selectBackend(const ComputeProfile& profile);
  
  // Get backend priority order for a profile
  std::vector<BackendType> getBackendPriority(const ComputeProfile& profile);
  
  // Check if backend is viable for profile
  bool isBackendViable(BackendType backend, const ComputeProfile& profile);
  
  // Estimate performance for backend/profile combo
  double estimatePerformance(BackendType backend, const ComputeProfile& profile);
  
private:
  static SimdLevel detectSimdLevel(const std::string& userAgent, bool wasmSimd128);
  static ThreadSupport detectThreadSupport(bool sharedArrayBuffer, uint32_t concurrency);
  static GpuBackend detectGpuBackend(bool webgpu, bool webgl);
};

// Adaptive fidelity (Phase 11): constrained clients reduce sim frequency, model budget,
// and world detail without changing the organism's identity. All knobs affect ONLY
// throughput/resolution — never the deterministic tick semantics (bit-exact replay for
// the same seed is preserved; a slower client simply covers less simulated ground per
// wall-second).
enum class FidelityLevel : uint8_t {
  Low = 0,     // constrained client: slow pacing, low model budget, small world
  Medium = 1,  // typical client: balanced pacing/model/world
  High = 2,    // capable client / native: full speed, full model budget
};

struct FidelitySettings {
  FidelityLevel level = FidelityLevel::Medium;

  // Sim pacing: simulated seconds per wall second the client can sustain.
  double simSecondsPerWallSecond = 500.0;
  // Model budget: fraction of learning/ML work per tick (0.5 = half inference budget).
  double modelBudgetScale = 1.0;
  // World detail: scale factor applied to world size on fresh generation (1.0 = full).
  double worldDetailScale = 1.0;
  // Whether optional expensive features (LLM reflection, life review) are enabled.
  bool llmReflectionEnabled = true;
  // Whether sleep consolidation is deferred to lighter loads.
  bool deferConsolidation = false;

  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  std::string toString() const;
};

// Maps a compute profile (or an explicit level) to concrete fidelity settings.
class FidelityController {
public:
  static FidelitySettings settingsFor(FidelityLevel level);
  static FidelitySettings settingsForProfile(const ComputeProfile& profile);
  // Pick the most capable level the profile supports (for auto-selection).
  static FidelityLevel autoLevel(const ComputeProfile& profile);
};

} // namespace eidolon

#endif // EIDOLON_COMPUTE_PROFILE_HPP