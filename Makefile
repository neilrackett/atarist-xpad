# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

#
# xpad is consumed by copying src/xpad.h and src/xpad.c into a port, and
# src/xpad_provider.c as well if it publishes rather than reads. This
# Makefile deliberately builds no library. It exists to prove the
# reference implementation still compiles for the ST, and to run the ABI
# assertions on the host.
#
# ./verify.sh runs every test there is. It has to be a script rather
# than a target because the ST builds need the container and Hatari must
# not run inside it. The individual targets, when you want just one:
#
#   make                               host tests, no toolchain
#   STCMD_NO_TTY=1 stcmd make check    compile for the ST, warnings fatal
#   STCMD_NO_TTY=1 stcmd make st       link everything that runs on an ST
#   make hatari                        ABI assertions on an emulated ST
#   make hatari-joystick               joystick driver self test
#   make hatari-keyboard               keyboard driver self test
#   make hatari-view                   viewer against its demo provider
#   make hatari-integration            drivers from AUTO, viewer reads them
#

# The warnings policy applies to both builds. Keep it in one place so
# tightening the ST build cannot quietly leave the host build looser.
WARNINGS    = -Wall -Wextra -Werror

CC          = m68k-atari-mint-gcc
AS          = m68k-atari-mint-as
VASM        = vasmm68k_mot
CFLAGS      = $(WARNINGS) -O2 -fomit-frame-pointer -m68000

HOSTCC      = cc
# -Itest supplies a stub <mint/osbind.h> so both halves build unmodified.
# The cast warnings are inherent to compiling code written for 32-bit
# pointers on an LP64 host; the affected functions are never called here.
HOSTCFLAGS  = $(WARNINGS) -O1 -std=c11 -Itest \
              -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

SRC         = src
DRIVERS     = $(SRC)/drivers
BUILD       = build
TOOLS       = $(SRC)/tools

# test runs anywhere; check needs the cross toolchain. Default to the
# one a newcomer or a pre-commit hook can actually run.
.DEFAULT_GOAL := test

.PHONY: all check test inc inc-check examples st hatari hatari-joystick hatari-keyboard hatari-view hatari-integration drivers tools clean

all: check test

# Compile the reference implementation with the ST toolchain. No link:
# there is no program here, only a translation unit that must be clean.
# test/abi.c is syntax checked too, so the frozen layout is asserted for
# the architecture the ABI is for and not only for the host.
# The worked assembly consumer has to keep assembling, or it is just a
# comment that looks like code. Uses the generated equates, so this also
# proves those are usable by an assembler and not merely well formed.
# Both dialects, because both are shipped and an unassembled example is
# just a comment that looks like code. -x makes an unresolved symbol an
# error, so this also proves every equate the example names exists.
$(BUILD)/xpadread.o: examples/asm/xpadread.s $(SRC)/xpad_gas.inc | $(BUILD)
	$(AS) -m68000 -I$(SRC) examples/asm/xpadread.s -o $@

$(BUILD)/xpadread_devpac.o: examples/asm/xpadread_devpac.s $(SRC)/xpad.inc | $(BUILD)
	$(VASM) -Faout -quiet -x -m68000 -devpac -I$(SRC) \
	    examples/asm/xpadread_devpac.s -o $@

examples: $(BUILD)/xpadread.o $(BUILD)/xpadread_devpac.o
	@echo "both assembly examples assemble, in both dialects"

check: examples | $(BUILD)
	$(CC) $(CFLAGS) -c $(SRC)/xpad.c -o $(BUILD)/xpad.o
	$(CC) $(CFLAGS) -c $(SRC)/xpad_provider.c -o $(BUILD)/xpad_provider.o
	$(CC) $(CFLAGS) -fsyntax-only test/abi.c
	@echo "both halves compile clean for m68000, ABI assertions hold there"

# Host build of the ABI assertions. Most of the file is static
# assertions, so a violation fails this compile rather than the run.
# Driver translation logic runs here too: it is pure logic, kept free of
# TOS dependencies precisely so it does not need the emulator.
test: | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) test/abi.c $(SRC)/xpad.c $(SRC)/xpad_provider.c -o $(BUILD)/abi
	@$(BUILD)/abi
	@echo
	$(HOSTCC) $(HOSTCFLAGS) test/joystick.c -o $(BUILD)/joy
	@$(BUILD)/joy
	@echo
	$(HOSTCC) $(HOSTCFLAGS) test/keyboard.c -o $(BUILD)/key
	@$(BUILD)/key
	@echo
	@$(MAKE) --no-print-directory inc-check

# The assembler equates are generated from xpad.h, so they cannot be
# transcribed wrongly; this catches them being left stale after the
# header changes, which is the only way they can now go wrong.
$(BUILD)/geninc: $(TOOLS)/geninc.c $(SRC)/xpad.h | $(BUILD)
	$(HOSTCC) $(HOSTCFLAGS) $(TOOLS)/geninc.c -o $@

inc: $(BUILD)/geninc
	@$(BUILD)/geninc      > $(SRC)/xpad.inc
	@$(BUILD)/geninc -gas > $(SRC)/xpad_gas.inc
	@echo "regenerated $(SRC)/xpad.inc and $(SRC)/xpad_gas.inc"

