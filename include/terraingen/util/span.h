// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Span`, a non-owning view of contiguous elements that keeps the library at C++17.
///
/// It mirrors the dynamic-extent subset of `std::span` (lower-case member names included), so
/// callers written against `std::span` read the same. A C++20 caller can pass a `std::span`, a
/// `std::vector`, a `std::array` or a C array wherever a `Span` is expected.
#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace terraingen {

namespace detail {

/// Whether `C` is a contiguous container whose `data()` points to elements convertible to `T`.
template <typename T, typename C, typename = void> struct IsSpanContainer : std::false_type {};

template <typename T, typename C>
struct IsSpanContainer<
    T, C,
    std::void_t<decltype(std::data(std::declval<C&>())), decltype(std::size(std::declval<C&>()))>>
    : std::is_convertible<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))> (*)[],
                          T (*)[]> {};

}  // namespace detail

/// A view of `size()` contiguous `T`s owned elsewhere; `T` may be const.
template <typename T> class Span {
  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using pointer = T*;
    using reference = T&;
    using iterator = T*;

    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : data_(data), size_(size) {}
    constexpr Span(T* first, T* last) noexcept
        : data_(first), size_(static_cast<std::size_t>(last - first)) {}

    template <std::size_t N> constexpr Span(T (&array)[N]) noexcept : data_(array), size_(N) {}

    /// Views a contiguous container: a vector, an array, a string or another span.
    template <typename C, typename = std::enable_if_t<!std::is_same_v<std::decay_t<C>, Span> &&
                                                      detail::IsSpanContainer<T, C>::value>>
    constexpr Span(C&& container) noexcept
        : data_(std::data(container)), size_(std::size(container)) {}

    constexpr T* data() const noexcept {
        return data_;
    }
    constexpr std::size_t size() const noexcept {
        return size_;
    }
    constexpr std::size_t size_bytes() const noexcept {
        return size_ * sizeof(T);
    }
    constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    constexpr T* begin() const noexcept {
        return data_;
    }
    constexpr T* end() const noexcept {
        return data_ + size_;
    }

    constexpr T& operator[](std::size_t index) const {
        assert(index < size_);
        return data_[index];
    }
    constexpr T& front() const {
        return (*this)[0];
    }
    constexpr T& back() const {
        return (*this)[size_ - 1];
    }

    /// The first `count` elements.
    constexpr Span first(std::size_t count) const {
        assert(count <= size_);
        return {data_, count};
    }
    /// The last `count` elements.
    constexpr Span last(std::size_t count) const {
        assert(count <= size_);
        return {data_ + (size_ - count), count};
    }
    /// The `count` elements from `offset`, or all of them to the end when `count` is omitted.
    constexpr Span subspan(std::size_t offset,
                           std::size_t count = static_cast<std::size_t>(-1)) const {
        assert(offset <= size_);
        const std::size_t rest = size_ - offset;
        assert(count == static_cast<std::size_t>(-1) || count <= rest);
        return {data_ + offset, count == static_cast<std::size_t>(-1) ? rest : count};
    }

  private:
    T* data_ = nullptr;
    std::size_t size_ = 0;
};

template <typename T, std::size_t N> Span(T (&)[N]) -> Span<T>;
template <typename C>
Span(C&) -> Span<std::remove_pointer_t<decltype(std::data(std::declval<C&>()))>>;
template <typename C>
Span(const C&) -> Span<std::remove_pointer_t<decltype(std::data(std::declval<const C&>()))>>;
template <typename T> Span(T*, std::size_t) -> Span<T>;

/// The bytes of `span`, read-only.
template <typename T> Span<const std::byte> AsBytes(Span<T> span) noexcept {
    return {reinterpret_cast<const std::byte*>(span.data()), span.size_bytes()};
}

/// The bytes of `span`, writable.
template <typename T, typename = std::enable_if_t<!std::is_const_v<T>>>
Span<std::byte> AsWritableBytes(Span<T> span) noexcept {
    return {reinterpret_cast<std::byte*>(span.data()), span.size_bytes()};
}

}  // namespace terraingen
