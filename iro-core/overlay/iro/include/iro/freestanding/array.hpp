// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

template<class T, size_t N>
struct array {
  T elems[N > 0 ? N : 1];

  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using iterator = T*;
  using const_iterator = const T*;

  constexpr reference operator[](size_t i) noexcept { return elems[i]; }
  constexpr const_reference operator[](size_t i) const noexcept { return elems[i]; }

  constexpr reference front() noexcept { return elems[0]; }
  constexpr const_reference front() const noexcept { return elems[0]; }

  constexpr reference back() noexcept { return elems[N - 1]; }
  constexpr const_reference back() const noexcept { return elems[N - 1]; }

  constexpr T* data() noexcept { return elems; }
  constexpr const T* data() const noexcept { return elems; }

  constexpr iterator begin() noexcept { return elems; }
  constexpr iterator end() noexcept { return elems + N; }
  constexpr const_iterator begin() const noexcept { return elems; }
  constexpr const_iterator end() const noexcept { return elems + N; }
  constexpr const_iterator cbegin() const noexcept { return elems; }
  constexpr const_iterator cend() const noexcept { return elems + N; }

  static constexpr size_type size() noexcept { return N; }
  static constexpr bool empty() noexcept { return N == 0; }

  constexpr void fill(const T& value) noexcept {
    for (size_t i = 0; i < N; ++i) {
      elems[i] = value;
    }
  }

  constexpr void swap(array& other) noexcept {
    for (size_t i = 0; i < N; ++i) {
      auto tmp = move(elems[i]);
      elems[i] = move(other.elems[i]);
      other.elems[i] = move(tmp);
    }
  }
};

template<class T, size_t N>
constexpr bool operator==(const array<T, N>& a, const array<T, N>& b) noexcept {
  for (size_t i = 0; i < N; ++i) {
    if (!(a[i] == b[i])) {
      return false;
    }
  }
  return true;
}

} // namespace iro::freestanding
