// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/expected.hpp>
#include <iro/freestanding/utility.hpp>

/**
 * @file try.hpp
 * @brief Error propagation macros for expected<T, E> types.
 *
 * These macros provide Rust-like early-return error propagation using
 * GNU statement expressions (supported by Clang 18+ and GCC).
 *
 * Usage:
 *   expected<int, errc> get_value();
 *   expected<string, errc> process() {
 *     int val = IRO_TRY(get_value());
 *     // ... use val ...
 *     return string{...};
 *   }
 */

namespace iro::freestanding {

template<class E>
using decay_error_t = remove_cvref_t<E>;

template<class E>
constexpr unexpected<decay_error_t<E>> make_unexpected_from_error(E&& e) noexcept {
  return unexpected<decay_error_t<E>>(forward<E>(e));
}

} // namespace iro::freestanding

/**
 * @brief Extract value from expected or return early with error.
 *
 * If expr evaluates to an expected with a value, extracts and yields that value.
 * If expr evaluates to an expected with an error, returns unexpected(error) immediately.
 *
 * Requires: Function return type must be expected<U, E> where E matches expr's error type.
 */
#define IRO_TRY(expr) \
  __extension__({ \
    auto&& _iro_try_result = (expr); \
    if (!_iro_try_result.has_value()) { \
      return ::iro::freestanding::make_unexpected_from_error( \
          ::iro::freestanding::move(_iro_try_result.error())); \
    } \
    ::iro::freestanding::move(_iro_try_result.value()); \
  })

/**
 * @brief Extract value from expected or return early with transformed error.
 *
 * Like IRO_TRY but applies a transformation function to the error before returning.
 * Useful when calling functions that return different error types.
 *
 * Usage:
 *   expected<int, my_error> process() {
 *     int val = IRO_TRY_MAP(get_value(), [](errc e) { return my_error{e}; });
 *     return val;
 *   }
 */
#define IRO_TRY_MAP(expr, error_mapper) \
  __extension__({ \
    auto&& _iro_try_result = (expr); \
    if (!_iro_try_result.has_value()) { \
      auto _iro_mapped = (error_mapper)(::iro::freestanding::move(_iro_try_result.error())); \
      return ::iro::freestanding::make_unexpected_from_error(::iro::freestanding::move(_iro_mapped)); \
    } \
    ::iro::freestanding::move(_iro_try_result.value()); \
  })

/**
 * @brief Check expected and return early with error, discarding the value.
 *
 * Useful when you only care about success/failure, not the value itself.
 *
 * Usage:
 *   expected<void_t, errc> do_stuff() {
 *     IRO_TRY_VOID(some_operation());
 *     IRO_TRY_VOID(another_operation());
 *     return {};
 *   }
 */
#define IRO_TRY_VOID(expr) \
  do { \
    auto&& _iro_try_result = (expr); \
    if (!_iro_try_result.has_value()) { \
      return ::iro::freestanding::make_unexpected_from_error( \
          ::iro::freestanding::move(_iro_try_result.error())); \
    } \
  } while (0)

/**
 * @brief Assign value from expected to variable or return early with error.
 *
 * Alternative syntax when you want explicit variable declaration.
 *
 * Usage:
 *   IRO_TRY_ASSIGN(int, value, get_value());
 *   // value is now usable
 */
#define IRO_TRY_ASSIGN(type, var, expr) \
  type var = IRO_TRY(expr)
