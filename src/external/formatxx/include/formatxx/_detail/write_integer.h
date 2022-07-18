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

#if !defined(_guard_FORMATXX_DETAIL_WRITE_INTEGER_H)
#define _guard_FORMATXX_DETAIL_WRITE_INTEGER_H
#pragma once

#include "format_util.h"
#include <type_traits>
#include <limits>
#include <climits>

namespace formatxx {
namespace _detail {

	template <typename CharT, typename T>
    constexpr void write_integer(basic_format_writer<CharT>& out, T value, basic_format_options<CharT> const& options);

	struct prefix_helper {
		// type prefix (2), sign (1)
		static constexpr std::size_t buffer_size() { return 3; }

		template <typename CharT>
		static constexpr basic_string_view<CharT> write(CharT* buffer, basic_format_options<CharT> const& options, bool negative, bool add_sign) {
			CharT* const end = buffer + buffer_size();
			CharT* ptr = end;

			// add numeric type prefix (2)
			if (options.alternate_form) {
				*--ptr = options.specifier;
				*--ptr = FormatTraits<CharT>::cZero;
			}

			// add sign (1)
            if (add_sign) {
                if (negative) {
                    *--ptr = FormatTraits<CharT>::cMinus;
                }
                else if (options.sign == format_sign::always) {
                    *--ptr = FormatTraits<CharT>::cPlus;
                }
                else if (options.sign == format_sign::space) {
                    *--ptr = FormatTraits<CharT>::cSpace;
                }
            }

			return { ptr, end };
		}
	};

	struct decimal_helper {
		// buffer must be one larger than digits10, as that trait is the maximum number of
		// base-10 digits represented by the type in their entirety, e.g. 8-bits can store
		// 99 but not 999, so its digits10 is 2, even though the value 255 could be stored
		// and has 3 digits.
		template <typename UnsignedT>
		static constexpr std::size_t buffer_size = std::numeric_limits<UnsignedT>::digits10 + 1;

        static constexpr bool use_signs = true;

		template <typename CharT, typename UnsignedT>
		static constexpr basic_string_view<CharT> write(CharT* buffer, UnsignedT value) {
			// we'll work on every two decimal digits (groups of 100). notes taken from cppformat,
			// which took the notes from Alexandrescu from "Three Optimization Tips for C++"
			CharT const* const table = FormatTraits<CharT>::sDecimalPairs;

			CharT* const end = buffer + buffer_size<UnsignedT>;
			CharT* ptr = end;

			// work on every two decimal digits (groups of 100). notes taken from cppformat,
			// which took the notes from Alexandrescu from "Three Optimization Tips for C++"
			while (value >= 100) {
				// I feel like we could do the % and / better... somehow
				// we multiply the index by two to find the pair of digits to index
				unsigned const digit = (value % 100) << 1;
				value /= 100;

				// write out both digits of the given index
				*--ptr = table[digit + 1];
				*--ptr = table[digit];
			}

			if (value >= 10) {
				// we have two digits left; this is identical to the above loop, but without the division
				unsigned const digit = static_cast<unsigned>(value << 1);
				*--ptr = table[digit + 1];
				*--ptr = table[digit];
			}
			else {
				// we have but a single digit left, so this is easy
				*--ptr = static_cast<char>(FormatTraits<CharT>::cZero + value);
			}

			return { ptr, end };
		}
	};

	template <bool LowerCase>
	struct hexadecimal_helper {
		// 2 hex digits per octet
		template <typename UnsignedT>
		static constexpr std::size_t buffer_size = 2 * sizeof(UnsignedT);

        static constexpr bool use_signs = false;

		template <typename CharT, typename UnsignedT>
		static constexpr basic_string_view<CharT> write(CharT* buffer, UnsignedT value) {
			CharT* const end = buffer + buffer_size<UnsignedT>;
			CharT* ptr = end;

			CharT const* const alphabet = LowerCase ?
				FormatTraits<CharT>::sHexadecimalLower :
				FormatTraits<CharT>::sHexadecimalUpper;

			do {
				*--ptr = alphabet[value & 0xF];
			} while ((value >>= 4) != 0);

			return { ptr, end };
		}
	};

