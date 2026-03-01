#
# IRO integration entrypoint for kernel Makefile
# Conforms to IRO-TOOL-SPEC-4.2
#
# This file is included from the kernel's top-level Makefile via:
#   -include $(srctree)/scripts/iro/kbuild/iro.mk
#
# It loads the IRO host tools, depcheck enforcement, and probe generation
# pipeline in a modular fashion.
#

IRO_ROOT := $(srctree)/scripts/iro
IRO_KBUILD_DIR := $(IRO_ROOT)/kbuild

# 1. Host tools: builds layout_parse, gen_probe, depcheck
include $(IRO_KBUILD_DIR)/iro.host.mk

# 2. Depcheck: header boundary enforcement for C++ code
include $(IRO_KBUILD_DIR)/iro.depcheck.mk

# 3. Probe generation: manifest → C → ELF → headers pipeline
include $(IRO_KBUILD_DIR)/iro.probe.mk
