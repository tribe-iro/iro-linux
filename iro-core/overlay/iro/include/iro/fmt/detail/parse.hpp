// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/fmt/fixed_string.hpp>
#include <iro/fmt/spec.hpp>
#include <iro/freestanding/array.hpp>
#include <iro/freestanding/cstddef.hpp>

namespace iro::fmt::detail {

template<freestanding::size_t Arity, freestanding::size_t N>
struct parse_result {
  freestanding::array<fmt_spec, Arity> specs{};
  freestanding::size_t placeholders = 0;
  bool ok = true;
};

template<freestanding::size_t Arity, freestanding::size_t N>
consteval parse_result<Arity, N> parse_format(fixed_string<N> fmt) {
  parse_result<Arity, N> result{};
  freestanding::size_t i = 0;
  while (i + 1 < N) { // include terminating NUL
    char ch = fmt.value[i];
    if (ch == '{') {
      if (i + 1 < N && fmt.value[i + 1] == '{') {
        i += 2;
        continue;
      }
      if (result.placeholders >= Arity) {
        result.ok = false;
        break;
      }
      fmt_spec spec = fmt_spec::default_;
      ++i;
      if (i + 1 >= N) {
        result.ok = false;
        break;
      }
      if (fmt.value[i] == ':') {
        ++i;
        if (i + 1 >= N) {
          result.ok = false;
          break;
        }
        char spec_ch = fmt.value[i];
        switch (spec_ch) {
          case 'd': spec = fmt_spec::dec; break;
          case 'x': spec = fmt_spec::hex; break;
          case 'p': spec = fmt_spec::ptr; break;
          case 's': spec = fmt_spec::str; break;
          default: result.ok = false; break;
        }
        ++i;
      }
      if (!result.ok || fmt.value[i] != '}') {
        result.ok = false;
        break;
      }
      result.specs[result.placeholders] = spec;
      ++result.placeholders;
      ++i;
    } else if (ch == '}') {
      if (i + 1 < N && fmt.value[i + 1] == '}') {
        i += 2;
        continue;
      }
      result.ok = false;
      break;
    } else {
      ++i;
    }
  }
  result.ok = result.ok && (result.placeholders == Arity);
  return result;
}

} // namespace iro::fmt::detail
