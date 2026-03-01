# IRO Shadow Types

Shadow headers live under `iro/shadow/<subsystem>/` and mirror kernel layouts without including kernel headers. Each shadow must:

- `#include <generated/iro/layout_<set>.hpp>` provided by IRO-TOOL, then `#include <iro/shadow/schema_check.hpp>` to validate schema compatibility.
- `static_assert` size, alignment, and field offsets against generated constants.
- Use trait checks from `iro::freestanding` (`is_standard_layout_v`, `is_trivially_copyable_v`, `is_trivially_destructible_v`).
- Avoid C++ bitfields for kernel bitfields; use shim accessors instead.

Follow the padding/partial declaration guidance from IRO-CORE-SPEC-4.1 §4.3.
