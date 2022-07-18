// litexx - Liteweight C++ utility library
//
// This is free and unencumbered software released into the public domain.
//
// Anyone is free to copy, modify, publish, use, compile, sell, or
// distribute this software, either in source code form or as a compiled
// binary, for any purpose, commercial or non - commercial, and by any
// means.
//
// In jurisdictions that recognize copyright laws, the author or authors
// of this software dedicate any and all copyright interest in the
// software to the public domain. We make this dedication for the benefit
// of the public at large and to the detriment of our heirs and
// successors. We intend this dedication to be an overt act of
// relinquishment in perpetuity of all present and future rights to this
// software under copyright law.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// For more information, please refer to <http://unlicense.org/>
//
// Authors:
//   Sean Middleditch <sean@middleditch.us>

#include <type_traits>

#if !defined(_guard_LITEXX_TYPE_TRAITS_H)
#define _guard_LITEXX_TYPE_TRAITS_H
#pragma once

namespace litexx {
    template <typename... A>
    using void_t = void;

    template <typename T>
    T declval();

    template <bool C, typename R = void>
    struct enable_if {};

    template <typename R>
    struct enable_if<true, R> { using type = R; };

    template <bool C, typename R = void>
    using enable_if_t = typename enable_if<C, R>::type;

    template <typename T>
    struct remove_cv { using type = T; };

    template <typename T>
    struct remove_cv<T const> { using type = T; };

    template <typename T>
    struct remove_cv<T volatile> { using type = T; };

    template <typename T>
    struct remove_cv<T const volatile> { using type = T; };

    template <typename T>
    using remove_cv_t = typename remove_cv<T>::type;

    template <typename T>
    struct remove_reference { using type = T; };

    template <typename T>
    struct remove_reference<T&> { using type = T; };

    template <typename T>
    using remove_reference_t = typename remove_reference<T>::type;

    template <typename T>
    using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

    template <typename T, typename U>
    struct is_same { static constexpr bool value = false; };

    template <typename T>
    struct is_same<T, T> { static constexpr bool value = true; };

    template <typename T, typename U>
    static constexpr bool is_same_v = is_same<T, U>::value;
}

#endif // !defined(_guard_LITEXX_TYPE_TRAITS_H)
