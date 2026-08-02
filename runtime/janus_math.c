#include <math.h>
#include <stdbool.h>

/*
 * Keep the standard library bound to a small Janus-owned ABI instead of
 * exposing platform-specific libm symbols directly.  In particular, several
 * C classification operations are macros rather than linkable functions.
 */

#define JANUS_MATH_UNARY(name)                                               \
  double janus_math_##name(double value) { return name(value); }             \
  float janus_math_##name##f(float value) { return name##f(value); }

#define JANUS_MATH_BINARY(name)                                              \
  double janus_math_##name(double left, double right) {                      \
    return name(left, right);                                                \
  }                                                                          \
  float janus_math_##name##f(float left, float right) {                      \
    return name##f(left, right);                                             \
  }

JANUS_MATH_UNARY(fabs)
JANUS_MATH_UNARY(ceil)
JANUS_MATH_UNARY(floor)
JANUS_MATH_UNARY(trunc)
JANUS_MATH_UNARY(round)
JANUS_MATH_UNARY(sqrt)
JANUS_MATH_UNARY(cbrt)
JANUS_MATH_UNARY(exp)
JANUS_MATH_UNARY(exp2)
JANUS_MATH_UNARY(expm1)
JANUS_MATH_UNARY(log)
JANUS_MATH_UNARY(log2)
JANUS_MATH_UNARY(log10)
JANUS_MATH_UNARY(log1p)
JANUS_MATH_UNARY(sin)
JANUS_MATH_UNARY(cos)
JANUS_MATH_UNARY(tan)
JANUS_MATH_UNARY(asin)
JANUS_MATH_UNARY(acos)
JANUS_MATH_UNARY(atan)
JANUS_MATH_UNARY(sinh)
JANUS_MATH_UNARY(cosh)
JANUS_MATH_UNARY(tanh)
JANUS_MATH_UNARY(asinh)
JANUS_MATH_UNARY(acosh)
JANUS_MATH_UNARY(atanh)
JANUS_MATH_UNARY(erf)
JANUS_MATH_UNARY(erfc)
JANUS_MATH_UNARY(tgamma)
JANUS_MATH_UNARY(lgamma)

JANUS_MATH_BINARY(fmin)
JANUS_MATH_BINARY(fmax)
JANUS_MATH_BINARY(fdim)
JANUS_MATH_BINARY(fmod)
JANUS_MATH_BINARY(remainder)
JANUS_MATH_BINARY(pow)
JANUS_MATH_BINARY(hypot)
JANUS_MATH_BINARY(atan2)
JANUS_MATH_BINARY(copysign)
JANUS_MATH_BINARY(nextafter)

bool janus_math_isfinite(double value) { return isfinite(value); }
bool janus_math_isfinitef(float value) { return isfinite(value); }
bool janus_math_isinf(double value) { return isinf(value); }
bool janus_math_isinff(float value) { return isinf(value); }
bool janus_math_isnan(double value) { return isnan(value); }
bool janus_math_isnanf(float value) { return isnan(value); }
bool janus_math_isnormal(double value) { return isnormal(value); }
bool janus_math_isnormalf(float value) { return isnormal(value); }
bool janus_math_signbit(double value) { return signbit(value); }
bool janus_math_signbitf(float value) { return signbit(value); }

