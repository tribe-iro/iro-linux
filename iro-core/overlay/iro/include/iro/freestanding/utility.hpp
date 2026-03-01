// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/type_traits.hpp>

/**
 * @file utility.hpp
 * @brief Core utility helpers: move/forward/swap and tag types.
 */

namespace iro::freestanding {

struct in_place_t { explicit in_place_t() = default; };
template<class T> struct in_place_type_t { explicit in_place_type_t() = default; };
template<size_t I> struct in_place_index_t { explicit in_place_index_t() = default; };

inline constexpr in_place_t in_place{};
template<class T> inline constexpr in_place_type_t<T> in_place_type{};
template<size_t I> inline constexpr in_place_index_t<I> in_place_index{};

template<class T>
IRO_ALWAYS_INLINE constexpr T&& forward(remove_reference_t<T>& t) noexcept {
  return static_cast<T&&>(t);
}

template<class T>
IRO_ALWAYS_INLINE constexpr T&& forward(remove_reference_t<T>&& t) noexcept {
  static_assert(!is_lvalue_reference<T>::value, "bad forward");
  return static_cast<T&&>(t);
}

template<class T>
IRO_ALWAYS_INLINE constexpr remove_reference_t<T>&& move(T&& t) noexcept {
  return static_cast<remove_reference_t<T>&&>(t);
}

template<class T>
IRO_ALWAYS_INLINE constexpr conditional_t<
    !is_nothrow_move_constructible<T>::value && is_copy_constructible<T>::value,
    const T&,
    T&&
> move_if_noexcept(T& t) noexcept {
  using return_t = conditional_t<
      !is_nothrow_move_constructible<T>::value && is_copy_constructible<T>::value,
      const T&,
      T&&>;
  return static_cast<return_t>(t);
}

template<class T>
constexpr void swap(T& a, T& b) noexcept {
  T tmp = move(a);
  a = move(b);
  b = move(tmp);
}

template<class T, class U = T>
constexpr T exchange(T& obj, U&& new_value) noexcept {
  T old = move(obj);
  obj = forward<U>(new_value);
  return old;
}

template<class T>
constexpr const T& as_const(T& t) noexcept { return t; }

template<class T>
void as_const(const T&&) = delete;

} // namespace iro::freestanding
