# IRO Full-Lifecycle Integration Test Specification

## Document Classification

```
Identifier:        IRO-TEST-SPEC-4.1
Type:              Formal Technical Specification (Normative)
Domain:            End-to-end integration testing for IRO-TOOL + IRO-CORE in Linux kernel builds
Status:            Definitive / Production-Ready
Dependencies:      IRO-TOOL-SPEC-4.1, IRO-CORE-SPEC-4.1
Primary Target:    x86_64 kernel build + QEMU boot + deterministic pass/fail signal
Secondary Target:  aarch64 cross-build validation (compile-only; no QEMU boot required in v4.1)
Toolchains:        Target build: LLVM/Clang 18+; Host build: HOSTCC; Emulator: qemu-system-x86_64
Scope:             “Developer weapon” — fastest reliable end-to-end feedback loop for kernel C+++IRO
```

---

## §0 Executive Contract

IRO-TEST is the **authoritative end-to-end integration test** for the IRO stack. It validates the full lifecycle:

1. **Acquire** a Linux kernel tree.
2. **Apply** IRO overlay using a single `.sh` script (override-only; no manual steps).
3. **Configure** a kernel build with IRO enabled.
4. **Build** vmlinux/bzImage/modules, including:

   * IRO host tools (layout_parse/depcheck/gen_probe),
   * probe pipeline (manifest → probe → ELF note → generated headers),
   * shims,
   * at least one C++ translation unit consuming IRO-CORE and generated layouts.
5. **Verify** IRO determinism/integrity (`iro-verify`).
6. **Boot** the kernel in QEMU and run an in-kernel IRO integration test payload (ITest).
7. **Return** a deterministic PASS/FAIL result with captured artifacts for debugging.

IRO-TEST is designed as a **post-PhD correctness gate** (tight, explicit, deterministic), but remains **pragmatic and ergonomic**: it must be runnable by a developer with *one command* and produce actionable diagnostics.

---

## §1 Goals and Non-Goals

### §1.1 Goals

IRO-TEST SHALL provide:

**T1. True lifecycle validation**

* The test MUST exercise both:

  * **IRO-TOOL** (generation + boundary enforcement + determinism)
  * **IRO-CORE** (freestanding C++ compile/link + ABI consumption + runtime execution)

**T2. Developer ergonomics**

* A single entrypoint script MUST run the entire lifecycle with no manual edits.
* Failure output MUST be actionable (log markers + preserved artifacts).

**T3. Deterministic pass/fail**

* PASS/FAIL MUST be determined by a stable marker protocol plus a deterministic QEMU exit mechanism.

**T4. Minimal moving parts**

* The test MUST NOT depend on initramfs/busybox/userspace to pass.
* The test MUST NOT require KUnit or any additional kernel testing harness.

**T5. Strong boundary assertions**

* The test MUST prove the clean-room boundary is enforced (via a negative depcheck test).

**T6. Determinism and integrity**

* The test MUST validate `layout_parse --verify` (`make iro-verify`) and MUST include a tamper check.

### §1.2 Non-Goals

IRO-TEST SHALL NOT:

* benchmark performance
* provide coverage metrics
* validate kernel subsystems unrelated to IRO
* validate memory-safety semantics beyond IRO’s boundary guarantees
* require upstream kernel maintainer workflows (patch stacks, rebases, submodules)

---

## §2 Terminology

* **IRO Repo**: the standalone IRO repository containing overlay + scripts.
* **Kernel Under Test (KUT)**: the Linux kernel source tree used for a test run.
* **Overlay Apply**: override-only install of IRO files into KUT plus minimal Kbuild/Kconfig hooks.
* **Objtree**: the kernel build output directory (`O=`).
* **ITest**: the in-kernel IRO integration payload compiled into vmlinux.
* **Runner**: the `iro-itest.sh` top-level script coordinating the lifecycle.

---

## §3 Repository and Directory Structure

IRO-TEST assumes the IRO repository contains:

```text
iro-tool/ (or iro/)
├── overlay/
│   ├── scripts/iro/                     (IRO-TOOL host tools + kbuild)
│   └── iro/                             (IRO manifests, shims, abi headers, tests)
│       ├── layout/
│       ├── shim/
│       ├── include/iro/abi/
│       └── tests/                       (ITest payload and negative test sources)
├── tools/
│   ├── iro-apply.sh                     (override-only apply into KUT)
│   └── (optional helpers)
└── ci/
    └── iro-itest.sh                     (the single command runner)
```

