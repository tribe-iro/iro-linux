// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/err/errc.hpp>

namespace iro::err {

constexpr errc to_errc(int positive_errno) noexcept {
  return errc{positive_errno};
}

constexpr int to_errno(errc e) noexcept { return e.value; }

constexpr int to_kernel_error(errc e) noexcept { return -to_errno(e); }

} // namespace iro::err
