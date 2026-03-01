// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>

namespace iro::freestanding::detail {

constexpr void* copy_bytes(void* dst, const void* src, size_t n) noexcept {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

constexpr void* move_bytes(void* dst, const void* src, size_t n) noexcept {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  if (d < s) {
    for (size_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
  } else if (d > s) {
    for (size_t i = n; i > 0; --i) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

constexpr void* fill_bytes(void* dst, unsigned char value, size_t n) noexcept {
  auto* d = static_cast<unsigned char*>(dst);
  for (size_t i = 0; i < n; ++i) {
    d[i] = value;
  }
  return dst;
}

} // namespace iro::freestanding::detail
