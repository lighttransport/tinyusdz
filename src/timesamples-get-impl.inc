// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TypedTimeSamples::get() implementations
// This file is included by timesamples.cc and split instantiation files

//
// TypedTimeSamples::get() implementations
//

// Get value for non-interpolatable types
template<typename T>
template<typename V, std::enable_if_t<!value::LerpTraits<V>::supported(), std::nullptr_t>>
bool TypedTimeSamples<T>::get(T *dst, double t,
                              value::TimeSampleInterpolationType interp) const {
  (void)interp;

  if (!dst) {
    return false;
  }

  if (empty()) {
    return false;
  }

  if (_dirty) {
    update();
  }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  // AoS layout implementation
  if (value::TimeCode(t).is_default()) {
    // FIXME: Use the first item for now.
    // TODO: Handle blocked
    (*dst) = _samples[0].value;
    return true;
  } else {

    if (_samples.size() == 1) {
      (*dst) = _samples[0].value;
      return true;
    }

    // Held = nearest preceding value for a given time.
    auto it = std::upper_bound(
      _samples.begin(), _samples.end(), t,
      [](double tval, const Sample &a) { return tval < a.t; });

    const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

    (*dst) = it_minus_1->value;
    return true;
  }
#else
  // SoA layout implementation
  if (value::TimeCode(t).is_default()) {
    // FIXME: Use the first item for now.
    // TODO: Handle blocked
    (*dst) = _values[0];
    return true;
  } else {

    if (_times.size() == 1) {
      (*dst) = _values[0];
      return true;
    }

    // Held = nearest preceding value for a given time.
    auto it = std::upper_bound(_times.begin(), _times.end(), t);
    size_t idx = (it == _times.begin()) ? 0 : static_cast<size_t>(std::distance(_times.begin(), it) - 1);

    (*dst) = _values[idx];
    return true;
  }
#endif
}

// Get value for interpolatable types
template<typename T>
template<typename V, std::enable_if_t<value::LerpTraits<V>::supported(), std::nullptr_t>>
bool TypedTimeSamples<T>::get(T *dst, double t,
                              value::TimeSampleInterpolationType interp) const {
  if (!dst) {
    return false;
  }

  if (empty()) {
    return false;
  }

  if (_dirty) {
    update();
  }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  // AoS layout implementation
  if (value::TimeCode(t).is_default()) {
    // FIXME: Use the first item for now.
    // TODO: Handle blocked
    (*dst) = _samples[0].value;
    return true;
  } else {

    if (_samples.size() == 1) {
      (*dst) = _samples[0].value;
      return true;
    }

    auto it = std::lower_bound(
      _samples.begin(), _samples.end(), t,
      [](const Sample &a, double tval) { return a.t < tval; });

    if (interp == value::TimeSampleInterpolationType::Linear) {

      // MS STL does not allow seek vector iterator before begin
      // Issue #110
      const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

      size_t idx0 = size_t((std::max)(
          int64_t(0),
          (std::min)(int64_t(_samples.size() - 1),
                   int64_t(std::distance(_samples.begin(), it_minus_1)))));
      size_t idx1 =
          size_t((std::max)(int64_t(0), (std::min)(int64_t(_samples.size() - 1),
                                               int64_t(idx0) + 1)));

      double tl = _samples[idx0].t;
      double tu = _samples[idx1].t;

      double dt = (t - tl);
      if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
        // slope is zero.
        dt = 0.0;
      } else {
        dt /= (tu - tl);
      }

      // Just in case.
      dt = (std::max)(0.0, (std::min)(1.0, dt));

      const T &p0 = _samples[idx0].value;
      const T &p1 = _samples[idx1].value;

      const T p = lerp(p0, p1, dt);

      (*dst) = std::move(p);
      return true;
    } else {
      if (it == _samples.end()) {
        // ???
        return false;
      }

      (*dst) = it->value;
      return true;
    }
  }
#else
  // SoA layout implementation
  if (value::TimeCode(t).is_default()) {
    // FIXME: Use the first item for now.
    // TODO: Handle blocked
    (*dst) = _values[0];
    return true;
  } else {

    if (_times.size() == 1) {
      (*dst) = _values[0];
      return true;
    }

    auto it = std::lower_bound(_times.begin(), _times.end(), t);

    if (interp == value::TimeSampleInterpolationType::Linear) {

      // MS STL does not allow seek vector iterator before begin
      // Issue #110
      const auto it_minus_1 = (it == _times.begin()) ? _times.begin() : (it - 1);

      size_t idx0 = size_t((std::max)(
          int64_t(0),
          (std::min)(int64_t(_times.size() - 1),
                   int64_t(std::distance(_times.begin(), it_minus_1)))));
      size_t idx1 =
          size_t((std::max)(int64_t(0), (std::min)(int64_t(_times.size() - 1),
                                               int64_t(idx0) + 1)));

      double tl = _times[idx0];
      double tu = _times[idx1];

      double dt = (t - tl);
      if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
        // slope is zero.
        dt = 0.0;
      } else {
        dt /= (tu - tl);
      }

      // Just in case.
      dt = (std::max)(0.0, (std::min)(1.0, dt));

      const T &p0 = _values[idx0];
      const T &p1 = _values[idx1];

      const T p = lerp(p0, p1, dt);

      (*dst) = std::move(p);
      return true;
    } else {
      if (it == _times.end()) {
        // ???
        return false;
      }

      size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
      (*dst) = _values[idx];
      return true;
    }
  }
#endif

  return false;
}
