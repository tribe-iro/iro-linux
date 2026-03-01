// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>

namespace iro::freestanding {

template<class T>
class initializer_list {
public:
  using value_type = T;
  using reference = const T&;
  using const_reference = const T&;
  using size_type = size_t;
  using iterator = const T*;
  using const_iterator = const T*;

  constexpr initializer_list() noexcept : m_begin(nullptr), m_size(0) {}
  constexpr initializer_list(const T* b, size_t s) noexcept : m_begin(b), m_size(s) {}

  constexpr const T* begin() const noexcept { return m_begin; }
  constexpr const T* end() const noexcept { return m_begin + m_size; }
  constexpr size_t size() const noexcept { return m_size; }

private:
  const T* m_begin;
  size_t m_size;
};

template<class T>
constexpr const T* begin(initializer_list<T> il) noexcept {
  return il.begin();
}

template<class T>
constexpr const T* end(initializer_list<T> il) noexcept {
  return il.end();
}

} // namespace iro::freestanding
