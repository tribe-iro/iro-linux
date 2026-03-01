// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/abi/gfp.h>
#include <iro/freestanding/cstddef.hpp>

namespace iro::mem::detail {

inline bool is_alignment_supported(freestanding::size_t align) noexcept {
  return align <= iro_kmalloc_minalign();
}

template<class T>
inline bool is_type_alignment_supported() noexcept {
  return is_alignment_supported(alignof(T));
}

} // namespace iro::mem::detail
