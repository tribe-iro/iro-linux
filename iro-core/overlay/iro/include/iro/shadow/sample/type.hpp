// Sample shadow demonstrating layout verification.
#pragma once

#include <generated/iro/layout_sample.hpp>
#include <iro/shadow/schema_check.hpp>
#include <iro/shadow/validate.hpp>

namespace iro::shadow::sample {

struct sample_type {
  int a;
  int b;
};

// Full validation with single macro (recommended)
IRO_VALIDATE_SHADOW_WITH_FIELDS(sample_type, sample_type, a, b);

} // namespace iro::shadow::sample
