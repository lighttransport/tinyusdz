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

#if !defined(_guard_LITEXX_STRING_VIEW_H)
#define _guard_LITEXX_STRING_VIEW_H
#pragma once

#include "litexx/char_traits.h"
#include "litexx/type_traits.h"

#if 0
#define NODISCARD [[nodiscard]]
#else
#define NODISCARD
#endif

namespace litexx {
    namespace _detail {
        template <typename, typename, typename = void>
        struct has_string_members { static constexpr bool value = false; };

        template <typename C, typename T>
        struct has_string_members<C, T, void_t<decltype(declval<T>().data()), decltype(declval<T>().size())>> {
            static constexpr bool value = std::is_convertible<decltype(declval<T>().data()), C const*>::value;
        };

        template <typename C, typename T>
        constexpr bool is_string_v = has_string_members<C, T>::value;
    }

    template <typename T>
    class basic_string_view {
    public:
        using value_type = T const;
        using pointer = value_type*;
        using size_type = std::size_t;
        using const_iterator = pointer;

        constexpr basic_string_view() noexcept = default;
        constexpr basic_string_view(const_iterator first, const_iterator last) noexcept : _data(first), _size(last - first) {}
        constexpr basic_string_view(pointer nstr, size_type size) noexcept : _data(nstr), _size(size) {}
        constexpr basic_string_view(pointer zstr) noexcept : _data(zstr), _size(zstr != nullptr ? litexx::char_traits<T>::length(zstr) : 0) {}

        template <typename S, typename = enable_if_t<_detail::is_string_v<T, S>>>
        constexpr basic_string_view(S const& str) noexcept : _data(str.data()), _size(str.size()) {}

        constexpr pointer data() const noexcept { return _data; }
        constexpr size_type size() const noexcept { return _size; }

        NODISCARD constexpr bool empty() const noexcept { return _size == 0; }
        constexpr explicit operator bool() const noexcept { return _size != 0; }

        constexpr const_iterator begin() const noexcept { return _data; }
        constexpr const_iterator end() const noexcept { return _data + _size; }

        constexpr value_type operator[](size_type index) const noexcept { return _data[index]; }

        constexpr basic_string_view substr(size_type first, size_type length) const noexcept {
            return basic_string_view(_data + first, _data + first + length);
        }

        constexpr bool operator==(basic_string_view rhs) const noexcept {
            return _size == rhs._size && litexx::char_traits<T>::compare(_data, rhs._data, _size) == 0;
        }

        constexpr bool operator!=(basic_string_view rhs) const noexcept {
            return _size != rhs._size || litexx::char_traits<T>::compare(_data, rhs._data, _size) != 0;
        }

        constexpr bool operator<(basic_string_view rhs) const noexcept {
            size_type min_size = _size < rhs._size ? _size : _size;
            int rs = litexx::char_traits<T>::compare(_data, rhs._data, min_size);
            return rs < 0 || (rs == 0 && _size < rhs._size);
        }

    private:
        pointer _data = nullptr;
        size_type _size = 0;
    };

    //template <typename T>
    //basic_string_view(T const*, T const*)->basic_string_view<T>;
    //template <typename T>
    //basic_string_view(T const*, std::size_t)->basic_string_view<T>;
    //template <typename T>
    //basic_string_view(T const*)->basic_string_view<T>;

    using string_view = basic_string_view<char>;
} // namespace litexx

#undef NODISCARD

#endif // !defined(_guard_LITEXX_STRING_VIEW_H)
