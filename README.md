# iro-linux

> **v0.1 — pre-alpha. Everything is subject to change. Do not use in production.**

`iro-linux` is a Linux-kernel interop workspace for using modern C++ (C++23, freestanding) in kernel-adjacent code with explicit ABI/layout contracts.

## Status

**Pre-alpha (v0.1).** This project is under active development. APIs, file formats, generated output, and internal conventions are all unstable and will change without notice. There are no compatibility guarantees of any kind.

What works today may break tomorrow. If you evaluate this project, pin to a specific commit.

## Structure

- `iro-core` — C++23 freestanding library: type traits, containers (`expected`, `optional`, `span`, `box`, `boxed_slice`), error handling, formatting, logging, and ABI boundary headers for kernel interop.
- `iro-tool` — Host-side code generation and validation tools (`gen_probe`, `layout_parse`, `depcheck`), IML manifest parser, Kbuild integration, and kernel overlay scripts.
- `iro-test` — Integration test specification (spec only; runner not yet implemented).

## Why this exists

Linux kernel internals are C-first and change across versions and configurations. `iro-linux` provides a deterministic build-time pipeline to:

1. declare required layout contracts in IML manifests,
2. probe kernel struct layouts via generated C translation units,
3. parse the resulting ELF/DWARF data,
4. generate validated C/C++ headers with size, alignment, and offset guarantees,
5. enforce header dependency boundaries so C++ code never includes raw kernel headers.

## Build and test (iro-tool)

Requires Clang 18+ and CMake. GCC 14+ may build but is not the primary toolchain.

```bash
cd iro-tool
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## License

**GNU General Public License v2.0 only** (`GPL-2.0-only`), aligned with Linux kernel licensing.

See `LICENSE` (or `COPYING`) for full terms.
