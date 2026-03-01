#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# iro-probe-diag.sh — Probe compilation error diagnostic helper
#
# This script analyzes probe compilation errors and provides actionable
# suggestions. It can be used in two modes:
#
# 1. WRAPPER MODE: Wrap the compiler invocation
#    ./iro-probe-diag.sh gcc -c probe.c -o probe.o ...
#
# 2. ANALYZE MODE: Post-process saved error output
#    ./iro-probe-diag.sh --analyze errors.txt manifest.toml
#
# The script recognizes common error patterns from gcc/clang and suggests
# fixes specific to IRO manifests (missing headers, unknown types, etc.).

set -eu

VERSION="1.5.0"

# -----------------------------------------------------------------------------
# Error Pattern Database
# -----------------------------------------------------------------------------
# Each pattern has: regex, category, suggestion_template
# Suggestions can use {1}, {2}, etc. for capture groups

diagnose_error() {
    local line="$1"
    local manifest="$2"

    # Pattern: unknown type name 'foo'
    if printf '%s\n' "$line" | grep -qE "unknown type name '([^']+)'"; then
        type=$(printf '%s\n' "$line" | sed -nE "s/.*unknown type name '([^']+)'.*/\\1/p")
        cat <<EOF

┌─ IRO DIAGNOSTIC ────────────────────────────────────────────────────────────
│ ERROR: Unknown type '$type'
│
│ LIKELY CAUSE:
│   The type '$type' is used but not defined. This usually means a
│   required kernel header is missing from your manifest's includes list.
│
│ SUGGESTED FIX:
│   Add the appropriate header to your manifest (${manifest:-manifest.toml}):
│
│   includes = [
│       # ... existing includes ...
EOF
        case "$type" in
            pid_t|uid_t|gid_t|off_t|size_t|ssize_t|*_t)
                printf '│       "linux/types.h",  # for %s\n' "$type" ;;
            task_struct|thread_struct)
                printf '│       "linux/sched.h",  # for struct %s\n' "$type" ;;
            file|inode|dentry|super_block)
                printf '│       "linux/fs.h",     # for struct %s\n' "$type" ;;
            sk_buff|socket|sock)
                printf '│       "linux/skbuff.h", # for struct %s\n' "$type"
                printf '│       "net/sock.h",     # alternative for sock types\n' ;;
            mm_struct|vm_area_struct)
                printf '│       "linux/mm_types.h", # for struct %s\n' "$type" ;;
            cred)
                printf '│       "linux/cred.h",   # for struct %s\n' "$type" ;;
            list_head|hlist_head|hlist_node)
                printf '│       "linux/list.h",   # for struct %s\n' "$type" ;;
            spinlock_t|mutex|rwlock_t)
                printf '│       "linux/spinlock.h", # for %s\n' "$type" ;;
            atomic_t|atomic64_t)
                printf '│       "linux/atomic.h", # for %s\n' "$type" ;;
            ktime_t|timespec64|timeval)
                printf '│       "linux/time.h",   # for %s\n' "$type" ;;
            u8|u16|u32|u64|s8|s16|s32|s64|__u8|__u16|__u32|__u64)
                printf '│       "linux/types.h",  # for %s\n' "$type" ;;
            *)
                printf '│       # Search: grep -r "struct %s" include/linux/\n' "$type" ;;
        esac
        cat <<EOF
│   ]
│
│ DEBUGGING:
│   Run: grep -rn "struct $type" \$KERNEL_SRC/include/linux/ | head -5
└──────────────────────────────────────────────────────────────────────────────
EOF
        return 0
    fi

    # Pattern: 'struct foo' has no member named 'bar'
    if printf '%s\n' "$line" | grep -qE "'struct ([^']+)' has no member named '([^']+)'"; then
        struct=$(printf '%s\n' "$line" | sed -nE "s/.*'struct ([^']+)' has no member named.*/\\1/p")
        field=$(printf '%s\n' "$line" | sed -nE "s/.*has no member named '([^']+)'.*/\\1/p")
        cat <<EOF

┌─ IRO DIAGNOSTIC ────────────────────────────────────────────────────────────
│ ERROR: struct $struct has no member named '$field'
│
│ LIKELY CAUSES:
│   1. The field name is misspelled in your manifest
│   2. The field exists only with certain CONFIG_* options
│   3. The field was renamed or removed in this kernel version
│   4. The field is inside a nested anonymous struct/union
│
│ SUGGESTED FIXES:
│
│   Check field exists:
│     grep -A50 "struct $struct {" \$KERNEL_SRC/include/linux/*.h | grep "$field"
│
│   Check kernel config requirements:
│     grep -B5 "$field" \$KERNEL_SRC/include/linux/*.h | grep -i config
│
│   Update manifest (${manifest:-manifest.toml}):
│     [types.$struct]
│     c_type = "struct $struct"
│     fields = [
│         # "$field",  # <- verify this field name
│     ]
└──────────────────────────────────────────────────────────────────────────────
EOF
        return 0
    fi

    # Pattern: dereferencing pointer to incomplete type 'struct foo'
    if printf '%s\n' "$line" | grep -qE "incomplete type '(struct )?([^']+)'"; then
        struct=$(printf '%s\n' "$line" | sed -nE "s/.*incomplete type '(struct )?([^']+)'.*/\\2/p")
        cat <<EOF

