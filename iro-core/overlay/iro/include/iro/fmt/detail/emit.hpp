// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/cstdint.hpp>
#include <iro/freestanding/string_view.hpp>
#include <iro/freestanding/type_traits.hpp>

namespace iro::fmt::detail {

struct buffer {
  char* out;
  freestanding::size_t cap;
  freestanding::size_t written;

  constexpr buffer(char* o, freestanding::size_t c) noexcept : out(o), cap(c), written(0) {}

  constexpr void push(char c) noexcept {
    if (cap != 0 && written + 1 < cap) {
      out[written] = c;
    }
    ++written;
  }

  constexpr void append(const char* data, freestanding::size_t len) noexcept {
    for (freestanding::size_t i = 0; i < len; ++i) {
      push(data[i]);
    }
  }

  constexpr void append(freestanding::string_view sv) noexcept { append(sv.data(), sv.size()); }
};

inline constexpr const char* hex_digits = "0123456789abcdef";

template<class Int>
constexpr freestanding::size_t emit_unsigned(buffer& buf, Int value, bool hex) noexcept {
  char tmp[sizeof(Int) * 2 + 1];
  freestanding::size_t idx = 0;
  if (value == 0) {
    tmp[idx++] = '0';
  } else {
    while (value != 0) {
      if (hex) {
        auto digit = static_cast<unsigned>(value & 0xf);
        value = static_cast<Int>(value >> 4);
        tmp[idx++] = hex_digits[digit];
      } else {
        auto digit = static_cast<unsigned>(value % 10);
        value = static_cast<Int>(value / 10);
        tmp[idx++] = static_cast<char>('0' + digit);
      }
    }
  }
  for (freestanding::size_t i = 0; i < idx; ++i) {
    buf.push(tmp[idx - 1 - i]);
  }
  return idx;
}

template<class Int>
constexpr freestanding::size_t emit_signed(buffer& buf, Int value, bool hex) noexcept {
  using Unsigned = typename freestanding::conditional_t<sizeof(Int) == 8, freestanding::uint64_t, freestanding::uint32_t>;
  Unsigned u = static_cast<Unsigned>(value);
  if (hex) {
    return emit_unsigned(buf, u, true);
  }
  if (value < 0) {
    buf.push('-');
    u = static_cast<Unsigned>(0 - u);
    return emit_unsigned(buf, u, false) + 1;
  }
  return emit_unsigned(buf, u, false);
}

} // namespace iro::fmt::detail
