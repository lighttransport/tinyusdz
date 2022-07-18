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

#if !defined(_guard_FORMATXX_DETAIL_FORMAT_ARG_IMPL_H)
#define _guard_FORMATXX_DETAIL_FORMAT_ARG_IMPL_H
#pragma once

#include "format_traits.h"
#include "write_integer.h"
#include "write_string.h"
#include "write_float.h"
#include "write_wide.h"

#include <cinttypes>

template <typename CharT>
formatxx::result_code FORMATXX_API formatxx::_detail::basic_format_arg<CharT>::format_into(basic_format_writer<CharT>& output, basic_format_options<CharT> const& options) const {
    switch (_type) {
    case _detail::format_arg_type::unknown:
        return result_code::success;
    case _detail::format_arg_type::char_t:
        _detail::write_char(output, *static_cast<char const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::wchar:
        _detail::write_char(output, *static_cast<wchar_t const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::signed_char:
        _detail::write_integer(output, *static_cast<signed char const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::unsigned_char:
        _detail::write_integer(output, *static_cast<unsigned char const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::signed_int:
        _detail::write_integer(output, *static_cast<signed int const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::unsigned_int:
        _detail::write_integer(output, *static_cast<unsigned int const*>(_value), options);
        return result_code::success;
    case _detail::format_arg_type::signed_short_int:
		_detail::write_integer(output, *static_cast<signed short const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::unsigned_short_int:
		_detail::write_integer(output, *static_cast<unsigned short const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::signed_long_int:
		_detail::write_integer(output, *static_cast<signed long const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::unsigned_long_int:
		_detail::write_integer(output, *static_cast<unsigned long const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::signed_long_long_int:
		_detail::write_integer(output, *static_cast<signed long long const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::unsigned_long_long_int:
		_detail::write_integer(output, *static_cast<unsigned long long const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::single_float:
		_detail::write_float(output, *static_cast<float const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::double_float:
		_detail::write_float(output, *static_cast<double const*>(_value), options);
		return result_code::success;
    case _detail::format_arg_type::boolean:
		_detail::write_string(output, *static_cast<bool const*>(_value) ? _detail::FormatTraits<CharT>::sTrue : _detail::FormatTraits<CharT>::sFalse, options);
		return result_code::success;
    case _detail::format_arg_type::char_string:
		_detail::write_string(output, string_view(*static_cast<char const* const*>(_value)), options);
		return result_code::success;
    case _detail::format_arg_type::wchar_string:
		_detail::write_string(output, wstring_view(*static_cast<wchar_t const* const*>(_value)), options);
		return result_code::success;
    case _detail::format_arg_type::null_pointer:
		_detail::write_string(output, _detail::FormatTraits<CharT>::sNullptr, options);
		return result_code::success;
    case _detail::format_arg_type::void_pointer:
		_detail::write_integer(output, reinterpret_cast<std::uintptr_t>(*static_cast<void const* const*>(_value)), options);
		return result_code::success;
    case _detail::format_arg_type::custom:
        return _thunk(output, _value, options);
    //default:
    //    return result_code::success;
    }
}

#endif // !defined(_guard_FORMATXX_DETAIL_FORMAT_ARG_IMPL_H)
