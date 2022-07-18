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

#if !defined(_guard_FORMATXX_DETAIL_FORMAT_UTIL_H)
#define _guard_FORMATXX_DETAIL_FORMAT_UTIL_H
#pragma once

namespace formatxx {
namespace _detail {

	template <typename CharT>
	constexpr void write_padding(basic_format_writer<CharT>& out, CharT pad_char, std::size_t count) {
		// FIXME: this is not even remotely the most efficient way to do this
		for (std::size_t i = 0; i != count; ++i) {
			out.write({ &pad_char, 1 });
		}
	}

	template <typename CharT>
    constexpr void write_padded_align_right(basic_format_writer<CharT>& out, basic_string_view<CharT> string, CharT pad_char, std::size_t count) {
		if (count > string.size()) {
			write_padding(out, pad_char, count - string.size());
		}

		out.write(string);
	}

	template <typename CharT>
    constexpr void write_padded_align_left(basic_format_writer<CharT> & out, basic_string_view<CharT> string, CharT pad_char, std::size_t count) {
		out.write(string);

		if (count > string.size()) {
			write_padding(out, pad_char, count - string.size());
		}
	}

	template <typename CharT>
    constexpr void write_padded_aligned(basic_format_writer<CharT> & out, basic_string_view<CharT> string, CharT pad_char, std::size_t count, bool align_left) {
		if (!align_left) {
			write_padded_align_right(out, string, pad_char, count);
		}
		else {
			write_padded_align_left(out, string, pad_char, count);
		}
	}

	template <typename CharT>
	constexpr auto trim_string(basic_string_view<CharT> string, std::size_t max_size) noexcept -> basic_string_view<CharT> {
		return string.size() < max_size ? string : basic_string_view<CharT>(string.data(), max_size);
	}

	template <typename CharT>
	constexpr bool string_contains(basic_string_view<CharT> haystack, CharT needle) noexcept {
		for (CharT const c : haystack) {
			if (c == needle) {
				return true;
			}
		}
		return false;
	}

}} // namespace formatxx::_detail

#endif // _guard_FORMATXX_DETAIL_FORMAT_UTIL_H
