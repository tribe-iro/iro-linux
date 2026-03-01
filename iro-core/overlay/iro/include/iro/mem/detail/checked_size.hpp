// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/err/errc.hpp>
#include <iro/err/errno_constants.hpp>
#include <iro/freestanding/cstddef.hpp>
#include <iro/freestanding/detail/overflow.hpp>

namespace iro::mem::detail {

template<class T>
inline bool compute_array_size(freestanding::size_t count, freestanding::size_t& out) noexcept {
  return !freestanding::detail::mul_overflow(sizeof(T), count, out);
}

} // namespace iro::mem::detail
