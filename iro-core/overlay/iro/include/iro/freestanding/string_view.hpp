// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/detail/constexpr_mem.hpp>

namespace iro::freestanding {

class string_view {
public:
  using size_type = size_t;
  using value_type = char;
  using pointer = const char*;
  using const_pointer = const char*;
  using reference = const char&;
  using const_reference = const char&;
  using iterator = const char*;
  using const_iterator = const char*;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr string_view() noexcept : m_data(nullptr), m_size(0) {}
  constexpr string_view(const char* str, size_t len) noexcept : m_data(str), m_size(len) {}
  constexpr string_view(const char* str) noexcept : m_data(str), m_size(length(str)) {}

  // Iterators
  constexpr const_iterator begin() const noexcept { return m_data; }
  constexpr const_iterator end() const noexcept { return m_data + m_size; }
  constexpr const_iterator cbegin() const noexcept { return m_data; }
  constexpr const_iterator cend() const noexcept { return m_data + m_size; }

  // Capacity
  constexpr const char* data() const noexcept { return m_data; }
  constexpr size_t size() const noexcept { return m_size; }
  constexpr size_t length() const noexcept { return m_size; }
  constexpr bool empty() const noexcept { return m_size == 0; }

  // Element access
  constexpr const char& operator[](size_t idx) const noexcept { return m_data[idx]; }

  constexpr const char& at(size_t idx) const noexcept {
    freestanding::debug_assert(idx < m_size);
    return m_data[idx];
  }

  constexpr const char& front() const noexcept {
    freestanding::debug_assert(m_size > 0);
    return m_data[0];
  }

  constexpr const char& back() const noexcept {
    freestanding::debug_assert(m_size > 0);
    return m_data[m_size - 1];
  }

  // Modifiers (modify view, not underlying data)
  constexpr void remove_prefix(size_t n) noexcept {
    freestanding::debug_assert(n <= m_size);
    m_data += n;
    m_size -= n;
  }

  constexpr void remove_suffix(size_t n) noexcept {
    freestanding::debug_assert(n <= m_size);
    m_size -= n;
  }

  constexpr void swap(string_view& other) noexcept {
    const char* tmp_data = m_data;
    size_t tmp_size = m_size;
    m_data = other.m_data;
    m_size = other.m_size;
    other.m_data = tmp_data;
    other.m_size = tmp_size;
  }

  // String operations
  constexpr string_view substr(size_t pos = 0, size_t count = npos) const noexcept {
    freestanding::debug_assert(pos <= m_size);
    const size_t rlen = (count == npos || count > m_size - pos) ? (m_size - pos) : count;
    return string_view(m_data + pos, rlen);
  }

  constexpr bool starts_with(string_view sv) const noexcept {
    if (sv.m_size > m_size) return false;
    for (size_t i = 0; i < sv.m_size; ++i) {
      if (m_data[i] != sv.m_data[i]) return false;
    }
    return true;
  }

  constexpr bool starts_with(char c) const noexcept {
    return m_size > 0 && m_data[0] == c;
  }

  constexpr bool ends_with(string_view sv) const noexcept {
    if (sv.m_size > m_size) return false;
    const size_t offset = m_size - sv.m_size;
    for (size_t i = 0; i < sv.m_size; ++i) {
      if (m_data[offset + i] != sv.m_data[i]) return false;
    }
    return true;
  }

  constexpr bool ends_with(char c) const noexcept {
    return m_size > 0 && m_data[m_size - 1] == c;
  }

  constexpr size_t find(char c, size_t pos = 0) const noexcept {
    for (size_t i = pos; i < m_size; ++i) {
      if (m_data[i] == c) return i;
    }
    return npos;
  }

  constexpr size_t find(string_view sv, size_t pos = 0) const noexcept {
    if (sv.empty()) return pos <= m_size ? pos : npos;
    if (sv.m_size > m_size) return npos;
    const size_t last_possible = m_size - sv.m_size;
    for (size_t i = pos; i <= last_possible; ++i) {
      if (substr(i, sv.m_size).compare(sv) == 0) return i;
    }
    return npos;
  }

  constexpr size_t rfind(char c, size_t pos = npos) const noexcept {
    if (m_size == 0) return npos;
    size_t i = (pos == npos || pos >= m_size) ? m_size - 1 : pos;
    for (;; --i) {
      if (m_data[i] == c) return i;
      if (i == 0) break;
    }
    return npos;
  }

  constexpr int compare(string_view other) const noexcept {
    const size_t min_size = m_size < other.m_size ? m_size : other.m_size;
    for (size_t i = 0; i < min_size; ++i) {
      if (m_data[i] < other.m_data[i]) return -1;
      if (m_data[i] > other.m_data[i]) return 1;
    }
    if (m_size < other.m_size) return -1;
    if (m_size > other.m_size) return 1;
    return 0;
  }

  friend constexpr bool operator==(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) == 0;
  }

  friend constexpr bool operator!=(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) != 0;
  }

  friend constexpr bool operator<(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) < 0;
  }

  friend constexpr bool operator<=(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) <= 0;
  }

  friend constexpr bool operator>(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) > 0;
  }

  friend constexpr bool operator>=(string_view lhs, string_view rhs) noexcept {
    return lhs.compare(rhs) >= 0;
  }

private:
  static constexpr size_t length(const char* s) noexcept {
    size_t n = 0;
    while (s && s[n] != '\0') { ++n; }
    return n;
  }

  const char* m_data;
  size_t m_size;
};

} // namespace iro::freestanding
