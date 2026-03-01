<!-- IRO-TOOL-SPEC version: 4.2 -->
# IRO Tool Enhancement Specification

## Scope

This document describes the implemented behavior of the current `iro-tool` stack:
`layout_parse`, `gen_probe`, and `depcheck`.

## Version Axes

- Tool spec version: `4.2` (this document tag).
- Layout/note schema version: `4.2` (`kLayoutSchemaMajor=4`, `kLayoutSchemaMinor=2`).
- Manifest language schema version: `1.5` (`kManifestSchemaVersionString`).
- Tool binary versions: `1.5.0` (`kLayoutParseVersion`, `kGenProbeVersion`, `kDepcheckVersion`).

## Compiler Support

- Primary/tested compiler: Clang 18+.
- Courtesy compiler: GCC 14+.
- Warning flags are gated by compiler family:
  - Common flags applied to Clang and GCC.
  - Clang-only warnings applied only when `CMAKE_CXX_COMPILER_ID` matches `Clang`.
  - GCC builds must not fail due to unknown warning flags.

## DWARF and Enum Semantics

- DIE references use absolute `.debug_info` offsets.
- Reader validates DIE reference offsets against `.debug_info` size before lookup.
- ELF/DWARF object reads use bounds-checked `memcpy` paths (no struct `reinterpret_cast` reads).
- Enum extraction preserves signedness:
  - Model: `{ raw: uint64_t, is_signed: bool }`.
  - Negative signed values are retained as two's-complement payload plus signedness flag.
  - Emission:
    - Negative signed values: parenthesized `ll` literal.
    - Non-negative values: `ull` literal.

## Determinism

- Generated output remains deterministic for equivalent manifest/probe/probe-cmd inputs.
- Output files are updated with idempotent compare-and-replace semantics.

## Kbuild Integration Notes

- `CONFIG_IRO=y` with empty `iro-layout-y` emits a warning by default.
- `CONFIG_IRO_STRICT=y` upgrades the empty-layout condition to an error.
- Depcheck strict mode validates that the expected Kbuild hook (`rule_cc_o_cxx`) exists before override.