### §3.1 Minimal Kernel Touch Points

IRO-TEST SHALL assume Overlay Apply modifies only:

* KUT top-level `Makefile` to include: `-include $(srctree)/scripts/iro/kbuild/iro.mk`
* KUT top-level `Kconfig` to include: `source "iro/Kconfig"`

No other manual modifications to kernel build files are permitted.

---

## §4 Runner Contract

### §4.1 Entry Point (Normative)

IRO-TEST MUST provide a single executable shell script:

* `ci/iro-itest.sh`

The runner MUST exit:

* `0` on PASS
* non-zero on FAIL

The runner MUST be idempotent:

* multiple invocations over the same KUT MUST not require manual cleanup
* re-application MUST be override-only (no uninstall required)

### §4.2 Inputs (Normative)

The runner MUST support:

* `IRO_KERNEL_DIR`: path to an existing KUT (optional)
* `IRO_KERNEL_GIT_URL`: repo URL to clone if `IRO_KERNEL_DIR` not provided (optional; default may be stable tree)
* `IRO_KERNEL_REF`: branch/tag/commit to check out (optional)
* `IRO_WORKDIR`: directory for caches/artifacts/build outputs (required default)
* `IRO_BUILD_DIR`: objtree output directory (optional; default inside workdir)
* `IRO_JOBS`: parallel jobs (optional)
* `IRO_TIMEOUT_SECS`: QEMU timeout (optional; default 90)
* `IRO_LLVM`: if set, runner SHALL pass `LLVM=1` to make (recommended)

The runner MUST not require interactive input.

### §4.3 Environment Validation (Normative)

The runner MUST validate prerequisites before starting:

* `make`
* `clang` (target compiler)
* `ld.lld` (or toolchain suitable for `LLVM=1` builds)
* `qemu-system-x86_64`
* standard Unix tools: `tar`, `grep`, `sed`, `timeout` (or equivalent)

If a prerequisite is missing, runner MUST fail early with a diagnostic naming the missing tool.

---

## §5 Lifecycle Pipeline

### §5.1 Acquire Kernel Under Test (KUT)

If `IRO_KERNEL_DIR` is provided:

* runner SHALL use it as KUT

Otherwise:

* runner SHALL clone a KUT into `${IRO_WORKDIR}/linux`
* runner SHOULD use shallow clone for speed:

  * `git clone --depth 1 --branch <ref> <url> <dir>`

The runner MUST print:

* kernel source path
* kernel ref or commit id (best-effort)

### §5.2 Apply IRO Overlay (Override-Only)

Runner MUST execute:

* `tools/iro-apply.sh <KUT>`

Postconditions (Normative):

* `KUT/scripts/iro/` exists
* `KUT/iro/` exists
* KUT `Makefile` contains the IRO include hook
* KUT `Kconfig` sources `iro/Kconfig`

If any postcondition fails, runner MUST stop with an error.

### §5.3 Configure Kernel Build

Runner MUST:

1. run a baseline config:

   * `make -C <KUT> O=<objtree> LLVM=1 defconfig` (x86_64 default profile)
2. enable IRO-required configs using `scripts/config` if present:

   * `CONFIG_IRO_CXX=y`
   * `CONFIG_IRO_STRICT=y`
   * `CONFIG_IRO_ITEST=y`

Then:

* `make -C <KUT> O=<objtree> LLVM=1 olddefconfig`

**Opinionated rule (Normative):** IRO-TEST uses a deterministic config base (`defconfig + IRO toggles`). It MUST NOT depend on developer’s local kernel config.

### §5.4 Build Phase

Runner MUST build:

* `bzImage` (or equivalent bootable kernel artifact)
* vmlinux (implicitly)
* required IRO host tools (hostprogs)
* required shims and ITest payload

Example (x86_64):

* `make -C <KUT> O=<objtree> LLVM=1 -j<N> bzImage`

Build MUST include:

* at least one layout set generation (probe pipeline executed)
* at least one C++ TU (IRO-CORE consumers) built into vmlinux

### §5.5 Verification Phase

Runner MUST run:

* `make -C <KUT> O=<objtree> LLVM=1 iro-verify`

This step is normative gating for IRO-TOOL determinism and drift detection.

---

## §6 ITest Payload Contract (In-Kernel Runtime Validation)

### §6.1 Purpose

ITest is the minimal runtime proof that:

