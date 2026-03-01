# IRO Pre-GA Technical Remediation Plan

## 0. Scope and Constraints

This plan addresses the validated defects and design gaps in `iro-core` and `iro-tool` with pre-GA freedom to break APIs.

Hard constraints:
1. Correctness and memory safety are non-negotiable.
2. Performance regressions in hot paths (`layout_parse`, `dwarf_reader`) are unacceptable unless traded for explicit security guarantees and then clawed back.
3. Cohesiveness over patchwork: remove parallel patterns and inconsistent semantics.
4. DRY and SOLID are mandatory in implementation, not aspirational.
5. No compatibility shims for pre-GA behavior unless they reduce risk without long-term maintenance cost.

---

## 1. Five-Expert Design Conversation (Technical Deliberation)

Participants:
1. **Ari (Architecture/Abstractions)**
2. **Mina (C++ Correctness/Exception Safety)**
3. **Rook (Binary Parsing/Security Hardening)**
4. **Tess (Toolchain/Build Integration)**
5. **Vale (Verification/Performance Engineering)**

### Round 1: `unexpected`/`make_unexpected` and `IRO_TRY` are fundamentally broken

**Ari**: We currently have an API surface that *appears* ergonomic (`make_unexpected`, `IRO_TRY`) but fails for ordinary lvalue error expressions. This is a cohesion failure: users get two error propagation idioms, and one is unusable.

**Mina**: Root causes are type-system level:
1. `unexpected<E>` permits `E` to become reference-qualified (`E = const errc&`, `E = errc&`).
2. Constructor overload set in `unexpected<E>` collapses for reference instantiations.
3. `make_unexpected(E&&)` does not decay `E`.
4. `IRO_TRY` returns `unexpected<decltype(error())>`, which is reference type.

**Rook**: This is not cosmetic; it prevents clean fail-fast paths and incentivizes ad-hoc manual branching in parser code.

**Tess**: If we keep macros but they are broken, downstreams will create local alternatives. That is anti-DRY and fragments style.

**Vale**: Performance concerns are irrelevant here: all fixes are compile-time typing changes.

**Decision**:
1. Make `unexpected<T>` **value-type only** (`static_assert(!is_reference<T>)`).
2. Change factory to `make_unexpected(E&&) -> unexpected<remove_cvref_t<E>>`.
3. Refactor macros to return `unexpected<remove_cvref_t<decltype(_res.error())>>`.
4. Keep macros for now (breaking-free pre-GA), but route logic through one internal helper to avoid duplicated macro semantics.

**Why this is SOTA**:
- Matches modern `expected`-style APIs where error objects are owned values.
- Eliminates reference-instantiation hazards and dangling propagation risks.

**What we explicitly reject**:
- Supporting `unexpected<E&>` by special-case partials. Too brittle, violates ownership clarity.

---

### Round 2: `try_new`/`iro_new` noexcept contract violation and termination behavior

**Mina**: `try_new` is `noexcept` but calls potentially throwing constructors. In exception-enabled builds this terminates and leaks allocated raw memory. API says “returns `expected`”, implementation says “may abort”. This is contract fraud.

**Ari**: We need one coherent policy for constructor failure across allocator wrappers.

**Rook**: For kernel/freestanding context we usually compile without exceptions, but host tests compile with exceptions. This duality must be encoded explicitly, not assumed.

**Tess**: Policy must compile cleanly in both modes.

**Vale**: We should prefer deterministic behavior:
1. If exceptions disabled: static constraints enforce nothrow construction.
2. If exceptions enabled: catch, free, return explicit error.

**Decision**:
1. `try_new<T>`:
   - `#if __cpp_exceptions`: construct in `try` block; on throw, `kfree(raw)` and return `unexpected(err::einval)`.
   - `#else`: `static_assert(is_nothrow_constructible<T, Args...>)`.
2. `iro_new<T>`:
   - stays pointer API but implemented via `try_new<T>` for DRY semantics.
   - returns `nullptr` on any failure path.
3. Add explicit policy comment in header: constructor exceptions map to `einval` (pre-GA simplification).

**Why this is SOTA**:
- Explicitly models build-mode differences and preserves resource safety in both modes.
- Single code path for allocation semantics reduces divergence risk.

**What we explicitly reject**:
- Silent `std::terminate` behavior hidden behind `noexcept`.
- Duplicate error-handling logic in `iro_new` and `try_new`.

---

### Round 3: `expected`/`optional` transition safety and invariants

**Mina**: `expected` cross-arm assignment and `optional::emplace` state updates are mostly repaired, but we still need a hard invariant model and mechanized test coverage.

