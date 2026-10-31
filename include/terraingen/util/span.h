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

/// A view of `size()` contiguous `T`s owned elsewhere; `T` may be const. Its members mirror the
/// C++20 `std::span` members of the same names.
template <typename T> class Span {
  public:
    using element_type = T;
    using value_type = std::remove_cv_t<T>;
    using size_type = std::size_t;
    using pointer = T*;
    using reference = T&;
    using iterator = T*;

    constexpr Span() noexcept = default;
    constexpr Span(T* data, std::size_t size) noexcept : m_data(data), m_size(size) {}
    constexpr Span(T* first, T* last) noexcept
        : m_data(first), m_size(static_cast<std::size_t>(last - first)) {}

    template <std::size_t N> constexpr Span(T (&array)[N]) noexcept : m_data(array), m_size(N) {}

    /// Views a contiguous container: a vector, an array, a string or another span.
    template <typename C, typename = std::enable_if_t<!std::is_same_v<std::decay_t<C>, Span> &&
                                                      detail::IsSpanContainer<T, C>::value>>
    constexpr Span(C&& container) noexcept
        : m_data(std::data(container)), m_size(std::size(container)) {}

    constexpr T* data() const noexcept {
        return m_data;
    }
    constexpr std::size_t size() const noexcept {
        return m_size;
    }
    constexpr std::size_t size_bytes() const noexcept {
        return m_size * sizeof(T);
    }
    constexpr bool empty() const noexcept {
        return m_size == 0;
    }

    constexpr T* begin() const noexcept {
        return m_data;
    }
    constexpr T* end() const noexcept {
        return m_data + m_size;
    }

    constexpr T& operator[](std::size_t index) const {
        assert(index < m_size);
        return m_data[index];
    }
    constexpr T& front() const {
        return (*this)[0];
    }
    constexpr T& back() const {
        return (*this)[m_size - 1];
    }

    /// The first `count` elements.
    constexpr Span first(std::size_t count) const {
        assert(count <= m_size);
        return {m_data, count};
    }
    /// The last `count` elements.
    constexpr Span last(std::size_t count) const {
        assert(count <= m_size);
        return {m_data + (m_size - count), count};
    }
    /// The `count` elements from `offset`, or all of them to the end when `count` is omitted.
    constexpr Span subspan(std::size_t offset,
                           std::size_t count = static_cast<std::size_t>(-1)) const {
        assert(offset <= m_size);
        const std::size_t rest = m_size - offset;
        assert(count == static_cast<std::size_t>(-1) || count <= rest);
        return {m_data + offset, count == static_cast<std::size_t>(-1) ? rest : count};
    }

  private:
    T* m_data = nullptr;
    std::size_t m_size = 0;
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
