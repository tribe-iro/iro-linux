// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/fmt/format_string.hpp>
#include <iro/fmt/formatter.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::fmt::detail {

template<freestanding::size_t Index, class T, class... Rest>
constexpr decltype(auto) get_arg(T&& first, Rest&&... rest) {
  if constexpr (Index == 0) {
    return freestanding::forward<T>(first);
  } else {
    return get_arg<Index - 1>(freestanding::forward<Rest>(rest)...);
  }
}

template<freestanding::size_t Index, class... Args>
constexpr void format_arg(fmt_spec spec, buffer& buf, Args&&... args) {
  auto&& value = get_arg<Index>(freestanding::forward<Args>(args)...);
  using ValueType = freestanding::remove_cvref_t<decltype(value)>;
  formatter<ValueType> f{};
  auto use_spec = f.parse(spec);
  (void)f.format(freestanding::forward<decltype(value)>(value), use_spec, buf);
}

template<freestanding::size_t Current, class... Args>
constexpr void dispatch_format(freestanding::size_t target, fmt_spec spec, buffer& buf, Args&&... args) {
  if constexpr (Current < sizeof...(Args)) {
    if (target == Current) {
      format_arg<Current>(spec, buf, freestanding::forward<Args>(args)...);
    } else {
      dispatch_format<Current + 1>(target, spec, buf, freestanding::forward<Args>(args)...);
    }
  }
}

} // namespace iro::fmt::detail

namespace iro::fmt {

template<class... Args>
freestanding::size_t format_to(char* out, freestanding::size_t out_cap,
                               format_string<sizeof...(Args)> fmt_str,
                               Args&&... args) noexcept {
  detail::buffer buf(out, out_cap);

  const char* data = fmt_str.data();
  freestanding::size_t size = fmt_str.size();

  freestanding::size_t arg_index = 0;
  freestanding::size_t i = 0;
  while (i < size) {
    char ch = data[i];
    if (ch == '{') {
      if (i + 1 < size && data[i + 1] == '{') {
        buf.push('{');
        i += 2;
        continue;
      }
      fmt::fmt_spec spec = fmt_str.specs()[arg_index];
      ++i;
      while (i < size && data[i] != '}') {
        ++i;
      }
      detail::dispatch_format<0>(arg_index, spec, buf, freestanding::forward<Args>(args)...);
      ++arg_index;
      if (i < size && data[i] == '}') {
        ++i;
      }
      continue;
    } else if (ch == '}') {
      if (i + 1 < size && data[i + 1] == '}') {
        buf.push('}');
        i += 2;
        continue;
      }
      // malformed brace; already rejected at compile time.
      ++i;
      continue;
    }
    buf.push(ch);
    ++i;
  }

  if (out_cap > 0) {
    freestanding::size_t pos = buf.written < out_cap ? buf.written : out_cap - 1;
    out[pos] = '\0';
  }
  return buf.written;
}

} // namespace iro::fmt
