// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/err/errc.hpp>
#include <iro/freestanding/expected.hpp>
#include <iro/freestanding/unique_ptr.hpp>
#include <iro/mem/alloc.hpp>

namespace iro::mem {

template<class T>
struct default_delete {
  constexpr void operator()(T* p) const noexcept { iro_delete(p); }
};

template<class T>
using box = freestanding::unique_ptr<T, default_delete<T>>;

template<class T, class... Args>
freestanding::expected<box<T>, err::errc> make_box(gfp_mask flags, Args&&... args) noexcept {
  auto result = try_new<T>(flags, freestanding::forward<Args>(args)...);
  if (!result.has_value()) {
    return freestanding::expected<box<T>, err::errc>(freestanding::unexpected<err::errc>(result.error()));
  }
  return freestanding::expected<box<T>, err::errc>(box<T>(result.value()));
}

} // namespace iro::mem