* the kernel boots with IRO present,
* at least one C++ TU using IRO-CORE executes in-kernel,
* shim boundary calls work,
* formatting/logging path works,
* the build-time layout assertions were actually compiled.

### §6.2 Build Integration (Normative)

ITest SHALL be compiled into vmlinux when `CONFIG_IRO_ITEST=y`.

ITest SHOULD be implemented as a `late_initcall` or equivalent initcall that executes after core subsystems are available.

ITest MUST NOT require userland to execute.

### §6.3 Required Capabilities (Normative)

The ITest C++ translation unit MUST:

**(A) Prove IRO-CORE compilation and usage**

* include `<iro/iro.hpp>`
* instantiate and use at least:

  * `iro::expected` OR `iro::optional`
  * `iro::span`
  * one owning type: `iro::mem::box<T>` or `iro::mem::boxed_slice<T>`
* call `iro::io::log(...)` at least once with a compile-time validated format string.

**(B) Prove IRO-TOOL generated layouts are consumed**

* include at least one generated layout header:

  * `<generated/iro/layout_<set>.hpp>`
* include at least one shadow header with static ABI asserts enabled, OR include at least one direct static assertion against a generated constant.

**(C) Prove shim ABI calls are live**

* invoke at least one ABI helper from Core’s ABI headers:

  * `iro_gfp_kernel()` and/or `iro_kmalloc_minalign()`, and/or
  * `iro_is_err()/iro_ptr_err()/iro_err_ptr()`, and/or
  * `iro_printk_level()` via Core logging path

**(D) Provide deterministic PASS/FAIL markers**

* print required markers to the kernel console (serial), per §7.

### §6.4 Test-only QEMU Exit Mechanism (Normative for x86_64)

To avoid indefinite QEMU runs, ITest MUST terminate QEMU deterministically using the “debug exit” device on x86_64.

This SHALL be implemented via a **test-only C shim** that is compiled only under:

* `CONFIG_IRO_ITEST=y`
* `CONFIG_X86_64=y` (or equivalent)

The shim SHALL:

* perform an I/O port write to the debug-exit port (default `0xF4`)

The C++ ITest SHALL call:

* `iro_qemu_exit(PASS_CODE)` on pass
* `iro_qemu_exit(FAIL_CODE)` on fail

QEMU is configured with:

* `-device isa-debug-exit,iobase=0xf4,iosize=0x04`

**Note:** IRO-TEST does not normatively define the host process exit code mapping. PASS/FAIL is defined by markers (§7) plus “QEMU terminated without timeout.”

---

## §7 Marker Protocol (Normative)

ITest MUST print stable, machine-parseable markers to the serial console:

* `IRO_ITEST_BEGIN`
* `IRO_ITEST_PASS`
* `IRO_ITEST_FAIL:<reason>`

Rules:

* `IRO_ITEST_BEGIN` MUST appear before any PASS/FAIL.
* Exactly one terminal marker MUST appear: PASS or FAIL.
* On FAIL, `<reason>` MUST be a short ASCII token (no spaces), e.g.:

  * `layout_assert`
  * `abi_errptr`
  * `alloc_fail`
  * `unexpected`

The runner MUST parse QEMU output:

* PASS requires `IRO_ITEST_PASS` present
* FAIL occurs if:

  * `IRO_ITEST_FAIL:` is present, OR
  * QEMU times out, OR
  * build/verify phase fails

---

## §8 QEMU Boot Contract (x86_64)

### §8.1 QEMU Invocation Requirements (Normative)

Runner MUST boot the built kernel with:

* serial console enabled (`console=ttyS0`)
* no graphical window (`-nographic`)
* no reboot loops (`-no-reboot`)
* debug-exit device configured

Runner MUST enforce a timeout (default 90 seconds).

### §8.2 Kernel Command Line (Recommended Baseline)

Runner SHOULD use:

* `console=ttyS0`
* `earlycon=uart,io,0x3f8,115200` (optional)
* `loglevel=7`
* `panic=-1` (or similar) to avoid reboot loops on panic, but test expects debug-exit path

---

## §9 Negative Tests (Required)

IRO-TEST MUST include at least these negative tests, executed by the runner as distinct phases.

### §9.1 Depcheck Negative Test (Required)

Purpose: prove the clean-room boundary enforcement is real.

Mechanism (Normative):

* Build a dedicated C++ translation unit that includes a forbidden kernel header (e.g., `<linux/sched.h>`).
* The build MUST fail with depcheck reporting a forbidden include.

Constraints:

