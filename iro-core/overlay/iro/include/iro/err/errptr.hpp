// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/abi/errptr.h>
#include <iro/err/conversions.hpp>
#include <iro/err/errc.hpp>

namespace iro::err {

template<class T>
inline bool is_err(const T* p) noexcept {
  return iro_is_err(static_cast<const void*>(p));
}

template<class T>
inline errc ptr_err(const T* p) noexcept {
  long neg = iro_ptr_err(static_cast<const void*>(p));
  return errc{static_cast<int>(-neg)};
}

template<class T = void>
inline T* err_ptr(errc e) noexcept {
  return static_cast<T*>(iro_err_ptr(to_kernel_error(e)));
}

} // namespace iro::err
