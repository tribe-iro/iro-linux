// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>

/**
 * @file assert.hpp
 * @brief Debug trapping helpers.
 */

namespace iro::freestanding {

IRO_ALWAYS_INLINE constexpr void trap() noexcept { iro::detail::trap(); }

IRO_ALWAYS_INLINE constexpr void debug_assert(bool cond) noexcept {
#ifdef IRO_DEBUG
  if (IRO_UNLIKELY(!cond)) {
    iro::detail::trap();
  }
#else
  (void)cond;
#endif
}

} // namespace iro::freestanding
