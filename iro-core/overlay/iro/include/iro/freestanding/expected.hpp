// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

template<class E>
class unexpected {
public:
  static_assert(!is_reference<E>::value, "unexpected<E> requires a non-reference error type");

  constexpr explicit unexpected(const E& e) : m_error(e) {}
  constexpr explicit unexpected(E&& e) : m_error(move(e)) {}

  constexpr const E& value() const & noexcept { return m_error; }
  constexpr E& value() & noexcept { return m_error; }
  constexpr E&& value() && noexcept { return move(m_error); }

private:
  E m_error;
};

template<class E>
constexpr unexpected<remove_cvref_t<E>> make_unexpected(E&& e) {
  using error_type = remove_cvref_t<E>;
  return unexpected<error_type>(forward<E>(e));
}

template<class T, class E>
class IRO_NODISCARD expected {
public:
  using value_type = T;
  using error_type = E;

  static_assert(is_nothrow_move_constructible<T>::value &&
                    is_nothrow_move_constructible<E>::value,
                "expected<T,E> requires nothrow move construction for both arms");

  constexpr expected() noexcept : m_has_value(true) { new (&m_storage.value) T(); }
  constexpr expected(const T& v) : m_has_value(true) { new (&m_storage.value) T(v); }
  constexpr expected(T&& v) : m_has_value(true) { new (&m_storage.value) T(move(v)); }
  constexpr expected(const unexpected<E>& e) : m_has_value(false) { new (&m_storage.error) E(e.value()); }
  constexpr expected(unexpected<E>&& e) : m_has_value(false) { new (&m_storage.error) E(move(e.value())); }

  expected(const expected& other) : m_has_value(other.m_has_value) {
    if (m_has_value) {
      new (&m_storage.value) T(other.value());
    } else {
      new (&m_storage.error) E(other.error());
    }
  }

  expected(expected&& other) noexcept(is_nothrow_move_constructible<T>::value && is_nothrow_move_constructible<E>::value)
      : m_has_value(other.m_has_value) {
    if (m_has_value) {
      new (&m_storage.value) T(move(other.value()));
    } else {
      new (&m_storage.error) E(move(other.error()));
    }
  }

  ~expected() { destroy(); }

  expected& operator=(const expected& other) {
    if (this == &other) { return *this; }
    if (m_has_value && other.m_has_value) {
      value() = other.value();
    } else if (!m_has_value && !other.m_has_value) {
      error() = other.error();
    } else if (other.m_has_value) {
      T tmp(other.value());
      destroy();
      new (&m_storage.value) T(move(tmp));
      m_has_value = true;
    } else {
      E tmp(other.error());
      destroy();
      new (&m_storage.error) E(move(tmp));
      m_has_value = false;
    }
    return *this;
  }

  expected& operator=(expected&& other) noexcept(
      is_nothrow_move_assignable<T>::value &&
      is_nothrow_move_assignable<E>::value &&
      is_nothrow_move_constructible<T>::value &&
      is_nothrow_move_constructible<E>::value) {
    if (this == &other) { return *this; }
    if (m_has_value && other.m_has_value) {
      value() = move(other.value());
    } else if (!m_has_value && !other.m_has_value) {
      error() = move(other.error());
    } else if (other.m_has_value) {
      T tmp(move(other.value()));
      destroy();
      new (&m_storage.value) T(move(tmp));
      m_has_value = true;
    } else {
      E tmp(move(other.error()));
      destroy();
      new (&m_storage.error) E(move(tmp));
      m_has_value = false;
    }
    return *this;
  }

  template<class... Args>
  constexpr T& emplace(Args&&... args) {
    T tmp(forward<Args>(args)...);
    destroy();
    new (&m_storage.value) T(move(tmp));
    m_has_value = true;
    return value();
  }

  IRO_NODISCARD constexpr bool has_value() const noexcept { return m_has_value; }
  constexpr explicit operator bool() const noexcept { return has_value(); }

  IRO_NODISCARD constexpr T& value() & {
    if (!m_has_value) { freestanding::trap(); }
    return m_storage.value;
  }

  IRO_NODISCARD constexpr const T& value() const& {
    if (!m_has_value) { freestanding::trap(); }
    return m_storage.value;
  }

  IRO_NODISCARD constexpr T&& value() && {
    if (!m_has_value) { freestanding::trap(); }
    return move(m_storage.value);
  }

  template<class U>
  IRO_NODISCARD constexpr T value_or(U&& default_value) const& {
    return m_has_value ? m_storage.value : static_cast<T>(forward<U>(default_value));
  }

  template<class U>
  IRO_NODISCARD constexpr T value_or(U&& default_value) && {
    return m_has_value ? move(m_storage.value) : static_cast<T>(forward<U>(default_value));
  }

  IRO_NODISCARD constexpr E& error() & {
    if (m_has_value) { freestanding::trap(); }
    return m_storage.error;
  }

  IRO_NODISCARD constexpr const E& error() const& {
    if (m_has_value) { freestanding::trap(); }
    return m_storage.error;
  }

  IRO_NODISCARD constexpr E&& error() && {
    if (m_has_value) { freestanding::trap(); }
    return move(m_storage.error);
  }

  template<class G>
  IRO_NODISCARD constexpr E error_or(G&& default_error) const& {
    return m_has_value ? static_cast<E>(forward<G>(default_error)) : m_storage.error;
  }

  template<class G>
  IRO_NODISCARD constexpr E error_or(G&& default_error) && {
    return m_has_value ? static_cast<E>(forward<G>(default_error)) : move(m_storage.error);
  }

private:
  void destroy() noexcept {
    if (m_has_value) {
      m_storage.value.~T();
    } else {
      m_storage.error.~E();
    }
  }

  union storage_t {
    storage_t() {}
    ~storage_t() {}
    T value;
    E error;
  } m_storage;
  bool m_has_value;
};

} // namespace iro::freestanding
