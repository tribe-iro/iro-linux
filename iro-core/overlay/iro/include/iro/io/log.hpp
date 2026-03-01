// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/abi/printk.h>
#include <iro/fmt/format_to.hpp>
#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/utility.hpp>
#include <iro/io/log_level.hpp>

namespace iro::io {

template<class... Args>
void log(log_level lvl, fmt::format_string<sizeof...(Args)> fmt_str, Args&&... args) noexcept {
  constexpr freestanding::size_t kBufSize = 512;
  char buffer[kBufSize];
  auto written = fmt::format_to(buffer, kBufSize, fmt_str, freestanding::forward<Args>(args)...);

  if (written >= kBufSize && kBufSize > 4) {
    buffer[kBufSize - 4] = '.';
    buffer[kBufSize - 3] = '.';
    buffer[kBufSize - 2] = '.';
    buffer[kBufSize - 1] = '\0';
  }

  iro_printk_level(static_cast<int>(lvl), buffer);
}

} // namespace iro::io
