// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/detail/constexpr_mem.hpp>

namespace iro::fmt {

/**
 * @brief Compile-time string carrier capturing string literals.
 */
template<freestanding::size_t N>
struct fixed_string {
  char value[N];

  consteval fixed_string(const char (&str)[N]) noexcept : value{} {
    for (freestanding::size_t i = 0; i < N; ++i) {
      value[i] = str[i];
    }
  }

  constexpr freestanding::size_t size() const noexcept { return N ? N - 1 : 0; }
  constexpr const char* c_str() const noexcept { return value; }
};

} // namespace iro::fmt
