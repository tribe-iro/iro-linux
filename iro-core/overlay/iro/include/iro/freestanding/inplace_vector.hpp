// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

template<class T, size_t Capacity>
class inplace_vector {
public:
  using value_type = T;
  using size_type = size_t;
  using reference = T&;
  using const_reference = const T&;

  constexpr inplace_vector() noexcept : m_size(0) {}
  ~inplace_vector() noexcept { clear(); }

  inplace_vector(const inplace_vector&) = delete;
  inplace_vector& operator=(const inplace_vector&) = delete;

  constexpr size_t size() const noexcept { return m_size; }
  constexpr size_t capacity() const noexcept { return Capacity; }
  constexpr bool empty() const noexcept { return m_size == 0; }

  constexpr T* data() noexcept { return reinterpret_cast<T*>(m_storage); }
  constexpr const T* data() const noexcept { return reinterpret_cast<const T*>(m_storage); }

  constexpr T& operator[](size_t idx) noexcept { return data()[idx]; }
  constexpr const T& operator[](size_t idx) const noexcept { return data()[idx]; }

  constexpr T& front() noexcept { return (*this)[0]; }
  constexpr const T& front() const noexcept { return (*this)[0]; }

  constexpr T& back() noexcept { return (*this)[m_size - 1]; }
  constexpr const T& back() const noexcept { return (*this)[m_size - 1]; }

  constexpr bool try_push_back(const T& value) {
    if (m_size >= Capacity) {
      return false;
    }
    new (data() + m_size) T(value);
    ++m_size;
    return true;
  }

  constexpr bool try_push_back(T&& value) {
    if (m_size >= Capacity) {
      return false;
    }
    new (data() + m_size) T(move(value));
    ++m_size;
    return true;
  }

  template<class... Args>
  constexpr bool try_emplace_back(Args&&... args) {
    if (m_size >= Capacity) {
      return false;
    }
    new (data() + m_size) T(forward<Args>(args)...);
    ++m_size;
    return true;
  }

  constexpr void push_back(const T& value) {
    freestanding::debug_assert(try_push_back(value));
  }

  constexpr void push_back(T&& value) {
    freestanding::debug_assert(try_push_back(move(value)));
  }

  template<class... Args>
  constexpr T& emplace_back(Args&&... args) {
    freestanding::debug_assert(try_emplace_back(forward<Args>(args)...));
    return back();
  }

  constexpr void pop_back() noexcept {
    freestanding::debug_assert(m_size > 0);
    --m_size;
    (data() + m_size)->~T();
  }

  constexpr void clear() noexcept {
    if constexpr (!is_trivially_destructible<T>::value) {
      for (size_t i = 0; i < m_size; ++i) {
        (data() + i)->~T();
      }
    }
    m_size = 0;
  }

private:
  alignas(T) unsigned char m_storage[sizeof(T) * (Capacity > 0 ? Capacity : 1)];
  size_t m_size;
};

} // namespace iro::freestanding
