//
// Simple span implementation for C++14 compatibility
//
// Based on C++20 std::span and span-lite, but simplified for TinyUSDZ needs
//

#ifndef NONSTD_SPAN_HPP
#define NONSTD_SPAN_HPP

#include <cstddef>
#include <iterator>
#include <type_traits>

#ifndef NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
# if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#  define NONSTD_SPAN_CONFIG_NO_EXCEPTIONS  0
# else
#  define NONSTD_SPAN_CONFIG_NO_EXCEPTIONS  1
# endif
#endif

#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
# include <stdexcept>
#endif

namespace nonstd {

constexpr const std::ptrdiff_t dynamic_extent = -1;

template<typename T, std::ptrdiff_t Extent = dynamic_extent>
class span {
public:
    // Member types
    using element_type = T;
    using value_type = typename std::remove_cv<T>::type;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    
    static constexpr std::ptrdiff_t extent = Extent;

    // Constructors
    constexpr span() noexcept 
        : data_(nullptr), size_(0) {
        static_assert(Extent <= 0, "Cannot default construct a fixed-size span with positive extent");
    }

    constexpr span(pointer ptr, size_type count) 
        : data_(ptr), size_(count) {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (Extent != dynamic_extent && count != static_cast<size_type>(Extent)) {
            throw std::logic_error("span size mismatch");
        }
#endif
    }

    constexpr span(pointer first, pointer last)
        : data_(first), size_(static_cast<size_type>(last - first)) {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (Extent != dynamic_extent && size_ != static_cast<size_type>(Extent)) {
            throw std::logic_error("span size mismatch");
        }
#endif
    }

    template<std::size_t N>
    constexpr span(element_type (&arr)[N]) noexcept
        : data_(arr), size_(N) {
        static_assert(Extent == dynamic_extent || N == static_cast<std::size_t>(Extent), 
                     "Array size must match span extent");
    }

    template<typename Container,
             typename = typename std::enable_if<
                 !std::is_array<Container>::value &&
                 !std::is_same<Container, span>::value &&
                 std::is_convertible<decltype(std::declval<Container>().data()), pointer>::value
             >::type>
    constexpr span(Container& cont)
        : data_(cont.data()), size_(cont.size()) {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (Extent != dynamic_extent && cont.size() != static_cast<size_type>(Extent)) {
            throw std::logic_error("container size mismatch");
        }
#endif
    }

    template<typename Container,
             typename = typename std::enable_if<
                 !std::is_array<Container>::value &&
                 !std::is_same<Container, span>::value &&
                 std::is_convertible<decltype(std::declval<const Container>().data()), pointer>::value
             >::type>
    constexpr span(const Container& cont)
        : data_(cont.data()), size_(cont.size()) {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (Extent != dynamic_extent && cont.size() != static_cast<size_type>(Extent)) {
            throw std::logic_error("container size mismatch");
        }
#endif
    }

    // Copy constructor
    constexpr span(const span& other) noexcept = default;

    // Assignment
    constexpr span& operator=(const span& other) noexcept = default;

    // Element access
    constexpr reference operator[](size_type idx) const {
        return data_[idx];
    }

    constexpr reference front() const {
        return data_[0];
    }

    constexpr reference back() const {
        return data_[size_ - 1];
    }

    constexpr pointer data() const noexcept {
        return data_;
    }

    // Iterators
    constexpr iterator begin() const noexcept {
        return data_;
    }

    constexpr iterator end() const noexcept {
        return data_ + size_;
    }

    constexpr const_iterator cbegin() const noexcept {
        return data_;
    }

    constexpr const_iterator cend() const noexcept {
        return data_ + size_;
    }

    constexpr reverse_iterator rbegin() const noexcept {
        return reverse_iterator(end());
    }

    constexpr reverse_iterator rend() const noexcept {
        return reverse_iterator(begin());
    }

    constexpr const_reverse_iterator crbegin() const noexcept {
        return const_reverse_iterator(cend());
    }

    constexpr const_reverse_iterator crend() const noexcept {
        return const_reverse_iterator(cbegin());
    }

    // Observers
    constexpr size_type size() const noexcept {
        return size_;
    }

    constexpr size_type size_bytes() const noexcept {
        return size_ * sizeof(element_type);
    }

    constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    // Subviews
    constexpr span<element_type, dynamic_extent> first(size_type count) const {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (count > size_) {
            throw std::out_of_range("span::first: count out of range");
        }
#endif
        return span<element_type, dynamic_extent>(data_, count);
    }

    constexpr span<element_type, dynamic_extent> last(size_type count) const {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (count > size_) {
            throw std::out_of_range("span::last: count out of range");
        }
#endif
        return span<element_type, dynamic_extent>(data_ + (size_ - count), count);
    }

    constexpr span<element_type, dynamic_extent> subspan(size_type offset, 
                                                         size_type count = static_cast<size_type>(dynamic_extent)) const {
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (offset > size_) {
            throw std::out_of_range("span::subspan: offset out of range");
        }
#endif
        size_type actual_count = (count == static_cast<size_type>(dynamic_extent)) ? (size_ - offset) : count;
#if ! NONSTD_SPAN_CONFIG_NO_EXCEPTIONS
        if (offset + actual_count > size_) {
            throw std::out_of_range("span::subspan: count exceeds span bounds");
        }
#endif
        return span<element_type, dynamic_extent>(data_ + offset, actual_count);
    }

private:
    pointer data_;
    size_type size_;
};

// Note: Deduction guides are C++17 feature, not available in C++14

// Helper functions
template<typename T>
constexpr span<T> make_span(T* ptr, typename span<T>::size_type count) {
    return span<T>(ptr, count);
}

template<typename T>
constexpr span<T> make_span(T* first, T* last) {
    return span<T>(first, last);
}

template<typename T, std::size_t N>
constexpr span<T, static_cast<std::ptrdiff_t>(N)> make_span(T (&arr)[N]) {
    return span<T, static_cast<std::ptrdiff_t>(N)>(arr);
}

template<typename Container>
constexpr span<typename Container::value_type> make_span(Container& cont) {
    return span<typename Container::value_type>(cont);
}

template<typename Container>
constexpr span<const typename Container::value_type> make_span(const Container& cont) {
    return span<const typename Container::value_type>(cont);
}

} // namespace nonstd

#endif // NONSTD_SPAN_HPP
