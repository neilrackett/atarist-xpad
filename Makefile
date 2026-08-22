# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

#
# xpad is consumed by copying xpad.h and xpad.c into a port, so this
# Makefile deliberately builds no library. It exists to prove the
# reference implementation still compiles for the ST, and to run the ABI
# assertions on the host.
#
#   make                               host ABI assertions, no toolchain
#   STCMD_NO_TTY=1 stcmd make check    compile for the ST, warnings fatal
#   STCMD_NO_TTY=1 stcmd make all      both
#
# The same assertions can also run on an emulated ST, which is the only
# place the cookie jar meets supervisor mode and 32-bit pointers. That
# takes two commands, because the build needs the container and the
# emulator does not:
#
#   STCMD_NO_TTY=1 stcmd make st       link build/ABI.TOS
#   make hatari                        run it under Hatari
#

# The warnings policy applies to both builds. Keep it in one place so
# tightening the ST build cannot quietly leave the host build looser.
WARNINGS    = -Wall -Wextra -Werror

CC          = m68k-atari-mint-gcc
CFLAGS      = $(WARNINGS) -O2 -fomit-frame-pointer -m68000

HOSTCC      = cc
# -Itest supplies a stub <mint/osbind.h> so xpad.c builds unmodified.
# The cast warnings are inherent to compiling code written for 32-bit
# pointers on an LP64 host; the affected functions are never called here.
HOSTCFLAGS  = $(WARNINGS) -O1 -std=c11 -Itest \
              -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

BUILD       = build

# test runs anywhere; check needs the cross toolchain. Default to the
# one a newcomer or a pre-commit hook can actually run.
.DEFAULT_GOAL := test

.PHONY: all check test st hatari clean

all: check test

# Compile the reference implementation with the ST toolchain. No link:
# there is no program here, only a translation unit that must be clean.
# test/abi.c is syntax checked too, so the frozen layout is asserted for
# the architecture the ABI is for and not only for the host.
check: | $(BUILD)
	$(CC) $(CFLAGS) -c xpad.c -o $(BUILD)/xpad.o
	$(CC) $(CFLAGS) -fsyntax-only test/abi.c
	@echo "xpad.c compiles clean for m68000, ABI assertions hold there"

# Host build of the ABI assertions. Most of the file is static
# assertions, so a violation fails this compile rather than the run.
test: | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) test/abi.c xpad.c -o $(BUILD)/abi
	@$(BUILD)/abi

# The ST build of the harness. No -Itest here: it must pick up the real
# <mint/osbind.h> and the real cookie jar at 0x5A0, not the host stubs.
$(BUILD)/ABI.TOS: test/abi.c xpad.c xpad.h | $(BUILD)
	$(CC) $(CFLAGS) test/abi.c xpad.c -o $@

st: $(BUILD)/ABI.TOS
	@echo "built $(BUILD)/ABI.TOS, now run: make hatari"

# Runs on the host, not in the container, since that is where Hatari is.
hatari:
	@test -f $(BUILD)/ABI.TOS || { 	    echo "build it first: STCMD_NO_TTY=1 stcmd make st"; exit 1; }
	@python3 test/run-hatari.py $(BUILD)/ABI.TOS

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
