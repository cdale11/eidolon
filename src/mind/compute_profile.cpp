#include "mind/compute_profile.hpp"
#include <algorithm>
#include <sstream>

namespace eidolon {

void ComputeProfile::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(simdLevel));
  w.u8(hasWasmSimd128 ? 1 : 0);
  w.u8(static_cast<uint8_t>(threadSupport));
  w.u32(maxWorkers);
  w.u8(hasSharedArrayBuffer ? 1 : 0);
  w.u8(static_cast<uint8_t>(gpuBackend));
  w.u8(hasWebGPU ? 1 : 0);
  w.u8(hasWebGL ? 1 : 0);
  w.u64(maxMemoryBytes);
  w.u64(preferredMemoryBytes);
  w.f64(estimatedSimStepsPerSec);
  w.f64(estimatedInferencesPerSec);
  w.str(userAgent);
  w.str(platform);
}

bool ComputeProfile::deserialize(struct BinaryReader& r) {
  uint8_t u8;
  if (!r.u8(u8)) return false;
  simdLevel = static_cast<SimdLevel>(u8);
  if (!r.u8(u8)) return false;
  hasWasmSimd128 = (u8 != 0);
  if (!r.u8(u8)) return false;
  threadSupport = static_cast<ThreadSupport>(u8);
  if (!r.u32(maxWorkers)) return false;
  if (!r.u8(u8)) return false;
  hasSharedArrayBuffer = (u8 != 0);
  if (!r.u8(u8)) return false;
  gpuBackend = static_cast<GpuBackend>(u8);
  if (!r.u8(u8)) return false;
  hasWebGPU = (u8 != 0);
  if (!r.u8(u8)) return false;
  hasWebGL = (u8 != 0);
  if (!r.u64(maxMemoryBytes)) return false;
  if (!r.u64(preferredMemoryBytes)) return false;
  if (!r.f64(estimatedSimStepsPerSec)) return false;
  if (!r.f64(estimatedInferencesPerSec)) return false;
  if (!r.str(userAgent)) return false;
  if (!r.str(platform)) return false;
  return true;
}

std::string ComputeProfile::toString() const {
  std::ostringstream oss;
  oss << "ComputeProfile:\n";
  oss << "  SIMD: ";
  switch (simdLevel) {
    case SimdLevel::None: oss << "None"; break;
    case SimdLevel::SSE2: oss << "SSE2"; break;
    case SimdLevel::SSE4: oss << "SSE4"; break;
    case SimdLevel::AVX: oss << "AVX"; break;
    case SimdLevel::AVX2: oss << "AVX2"; break;
    case SimdLevel::WASM_SIMD128: oss << "WASM SIMD128"; break;
  }
  oss << " (WASM SIMD128: " << (hasWasmSimd128 ? "yes" : "no") << ")\n";
  
  oss << "  Threading: ";
  switch (threadSupport) {
    case ThreadSupport::None: oss << "None"; break;
    case ThreadSupport::SingleThread: oss << "Single"; break;
    case ThreadSupport::WebWorkers: oss << "WebWorkers"; break;
    case ThreadSupport::SharedArrayBuffer: oss << "SharedArrayBuffer"; break;
    case ThreadSupport::NativePthreads: oss << "Native pthreads"; break;
  }
  oss << " (max workers: " << maxWorkers << ", SAB: " << (hasSharedArrayBuffer ? "yes" : "no") << ")\n";
  
  oss << "  GPU: ";
  switch (gpuBackend) {
    case GpuBackend::None: oss << "None"; break;
    case GpuBackend::WebGL: oss << "WebGL"; break;
    case GpuBackend::WebGPU: oss << "WebGPU"; break;
    case GpuBackend::CUDA: oss << "CUDA"; break;
    case GpuBackend::Vulkan: oss << "Vulkan"; break;
    case GpuBackend::Metal: oss << "Metal"; break;
    case GpuBackend::ROCm: oss << "ROCm"; break;
  }
  oss << " (WebGPU: " << (hasWebGPU ? "yes" : "no") << ", WebGL: " << (hasWebGL ? "yes" : "no") << ")\n";
  
  oss << "  Memory: max=" << (maxMemoryBytes / (1024*1024)) << "MB, preferred=" << (preferredMemoryBytes / (1024*1024)) << "MB\n";
  oss << "  Est. sim steps/s: " << estimatedSimStepsPerSec << "\n";
  oss << "  Est. inferences/s: " << estimatedInferencesPerSec << "\n";
  oss << "  UA: " << userAgent << "\n";
  return oss.str();
}

