// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/abi/kmem.h>
#include <iro/freestanding/cstddef.hpp>
#include <iro/mem/gfp_mask.hpp>

namespace iro::mem {

inline void* kmalloc(freestanding::size_t size, gfp_mask flags) noexcept {
  return iro_kmalloc(size, flags.v);
}

inline void* krealloc(void* ptr, freestanding::size_t size, gfp_mask flags) noexcept {
  return iro_krealloc(ptr, size, flags.v);
}

inline void kfree(void* ptr) noexcept { iro_kfree(ptr); }

} // namespace iro::mem
