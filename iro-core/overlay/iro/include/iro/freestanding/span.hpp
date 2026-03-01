// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

template<class T>
class span {
public:
  using element_type = T;
  using value_type = remove_cv_t<T>;
  using size_type = size_t;
  using difference_type = decltype(static_cast<T*>(nullptr) - static_cast<T*>(nullptr));
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr span() noexcept : m_ptr(nullptr), m_size(0) {}
  constexpr span(T* ptr, size_t count) noexcept : m_ptr(ptr), m_size(count) {}
  constexpr span(T* first, T* last) noexcept : m_ptr(first), m_size(static_cast<size_t>(last - first)) {}

  template<size_t N>
  constexpr span(T (&arr)[N]) noexcept : m_ptr(arr), m_size(N) {}

  // Element access
  constexpr reference operator[](size_t idx) const noexcept { return m_ptr[idx]; }

  constexpr reference at(size_t idx) const noexcept {
    freestanding::debug_assert(idx < m_size);
    return m_ptr[idx];
  }

  constexpr reference front() const noexcept {
    freestanding::debug_assert(m_size > 0);
    return m_ptr[0];
  }

  constexpr reference back() const noexcept {
    freestanding::debug_assert(m_size > 0);
    return m_ptr[m_size - 1];
  }

  constexpr pointer data() const noexcept { return m_ptr; }

  // Capacity
  constexpr size_t size() const noexcept { return m_size; }
  constexpr size_t size_bytes() const noexcept { return m_size * sizeof(T); }
  constexpr bool empty() const noexcept { return m_size == 0; }

  // Iterators
  constexpr iterator begin() const noexcept { return m_ptr; }
  constexpr iterator end() const noexcept { return m_ptr + m_size; }
  constexpr const_iterator cbegin() const noexcept { return m_ptr; }
  constexpr const_iterator cend() const noexcept { return m_ptr + m_size; }

  // Subviews
  constexpr span first(size_t count) const noexcept {
    freestanding::debug_assert(count <= m_size);
    return span(m_ptr, count);
  }

  constexpr span last(size_t count) const noexcept {
    freestanding::debug_assert(count <= m_size);
    return span(m_ptr + (m_size - count), count);
  }

  constexpr span subspan(size_t offset, size_t count = static_cast<size_t>(-1)) const noexcept {
    freestanding::debug_assert(offset <= m_size);
    const size_t available = m_size - offset;
    const size_t actual_count = (count == static_cast<size_t>(-1) || count > available)
        ? available
        : count;
    return span(m_ptr + offset, actual_count);
  }

private:
  T* m_ptr;
  size_t m_size;
};

// Deduction guide for arrays
template<class T, size_t N>
span(T (&)[N]) -> span<T>;

// Byte-view conversions
template<class T>
constexpr span<const unsigned char> as_bytes(span<T> s) noexcept {
  return span<const unsigned char>(
      reinterpret_cast<const unsigned char*>(s.data()),
      s.size_bytes());
}

template<class T>
constexpr span<unsigned char> as_writable_bytes(span<T> s) noexcept {
  return span<unsigned char>(
      reinterpret_cast<unsigned char*>(s.data()),
      s.size_bytes());
}

} // namespace iro::freestanding
