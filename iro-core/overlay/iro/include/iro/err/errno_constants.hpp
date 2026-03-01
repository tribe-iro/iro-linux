// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/err/errc.hpp>

namespace iro::err {

inline constexpr errc enomem{12};
inline constexpr errc einval{22};
inline constexpr errc eoverflow{75};

inline constexpr int ENOMEM_ = 12;
inline constexpr int EINVAL_ = 22;
inline constexpr int EOVERFLOW_ = 75;

} // namespace iro::err
