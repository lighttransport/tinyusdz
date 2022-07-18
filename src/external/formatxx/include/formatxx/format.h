// formatxx - C++ string formatting library.
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

#if !defined(_guard_FORMATXX_H)
#define _guard_FORMATXX_H
#pragma once

#include <type_traits>
#include "litexx/string_view.h"

#if !defined(FORMATXX_API)
#	if defined(_WIN32)
#		define FORMATXX_API __stdcall
#	else
#		define FORMATXX_API
#	endif
#endif

#if defined(_WIN32) && !defined(FORMATXX_PUBLIC)
#	if defined(FORMATXX_EXPORT)
#		define FORMATXX_PUBLIC __declspec(dllexport)
#	else
#		define FORMATXX_PUBLIC
#	endif
#elif __GNUC__ >= 4 && !defined(FORMATXX_PUBLIC)
#	if defined(FORMATXX_EXPORT)
#		define FORMATXX_PUBLIC __attribute__((visibility("default")))
#	else
#		define FORMATXX_PUBLIC
#	endif
#endif

namespace formatxx {
    template <typename CharT> using basic_string_view = litexx::basic_string_view<CharT>;
    template <typename CharT> class basic_format_writer;
    template <typename CharT> class basic_format_options;
    template <typename CharT> class basic_parse_spec_result;

    enum class result_code : unsigned int;
    enum class format_justify : unsigned char;
    enum class format_sign : unsigned char;

    using string_view = basic_string_view<char>;
    using format_writer = basic_format_writer<char>;
    using format_options = basic_format_options<char>;

    using wstring_view = basic_string_view<wchar_t>;
    using wformat_writer = basic_format_writer<wchar_t>;
    using wformat_options = basic_format_options<wchar_t>;

    template <typename CharT, typename FormatT, typename... Args> constexpr result_code format_to(basic_format_writer<CharT>& writer, FormatT const& format, Args const& ... args);
    template <typename CharT, typename FormatT, typename... Args> constexpr result_code printf_to(basic_format_writer<CharT>& writer, FormatT const& format, Args const& ... args);

    template <typename ResultT, typename FormatT, typename... Args> constexpr ResultT format_as(FormatT const& format, Args const& ... args);
    template <typename ResultT, typename FormatT, typename... Args> constexpr ResultT printf_as(FormatT const& format, Args const& ... args);

    template <typename CharT, typename T>
    constexpr result_code format_value_to(basic_format_writer<CharT>& writer, T const& value, basic_format_options<CharT> const& options = {});

    template <typename CharT> constexpr FORMATXX_PUBLIC basic_parse_spec_result<CharT> FORMATXX_API parse_format_spec(basic_string_view<CharT> spec_string) noexcept;
    template <typename CharT> constexpr FORMATXX_PUBLIC basic_parse_spec_result<CharT> FORMATXX_API parse_printf_spec(basic_string_view<CharT> spec_string) noexcept;
}

enum class formatxx::result_code : unsigned int {
    success,
    out_of_range,
    malformed_input,
    out_of_space,
};

enum class formatxx::format_justify : unsigned char {
    right,
    left,
    center
};

enum class formatxx::format_sign : unsigned char {
    negative,
    always,
    space
};

#include "formatxx/_detail/append_writer.h"
#include "formatxx/_detail/format_arg.h"

/// Interface for any buffer that the format library can write into.
template <typename CharT>
class formatxx::basic_format_writer {
public:
    virtual ~basic_format_writer() = default;

    /// Write a string slice.
    /// @param str The string to write.
    virtual void write(basic_string_view<CharT> str) = 0;
};

/// Result from parse_format_spec.
template <typename CharT>
class formatxx::basic_parse_spec_result {
public:
    result_code code = result_code::success;
    basic_format_options<CharT> options;
    basic_string_view<CharT> unparsed;
};

/// Extra formatting specifications.
template <typename CharT>
class formatxx::basic_format_options {
public:
    constexpr basic_format_options() noexcept : alternate_form(false), leading_zeroes(false) {}

    basic_string_view<CharT> user;
    unsigned width = 0;
    unsigned precision = ~0u;
    CharT specifier = 0;
    format_justify justify = format_justify::right;
    format_sign sign = format_sign::negative;
    bool alternate_form : 1;
    bool leading_zeroes : 1;
};

namespace formatxx {
    /// Default format helpers.
    FORMATXX_PUBLIC void FORMATXX_API format_value(format_writer& out, string_view str, format_options const& options = {}) noexcept;
    FORMATXX_PUBLIC void FORMATXX_API format_value(format_writer& out, wstring_view str, format_options const& options = {}) noexcept;
    FORMATXX_PUBLIC void FORMATXX_API format_value(wformat_writer& out, string_view str, wformat_options const& options = {}) noexcept;
    FORMATXX_PUBLIC void FORMATXX_API format_value(wformat_writer& out, wstring_view str, wformat_options const& options = {}) noexcept;
}

