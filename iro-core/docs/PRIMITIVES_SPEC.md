<!-- IRO-CORE-SPEC version: 4.1 -->
# IRO Core Primitives Specification

## Scope

This document captures the implemented guarantees of `iro-core` primitives in the
current pre-GA delivery.

## Core Spec Version

- Core spec version: `4.1` (matches `iro-core/VERSION` and `iro/version.hpp`).

## Safety Guarantees (Freestanding)

- `expected<T, E>`:
  - Cross-arm assignment uses construct-before-destroy staging.
  - Discriminator flips only after successful destination-arm construction.
  - `emplace` keeps prior state if argument construction fails.
  - Requires nothrow move construction of both arms.
- `optional<T>`:
  - `emplace` sets `m_has` only after successful construction.
  - Failed `emplace` leaves object disengaged.
- `move_if_noexcept`:
  - Returns `const T&` when move may throw and copy exists.
  - Returns `T&&` otherwise.
- `boxed_slice<T>`:
  - Partial element construction failure destroys already-constructed elements.
  - Raw allocation is released on construction failure.
  - `noexcept` contract is conditional on element construction.
  - `-fno-exceptions` builds require nothrow element construction.

## Traits and Utility Surface

- Added builtin-backed trait coverage for construction and noexcept assignment checks:
  - `is_constructible`, `is_nothrow_constructible`
  - `is_copy_constructible`, `is_nothrow_copy_constructible`
  - `is_move_constructible`, `is_nothrow_move_assignable`
- `is_nothrow_swappable<T>` reflects the freestanding `swap` implementation contract.

## Macro Ownership

- `config.hpp` owns branch prediction/compiler-interface macros.
- `annotations.hpp` owns static-analysis annotations and includes `config.hpp`.
- `IRO_LIKELY`/`IRO_UNLIKELY` are defined only once.
