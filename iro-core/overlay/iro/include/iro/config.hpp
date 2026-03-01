// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

/**
 * @file config.hpp
 * @brief Common configuration macros and attributes for IRO Core.
 *
 * This header is intentionally lightweight and free of vendor C++ headers so
 * it can be included from freestanding code in the kernel build.
 */

#pragma once

#include <iro/version.hpp>
#include <iro/freestanding/cstddef.hpp>

// Debug/Release toggles -----------------------------------------------------
// Build systems define IRO_DEBUG for debug builds. Release builds leave it
// undefined.

// Attributes and compiler hints ---------------------------------------------
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(nodiscard)
#    define IRO_NODISCARD [[nodiscard]]
#  else
#    define IRO_NODISCARD
#  endif
#else
#  define IRO_NODISCARD
#endif

#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(gnu::always_inline)
#    define IRO_ALWAYS_INLINE [[gnu::always_inline]] inline
#  else
#    define IRO_ALWAYS_INLINE inline
#  endif
#else
#  define IRO_ALWAYS_INLINE inline
#endif

#ifndef IRO_NOEXCEPT
#  define IRO_NOEXCEPT noexcept
#endif

#define IRO_LIKELY(x)   (__builtin_expect(!!(x), 1))
#define IRO_UNLIKELY(x) (__builtin_expect(!!(x), 0))

namespace iro::detail {

IRO_ALWAYS_INLINE constexpr void trap() noexcept {
  __builtin_trap();
}

} // namespace iro::detail
