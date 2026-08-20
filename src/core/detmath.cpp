#include "core/detmath.hpp"

#include <cmath>

namespace eidolon {
namespace detmath {
namespace {

// 2*pi and pi/2 to full double precision.
constexpr double kTwoPi = 6.28318530717958647692528676655900577;
constexpr double kHalfPi = 1.57079632679489661923132169163975144;

// ln(2) and 1/ln(2).
constexpr double kLn2 = 0.69314718055994530941723212145817657;
constexpr double kInvLn2 = 1.44269504088896340735992468100189214;

// Argument reduction to [-pi/4, pi/4]. Returns the quadrant index (0..3) and
// the reduced angle r. All arithmetic is plain IEEE doubles.
inline void reduce(double x, double& r, int& quad) noexcept {
  // Fold into [0, 2pi).
  const double n = std::floor(x / kTwoPi);
  const double y = x - n * kTwoPi;
  // Fold into [-pi/4, pi/4]; k = number of pi/2 steps, in [-2, 2].
  const double k = std::floor(y / kHalfPi + 0.5);
  quad = static_cast<int>(k) & 3;
  r = y - k * kHalfPi;
}

} // namespace

double sin(double x) noexcept {
  double r;
  int q;
  reduce(x, r, q);
  const double r2 = r * r;
  // sin(r) = r * (1 - r^2/6 + r^4/120 - r^6/5040 + r^8/362880 - r^10/39916800 + ...)
  double s = r * (1.0 + r2 * (-1.0 / 6.0 +
                             r2 * (1.0 / 120.0 +
                             r2 * (-1.0 / 5040.0 +
                             r2 * (1.0 / 362880.0 +
                             r2 * (-1.0 / 39916800.0 +
                             r2 * (1.0 / 6227020800.0)))))));
  // sin(x) quadrant mapping: q0=+sin(r), q1=+cos(r), q2=-sin(r), q3=-cos(r)
  switch (q) {
    case 0: return s;
    case 2: return -s;
    case 1: {
      double c = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 +
                            r2 * (-1.0 / 720.0 +
                            r2 * (1.0 / 40320.0 +
                            r2 * (-1.0 / 3628800.0 +
                            r2 * (1.0 / 479001600.0))))));
      return c;
    }
    default: {
      double c = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 +
                            r2 * (-1.0 / 720.0 +
                            r2 * (1.0 / 40320.0 +
                            r2 * (-1.0 / 3628800.0 +
                            r2 * (1.0 / 479001600.0))))));
      return -c;
    }
  }
}

double cos(double x) noexcept {
  double r;
  int q;
  reduce(x, r, q);
  const double r2 = r * r;
  // cos(r) = 1 - r^2/2 + r^4/24 - r^6/720 + r^8/40320 - r^10/3628800 + r^12/479001600
  double c = 1.0 + r2 * (-0.5 + r2 * (1.0 / 24.0 +
                        r2 * (-1.0 / 720.0 +
                        r2 * (1.0 / 40320.0 +
                        r2 * (-1.0 / 3628800.0 +
                        r2 * (1.0 / 479001600.0))))));
  // cos(x) quadrant mapping: q0=+cos(r), q1=-sin(r), q2=-cos(r), q3=+sin(r)
  switch (q) {
    case 0: return c;
    case 2: return -c;
    case 1: {
      double s = r * (1.0 + r2 * (-1.0 / 6.0 +
                             r2 * (1.0 / 120.0 +
                             r2 * (-1.0 / 5040.0 +
                             r2 * (1.0 / 362880.0 +
                             r2 * (-1.0 / 39916800.0 +
                             r2 * (1.0 / 6227020800.0)))))));
      return -s;
    }
    default: {
      double s = r * (1.0 + r2 * (-1.0 / 6.0 +
                             r2 * (1.0 / 120.0 +
                             r2 * (-1.0 / 5040.0 +
                             r2 * (1.0 / 362880.0 +
                             r2 * (-1.0 / 39916800.0 +
                             r2 * (1.0 / 6227020800.0)))))));
      return s;
    }
  }
}

float sinf(float x) noexcept { return static_cast<float>(sin(static_cast<double>(x))); }
float cosf(float x) noexcept { return static_cast<float>(cos(static_cast<double>(x))); }

double exp(double x) noexcept {
  // exp(x) = 2^n * exp(r), n = round(x/ln2), r in [-ln2/2, ln2/2].
  const double n = std::floor(x * kInvLn2 + 0.5);
  const double r = x - n * kLn2;
  // exp(r) Taylor: 1 + r + r^2/2 + r^3/6 + ... to r^11/39916800.
  double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6.0 +
                       r * (1.0 / 24.0 + r * (1.0 / 120.0 +
                       r * (1.0 / 720.0 + r * (1.0 / 5040.0 +
                       r * (1.0 / 40320.0 + r * (1.0 / 362880.0 +
                       r * (1.0 / 3628800.0 + r * (1.0 / 39916800.0)))))))))));
  return std::ldexp(p, static_cast<int>(n));
}

float expf(float x) noexcept { return static_cast<float>(exp(static_cast<double>(x))); }

double tanh(double x) noexcept {
  // Use the identity tanh(x) = 1 - 2/(exp(2x)+1) for |x| large, and a Taylor
  // series for small |x| where the identity suffers cancellation.
  const double ax = std::fabs(x);
  if (ax < 0.5493061443340549) {  // ln(3)/2
    const double x2 = x * x;
    // tanh(x) = x - x^3/3 + 2x^5/15 - 17x^7/315 + 62x^9/2835 - 1382x^11/155925
    double t = x * (1.0 + x2 * (-1.0 / 3.0 +
                    x2 * (2.0 / 15.0 +
                    x2 * (-17.0 / 315.0 +
                    x2 * (62.0 / 2835.0 +
                    x2 * (-1382.0 / 155925.0 +
                    x2 * (21844.0 / 6081075.0)))))));
    return t;
  }
  if (ax < 20.0) {
    const double e = exp(2.0 * ax);
    const double t = 1.0 - 2.0 / (e + 1.0);
    return (x < 0.0) ? -t : t;
  }
  return (x < 0.0) ? -1.0 : 1.0;
}

float tanhf(float x) noexcept { return static_cast<float>(tanh(static_cast<double>(x))); }

double log1p(double x) noexcept {
  if (x <= -1.0) return -std::numeric_limits<double>::infinity();
  const double y = 1.0 + x;
  if (y == 1.0) return x;
  int e;
  const double m = std::frexp(y, &e);  // y = m * 2^e, m in [0.5, 1)
  // ln(m) via atanh((m-1)/(m+1)); |t| <= 1/3, converges fast.
  const double t = (m - 1.0) / (m + 1.0);
  const double t2 = t * t;
  double a = t * (1.0 + t2 * (1.0 / 3.0 +
               t2 * (1.0 / 5.0 +
               t2 * (1.0 / 7.0 +
               t2 * (1.0 / 9.0 +
               t2 * (1.0 / 11.0 +
               t2 * (1.0 / 13.0 +
               t2 * (1.0 / 15.0 +
               t2 * (1.0 / 17.0 +
               t2 * (1.0 / 19.0))))))))));
  return static_cast<double>(e) * kLn2 + 2.0 * a;
}

float log1pf(float x) noexcept { return static_cast<float>(log1p(static_cast<double>(x))); }

} // namespace detmath
} // namespace eidolon