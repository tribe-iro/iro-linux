// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/err/conversions.hpp>
#include <iro/err/errno_constants.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>
#include <iro/freestanding/expected.hpp>
#include <iro/mem/detail/align.hpp>
#include <iro/mem/detail/checked_size.hpp>
#include <iro/mem/gfp_mask.hpp>
#include <iro/mem/kmem.hpp>

namespace iro::mem {

template<class T, class... Args>
freestanding::expected<T*, err::errc> try_new(gfp_mask flags, Args&&... args) noexcept;

/**
 * @brief Allocate and construct a T using kernel allocators. Returns nullptr on failure.
 */
template<class T, class... Args>
T* iro_new(gfp_mask flags, Args&&... args) noexcept {
  auto result = try_new<T>(flags, freestanding::forward<Args>(args)...);
  return result.has_value() ? result.value() : nullptr;
}

/**
 * @brief Allocate and construct a T using kernel allocators. Reports errors explicitly.
 */
template<class T, class... Args>
freestanding::expected<T*, err::errc> try_new(gfp_mask flags, Args&&... args) noexcept {
  if (!detail::is_type_alignment_supported<T>()) {
    return freestanding::expected<T*, err::errc>(freestanding::unexpected<err::errc>(err::einval));
  }
  void* raw = kmalloc(sizeof(T), flags);
  if (!raw) {
    return freestanding::expected<T*, err::errc>(freestanding::unexpected<err::errc>(err::enomem));
  }

#if defined(__cpp_exceptions)
  T* obj = nullptr;
  try {
    obj = new (raw) T(freestanding::forward<Args>(args)...);
  } catch (...) {
    kfree(raw);
    return freestanding::expected<T*, err::errc>(
        freestanding::unexpected<err::errc>(err::einval));
  }
#else
  static_assert(freestanding::is_nothrow_constructible<T, Args...>::value,
                "try_new requires nothrow construction when exceptions are disabled");
  T* obj = new (raw) T(freestanding::forward<Args>(args)...);
#endif

  return freestanding::expected<T*, err::errc>(obj);
}

template<class T>
void iro_delete(T* ptr) noexcept {
  if (!ptr) { return; }
  ptr->~T();
  kfree(ptr);
}

} // namespace iro::mem
