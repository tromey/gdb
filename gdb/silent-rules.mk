# If V is undefined or V=0 is specified, use the silent/verbose/compact mode.
V ?= 0
ifeq ($(V),0)

.DEFAULT_GOAL := all

set-color.mk: .
	@if test -t 1; then \
	  echo COLORIZE=1 > set-color.mk; \
	else \
	  echo COLORIZE=0 > set-color.mk; \
	fi

.PHONY: set-color.mk

include set-color.mk

ifeq ($(COLORIZE),1)
colorize = \033[32m$(1)\033[0m
else
colorize = $(1)
endif

ECHO_CXX =    @echo -e "  $(call colorize,CXX)    $@";
ECHO_CXXLD =  @echo -e "  $(call colorize,CXXLD)  $@";
ECHO_REGDAT = @echo -e "  $(call colorize,REGDAT) $@";
ECHO_GEN =    @echo -e "  $(call colorize,GEN)    $@";
ECHO_GEN_XML_BUILTIN = \
              @echo -e "  $(call colorize,GEN)    xml-builtin.c";
ECHO_GEN_XML_BUILTIN_GENERATED = \
              @echo -e "  $(call colorize,GEN)    xml-builtin-generated.c";
ECHO_INIT_C =  echo -e "  $(call colorize,GEN)    init.c" ||
ECHO_SIGN =   @echo -e "  $(call colorize,SIGN)   gdb";
SILENCE = @
endif
