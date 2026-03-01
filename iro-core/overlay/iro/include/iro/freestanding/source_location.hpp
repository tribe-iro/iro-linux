// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstdint.hpp>
#include <iro/freestanding/string_view.hpp>

namespace iro::freestanding {

class source_location {
public:
  constexpr source_location() noexcept
      : m_line(0), m_column(0), m_file(""), m_function("") {}

  constexpr source_location(uint_least32_t line, uint_least32_t column,
                            const char* file, const char* function) noexcept
      : m_line(line), m_column(column), m_file(file), m_function(function) {}

  static constexpr source_location current(const char* file = __builtin_FILE(),
                                           const char* function = __builtin_FUNCTION(),
                                           uint_least32_t line = __builtin_LINE(),
                                           uint_least32_t column = 0) noexcept {
    return source_location(line, column, file, function);
  }

  constexpr uint_least32_t line() const noexcept { return m_line; }
  constexpr uint_least32_t column() const noexcept { return m_column; }
  constexpr const char* file_name() const noexcept { return m_file; }
  constexpr const char* function_name() const noexcept { return m_function; }

private:
  uint_least32_t m_line;
  uint_least32_t m_column;
  const char* m_file;
  const char* m_function;
};

} // namespace iro::freestanding
