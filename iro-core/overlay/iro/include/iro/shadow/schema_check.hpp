// Copyright (c) The IRO Contributors
// SPDX-License-Identifier: GPL-2.0-only

#pragma once

#include <iro/version.hpp>

#ifndef IRO_LAYOUT_SCHEMA_MAJOR
#error "Generated layout header must define IRO_LAYOUT_SCHEMA_MAJOR"
#endif

#ifndef IRO_LAYOUT_SCHEMA_MINOR
#error "Generated layout header must define IRO_LAYOUT_SCHEMA_MINOR"
#endif

static_assert(IRO_LAYOUT_SCHEMA_MAJOR == 4, "IRO layout schema major mismatch");
static_assert(IRO_LAYOUT_SCHEMA_MINOR >= IRO_CORE_MIN_TOOL_SCHEMA_MINOR,
              "IRO layout schema minor too old for this Core");
