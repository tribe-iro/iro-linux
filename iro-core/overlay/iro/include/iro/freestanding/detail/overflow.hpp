// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>

namespace iro::freestanding::detail {

inline constexpr bool mul_overflow(size_t a, size_t b, size_t& out) noexcept {
  if (a == 0 || b == 0) {
    out = 0;
    return false;
  }
  if (a > static_cast<size_t>(-1) / b) {
    return true;
  }
  out = a * b;
  return false;
}

} // namespace iro::freestanding::detail
