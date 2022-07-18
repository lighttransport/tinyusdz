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

#if !defined(_guard_LITEXX_CHAR_TRAITS_H)
#define _guard_LITEXX_CHAR_TRAITS_H
#pragma once

#include <cstddef>

#if 0
#define NODISCARD [[nodiscard]]
#else
#define NODISCARD
#endif

namespace litexx {
    template <typename T>
    struct char_traits {
        using char_type = T;

        NODISCARD static constexpr std::size_t length(char_type const* string) noexcept {
            char_type const* pos = string;
            while (*pos != T{}) {
                ++pos;
            }
            return size_t(pos - string);
        }

        NODISCARD static constexpr int compare(char_type const* lhs, char_type const* rhs, std::size_t length) noexcept {
            for (std::size_t pos = 0; pos != length; ++pos) {
                if (lhs[pos] < rhs[pos]) {
                    return -1;
                }
                else if (lhs[pos] > rhs[pos]) {
                    return 1;
                }
            }
            return 0;
        }
    };

#if defined(LITEXX_USE_STRING_BUILTIN_CHAR_ONLY)
    template <>
    struct char_traits<char> {
        using char_type = char;

        NODISCARD static constexpr std::size_t length(char const* string) noexcept {
            return __builtin_strlen(string);
        }

        NODISCARD static constexpr int compare(char_type const* lhs, char_type const* rhs, std::size_t length) noexcept {
            return __builtin_memcmp(lhs, rhs, length);
        }
    };
#endif
}

#undef NODISCARD

#endif // !defined(_guard_LITEXX_CHAR_TRAITS_H)
