// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

/**
 * @file cstdint.hpp
 * @brief Self-hosted fixed-width integer aliases.
 */

namespace iro::freestanding {

using int8_t = __INT8_TYPE__;
using uint8_t = __UINT8_TYPE__;
using int16_t = __INT16_TYPE__;
using uint16_t = __UINT16_TYPE__;
using int32_t = __INT32_TYPE__;
using uint32_t = __UINT32_TYPE__;
using int64_t = __INT64_TYPE__;
using uint64_t = __UINT64_TYPE__;

using intptr_t = __INTPTR_TYPE__;
using uintptr_t = __UINTPTR_TYPE__;

using intmax_t = __INTMAX_TYPE__;
using uintmax_t = __UINTMAX_TYPE__;

using size_t = __SIZE_TYPE__;

} // namespace iro::freestanding
