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

#if !defined(_guard_FORMATXX_SMALL_STRING_H)
#define _guard_FORMATXX_SMALL_STRING_H
#pragma once

#include <litexx/string_view.h>
#include <cstring>

namespace formatxx::_detail {
    template <typename T>
    struct new_delete_allocator {
        static_assert(std::is_trivial_v<T>);

        T* allocate(std::size_t count) { return new T[count]; }
        void deallocate(T* ptr, std::size_t) { delete[] ptr; }
    };

} // namespace formatxx::_detail

namespace formatxx {
    template <typename CharT, std::size_t FixedCapacity, typename AllocatorT = _detail::new_delete_allocator<CharT>> class small_string;
} // namespace formatxx

/// A writer with a fixed buffer that will allocate when the buffer is exhausted.
template <typename CharT, std::size_t FixedCapacity, typename AllocatorT>
class formatxx::small_string : private AllocatorT {
public:
    using value_type = CharT;
    using size_type = std::size_t;
    using pointer = value_type *;
    using iterator = pointer;
    using const_iterator = value_type const*;

    small_string() noexcept = default;
    ~small_string();

    void append(value_type const* data, size_type length);

    bool empty() const noexcept { return _size == 0; }
    operator bool() const noexcept { return _size != 0; }

    pointer data() const noexcept { return _data == nullptr ? _buffer : _data; }
    size_type size() const noexcept { return _size; }
    size_type capacity() const noexcept { return _capacity == 0 ? FixedCapacity : _capacity; }

    void clear() noexcept { _size = 0; data()[0] = CharT(0); }
    pointer c_str() const noexcept { return data(); }

    iterator begin() noexcept { return data(); }
    iterator end() noexcept { return data() + _size; }

    const_iterator begin() const noexcept { return data(); }
    const_iterator end() const noexcept { return data() + _size; }

    operator litexx::basic_string_view<value_type>() const noexcept { return { data(), _size }; }

private:
    pointer _grow(std::size_t new_size);

    size_type _size = 0;
    size_type _capacity = 0;
    CharT* _data = nullptr;
    mutable CharT _buffer[FixedCapacity + 1] = { CharT(0), };
};

template <typename CharT, std::size_t FixedCapacity, typename AllocatorT>
formatxx::small_string<CharT, FixedCapacity, AllocatorT>::~small_string() {
    if (_data != nullptr) {
        this->deallocate(_data, _capacity + 1/*NUL*/);
    }
}

template <typename CharT, std::size_t FixedCapacity, typename AllocatorT>
auto formatxx::small_string<CharT, FixedCapacity, AllocatorT>::_grow(std::size_t new_size) -> pointer {
    auto const old_capacity = capacity();
    if (new_size <= old_capacity) {
        return data();
    }

    size_type new_capacity = old_capacity + (old_capacity >> 1); // += 50%
    if (new_capacity < new_size) {
        new_capacity = new_size;
    }

    value_type* tmp = this->allocate(new_capacity + 1/*NUL*/);
    std::memcpy(tmp, data(), sizeof(CharT) * _size + 1/*NUL*/);

    if (_data != nullptr) {
        this->deallocate(_data, old_capacity + 1/*NUL*/);
    }

    _data = tmp;
    _capacity = new_capacity;
    return _data;
}

template <typename CharT, std::size_t SizeN, typename AllocatorT>
void formatxx::small_string<CharT, SizeN, AllocatorT>::append(value_type const* data, size_type length) {
    value_type* mem = _grow(_size + length);
    std::memcpy(mem + _size, data, sizeof(CharT) * length);
    _size += length;
    mem[_size] = CharT(0);
}

#endif // !defined(_guard_FORMATXX_SMALL_STRING_H)
