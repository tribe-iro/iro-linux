// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>

namespace iro::freestanding {

template<class T>
struct default_delete {
  constexpr default_delete() noexcept = default;
  template<class U>
  constexpr default_delete(const default_delete<U>&) noexcept {}

  constexpr void operator()(T* ptr) const noexcept {
    if (ptr) {
      delete ptr;
    }
  }
};

template<class T, class Deleter = default_delete<T>>
class unique_ptr {
public:
  using pointer = T*;
  using element_type = T;
  using deleter_type = Deleter;

  constexpr unique_ptr() noexcept : m_ptr(nullptr), m_deleter() {}
  constexpr explicit unique_ptr(pointer p) noexcept : m_ptr(p), m_deleter() {}
  constexpr unique_ptr(pointer p, const Deleter& d) noexcept : m_ptr(p), m_deleter(d) {}

  unique_ptr(const unique_ptr&) = delete;
  unique_ptr& operator=(const unique_ptr&) = delete;

  constexpr unique_ptr(unique_ptr&& other) noexcept : m_ptr(other.release()), m_deleter(forward<Deleter>(other.m_deleter)) {}

  constexpr unique_ptr& operator=(unique_ptr&& other) noexcept {
    if (this != &other) {
      reset(other.release());
      m_deleter = forward<Deleter>(other.m_deleter);
    }
    return *this;
  }

  ~unique_ptr() noexcept { reset(); }

  constexpr pointer get() const noexcept { return m_ptr; }
  constexpr deleter_type& get_deleter() noexcept { return m_deleter; }
  constexpr const deleter_type& get_deleter() const noexcept { return m_deleter; }
  constexpr explicit operator bool() const noexcept { return m_ptr != nullptr; }

  constexpr pointer release() noexcept {
    pointer tmp = m_ptr;
    m_ptr = nullptr;
    return tmp;
  }

  constexpr void reset(pointer p = pointer()) noexcept {
    pointer old = m_ptr;
    m_ptr = p;
    if (old) {
      m_deleter(old);
    }
  }

  constexpr pointer operator->() const noexcept { return m_ptr; }
  constexpr element_type& operator*() const noexcept { return *m_ptr; }

  constexpr void swap(unique_ptr& other) noexcept {
    pointer tmp = m_ptr;
    m_ptr = other.m_ptr;
    other.m_ptr = tmp;
    freestanding::swap(m_deleter, other.m_deleter);
  }

private:
  pointer m_ptr;
  deleter_type m_deleter;
};

template<class T, class D>
constexpr void swap(unique_ptr<T, D>& a, unique_ptr<T, D>& b) noexcept {
  a.swap(b);
}

} // namespace iro::freestanding
