// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/freestanding/cstdint.hpp>

namespace iro::err {

/**
 * @brief Minimal error code wrapper storing a positive errno value.
 */
struct errc {
  int value;
};

} // namespace iro::err
