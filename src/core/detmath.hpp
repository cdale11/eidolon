// Deterministic cross-platform transcendental functions.
//
// glibc and musl (WASM) libm differ by ULPs for sin/cos/tanh/exp/log1p, which
// breaks bit-exact native/WASM parity. These replacements compute the same
// IEEE-754 arithmetic on both platforms (identical range reduction, identical
// polynomials, no FMA contraction), so every call is bit-identical.
// Accuracy is ~1e-13 relative on the ranges used by the simulation — far above
// what the sim needs, and importantly the SAME on both platforms.
#pragma once

namespace eidolon {
namespace detmath {

double sin(double x) noexcept;
double cos(double x) noexcept;
float sinf(float x) noexcept;
float cosf(float x) noexcept;

double exp(double x) noexcept;
float expf(float x) noexcept;

double tanh(double x) noexcept;
float tanhf(float x) noexcept;

double log1p(double x) noexcept;
float log1pf(float x) noexcept;

} // namespace detmath
} // namespace eidolon