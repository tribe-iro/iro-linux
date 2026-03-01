// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/config.hpp>
#include <iro/err/errno_constants.hpp>
#include <iro/freestanding/assert.hpp>
#include <iro/freestanding/new.hpp>
#include <iro/freestanding/span.hpp>
#include <iro/freestanding/expected.hpp>
#include <iro/freestanding/type_traits.hpp>
#include <iro/freestanding/utility.hpp>
#include <iro/mem/alloc.hpp>
#include <iro/mem/detail/align.hpp>
#include <iro/mem/detail/checked_size.hpp>

namespace iro::mem {

template<class T>
struct slice_release {
  T* ptr;
  freestanding::size_t n;
};

template<class T>
class boxed_slice {
public:
  constexpr boxed_slice() noexcept : m_ptr(nullptr), m_size(0) {}
  constexpr boxed_slice(T* ptr, freestanding::size_t n) noexcept : m_ptr(ptr), m_size(n) {}

  boxed_slice(boxed_slice&& other) noexcept : m_ptr(other.m_ptr), m_size(other.m_size) {
    other.m_ptr = nullptr;
    other.m_size = 0;
  }

  boxed_slice& operator=(boxed_slice&& other) noexcept {
    if (this != &other) {
      reset();
      m_ptr = other.m_ptr;
      m_size = other.m_size;
      other.m_ptr = nullptr;
      other.m_size = 0;
    }
    return *this;
  }

  boxed_slice(const boxed_slice&) = delete;
  boxed_slice& operator=(const boxed_slice&) = delete;

  ~boxed_slice() noexcept { reset(); }

  constexpr T* data() noexcept { return m_ptr; }
  constexpr const T* data() const noexcept { return m_ptr; }
  constexpr freestanding::size_t size() const noexcept { return m_size; }
  constexpr bool empty() const noexcept { return m_size == 0; }

  constexpr freestanding::span<T> as_span() noexcept { return freestanding::span<T>(m_ptr, m_size); }
  constexpr freestanding::span<const T> as_span() const noexcept { return freestanding::span<const T>(m_ptr, m_size); }

  slice_release<T> release() noexcept {
    slice_release<T> r{m_ptr, m_size};
    m_ptr = nullptr;
    m_size = 0;
    return r;
  }

  void reset() noexcept {
    if (m_ptr) {
      if constexpr (!freestanding::is_trivially_destructible<T>::value) {
        for (freestanding::size_t i = 0; i < m_size; ++i) {
          (m_ptr + i)->~T();
        }
      }
      kfree(m_ptr);
      m_ptr = nullptr;
      m_size = 0;
    }
  }

private:
  T* m_ptr;
  freestanding::size_t m_size;
};

template<class T, class... Args>
freestanding::expected<boxed_slice<T>, err::errc>
make_boxed_slice(freestanding::size_t count, gfp_mask flags, Args&&... ctor_args)
    noexcept(freestanding::is_nothrow_constructible<T, Args...>::value) {
  if (count == 0) {
    return freestanding::expected<boxed_slice<T>, err::errc>(boxed_slice<T>());
  }

  if (!detail::is_type_alignment_supported<T>()) {
#ifdef IRO_DEBUG
    iro::detail::trap();
#endif
    return freestanding::expected<boxed_slice<T>, err::errc>(freestanding::unexpected<err::errc>(err::einval));
  }

  freestanding::size_t total = 0;
  if (!detail::compute_array_size<T>(count, total)) {
    return freestanding::expected<boxed_slice<T>, err::errc>(freestanding::unexpected<err::errc>(err::eoverflow));
  }

  void* raw = kmalloc(total, flags);
  if (!raw) {
    return freestanding::expected<boxed_slice<T>, err::errc>(freestanding::unexpected<err::errc>(err::enomem));
  }

  T* ptr = static_cast<T*>(raw);
#if defined(__cpp_exceptions)
  freestanding::size_t constructed = 0;
  if constexpr (freestanding::is_nothrow_constructible<T, Args...>::value) {
    for (; constructed < count; ++constructed) {
      new (ptr + constructed) T(freestanding::forward<Args>(ctor_args)...);
    }
  } else {
    try {
      for (; constructed < count; ++constructed) {
        new (ptr + constructed) T(freestanding::forward<Args>(ctor_args)...);
      }
    } catch (...) {
      if constexpr (!freestanding::is_trivially_destructible<T>::value) {
        for (freestanding::size_t j = constructed; j > 0; --j) {
          (ptr + j - 1)->~T();
        }
      }
      kfree(raw);
      throw;
    }
  }
#else
  static_assert(freestanding::is_nothrow_constructible<T, Args...>::value,
                "make_boxed_slice requires nothrow construction when exceptions are disabled");
  for (freestanding::size_t i = 0; i < count; ++i) {
    new (ptr + i) T(freestanding::forward<Args>(ctor_args)...);
  }
#endif

  return freestanding::expected<boxed_slice<T>, err::errc>(boxed_slice<T>(ptr, count));
}

} // namespace iro::mem