* The negative test MUST NOT taint the main build graph permanently.
* It SHOULD be implemented as a dedicated make target:

  * `make iro-depcheck-negtest`
  * expected to fail; runner treats failure as PASS for the negtest phase only if depcheck violation is observed.

### §9.2 Tamper Test for `iro-verify` (Required)

Purpose: prove `iro-verify` detects drift.

Mechanism (Normative):

1. After a successful build, runner SHALL modify (tamper) one generated layout header in objtree, e.g.:

   * append a newline comment, or change one macro value.
2. Runner SHALL run `make iro-verify` again.
3. The second verify MUST fail.

Runner MUST restore/rebuild after tamper:

* either by rebuilding the set (preferred),
* or by discarding the objtree for the next run.

---

## §10 Determinism Test (Recommended)

Purpose: ensure “no-op rebuild does not regenerate outputs.”

Mechanism (Recommended):

* compute hashes of:

  * `O/include/generated/iro/layout_*.h`
  * `O/include/generated/iro/layout_*.hpp`
  * `O/include/generated/iro/layout_*.meta`
* run a second identical build step (`make bzImage` or `make all`)
* recompute hashes; they MUST match

This is recommended (not required) because it can be sensitive to toolchain noise, but should be used in CI where environment is stable.

---

## §11 Cross-Architecture Validation (Recommended)

### §11.1 aarch64 Compile-only (Recommended)

IRO-TEST SHOULD include a compile-only job:

* x86_64 host builds `ARCH=arm64` with `LLVM=1` and IRO enabled
* It MUST exercise:

  * probe compilation producing aarch64 ELF `.o`
  * host parsing of foreign-arch ELF note
  * header generation
  * `iro-verify` (build-time)

No QEMU boot required in v4.1.

---

## §12 Artifact Collection and Reporting (Required)

Runner MUST produce an artifact directory containing:

* `build.log`
* `verify.log`
* `qemu.log`
* `neg_depcheck.log`
* `tamper_verify.log` (or merged verify log with phase markers)
* a copy of at least:

  * one `layout_<set>.probe.cmd`
  * one generated header `layout_<set>.h/.hpp`
  * one `.meta`

On failure, runner MUST print a short summary:

* which phase failed (ACQUIRE/APPLY/CONFIG/BUILD/VERIFY/BOOT/NEGTEST/TAMPER)
* paths to logs
* last ~50 lines of relevant log (recommended)

---

## §13 Security and Hardening (Recommended)

Host tools (`layout_parse`, `depcheck`) SHOULD be built and tested under hardening/sanitizers in CI:

* address sanitizer builds for host tools (separate CI job)
* unit tests for malformed note handling (recommended adjunct test suite)

The primary IRO-TEST runner remains a functional integration harness and does not require sanitizers to pass.

---

## §14 Acceptance Criteria (Production-Ready Gate)

IRO-TEST is accepted when:

1. **One-command run** succeeds on the baseline kernel ref:

   * acquire → apply → configure → build → iro-verify → QEMU PASS.
2. **Depcheck negative** test fails for the right reason and is recognized as a negtest PASS.
3. **Tamper test** reliably causes `iro-verify` failure.
4. **Artifacts** are produced and sufficient to debug failures without re-running.
5. **No manual steps** are required at any point.

---

## Appendix A — Recommended Make Targets (Illustrative)

These targets SHOULD exist in the IRO Kbuild integration:

* `iro-verify` (required by IRO-TOOL-SPEC)
* `iro-depcheck-negtest` (required by this spec)
* `iro-itest` (optional: alias for `bzImage` build + verify + run; runner still remains canonical)

---

## Appendix B — Example QEMU Invocation (Illustrative)

```bash
qemu-system-x86_64 \
  -machine accel=tcg \
  -cpu qemu64 \
  -m 1024 \
  -kernel O/arch/x86/boot/bzImage \
  -append "console=ttyS0 earlycon=uart,io,0x3f8,115200 loglevel=7 panic=-1" \
  -nographic -no-reboot \
  -device isa-debug-exit,iobase=0xf4,iosize=0x04
```

---

## Appendix C — Example Marker Output (Illustrative)

```
[    0.000000] ... boot messages ...
[    2.314159] IRO_ITEST_BEGIN
[    2.314200] IRO: schema=4.1 tool_minor>=1 core=4.1
[    2.314250] IRO_ITEST_PASS
```

---

*End of Specification (IRO-TEST-SPEC-4.1)*

