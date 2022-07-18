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

#if !defined(_guard_FORMATXX_DETAIL_WRITE_FLOAT_H)
#define _guard_FORMATXX_DETAIL_WRITE_FLOAT_H
#pragma once

#include <cstdio>
#include <cwchar>

namespace formatxx {
namespace _detail {

	inline int float_helper(char* buf, int result, char const* fmt, int width, int precision, double value) noexcept {
		return std::snprintf(buf, result, fmt, width, precision, value);
	}

    inline int float_helper(wchar_t* buf, int result, wchar_t const* fmt, int width, int precision, double value) noexcept {
		return std::swprintf(buf, result, fmt, width, precision, value);
	}

	template <typename CharT>
    constexpr void write_float(basic_format_writer<CharT>& out, double value, basic_format_options<CharT> options) {
		constexpr std::size_t fmt_buf_size = 10;
		CharT fmt_buf[fmt_buf_size];
		CharT* fmt_ptr = fmt_buf + fmt_buf_size;

		// required NUL terminator for sprintf formats (1)
		*--fmt_ptr = 0;

		// every sprint call must have a valid code (1)
		switch (options.specifier) {
		case 'a':
		case 'A':
		case 'e':
		case 'E':
		case 'f':
		case 'F':
		case 'g':
		case 'G':
			*--fmt_ptr = options.specifier;
			break;
		default:
			*--fmt_ptr = 'f';
			break;
		}

		// we always pass in a width and precision, defaulting to 0 which has no effect
		// as width, and -1 which is a guaranteed "ignore" (3)
		*--fmt_ptr = '*';
		*--fmt_ptr = '.';
		*--fmt_ptr = '*';

		// these flags are mutually exclusive within themselves (1)
        switch (options.sign) {
        case format_sign::negative: break;
        case format_sign::always: *--fmt_ptr = FormatTraits<CharT>::cPlus; break;
        case format_sign::space: *--fmt_ptr = FormatTraits<CharT>::cSpace; break;
		}

		// these flags may all be set together (3)
		if (options.justify == format_justify::left) {
			*--fmt_ptr = '-';
		}
		if (options.leading_zeroes) {
			*--fmt_ptr = '0';
		}
		if (options.alternate_form) {
			*--fmt_ptr = FormatTraits<CharT>::cHash;
		}

		// every format must start with this (1)
		*--fmt_ptr = '%';

		constexpr std::size_t buf_size = 1078;
		CharT buf[buf_size];

		int const result = float_helper(buf, buf_size, fmt_ptr, options.width, options.precision, value);
		if (result > 0) {
			out.write({ buf, std::size_t(result) < buf_size ? std::size_t(result) : buf_size });
		}
	}

}} // namespace formatxx::_detail

#endif // _guard_FORMATXX_DETAIL_WRITE_FLOAT_H
