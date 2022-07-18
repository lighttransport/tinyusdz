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

#if !defined(_guard_FORMATXX_DETAIL_FORMAT_ARG_H)
#define _guard_FORMATXX_DETAIL_FORMAT_ARG_H
#pragma once

#include <initializer_list>

namespace formatxx {
namespace _detail {
    enum class format_arg_type;

    template <typename CharT> class basic_format_arg;
    template <typename CharT> class basic_format_arg_list;

    template <typename CharT, typename T> constexpr basic_format_arg<CharT> make_format_arg(T const& value) noexcept;
}}

enum class formatxx::_detail::format_arg_type {
    unknown,
    char_t,
    wchar,
    signed_char,
    unsigned_char,
    signed_int,
    unsigned_int,
    signed_short_int,
    unsigned_short_int,
    signed_long_int,
    unsigned_long_int,
    signed_long_long_int,
    unsigned_long_long_int,
    single_float,
    double_float,
    boolean,
    char_string,
    wchar_string,
    null_pointer,
    void_pointer,
    custom
};

/// Abstraction for a single formattable value
template <typename CharT>
class formatxx::_detail::basic_format_arg {
public:
    using thunk_type = result_code(FORMATXX_API*)(basic_format_writer<CharT>&, void const*, basic_format_options<CharT>);

    constexpr basic_format_arg() noexcept = default;
    constexpr basic_format_arg(_detail::format_arg_type type, void const* value) noexcept : _type(type), _value(value) {}
    constexpr basic_format_arg(thunk_type thunk, void const* value) noexcept : _type(_detail::format_arg_type::custom), _thunk(thunk), _value(value) {}

    FORMATXX_PUBLIC result_code FORMATXX_API format_into(basic_format_writer<CharT>& output, basic_format_options<CharT> const& options) const;

private:
    _detail::format_arg_type _type = _detail::format_arg_type::unknown;
    thunk_type _thunk = nullptr;
    void const* _value = nullptr;
};

/// Abstraction for a set of format arguments.
template <typename CharT>
class formatxx::_detail::basic_format_arg_list {
public:
    using format_arg_type = basic_format_arg<CharT>;
    using thunk_type = typename format_arg_type::thunk_type;
    using size_type = std::size_t;

    constexpr basic_format_arg_list() noexcept = default;
    constexpr basic_format_arg_list(std::initializer_list<format_arg_type> args) noexcept : _args(args.begin()), _count(args.size()) {}

    constexpr result_code format_arg(basic_format_writer<CharT>& output, size_type index, basic_format_options<CharT> const& options) const {
        return index < _count ? _args[index].format_into(output, options) : result_code::out_of_range;
    }

private:
    basic_format_arg<CharT> const* _args = nullptr;
    size_type _count = 0;
};

namespace formatxx{
namespace _detail {

    template <class... Types>
    struct VoidTester {
      using type = void;
    };

    template <class... Types>
    using void_t = typename VoidTester<Types...>::type;

    template <typename T>
    using decay_array_t = std::conditional_t<std::is_array<T>::value, std::remove_extent_t<T> const*, T>;

    template <typename T>
    using formattable_t = decay_array_t<std::remove_reference_t<T>>;

    template <typename C, typename T, typename V = void>
    struct has_format_value { static constexpr bool value = false; };
    template <typename C, typename T>
    struct has_format_value<C, T, void_t<decltype(format_value(std::declval<basic_format_writer<C>&>(), std::declval<T>(), std::declval<basic_format_options<C>>()))>> {
        static constexpr bool value = true;
    };

    template <typename T> struct type_of { static constexpr format_arg_type value = format_arg_type::unknown; };
#define FORMATXX_TYPE(x, e) template <> struct type_of<x> { static constexpr format_arg_type value = format_arg_type::e; };
    FORMATXX_TYPE(char, char_t);
    FORMATXX_TYPE(wchar_t, wchar);
    FORMATXX_TYPE(signed char, signed_char);
    FORMATXX_TYPE(unsigned char, unsigned_char);
    FORMATXX_TYPE(signed int, signed_int);
    FORMATXX_TYPE(unsigned int, unsigned_int);
    FORMATXX_TYPE(signed short, signed_short_int);
    FORMATXX_TYPE(unsigned short, unsigned_short_int);
    FORMATXX_TYPE(signed long, signed_long_int);
    FORMATXX_TYPE(unsigned long, unsigned_long_int);
    FORMATXX_TYPE(signed long long, signed_long_long_int);
    FORMATXX_TYPE(unsigned long long, unsigned_long_long_int);
    FORMATXX_TYPE(float, single_float);
    FORMATXX_TYPE(double, double_float);
    FORMATXX_TYPE(bool, boolean);
    FORMATXX_TYPE(char*, char_string);
    FORMATXX_TYPE(char const*, char_string);
    FORMATXX_TYPE(wchar_t*, char_string);
    FORMATXX_TYPE(wchar_t const*, wchar_string);
    FORMATXX_TYPE(std::nullptr_t, null_pointer);
    FORMATXX_TYPE(void*, void_pointer);
    FORMATXX_TYPE(void const*, void_pointer);
#undef FORMTAXX_TYPE

    template <typename CharT, typename T>
    constexpr result_code FORMATXX_API format_value_thunk(basic_format_writer<CharT>& out, void const* ptr, basic_format_options<CharT> options) {
        format_value(out, *static_cast<T const*>(ptr), options);
        return result_code::success;
    }

#if 0 // TODO
    template <typename CharT, typename T>
    constexpr basic_format_arg<CharT> make_format_arg(T const& value) noexcept {
        constexpr format_arg_type type = type_of<T>::value;

        if constexpr (type != format_arg_type::unknown) {
            return { type, &value };
        }
        else if constexpr (has_format_value<CharT, T>::value) {
            return basic_format_arg<CharT>(&format_value_thunk<CharT, T>, &value);
        }
        else if constexpr (std::is_pointer<T>::value) {
            return { format_arg_type::void_pointer, &value };
        }
        else if constexpr (std::is_enum<T>::value) {
            return { type_of<std::underlying_type_t<T>>::value, &value };
        }
        else {
            return {};
        }
    }
#endif
}}

#endif // !defined(_guard_FORMATXX_DETAIL_FORMAT_ARG_H)
