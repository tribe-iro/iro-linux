// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/abi/gfp.h>
#include <iro/freestanding/cstddef.hpp>

namespace iro::mem {

struct gfp_mask {
  unsigned int v;
  constexpr explicit gfp_mask(unsigned int raw) noexcept : v(raw) {}
};

constexpr gfp_mask operator|(gfp_mask a, gfp_mask b) noexcept {
  return gfp_mask{static_cast<unsigned int>(a.v | b.v)};
}

constexpr gfp_mask operator&(gfp_mask a, gfp_mask b) noexcept {
  return gfp_mask{static_cast<unsigned int>(a.v & b.v)};
}

inline gfp_mask gfp_kernel() noexcept { return gfp_mask{iro_gfp_kernel()}; }
inline gfp_mask gfp_atomic() noexcept { return gfp_mask{iro_gfp_atomic()}; }
inline gfp_mask gfp_nowait() noexcept { return gfp_mask{iro_gfp_nowait()}; }
inline gfp_mask gfp_noio() noexcept { return gfp_mask{iro_gfp_noio()}; }
inline gfp_mask gfp_nofs() noexcept { return gfp_mask{iro_gfp_nofs()}; }
inline gfp_mask gfp_zero() noexcept { return gfp_mask{iro_gfp_zero()}; }

} // namespace iro::mem
