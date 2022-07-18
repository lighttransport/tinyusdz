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

#if !defined(_guard_FORMATXX_DETAIL_WRITE_WIDE_H)
#define _guard_FORMATXX_DETAIL_WRITE_WIDE_H
#pragma once

#include <cwchar>
#include <cstdlib>
#include <climits>

namespace formatxx {
namespace _detail {

#if defined(_MSC_VER)
#   pragma warning(push)
#   pragma warning(disable: 4996)
#endif

    inline void write_char(wformat_writer& out, char ch, wformat_options const&) noexcept {
        std::mbstate_t state{};
        wchar_t wc;
        std::size_t const rs = std::mbrtowc(&wc, &ch, 1, &state);
        if (rs > 0 && rs < static_cast<std::size_t>(-2)) {
            out.write({ &wc, 1 });
        }
    }

    inline void write_string(wformat_writer& out, string_view str, wformat_options const&) noexcept {
        std::mbstate_t state{};
        for (auto const ch : str) {
            wchar_t wc;
            std::size_t const rs = std::mbrtowc(&wc, &ch, 1, &state);
            if (rs < static_cast<std::size_t>(-2)) {
                out.write({ &wc, 1 });
            }
        }
    }

    inline void write_char(format_writer& out, wchar_t ch, format_options const&) noexcept {
        std::mbstate_t state{};
        char mb[MB_LEN_MAX];
        std::size_t const rs = std::wcrtomb(mb, ch, &state);
        if (rs != static_cast<std::size_t>(-1)) {
            out.write({ mb, rs });
        }
    }

    inline void write_string(format_writer& out, wstring_view str, format_options const&) noexcept {
        std::mbstate_t state{};
        char mb[MB_LEN_MAX];
        for (auto const ch : str) {
            std::size_t const rs = std::wcrtomb(mb, ch, &state);
            if (rs != static_cast<std::size_t>(-1)) {
                out.write({ mb, rs });
            }
        }
    }

}} // namespace formatx::_detail

#if defined(_MSC_VER)
#   pragma warning(pop)
#endif

#endif // !defined(_guard_FORMATXX_DETAIL_WRITE_WIDE_H)
