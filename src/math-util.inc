#pragma once

#include "prim-types.hh"
#include "value-types.hh"

namespace tinyusdz {

namespace math {

template <class T>
struct constants;

template <>
struct constants<float> {
  static constexpr float pi() { return float(3.141592653589793238462643383279502884e+00); }
};

template <>
struct constants<double> {
  static constexpr double pi() { return 3.141592653589793238462643383279502884e+00; }
};

inline float radian(float angle) { return 3.141592f * angle / 180.0f; }

inline double radian(double angle) { return 3.141592653589793 * angle / 180.0; }

inline float angle(float radian) { return radian * 180.0f / 3.141592f; }

inline double angle(double radian) {
  return radian * 180.0 / 3.141592653589793;
}

// Simple subtraction is not robust. Use more robust fp diff compare.
// https://embeddeduse.com/2019/08/26/qt-compare-two-floats/
// https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
// TODO: ulp based comparison for more robust compare
inline bool is_close(float a, float b, const float eps) {
  float d = a - b;
  if (std::fabs(d) <= eps) {
    return true;
  }

  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b)));
}

inline bool is_close(double a, double b, const double eps) {
  double d = a - b;
  if (std::fabs(d) <= eps) {
    return true;
  }
  return std::fabs(d) <= (eps * std::fmax(std::fabs(a), std::fabs(b)));
}

inline bool is_close(value::float3 a, value::float3 b, const float eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps) &&
         is_close(a[2], b[2], eps);
}

inline bool is_close(value::double3 a, value::double3 b, const double eps) {
  return is_close(a[0], b[0], eps) && is_close(a[1], b[1], eps) &&
         is_close(a[2], b[2], eps);
}

//
// Computing sin(and cos) using angle * PI / 180.0 is problematic in some
// situation (notably 45*n, 90*n)
// https://stackoverflow.com/questions/6566512/value-of-sine-180-is-coming-out-as-1-22465e-16
//
// Also, sin(radian(45.0)) != cos(radian(45.0)) => 1 bit difference
//
// sin_pi/cos_pi + work around for +/- (pi/4 * n) solves such issue.
// ported from boost

// --------------------------------------------------------------------------
//  Copyright (c) 2007 John Maddock
//  Use, modification and distribution are subject to the
//  Boost Software License, Version 1.0. (See accompanying file
//  LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

template <class T>
inline T sin_pi_imp(T x) {
  if (x < 0) return -sin_pi_imp(T(-x));
  // sin of pi*x:
  bool invert{false};
  if (x < T(0.5)) {

    // [tinyusdz] sin(0.25*pi) still differs with cos(0.25*pi)
    // (whereas boost is not though(they compute sin/cos in higher bits?)
    if (is_close(x, T(0.25), T(0))) {
      return std::cos(constants<T>::pi() * x);
    }

    return std::sin(constants<T>::pi() * x);
  }
  if (x < 1) {
    invert = true;
    x = -x;
  } else
    invert = false;

  T rem = std::floor(x);

  // if(iconvert(rem) & 1)
  //    invert = !invert;
  //
  //  iconvert = static_cast<int>(truc(x))
  //  TODO: Check resulting value is within int32 min/max.
  //  TODO: We may simply do int-type cast since rem should be small.
  {
    T r = std::trunc(rem);
    int ival = static_cast<int>(r);
    if (ival & 1) {
      invert = !invert;
    }
  }

  rem = x - rem;
  if (rem > T(0.5)) rem = 1 - rem;
  if (is_close(rem, T(0.5), T(0.0))) return static_cast<T>(invert ? -1 : 1);

  // [tinyusdz] workaround for -(n * pi/4) and +(n * pi/4) so that it will give identical result with cosine.
  if (is_close(rem, T(0.25), T(0))) {
    rem = std::cos(constants<T>::pi() * rem);
  } else {
    rem = std::sin(constants<T>::pi() * rem);
  }

  return invert ? T(-rem) : rem;
}

template <class T>
inline T cos_pi_imp(T x) {
     // cos of pi*x:
     bool invert = false;
     if(std::fabs(x) < 0.25)
        return std::cos(constants<T>::pi() * x);
     
     if(x < 0)
     {  
        x = -x;
     } 
     T rem = std::floor(x);

     //if(iconvert(rem, pol) & 1)
     //   invert = !invert;
    {
      T r = std::trunc(rem);
      int ival = static_cast<int>(r);
      if (ival & 1) {
        invert = !invert;
      }
    }

     rem = x - rem;
     if(rem > T(0.5))
     {  
        rem = 1 - rem;
        invert = !invert;
     }
     if(is_close(rem, T(0.5), T(0)))
        return T(0);
     
     if(rem > T(0.25))
     {  
        rem = T(0.5) - rem;
        rem = std::sin(constants<T>::pi() * rem);
     }
     else
        rem = std::cos(constants<T>::pi() * rem);
     return invert ? T(-rem) : rem;
}


// --------------------------------------------------------------------------

inline double sin_pi(double x) {
  return sin_pi_imp(x);
}

inline double cos_pi(double x) {
  return cos_pi_imp(x);
}

}  // namespace math

}  // namespace tinyusdz
