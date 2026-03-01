// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/no_new.hpp>
#include <iro/config.hpp>
#include <iro/version.hpp>
#include <iro/annotations.hpp>

#include <iro/err/errc.hpp>
#include <iro/err/errno_constants.hpp>
#include <iro/err/conversions.hpp>
#include <iro/err/errptr.hpp>

#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/array.hpp>
#include <iro/freestanding/expected.hpp>
#include <iro/freestanding/initializer_list.hpp>
#include <iro/freestanding/inplace_vector.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/optional.hpp>
#include <iro/freestanding/source_location.hpp>
#include <iro/freestanding/span.hpp>
#include <iro/freestanding/string_view.hpp>
#include <iro/freestanding/try.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/unique_ptr.hpp>
#include <iro/freestanding/utility.hpp>

#include <iro/mem/alloc.hpp>
#include <iro/mem/box.hpp>
#include <iro/mem/boxed_slice.hpp>
#include <iro/mem/gfp_mask.hpp>
#include <iro/mem/kmem.hpp>

#include <iro/fmt/fixed_string.hpp>
#include <iro/fmt/format_string.hpp>
#include <iro/fmt/format_to.hpp>
#include <iro/fmt/formatter.hpp>

#include <iro/io/log.hpp>
#include <iro/io/log_level.hpp>

namespace iro {

// Core vocabulary types
template<class T>
using span = freestanding::span<T>;

template<class T, class E>
using expected = freestanding::expected<T, E>;

template<class T>
using optional = freestanding::optional<T>;

template<class E>
using unexpected = freestanding::unexpected<E>;

using string_view = freestanding::string_view;
using source_location = freestanding::source_location;
using nullopt_t = freestanding::nullopt_t;
inline constexpr nullopt_t nullopt = freestanding::nullopt;

// Error handling
using errc = err::errc;

template<class E>
constexpr auto make_unexpected(E&& e) {
  return freestanding::make_unexpected(freestanding::forward<E>(e));
}

// Memory management
using gfp_mask = mem::gfp_mask;

template<class T>
using box = mem::box<T>;

template<class T>
using boxed_slice = mem::boxed_slice<T>;

// Byte views
template<class T>
constexpr span<const unsigned char> as_bytes(span<T> s) noexcept {
  return freestanding::as_bytes(s);
}

template<class T>
constexpr span<unsigned char> as_writable_bytes(span<T> s) noexcept {
  return freestanding::as_writable_bytes(s);
}

} // namespace iro
