// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstddef.hpp>

/**
 * @file new.hpp
 * @brief Placement new/delete definitions only. Global new/delete are deleted in <iro/iro.hpp>.
 */

namespace iro::freestanding {

enum class align_val_t : size_t {};

} // namespace iro::freestanding

inline void* operator new(iro::freestanding::size_t, void* p) noexcept { return p; }
inline void* operator new[](iro::freestanding::size_t, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}
