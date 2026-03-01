// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/fmt/detail/emit.hpp>
#include <iro/fmt/spec.hpp>
#include <iro/freestanding/cstdint.hpp>
#include <iro/freestanding/string_view.hpp>
#include <iro/freestanding/type_traits.hpp>

namespace iro::fmt {

template<class T, class Enable = void>
struct formatter;

template<class T>
struct formatter<T, freestanding::enable_if_t<freestanding::is_integral<T>::value && !freestanding::is_same<T, bool>::value>> {
  constexpr fmt_spec parse(fmt_spec spec) const noexcept { return spec; }

  constexpr freestanding::size_t format(T value, fmt_spec spec, detail::buffer& buf) const noexcept {
    if (freestanding::is_signed<T>::value) {
      return detail::emit_signed(buf, value, spec == fmt_spec::hex || spec == fmt_spec::ptr);
    }
    return detail::emit_unsigned(buf, static_cast<freestanding::make_unsigned_t<T>>(value),
                                 spec == fmt_spec::hex || spec == fmt_spec::ptr);
  }
};

template<>
struct formatter<bool> {
  constexpr fmt_spec parse(fmt_spec spec) const noexcept { return spec; }

  constexpr freestanding::size_t format(bool value, fmt_spec, detail::buffer& buf) const noexcept {
    const char* s = value ? "true" : "false";
    buf.append(s, value ? 4 : 5);
    return value ? 4 : 5;
  }
};

template<class T>
struct formatter<T*, freestanding::enable_if_t<!freestanding::is_same<T, const char>::value>> {
  constexpr fmt_spec parse(fmt_spec spec) const noexcept { return spec == fmt_spec::default_ ? fmt_spec::ptr : spec; }

  constexpr freestanding::size_t format(T* ptr, fmt_spec spec, detail::buffer& buf) const noexcept {
    (void)spec;
    buf.append("0x", 2);
    return 2 + detail::emit_unsigned(buf, reinterpret_cast<freestanding::uintptr_t>(ptr), true);
  }
};

template<>
struct formatter<const char*> {
  constexpr fmt_spec parse(fmt_spec spec) const noexcept { return spec; }

  constexpr freestanding::size_t format(const char* s, fmt_spec spec, detail::buffer& buf) const noexcept {
    if (spec == fmt_spec::ptr) {
      buf.append("0x", 2);
      return 2 + detail::emit_unsigned(buf, reinterpret_cast<freestanding::uintptr_t>(s), true);
    }
    freestanding::size_t len = 0;
    if (s) {
      while (s[len] != '\0') { ++len; }
      buf.append(s, len);
    }
    return len;
  }
};

template<>
struct formatter<freestanding::string_view> {
  constexpr fmt_spec parse(fmt_spec spec) const noexcept { return spec; }

  constexpr freestanding::size_t format(freestanding::string_view sv, fmt_spec spec, detail::buffer& buf) const noexcept {
    if (spec == fmt_spec::ptr) {
      buf.append("0x", 2);
      return 2 + detail::emit_unsigned(buf, reinterpret_cast<freestanding::uintptr_t>(sv.data()), true);
    }
    buf.append(sv);
    return sv.size();
  }
};

} // namespace iro::fmt
