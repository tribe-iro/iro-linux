// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/fmt/detail/parse.hpp>
#include <iro/fmt/spec.hpp>
#include <iro/fmt/fixed_string.hpp>
#include <iro/freestanding/array.hpp>
#include <iro/freestanding/cstddef.hpp>

namespace iro::fmt {

/**
 * @brief Compile-time validated format string with arity baked into the type.
 */
template<freestanding::size_t Arity>
class format_string {
public:
  constexpr format_string(const format_string&) = default;
  constexpr format_string& operator=(const format_string&) = default;

  constexpr const char* data() const noexcept { return m_data; }
  constexpr freestanding::size_t size() const noexcept { return m_size; }
  constexpr const freestanding::array<fmt_spec, Arity>& specs() const noexcept { return m_specs; }

private:
  constexpr format_string(const char* data, freestanding::size_t size,
                          freestanding::array<fmt_spec, Arity> specs)
      : m_data(data), m_size(size), m_specs(specs) {}

  template<freestanding::size_t OtherArity, fmt::fixed_string Lit>
  friend consteval format_string<OtherArity> make_format_string();

  const char* m_data;
  freestanding::size_t m_size;
  freestanding::array<fmt_spec, Arity> m_specs{};
};

template<freestanding::size_t Arity, fmt::fixed_string Lit>
consteval format_string<Arity> make_format_string() {
  constexpr auto parsed = detail::parse_format<Arity>(Lit);
  static_assert(parsed.ok, "Format string is ill-formed or arity mismatch");
  return format_string<Arity>(Lit.value, Lit.size(), parsed.specs);
}
#define IRO_FMT_STRING(ARITY, LIT) ::iro::fmt::make_format_string<ARITY, ::iro::fmt::fixed_string{LIT}>()

} // namespace iro::fmt