void BackendSelection::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(type));
  w.str(reason);
  profile.serialize(w);
}

bool BackendSelection::deserialize(struct BinaryReader& r) {
  uint8_t u8;
  if (!r.u8(u8)) return false;
  type = static_cast<BackendType>(u8);
  if (!r.str(reason)) return false;
  return profile.deserialize(r);
}

SimdLevel ComputeProfileDetector::detectSimdLevel(const std::string& userAgent, bool wasmSimd128) {
  if (wasmSimd128) return SimdLevel::WASM_SIMD128;
  
  if (userAgent.find("x86_64") != std::string::npos || userAgent.find("amd64") != std::string::npos) {
    return SimdLevel::AVX2;
  } else if (userAgent.find("arm64") != std::string::npos || userAgent.find("aarch64") != std::string::npos) {
    return SimdLevel::SSE4;
  }
  return SimdLevel::None;
}

ThreadSupport ComputeProfileDetector::detectThreadSupport(bool sharedArrayBuffer, uint32_t concurrency) {
  if (sharedArrayBuffer && concurrency > 1) return ThreadSupport::SharedArrayBuffer;
  if (concurrency > 1) return ThreadSupport::WebWorkers;
  return ThreadSupport::SingleThread;
}

GpuBackend ComputeProfileDetector::detectGpuBackend(bool webgpu, bool webgl) {
  if (webgpu) return GpuBackend::WebGPU;
  if (webgl) return GpuBackend::WebGL;
  return GpuBackend::None;
}

ComputeProfile ComputeProfileDetector::fromJsCapabilities(
    bool wasmSimd128,
    bool sharedArrayBuffer,
    uint32_t hardwareConcurrency,
    bool webgpu,
    bool webgl,
    uint64_t maxMemory,
    const std::string& userAgent) {
  
  ComputeProfile profile;
  profile.userAgent = userAgent;
  
  profile.hasWasmSimd128 = wasmSimd128;
  profile.simdLevel = detectSimdLevel(userAgent, wasmSimd128);
  
  profile.hasSharedArrayBuffer = sharedArrayBuffer;
  profile.threadSupport = detectThreadSupport(sharedArrayBuffer, hardwareConcurrency);
  profile.maxWorkers = std::max<uint32_t>(1, hardwareConcurrency);
  
  profile.hasWebGPU = webgpu;
  profile.hasWebGL = webgl;
  profile.gpuBackend = detectGpuBackend(webgpu, webgl);
  
  profile.maxMemoryBytes = maxMemory > 0 ? maxMemory : 64 * 1024 * 1024;
  profile.preferredMemoryBytes = std::min<uint64_t>(profile.maxMemoryBytes * 4, 1024 * 1024 * 1024);
  
  if (userAgent.find("Windows") != std::string::npos) profile.platform = "Windows";
  else if (userAgent.find("Mac") != std::string::npos) profile.platform = "macOS";
  else if (userAgent.find("Linux") != std::string::npos) profile.platform = "Linux";
  else if (userAgent.find("Android") != std::string::npos) profile.platform = "Android";
  else if (userAgent.find("iPhone") != std::string::npos || userAgent.find("iPad") != std::string::npos) profile.platform = "iOS";
  else profile.platform = "Unknown";
  
  if (profile.hasWebGPU) {
    profile.estimatedSimStepsPerSec = 5000;
    profile.estimatedInferencesPerSec = 500;
  } else if (profile.simdLevel == SimdLevel::WASM_SIMD128 && profile.threadSupport >= ThreadSupport::SharedArrayBuffer) {
    profile.estimatedSimStepsPerSec = 2000 * std::min(profile.maxWorkers, 4u);
    profile.estimatedInferencesPerSec = 200 * std::min(profile.maxWorkers, 4u);
  } else if (profile.simdLevel == SimdLevel::WASM_SIMD128) {
    profile.estimatedSimStepsPerSec = 1000;
    profile.estimatedInferencesPerSec = 100;
  } else {
    profile.estimatedSimStepsPerSec = 500;
    profile.estimatedInferencesPerSec = 50;
  }
  
  return profile;
}