**Ari**: Define invariant as first-class API contract:
1. Discriminator reflects constructed arm state.
2. No destructor call on uninitialized storage.
3. Cross-arm transition is commit-style (construct-before-destroy or reconstruct-from-staged-temp with guaranteed no-throw second step).

**Vale**: We should use deterministic probe types in tests and run with ASan/UBSan. Cover copy-throw, move-throw, emplacement-throw matrices.

**Decision**:
1. Add invariant comments in `expected.hpp` and `optional.hpp` near assignment/emplace.
2. Ensure all cross-arm paths use staged temporary + post-construction discriminator flip.
3. Add targeted tests:
   - `expected`: value->error throw on copy, error->value throw on copy/move.
   - `optional`: emplace throw retains disengaged state.
4. Add compile-time no-throw constraints only where algorithm requires it.

**Why this is SOTA**:
- Strong exception-safety model is explicit and tested under fault injection.

---

### Round 4: DWARF parser overflow hardening and arithmetic consistency

**Rook**: `layout_parse` already uses checked arithmetic utilities; `dwarf_reader` has many raw `size_t` additions/multiplications. That inconsistency is a vulnerability class.

**Ari**: This violates DRY: we already have hardened helpers (`checked_add`, `checked_mul`).

**Vale**: Hardening must not crater performance. Use inline helpers and avoid repeated branching in tight loops where bounds are already prevalidated.

**Mina**: We can still enforce a pattern: boundary arithmetic in one function, then tight loop over proven ranges.

**Decision**:
1. Introduce local thin wrappers in `dwarf_reader.cc`:
   - `checked_off_add(size_t base, size_t delta, std::string_view what)`
   - `checked_off_mul(size_t a, size_t b, std::string_view what)`
   delegating to common checked helpers.
2. Replace all raw critical arithmetic in section/relocation/unit boundary computations.
3. Add malformed-fixture tests for integer wrap and out-of-range relocation references.
4. Preserve single-pass parsing and no dynamic bounds-map overhead.

**Why this is SOTA**:
- Parser hardening via systematic checked arithmetic is modern best practice.
- Uniform error surface improves diagnosability and auditability.

---

### Round 5: Enum signedness inconsistency (`extract_all` vs explicit values)

**Ari**: Current behavior is incoherent: explicit `values=[NEG]` rejects negatives, while `extract_all=true` handles signed enumerators.

**Mina**: This comes from `gen_probe` static assert forcing `(__uint128_t)(expr) <= UINT64_MAX`. Signed negatives fail by design.

**Rook**: Tool semantics should be consistent across extraction modes.

**Tess**: We can break output schema pre-GA. If needed, add explicit signedness bit in note record.

**Vale**: Preferred architecture:
1. Represent enum record payload as raw 64-bit two’s complement + signedness flag.
2. Stop performing unsigned-range-only assert.

**Decision**:
1. Extend enum record semantics:
   - `offset_or_id`: raw bits.
   - `flags`: add `F_SIGNED_ENUM_VALUE`.
2. `gen_probe`:
   - emit enum value as `(int64_t)` raw cast path when needed.
   - remove unsigned-only static assert.
3. `layout_parse` model:
   - preserve signedness from flags for explicit values and DWARF-extracted values uniformly.
4. Emission:
   - negative emits `(...ll)`, non-negative emits `...ull`.
5. Add fixtures for explicit negative enumerators.

**Why this is SOTA**:
- Unified semantic model independent of source path.
- Removes brittle policy branch.

---

### Round 6: Build/test ergonomics and anti-fragility

**Tess**: `build.sh` compiler fallback is wrong and can silently fail portability goals.

**Vale**: Tests pass now, but they miss broken API surfaces. We need “compile-fail to compile-pass” regressions integrated.

**Decision**:
1. Fix compiler fallback assignments in host script.
2. Add compile-only tests for:
   - `make_unexpected` with lvalue/rvalue errors.
   - `IRO_TRY`, `IRO_TRY_MAP`, `IRO_TRY_VOID` in realistic functions.
3. Add test that `try_new` with throwing ctor does not terminate and returns error (exception-enabled host run).
4. Keep deterministic integration tests; add new enum explicit-negative fixture.

---

## 2. Architecture Decisions (ADR Summary)

### ADR-001: Error value ownership is mandatory
- `unexpected<E>` stores owned `E`, never references.
- All propagation APIs decay types before instantiation.

### ADR-002: Allocation wrappers have mode-explicit constructor-failure semantics
- Exceptions enabled: catch/cleanup/return error.
- Exceptions disabled: compile-time nothrow requirement.

### ADR-003: Parser boundary arithmetic is always checked
- No raw `size_t` arithmetic for offset/size boundaries.
- One checked arithmetic idiom across toolchain code.