/// @internal
namespace formatxx {
namespace _detail {
    template <typename CharT>
    FORMATXX_PUBLIC result_code FORMATXX_API format_impl(basic_format_writer<CharT>& out, basic_string_view<CharT> format, basic_format_arg_list<CharT> args);
    template <typename CharT>
    FORMATXX_PUBLIC result_code FORMATXX_API printf_impl(basic_format_writer<CharT>& out, basic_string_view<CharT> format, basic_format_arg_list<CharT> args);
}}

extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::format_impl(basic_format_writer<char>& out, basic_string_view<char> format, basic_format_arg_list<char> args);
extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::printf_impl(basic_format_writer<char>& out, basic_string_view<char> format, basic_format_arg_list<char> args);
extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::basic_format_arg<char>::format_into(basic_format_writer<char>& output, basic_format_options<char> const& options) const;
extern template FORMATXX_PUBLIC formatxx::basic_parse_spec_result<char> FORMATXX_API formatxx::parse_format_spec(basic_string_view<char> spec_string) noexcept;
extern template FORMATXX_PUBLIC formatxx::basic_parse_spec_result<char> FORMATXX_API formatxx::parse_printf_spec(basic_string_view<char> spec_string) noexcept;

extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::format_impl(basic_format_writer<wchar_t>& out, basic_string_view<wchar_t> format, basic_format_arg_list<wchar_t> args);
extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::printf_impl(basic_format_writer<wchar_t>& out, basic_string_view<wchar_t> format, basic_format_arg_list<wchar_t> args);
extern template FORMATXX_PUBLIC formatxx::result_code FORMATXX_API formatxx::_detail::basic_format_arg<wchar_t>::format_into(basic_format_writer<wchar_t>& output, basic_format_options<wchar_t> const& options) const;
extern template FORMATXX_PUBLIC formatxx::basic_parse_spec_result<wchar_t> FORMATXX_API formatxx::parse_format_spec(basic_string_view<wchar_t> spec_string) noexcept;
extern template FORMATXX_PUBLIC formatxx::basic_parse_spec_result<wchar_t> FORMATXX_API formatxx::parse_printf_spec(basic_string_view<wchar_t> spec_string) noexcept;

/// Write the string format using the given parameters into a buffer.
/// @param writer The write buffer that will receive the formatted text.
/// @param format The primary text and formatting controls to be written.
/// @param args The arguments used by the formatting string.
/// @returns a result code indicating any errors.
template <typename CharT, typename FormatT, typename... Args>
constexpr formatxx::result_code formatxx::format_to(basic_format_writer<CharT>& writer, FormatT const& format, Args const& ... args) {
    return _detail::format_impl(writer, basic_string_view<CharT>(format), { _detail::make_format_arg<CharT, _detail::formattable_t<Args>>(args)... });
}

/// Write the printf format using the given parameters into a buffer.
/// @param writer The write buffer that will receive the formatted text.
/// @param format The primary text and printf controls to be written.
/// @param args The arguments used by the formatting string.
/// @returns a result code indicating any errors.
template <typename CharT, typename FormatT, typename... Args>
constexpr formatxx::result_code formatxx::printf_to(basic_format_writer<CharT>& writer, FormatT const& format, Args const& ... args) {
    return _detail::printf_impl(writer, basic_string_view<CharT>(format), { _detail::make_format_arg<CharT, _detail::formattable_t<Args>>(args)... });
}

/// Write the string format using the given parameters and return a string with the result.
/// @param format The primary text and formatting controls to be written.
/// @param args The arguments used by the formatting string.
/// @returns a formatted string.
template <typename ResultT, typename FormatT, typename... Args>
constexpr ResultT formatxx::format_as(FormatT const& format, Args const& ... args) {
    using char_type = typename ResultT::value_type;
    ResultT result;
    append_writer<ResultT> writer(result);
    _detail::format_impl(writer, basic_string_view<char_type>(format), { _detail::make_format_arg<char_type, _detail::formattable_t<Args>>(args)... });
    return result;
}

/// Write the printf format using the given parameters and return a string with the result.
/// @param format The primary text and printf controls to be written.
/// @param args The arguments used by the formatting string.
/// @returns a formatted string.
template <typename ResultT, typename FormatT, typename... Args>
constexpr ResultT formatxx::printf_as(FormatT const& format, Args const& ... args) {
    using char_type = typename ResultT::value_type;
    ResultT result;
    append_writer<ResultT> writer(result);
    _detail::printf_impl(writer, basic_string_view<char_type>(format), { _detail::make_format_arg<char_type, _detail::formattable_t<Args>>(args)... });
    return result;
}

/// Format a value into a buffer using the given options.
/// @param writer The write buffer that will receive the formatted text.
/// @param value The value to format.
/// @param options The format control options.
/// @returns a result code indicating any errors.
template <typename CharT, typename T>
constexpr formatxx::result_code formatxx::format_value_to(basic_format_writer<CharT>& writer, T const& value, basic_format_options<CharT> const& options) {
    return _detail::make_format_arg<CharT>(value).format_into(writer, options);
}

#endif // !defined(_guard_FORMATXX_H)
