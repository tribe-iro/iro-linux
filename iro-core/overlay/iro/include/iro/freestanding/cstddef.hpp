// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

/**
 * @file cstddef.hpp
 * @brief Self-hosted equivalents of <cstddef> essentials.
 */

namespace iro::freestanding {

using size_t = __SIZE_TYPE__;
using ptrdiff_t = __PTRDIFF_TYPE__;
using nullptr_t = decltype(nullptr);

inline constexpr size_t dynamic_extent = static_cast<size_t>(-1);

} // namespace iro::freestanding
