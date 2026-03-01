// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

namespace iro::io {

enum class log_level : int {
  emergency = 0,
  alert,
  critical,
  error,
  warning,
  notice,
  info,
  debug
};

} // namespace iro::io