BackendSelection ComputeProfileDetector::selectBackend(const ComputeProfile& profile) {
  auto priority = getBackendPriority(profile);
  
  for (auto backend : priority) {
    if (isBackendViable(backend, profile)) {
      BackendSelection selection;
      selection.type = backend;
      selection.profile = profile;
      
      switch (backend) {
        case BackendType::WebGPU:
          selection.reason = "WebGPU available - highest performance";
          break;
        case BackendType::WasmSimdMt:
          selection.reason = "WASM SIMD + multithreading available";
          break;
        case BackendType::WasmSimd:
          selection.reason = "WASM SIMD128 available";
          break;
        case BackendType::WasmPlain:
          selection.reason = "Basic WASM support";
          break;
        case BackendType::ServerFallback:
          selection.reason = "No viable client backend - falling back to server";
          break;
        case BackendType::Native:
          selection.reason = "Native execution";
          break;
      }
      return selection;
    }
  }
  
  BackendSelection selection;
  selection.type = BackendType::ServerFallback;
  selection.reason = "No viable backend found";
  selection.profile = profile;
  return selection;
}

std::vector<BackendType> ComputeProfileDetector::getBackendPriority(const ComputeProfile& profile) {
  std::vector<BackendType> priority;
  
  if (profile.hasWebGPU) {
    priority.push_back(BackendType::WebGPU);
  }
  
  if (profile.hasWasmSimd128 && profile.hasSharedArrayBuffer && profile.maxWorkers > 1) {
    priority.push_back(BackendType::WasmSimdMt);
  }
  
  if (profile.hasWasmSimd128) {
    priority.push_back(BackendType::WasmSimd);
  }
  
  priority.push_back(BackendType::WasmPlain);
  priority.push_back(BackendType::ServerFallback);
  
  return priority;
}

bool ComputeProfileDetector::isBackendViable(BackendType backend, const ComputeProfile& profile) {
  switch (backend) {
    case BackendType::WebGPU:
      return profile.hasWebGPU && profile.maxMemoryBytes >= 128 * 1024 * 1024;
    case BackendType::WasmSimdMt:
      return profile.hasWasmSimd128 && profile.hasSharedArrayBuffer && profile.maxWorkers > 1;
    case BackendType::WasmSimd:
      return profile.hasWasmSimd128;
    case BackendType::WasmPlain:
      return true;
    case BackendType::ServerFallback:
      return true;
    case BackendType::Native:
      return false;
  }
  return false;
}

double ComputeProfileDetector::estimatePerformance(BackendType backend, const ComputeProfile& profile) {
  if (!isBackendViable(backend, profile)) return 0.0;
  
  double baseSim = profile.estimatedSimStepsPerSec;
  double baseInference = profile.estimatedInferencesPerSec;
  
  switch (backend) {
    case BackendType::WebGPU:
      return baseSim * 2.0 + baseInference * 3.0;
    case BackendType::WasmSimdMt:
      return baseSim * 1.5 + baseInference * 1.5;
    case BackendType::WasmSimd:
      return baseSim + baseInference;
    case BackendType::WasmPlain:
      return baseSim * 0.5 + baseInference * 0.5;
    case BackendType::ServerFallback:
      return 100.0;
    case BackendType::Native:
      return baseSim * 3.0 + baseInference * 2.0;
  }
  return 0.0;
}

} // namespace eidolon