### ADR-004: Enum representation is signedness-aware end-to-end
- Signedness carried in record flags.
- Explicit and `extract_all` paths produce identical semantics.

### ADR-005: DRY error propagation implementation
- Macros are thin syntax over common helper semantics.
- No duplicate propagation logic per macro body.

---

## 3. Concrete Implementation Program (File-by-File)

## 3.1 `iro-core/overlay/iro/include/iro/freestanding/expected.hpp`

Changes:
1. Add `is_reference` trait usage guard in `unexpected<E>`:
   - `static_assert(!is_reference<E>::value, "unexpected<E> requires value type");`
2. Keep constructors, but now guaranteed non-reference type.
3. Update `make_unexpected` return type to decayed type (`remove_cvref_t<E>`).
4. Add internal helper alias `unexpected_decay_t<E>` for readability.
5. Ensure assignment/emplace invariants are documented inline.

Acceptance criteria:
1. `make_unexpected(err::einval)` compiles.
2. `make_unexpected(errc{})` compiles.
3. No instantiation of `unexpected<T&>` or `unexpected<const T&>` possible.

## 3.2 `iro-core/overlay/iro/include/iro/freestanding/try.hpp`

Changes:
1. Introduce helper macro/type alias using decayed error type.
2. Replace all `unexpected<decltype(_iro_try_result.error())>` with decayed equivalent.
3. Centralize helper in namespace:
   - `template<class X> using decay_error_t = remove_cvref_t<X>;`
4. Keep statement-expression strategy (pre-GA accepted GNU extension), but with one semantic path.

Acceptance criteria:
1. Macros compile in real `expected<int, errc>` function contexts.
2. No reference-based `unexpected` return instantiation.

## 3.3 `iro-core/overlay/iro/include/iro/mem/alloc.hpp`

Changes:
1. Reimplement `try_new` constructor block:
   - exception mode: `try { new (...) T(...) } catch (...) { kfree(raw); return unexpected(einval); }`
   - no-exception mode: static assert nothrow constructible.
2. Reimplement `iro_new` via `try_new`:
   - `auto r = try_new<T>(...); return r.has_value() ? r.value() : nullptr;`
3. Keep alignment and allocation failure semantics unchanged.

Acceptance criteria:
1. Throwing constructor no longer terminates process in host exception builds.
2. No memory leak on constructor throw.
3. `-fno-exceptions` build continues to compile with nothrow-constructible types.

## 3.4 `iro-tool/overlay/scripts/iro/layout_parse/dwarf_reader.cc`

Changes:
1. Replace raw arithmetic at these classes of sites:
   - relocation entry offset computation
   - symbol entry offset computation
   - section header table access
   - section bounds checks
   - unit end computation
   - `.debug_str_offsets` table indexing
2. Use checked helper wrappers everywhere boundary arithmetic occurs.
3. Keep tight loops unchanged once range proven.

Acceptance criteria:
1. Existing fixtures pass unchanged.
2. New malformed overflow fixtures fail with deterministic `ToolError` messages.
3. No sanitizer issues.

## 3.5 `iro-tool/overlay/scripts/iro/gen_probe/gen_probe.cc`

Changes:
1. Remove unsigned-only enum range static assert logic.
2. Emit enum records with signedness-aware encoding:
   - raw two’s complement in `offset_or_id`
   - set enum signedness flag where needed.
3. Update comments/spec references in file to new contract.

Acceptance criteria:
1. Explicit negative enum values compile and emit.
2. Positive enum values unchanged.

## 3.6 `iro-tool/overlay/scripts/iro/layout_parse/layout_parse.cc`

Changes:
1. Parse enum signedness flag from descriptor records.
2. Preserve signedness into model for explicit enum values.
3. Keep DWARF path behavior consistent with explicit path.
4. Emission logic uses signedness uniformly.

Acceptance criteria:
1. Output for explicit negative values matches `extract_all` signed style.
2. Determinism tests remain byte-stable for unchanged fixtures.

## 3.7 `iro-core/overlay/iro/tests/host/build.sh`

Changes:
1. Fix compiler fallback assignment logic (correct variable replacement when clang missing).
2. Add compile-and-run test binary for throwing `try_new` constructor case.
3. Add compile-only checks for `make_unexpected` and `IRO_TRY` macros.

Acceptance criteria:
1. Script works on machines with GCC-only toolchain.
2. New regression checks prevent reintroduction of API breakages.

## 3.8 Test fixtures

Additions:
1. `iro-tool/tests/fixtures/negative_enum_explicit/*`:
   - explicit `values=[NEG, POS]` with negative enumerator.
2. malformed DWARF arithmetic fixtures (or synthetic corrupted object injection in cmake script).