┌─ IRO DIAGNOSTIC ────────────────────────────────────────────────────────────
│ ERROR: Incomplete type 'struct $struct'
│
│ LIKELY CAUSE:
│   The struct is forward-declared but not fully defined. The header that
│   defines the full struct is missing from your manifest's includes list.
│
│ SUGGESTED FIX:
│   Find and add the defining header:
│
│   grep -rn "^struct $struct {" \$KERNEL_SRC/include/
│
│   Then add it to your manifest includes = [...].
└──────────────────────────────────────────────────────────────────────────────
EOF
        return 0
    fi

    # Pattern: 'foo.h' file not found
    if printf '%s\n' "$line" | grep -qE "'([^']+\\.h)' file not found"; then
        header=$(printf '%s\n' "$line" | sed -nE "s/.*'([^']+\\.h)' file not found.*/\\1/p")
        cat <<EOF

┌─ IRO DIAGNOSTIC ────────────────────────────────────────────────────────────
│ ERROR: Header '$header' not found
│
│ LIKELY CAUSES:
│   1. Missing include path (-I flag) in the compiler invocation
│   2. Architecture-specific header not available for target arch
│   3. Header requires CONFIG_* option to exist
│
│ DEBUGGING:
│   Check header exists:
│     find \$KERNEL_SRC/include -name "$(basename "$header")"
│
│   Check arch-specific:
│     ls \$KERNEL_SRC/arch/\$ARCH/include/
│
│ NOTE: If building with Kbuild, ensure the kernel tree is properly
│       configured (make defconfig / make oldconfig).
└──────────────────────────────────────────────────────────────────────────────
EOF
        return 0
    fi

    # Pattern: implicit declaration of function 'offsetof'
    if printf '%s\n' "$line" | grep -qE "implicit declaration.*offsetof"; then
        cat <<EOF

┌─ IRO DIAGNOSTIC ────────────────────────────────────────────────────────────
│ ERROR: Implicit declaration of 'offsetof'
│
│ CAUSE: The probe is missing <stddef.h>.
│
│ FIX: This is an IRO internal error. The generated probe should include
│      <stddef.h> automatically. Please report this bug.
└──────────────────────────────────────────────────────────────────────────────
EOF
        return 0
    fi

    # No pattern matched
    return 1
}

# -----------------------------------------------------------------------------
# Main Logic
# -----------------------------------------------------------------------------

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] COMMAND...
       $(basename "$0") --analyze ERROR_FILE [MANIFEST]

Probe compilation error diagnostic helper for IRO.

OPTIONS:
  --analyze FILE   Analyze saved error output from FILE
  --help           Show this help
  --version        Show version

WRAPPER MODE:
  $(basename "$0") gcc -c probe.c -o probe.o ...

  Wraps the compiler, captures stderr, and if compilation fails,
  provides IRO-specific diagnostic suggestions.

ANALYZE MODE:
  $(basename "$0") --analyze build.log manifest.toml

  Analyzes a saved build log and provides suggestions.

EXAMPLES:
  # Wrap compiler during build
  IRO_CC_WRAPPER="./iro-probe-diag.sh" make

  # Analyze failed build output
  make 2>&1 | tee build.log
  ./iro-probe-diag.sh --analyze build.log mymanifest.toml
EOF
}

analyze_file() {
    local errfile="$1"
    local manifest="${2:-}"
    local found_errors=0

    while IFS= read -r line || [ -n "$line" ]; do
        if diagnose_error "$line" "$manifest"; then
            found_errors=1
        fi
    done < "$errfile"

    if [ "$found_errors" -eq 0 ]; then
        printf '\nNo recognized IRO-related errors found in the log.\n'
        printf 'If you believe this is an IRO issue, please report it with the full error output.\n'
    fi
}

wrapper_mode() {
    local errfile
    errfile=$(mktemp)
    trap 'rm -f "$errfile"' EXIT

    # Run the command, capturing stderr
    local rc=0
    "$@" 2> >(tee "$errfile" >&2) || rc=$?

    if [ "$rc" -ne 0 ]; then
        printf '\n' >&2
        printf '═══════════════════════════════════════════════════════════════════════════════\n' >&2
        printf ' IRO PROBE COMPILATION FAILED — Analyzing errors...\n' >&2
        printf '═══════════════════════════════════════════════════════════════════════════════\n' >&2

        # Try to find manifest from command line args
        local manifest=""
        for arg in "$@"; do
            case "$arg" in
                *.toml) manifest="$arg" ;;
            esac
        done

        analyze_file "$errfile" "$manifest" >&2
    fi

    exit "$rc"
}

# Parse arguments
case "${1:-}" in
    --help|-h)
        usage
        exit 0
        ;;
    --version)
        printf 'iro-probe-diag %s\n' "$VERSION"
        exit 0
        ;;
    --analyze)
        if [ -z "${2:-}" ]; then
            printf 'Error: --analyze requires a file argument\n' >&2
            exit 1
        fi
        analyze_file "$2" "${3:-}"
        exit 0
        ;;
    "")
        usage
        exit 1
        ;;
    *)
        wrapper_mode "$@"
        ;;
esac
