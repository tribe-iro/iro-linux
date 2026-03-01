# IRO host-side tools Kbuild integration
# Builds layout_parse, gen_probe, and depcheck as host programs

hostprogs-$(CONFIG_IRO_CXX)   += layout_parse depcheck gen_probe

layout_parse-objs := scripts/iro/layout_parse/layout_parse.o \
                     scripts/iro/layout_parse/dwarf_reader.o
depcheck-objs     := scripts/iro/depcheck/depcheck.o
gen_probe-objs    := scripts/iro/gen_probe/gen_probe.o

# Tool paths for use by other Makefiles
IRO_LAYOUT_PARSE := $(objtree)/scripts/iro/layout_parse
IRO_DEPCHECK     := $(objtree)/scripts/iro/depcheck
IRO_GEN_PROBE    := $(objtree)/scripts/iro/gen_probe

# IRO host tools are C++23 per IRO-TOOL-SPEC-4.2 §0.1
IRO_HOST_CXXSTD  ?= -std=gnu++23
IRO_HOST_WARN    ?= -Wall -Wextra -Wpedantic

# Host C++ flags
# We set flags for both the basename and the full object path because different
# kernel versions use different naming conventions for HOSTCXXFLAGS_* variables.
# This ensures compatibility across kernel versions.

# Basename pattern (used by older kernel versions)
HOSTCXXFLAGS_layout_parse.o += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_dwarf_reader.o += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_depcheck.o     += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_gen_probe.o    += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)

# Full path pattern (used by newer kernel versions)
HOSTCXXFLAGS_scripts/iro/layout_parse/layout_parse.o += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_scripts/iro/layout_parse/dwarf_reader.o += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_scripts/iro/depcheck/depcheck.o         += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
HOSTCXXFLAGS_scripts/iro/gen_probe/gen_probe.o       += $(IRO_HOST_CXXSTD) $(IRO_HOST_WARN)
