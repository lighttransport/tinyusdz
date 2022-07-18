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

#if !defined(_guard_FORMATXX_DETAIL_APPEND_WRITER_H)
#define _guard_FORMATXX_DETAIL_APPEND_WRITER_H
#pragma once

#include <litexx/string_view.h>

namespace formatxx {
    template <typename CharT> class basic_format_writer;

    template <typename ContainerT> class append_writer;
}

/// Writer that calls append(data, size) on wrapped value.
template <typename ContainerT>
class formatxx::append_writer : public formatxx::basic_format_writer<typename ContainerT::value_type>{
public:
    constexpr append_writer(ContainerT& container) : _container(container) {}

    constexpr void write(litexx::basic_string_view<typename ContainerT::value_type> str) override {
        _container.append(str.data(), str.size());
    }

private:
    ContainerT & _container;
};

#endif // !defined(_guard_FORMATXX_DETAIL_APPEND_WRITER_H)
