#pragma once

#include "optional_common.h"
#include "wise_enum.h"

#include <type_traits>

namespace wise_enum {

template <class T>
constexpr bool is_enum_value(std::underlying_type_t<T> enum_integral) {
  const auto &r = range<T>;
  // range-for loop is not constexpr in 14
  for (std::size_t i = 0; i != r.size(); ++i) {
    if (static_cast<std::underlying_type_t<T>>(r[i].value) == enum_integral) {
      return true;
    }
  }
  return false;
}
/*
  A optional for enums that takes the same space as the underlying enum by
  using an integer value that doesn't correspond to any enumerator to indicate
  the "empty" state.

  Requires c++14.
 */

/*
  Implementation note: this doesn't exactly implement the subset of the optional
  interface correctly, in particular operator* and value return by value. This
  is slightly unfortunate but all the other possibilities involve arguably worse
  tradeoffs:
   - Having an integral member and returning enum references would be UB, so to
  avoid UB and return references we have to store an enum
   - At this point, a careful reading of the standard is needed to see whether
  it is legal to store non enumerator values in an enum. As it turns out,
  storing underlying_type::max in an enum is not in general well defined, enums
  of implicit storage can only legally store values in the "bitset range" of
  their enumerators. Enum classes and explicit storage enums don't have this
  issue, but detecting explicit storage-ness is not possible, so it would mean
  only supporting enum class to avoid this UB
   - Finally, we can actually avoid all UB by use memcpy and memcmp instead of
  simple assignment and comparison. However, these functions are not constexpr,
  so we would be forced to un-constexpr many functions.

   In conclusion, returning by value isn't ideal, but discrepancies between its
  behavior and normal optional will mostly be caught at compile time (e.g. *foo
  = MyEnum::BAR will not compile for compact optional). None of the
  discrepancies are critical to usage.
 */
template <class T, std::underlying_type_t<T> invalid =
                       std::numeric_limits<std::underlying_type_t<T>>::max()>
class compact_optional {

  using integral_type = std::underlying_type_t<T>;

  static_assert(is_wise_enum_v<T>,
                "wise enum compact_optional is only for wise enum types");
  static_assert(
      !is_enum_value<T>(invalid),
      "Error, can't use this invalid value as it is taken by an enumerator");

public:
  compact_optional() = default;

  compact_optional(T t) : m_storage(static_cast<integral_type>(t)) {}

  constexpr T operator*() const { return static_cast<T>(m_storage); }

  constexpr bool has_value() const noexcept { return m_storage != invalid; }
  constexpr explicit operator bool() const noexcept {
    return m_storage != invalid;
  }

  constexpr T value() const {
    if (m_storage != invalid)
      return static_cast<T>(m_storage);
    else
      WISE_ENUM_OPTIONAL_BAD_ACCESS;
  }

  template <class U>
  constexpr T value_or(U &&u) {
    if (has_value())
      return static_cast<T>(m_storage);
    else
      return std::forward<U>(u);
  }

  void reset() noexcept { m_storage = invalid; }

  compact_optional(const compact_optional &other) = default;
  compact_optional(compact_optional &&other) = default;
  compact_optional &operator=(const compact_optional &other) = default;
  compact_optional &operator=(compact_optional &&other) = default;

private:
  integral_type m_storage = invalid;
};

} // namespace wise_enum