Acceptance criteria:
1. `integration_dwarf_fixtures` validates explicit-negative and extract_all parity.
2. malformed object cases fail safely.

---

## 4. Dependency Graph and Parallel Work Plan

Parallelizable tracks:
1. **Track A (Core typing + propagation)**
   - `expected.hpp` + `try.hpp` + compile-only regressions.
2. **Track B (Allocation semantics)**
   - `alloc.hpp` + throwing-ctor host tests.
3. **Track C (DWARF hardening)**
   - `dwarf_reader.cc` + malformed fixtures.
4. **Track D (Enum semantics)**
   - `gen_probe.cc` + `layout_parse.cc` + explicit-negative fixture.
5. **Track E (Build script portability)**
   - `build.sh` fallback fix.

Merge order:
1. A, B, E (independent)
2. C
3. D (touches note semantics and parser/emitter consistency)
4. global stabilization run

---

## 5. Detailed Validation Matrix

## 5.1 Core compile/runtime matrix

Compilers:
1. Clang 18+ (`-std=c++23`)
2. GCC 14+ (`-std=c++23`)

Modes:
1. exceptions on
2. `-fno-exceptions`

Checks:
1. `make_unexpected` lvalue/rvalue cases compile.
2. `IRO_TRY` macro usage compile in real function returns.
3. throwing ctor through `try_new` returns error (exceptions on).
4. no-exceptions mode compiles for nothrow types.

## 5.2 Tool integration matrix

1. Existing deterministic emission test passes.
2. Existing DWARF fixtures pass.
3. New explicit-negative enum fixture passes.
4. malformed-overflow fixtures fail with non-crash deterministic diagnostics.

## 5.3 Sanitizer/security checks

1. ASan + UBSan on `iro-tool` tests.
2. Host core tests under ASan where supported.
3. Ensure no OOB read/write from crafted section offsets in new malformed fixtures.

---

## 6. Performance and Cohesiveness Gates

## 6.1 Performance gates

1. `integration_dwarf_fixtures` wall-clock must not regress >10% median on same machine.
2. `layout_parse` for multi-unit fixture must remain single-pass without additional map scans beyond existing structure lookups.

## 6.2 Cohesiveness gates

1. No duplicated error propagation semantics between `make_unexpected` and macros.
2. No raw boundary arithmetic left in DWARF parser boundary checks.
3. Enum signedness behavior identical between explicit and extract_all flows.

---

## 7. Intentional Breaking Changes (Pre-GA)

1. `unexpected<E>` no longer accepts reference types (compile-time break by design).
2. `make_unexpected` now always decays argument type.
3. Enum descriptor semantics gain signedness flag usage for explicit path; old outputs are not compatibility targets.

Rationale: these breaks eliminate invalid states and semantic incoherence early, before GA lock-in.

---

## 8. Risks and Mitigations

1. **Risk**: enum flag integration drifts between generator and parser.
   - **Mitigation**: add fixture comparing explicit and extract_all outputs for same enum.

2. **Risk**: checked arithmetic introduces noisy error messages.
   - **Mitigation**: standardize `what` strings; include offset context in fatal paths.

3. **Risk**: macro refactor changes return type deduction edge cases.
   - **Mitigation**: compile-only tests for const/non-const lvalue, rvalue, mapped errors.

4. **Risk**: allocation policy changes error code mapping expectations.
   - **Mitigation**: document constructor-failure mapping and keep one code path via `try_new`.

---

## 9. Definition of Done (Non-Negotiable)

1. All defects in sections 1-6 have code fixes and regression tests.
2. `ctest --test-dir iro-tool/build* --output-on-failure` passes.
3. `iro-core` host test script passes with both Clang and GCC where available.
4. New compile-only regression tests prevent reintroduction of broken `unexpected`/`IRO_TRY` behavior.
5. No known unchecked boundary arithmetic remains in DWARF section/offset computations.
6. Enum signedness semantics are coherent across explicit and extract_all paths.
7. No new TODO/HACK/workaround comments introduced.

---

## 10. Immediate Execution Checklist

1. Patch `expected.hpp` (`unexpected` constraints + decayed factory).
2. Patch `try.hpp` (decayed error return macros).
3. Patch `alloc.hpp` (mode-explicit constructor failure handling + DRY through `try_new`).
4. Add core regression tests (compile-only + throwing ctor runtime).
5. Patch `dwarf_reader.cc` checked arithmetic sweep.
6. Add malformed DWARF overflow fixtures/tests.
7. Patch enum handling in `gen_probe.cc` + `layout_parse.cc`.
8. Add explicit-negative enum fixture.
9. Fix `build.sh` compiler fallback.
10. Run full validation matrix and lock baseline timings.