	struct octal_helper {
		// up to three 3 octal digits per octet - FIXME is that right? I don't think that's right
		template <typename UnsignedT>
		static constexpr std::size_t buffer_size = 3 * sizeof(UnsignedT);

        static constexpr bool use_signs = true;

		template <typename CharT, typename UnsignedT>
		static constexpr basic_string_view<CharT> write(CharT* buffer, UnsignedT value) {
			CharT* const end = buffer + buffer_size<UnsignedT>;
			CharT* ptr = end;

			// the octal alphabet is a subset of hexadecimal,
			// and doesn't depend on casing.
			CharT const* const alphabet = FormatTraits<CharT>::sHexadecimalLower;

			do {
				*--ptr = alphabet[value & 0x7];
			} while ((value >>= 3) != 0);

			return { ptr, end };
		}
	};

	struct binary_helper {
		// one digit per bit of the input
		template <typename UnsignedT>
		static constexpr std::size_t buffer_size = std::numeric_limits<UnsignedT>::digits;

        static constexpr bool use_signs = true;

		template <typename CharT, typename UnsignedT>
		static constexpr basic_string_view<CharT> write(CharT* buffer, UnsignedT value) {
			CharT* const end = buffer + buffer_size<UnsignedT>;
			CharT* ptr = end;

			do {
				*--ptr = static_cast<CharT>(FormatTraits<CharT>::cZero + (value & 1));
			} while ((value >>= 1) != 0);

			return { ptr, end };
		}
	};

	template <typename HelperT, typename CharT, typename ValueT>
	constexpr void write_integer_helper(basic_format_writer<CharT> & out, ValueT raw_value, basic_format_options<CharT> const& options) {
		using unsigned_type = std::make_unsigned_t<ValueT>;

		// convert to an unsigned value to make the formatting easier; note that must
		// subtract from 0 _after_ converting to deal with 2's complement format
		// where (abs(min) > abs(max)), otherwise we'd not be able to format -min<T>
		unsigned_type const unsigned_value = raw_value >= 0 ? raw_value : 0 - static_cast<unsigned_type>(raw_value);

		// calculate prefixes like signs
		CharT prefix_buffer[prefix_helper::buffer_size()];
		auto const prefix = prefix_helper::write(prefix_buffer, options, raw_value < 0, HelperT::use_signs);

		// generate the actual number
		CharT value_buffer[HelperT::template buffer_size<unsigned_type>];
		auto const result = HelperT::write(value_buffer, unsigned_value);

		if (options.precision != ~0u) {
			out.write(prefix);
			write_padded_align_right(out, result, FormatTraits<CharT>::cZero, options.precision);
		}
		else {
			std::size_t const output_length = prefix.size() + result.size();
			std::size_t const padding = options.width > output_length ? options.width - output_length : 0;

			if (options.justify == format_justify::left) {
				out.write(prefix);
				out.write(result);
				write_padding(out, FormatTraits<CharT>::cSpace, padding);
			}
			else if (options.leading_zeroes) {
				out.write(prefix);
				write_padding(out, FormatTraits<CharT>::cZero, padding);
				out.write(result);
			}
			else {
				write_padding(out, FormatTraits<CharT>::cSpace, padding);
				out.write(prefix);
				out.write(result);
			}
		}
	}

	template <typename CharT, typename T>
	constexpr void write_integer(basic_format_writer<CharT> & out, T raw, basic_format_options<CharT> const& options) {
		switch (options.specifier) {
		default:
		case 0:
		case 'i':
		case 'd':
		case 'D':
			return write_integer_helper<decimal_helper>(out, raw, options);
		case 'x':
			return write_integer_helper<hexadecimal_helper</*lower=*/true>>(out, std::make_unsigned_t<T>(raw), options);
		case 'X':
			return write_integer_helper<hexadecimal_helper</*lower=*/false>>(out, std::make_unsigned_t<T>(raw), options);
		case 'o':
			return write_integer_helper<octal_helper>(out, raw, options);
			break;
		case 'b':
			return write_integer_helper<binary_helper>(out, raw, options);
			break;
		}
	}

}} // namespace formatxx::_detail

#endif // _guard_FORMATXX_DETAIL_WRITE_INTEGER_H
