#!/bin/sh
set -eu

# IRO apply script: overlay-only, no rsync, no git required.
# Usage:
#   ./tools/iro-apply.sh /path/to/linux
#
# Optional:
#   IRO_ENABLE_CONFIG=1  -> auto-enable CONFIG_IRO_CXX/STRICT if .config exists

fail() { echo "iro-apply: ERROR: $*" >&2; exit 1; }
info() { echo "iro-apply: $*" >&2; }

need_cmd() { command -v "$1" >/dev/null 2>&1 || fail "missing required command: $1"; }

need_cmd tar
need_cmd grep
need_cmd sed

KERNEL_DIR="${1:-}"
[ -n "$KERNEL_DIR" ] || fail "missing kernel dir argument"
[ -d "$KERNEL_DIR" ] || fail "kernel dir not found: $KERNEL_DIR"
[ -f "$KERNEL_DIR/Makefile" ] || fail "not a kernel tree (missing Makefile): $KERNEL_DIR"
[ -f "$KERNEL_DIR/Kconfig" ] || fail "not a kernel tree (missing Kconfig): $KERNEL_DIR"

# Resolve IRO repo root relative to this script
SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
IRO_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)"
OVERLAY="$IRO_ROOT/overlay"

[ -d "$OVERLAY" ] || fail "missing overlay directory: $OVERLAY"
[ -d "$OVERLAY/scripts/iro" ] || fail "missing overlay/scripts/iro"
[ -d "$OVERLAY/iro" ] || fail "missing overlay/iro"

info "Applying IRO overlay from: $OVERLAY"
info "Into kernel tree:          $KERNEL_DIR"

# --- 1) Overlay copy using tar pipeline (no rsync) ---
# This overwrites existing files under scripts/iro and iro/ (override-only).
( cd "$OVERLAY" && tar cf - scripts/iro iro ) | ( cd "$KERNEL_DIR" && tar xpf - )

# --- 2) Patch kernel Makefile: include IRO kbuild entrypoint ---
MAKEFILE="$KERNEL_DIR/Makefile"
INCLUDE_LINE='-include $(srctree)/scripts/iro/kbuild/iro.mk'
MARKER_BEGIN='# IRO-TOOL: begin auto-hook'
MARKER_END='# IRO-TOOL: end auto-hook'

if grep -Fq "$INCLUDE_LINE" "$MAKEFILE"; then
  info "Makefile already includes IRO hook."
else
  info "Installing Makefile hook."

  # Backup once (non-destructive)
  [ -f "$MAKEFILE.iro.bak" ] || cp "$MAKEFILE" "$MAKEFILE.iro.bak"

  # Append near end (least fragile across kernel versions)
  {
    echo ""
    echo "$MARKER_BEGIN"
    echo "$INCLUDE_LINE"
    echo "$MARKER_END"
  } >>"$MAKEFILE"
fi

# --- 3) Patch kernel Kconfig: source IRO Kconfig ---
KCONFIG="$KERNEL_DIR/Kconfig"
SOURCE_LINE='source "iro/Kconfig"'

if grep -Fq "$SOURCE_LINE" "$KCONFIG"; then
  info "Kconfig already sources iro/Kconfig."
else
  info "Installing Kconfig hook."

  [ -f "$KCONFIG.iro.bak" ] || cp "$KCONFIG" "$KCONFIG.iro.bak"

  {
    echo ""
    echo "$MARKER_BEGIN"
    echo "$SOURCE_LINE"
    echo "$MARKER_END"
  } >>"$KCONFIG"
fi

# --- 4) Optional: auto-enable configs if desired and possible ---
# This avoids manual menuconfig when .config already exists.
if [ "${IRO_ENABLE_CONFIG:-0}" = "1" ]; then
  if [ -f "$KERNEL_DIR/.config" ] && [ -x "$KERNEL_DIR/scripts/config" ]; then
    info "Enabling CONFIG_IRO_CXX and CONFIG_IRO_STRICT in existing .config"
    "$KERNEL_DIR/scripts/config" --enable IRO_CXX --enable IRO_STRICT
    # olddefconfig makes Kconfig settle dependencies deterministically
    if command -v make >/dev/null 2>&1; then
      make -C "$KERNEL_DIR" olddefconfig >/dev/null
    fi
  else
    info "Skipping config enable (need existing .config and scripts/config executable)."
  fi
fi

info "Done."
info "Next: build normally (e.g., make -C \"$KERNEL_DIR\" O=... olddefconfig && make -j)."
