# IRO depcheck Kbuild integration
# Enforces header boundary for C++ translation units (§15)

# Configurable allow/deny prefixes
# These match the defaults in depcheck.cc but can be overridden
IRO_DEPCHECK_ALLOW ?= include/generated/iro/ iro/include/
IRO_DEPCHECK_DENY  ?= include/linux/ arch/ include/asm include/generated/asm

# Build allow/deny flags from the lists
iro_depcheck_allow_flags = $(foreach p,$(IRO_DEPCHECK_ALLOW),--allow $(p))
iro_depcheck_deny_flags = $(foreach p,$(IRO_DEPCHECK_DENY),--deny $(p))

define __iro_depcheck
	$(Q)$(IRO_DEPCHECK) --depfile $(depfile) --srctree $(srctree) --objtree $(objtree) \
		$(iro_depcheck_allow_flags) $(iro_depcheck_deny_flags)
endef

# Hook into C++ compilation when CONFIG_IRO_STRICT is enabled
# This adds depcheck validation after each C++ object file is compiled
ifeq ($(CONFIG_IRO_STRICT),y)
  # Tested against Kbuild >= 6.6. If this symbol changes upstream,
  # fail fast with a clear error instead of silently disabling checks.
  ifndef rule_cc_o_cxx
    $(error IRO depcheck: rule_cc_o_cxx not found -- Kbuild version may be incompatible)
  endif
  define rule_cc_o_cxx
	$(call echo-cmd,depfile) \
	$(cmd_and_savecmd); \
	$(call __iro_depcheck)
  endef
endif
