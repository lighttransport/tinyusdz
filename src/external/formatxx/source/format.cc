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

#include <formatxx/format.h>

#include <formatxx/_detail/format_arg_impl.h>
#include <formatxx/_detail/format_impl.h>
#include <formatxx/_detail/parse_format.h>
#include <formatxx/_detail/parse_printf.h>
#include <formatxx/_detail/printf_impl.h>
#include <formatxx/_detail/write_string.h>

namespace formatxx {
    FORMATXX_PUBLIC void FORMATXX_API format_value(format_writer& output, string_view value, format_options const& options) noexcept {
        _detail::write_string(output, value, options);
    }

    FORMATXX_PUBLIC void FORMATXX_API format_value(format_writer& output, wstring_view value, format_options const& options) noexcept {
        _detail::write_string(output, value, options);
    }

    FORMATXX_PUBLIC void FORMATXX_API format_value(wformat_writer& output, string_view value, wformat_options const& options) noexcept {
        _detail::write_string(output, value, options);
    }

    FORMATXX_PUBLIC void FORMATXX_API format_value(wformat_writer& output, wstring_view value, wformat_options const& options) noexcept {
        _detail::write_string(output, value, options);
    }

	template FORMATXX_PUBLIC result_code FORMATXX_API _detail::format_impl(basic_format_writer<char>& out, basic_string_view<char> format, basic_format_arg_list<char> args);
	template FORMATXX_PUBLIC result_code FORMATXX_API _detail::printf_impl(basic_format_writer<char>& out, basic_string_view<char> format, basic_format_arg_list<char> args);
    template FORMATXX_PUBLIC result_code FORMATXX_API _detail::basic_format_arg<char>::format_into(basic_format_writer<char>& output, basic_format_options<char> const& options) const;
	template FORMATXX_PUBLIC basic_parse_spec_result<char> FORMATXX_API parse_format_spec(basic_string_view<char> spec_string) noexcept;
    template FORMATXX_PUBLIC basic_parse_spec_result<char> FORMATXX_API parse_printf_spec(basic_string_view<char> spec_string) noexcept;

    template FORMATXX_PUBLIC result_code FORMATXX_API _detail::format_impl(basic_format_writer<wchar_t>& out, basic_string_view<wchar_t> format, basic_format_arg_list<wchar_t> args);
    template FORMATXX_PUBLIC result_code FORMATXX_API _detail::printf_impl(basic_format_writer<wchar_t>& out, basic_string_view<wchar_t> format, basic_format_arg_list<wchar_t> args);
    template FORMATXX_PUBLIC result_code FORMATXX_API _detail::basic_format_arg<wchar_t>::format_into(basic_format_writer<wchar_t>& output, basic_format_options<wchar_t> const& options) const;
    template FORMATXX_PUBLIC basic_parse_spec_result<wchar_t> FORMATXX_API parse_format_spec(basic_string_view<wchar_t> spec_string) noexcept;
    template FORMATXX_PUBLIC basic_parse_spec_result<wchar_t> FORMATXX_API parse_printf_spec(basic_string_view<wchar_t> spec_string) noexcept;
} // namespace formatxx
