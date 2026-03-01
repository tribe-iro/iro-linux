// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

struct nullopt_t { explicit constexpr nullopt_t(int) {} };
inline constexpr nullopt_t nullopt{0};

template<class T>
class IRO_NODISCARD optional {
public:
  using value_type = T;

  constexpr optional() noexcept : m_has(false) {}
  constexpr optional(nullopt_t) noexcept : m_has(false) {}

  constexpr optional(const T& value) : m_has(true) { new (&m_storage) T(value); }
  constexpr optional(T&& value) : m_has(true) { new (&m_storage) T(move(value)); }

  template<class... Args>
  constexpr explicit optional(in_place_t, Args&&... args) : m_has(true) {
    new (&m_storage) T(forward<Args>(args)...);
  }

  optional(const optional& other) : m_has(other.m_has) {
    if (m_has) { new (&m_storage) T(*other); }
  }

  optional(optional&& other) noexcept(is_nothrow_move_constructible<T>::value) : m_has(other.m_has) {
    if (m_has) { new (&m_storage) T(move(*other)); }
  }

  ~optional() { reset(); }

  optional& operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

  optional& operator=(const optional& other) {
    if (this == &other) { return *this; }
    if (m_has && other.m_has) {
      **this = *other;
    } else if (other.m_has) {
      emplace(*other);
    } else {
      reset();
    }
    return *this;
  }

  optional& operator=(optional&& other) noexcept(
      is_nothrow_move_assignable<T>::value &&
      is_nothrow_move_constructible<T>::value &&
      is_nothrow_destructible<T>::value) {
    if (this == &other) { return *this; }
    if (m_has && other.m_has) {
      **this = move(*other);
    } else if (other.m_has) {
      emplace(move(*other));
    } else {
      reset();
    }
    return *this;
  }

  template<class... Args>
  T& emplace(Args&&... args) {
    reset();
    new (&m_storage) T(forward<Args>(args)...);
    m_has = true;
    return **this;
  }

  IRO_NODISCARD constexpr T* operator->() { return reinterpret_cast<T*>(&m_storage); }
  IRO_NODISCARD constexpr const T* operator->() const { return reinterpret_cast<const T*>(&m_storage); }

  IRO_NODISCARD constexpr T& operator*() { return *reinterpret_cast<T*>(&m_storage); }
  IRO_NODISCARD constexpr const T& operator*() const { return *reinterpret_cast<const T*>(&m_storage); }

  IRO_NODISCARD constexpr explicit operator bool() const noexcept { return m_has; }
  IRO_NODISCARD constexpr bool has_value() const noexcept { return m_has; }

  IRO_NODISCARD constexpr T& value() & {
    if (!m_has) { freestanding::trap(); }
    return **this;
  }

  IRO_NODISCARD constexpr const T& value() const& {
    if (!m_has) { freestanding::trap(); }
    return **this;
  }

  IRO_NODISCARD constexpr T&& value() && {
    if (!m_has) { freestanding::trap(); }
    return move(**this);
  }

  template<class U>
  IRO_NODISCARD constexpr T value_or(U&& default_value) const& {
    return m_has ? **this : static_cast<T>(forward<U>(default_value));
  }

  template<class U>
  IRO_NODISCARD constexpr T value_or(U&& default_value) && {
    return m_has ? move(**this) : static_cast<T>(forward<U>(default_value));
  }

  constexpr void reset() noexcept {
    if (m_has) {
      reinterpret_cast<T*>(&m_storage)->~T();
      m_has = false;
    }
  }

private:
  alignas(T) unsigned char m_storage[sizeof(T)];
  bool m_has;
};

} // namespace iro::freestanding