inc-check: $(BUILD)/geninc
	@$(BUILD)/geninc      > $(BUILD)/xpad.inc.new
	@$(BUILD)/geninc -gas > $(BUILD)/xpad_gas.inc.new
	@if ! diff -q $(SRC)/xpad.inc $(BUILD)/xpad.inc.new >/dev/null || \
	    ! diff -q $(SRC)/xpad_gas.inc $(BUILD)/xpad_gas.inc.new >/dev/null; then \
		echo "the assembler equates are stale: run 'make inc'"; \
		diff -u $(SRC)/xpad.inc $(BUILD)/xpad.inc.new | head -20; \
		exit 1; \
	fi
	@echo "assembler equates match xpad.h"

# The ST build of the harness. No -Itest here: it must pick up the real
# <mint/osbind.h> and the real cookie jar at 0x5A0, not the host stubs.
$(BUILD)/ABI.TOS: test/abi.c $(SRC)/xpad.c $(SRC)/xpad_provider.c $(SRC)/xpad.h | $(BUILD)
	$(CC) $(CFLAGS) test/abi.c $(SRC)/xpad.c $(SRC)/xpad_provider.c -o $@

# Drivers are whole programs, not part of the library, so each gets its
# own directory: this one needs a joyvec trampoline the core never does.
JOYDIR = $(DRIVERS)/joystick
JOYSRC = $(JOYDIR)/joystick.c $(JOYDIR)/joyvec.s $(SRC)/xpad.c \
         $(SRC)/xpad_provider.c
JOYDEP = $(JOYSRC) $(JOYDIR)/translate.h $(SRC)/xpad.h

$(BUILD)/XPADJOY.PRG: $(JOYDEP) | $(BUILD)
	$(CC) $(CFLAGS) $(JOYSRC) -o $@

# The same source built to run its self test and exit. A separate binary
# because Hatari's --auto takes a path and no arguments, so the resident
# driver's -t flag is reachable by hand but not by the harness.
$(BUILD)/JOYTEST.TOS: $(JOYDEP) | $(BUILD)
	$(CC) $(CFLAGS) -DXPAD_SELFTEST $(JOYSRC) -o $@

KEYDIR = $(DRIVERS)/keyboard
KEYSRC = $(KEYDIR)/keyboard.c $(KEYDIR)/kbdvec.s $(SRC)/xpad.c \
         $(SRC)/xpad_provider.c
KEYDEP = $(KEYSRC) $(KEYDIR)/keymap.h $(SRC)/xpad.h

$(BUILD)/XPADKEY.PRG: $(KEYDEP) | $(BUILD)
	$(CC) $(CFLAGS) $(KEYSRC) -o $@

$(BUILD)/KEYTEST.TOS: $(KEYDEP) | $(BUILD)
	$(CC) $(CFLAGS) -DXPAD_SELFTEST $(KEYSRC) -o $@

# Standalone programs that link the core but are not part of it.
VIEWSRC = $(TOOLS)/xpadview.c $(SRC)/xpad.c $(SRC)/xpad_provider.c
VIEWDEP = $(VIEWSRC) $(SRC)/xpad.h

$(BUILD)/XPADVIEW.PRG: $(VIEWDEP) | $(BUILD)
	$(CC) $(CFLAGS) $(VIEWSRC) -o $@

$(BUILD)/VIEWTEST.TOS: $(VIEWDEP) | $(BUILD)
	$(CC) $(CFLAGS) -DXPAD_SELFTEST $(VIEWSRC) -o $@

tools: $(BUILD)/XPADVIEW.PRG
	@echo "built $(BUILD)/XPADVIEW.PRG"

drivers: $(BUILD)/XPADJOY.PRG $(BUILD)/XPADKEY.PRG
	@echo "built $(BUILD)/XPADJOY.PRG and $(BUILD)/XPADKEY.PRG"

st: $(BUILD)/ABI.TOS $(BUILD)/XPADJOY.PRG $(BUILD)/JOYTEST.TOS \
    $(BUILD)/XPADKEY.PRG $(BUILD)/KEYTEST.TOS \
    $(BUILD)/XPADVIEW.PRG $(BUILD)/VIEWTEST.TOS
	@echo "built everything that runs on an ST into $(BUILD)"
	@echo "run: make hatari / hatari-joystick / hatari-keyboard / hatari-view"

# Runs on the host, not in the container, since that is where Hatari is.
hatari:
	@python3 test/run-hatari.py $(BUILD)/ABI.TOS

# The joystick driver's self test on an emulated ST: it drives the real
# trampoline with fabricated packets and installs nothing.
hatari-joystick:
	@python3 test/run-hatari.py $(BUILD)/JOYTEST.TOS

# The keyboard driver's self test, likewise.
hatari-keyboard:
	@python3 test/run-hatari.py $(BUILD)/KEYTEST.TOS

# The viewer against its own demo provider: publishes a block, finds it
# through the cookie, reads it back and prints one frame.
hatari-view:
	@python3 test/run-hatari.py $(BUILD)/VIEWTEST.TOS

# End to end: a driver installed from AUTO, then a separate program
# finding it through the cookie jar and reading its pads. This is the
# only test where provider and consumer are different processes.
hatari-integration:
	@python3 test/run-hatari.py $(BUILD)/VIEWTEST.TOS $(BUILD)/XPADJOY.PRG
	@echo
	@python3 test/run-hatari.py $(BUILD)/VIEWTEST.TOS $(BUILD)/XPADKEY.PRG

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	@rm -rf $(BUILD)
