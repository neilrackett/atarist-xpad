# AGENTS.md

Guidance for AI coding agents working in `atarist-xpad`.

## What this is

A transport-neutral shared state block that exposes every button, stick
and trigger of a modern gamepad to native Atari ST software. A provider
publishes an `XPAD` block via the cookie jar; consumers poll it.

**This repo is primarily an interface, not an implementation.** Its value
comes from other people's code depending on the layout being stable.
Treat `src/xpad.h` as a published ABI, not as source you are free to tidy.

Read `README.md` before making changes. It is the spec; this file only
covers working practices.

## Layout

```
src/xpad.h          the ABI: struct layout, button bitmask, declarations
src/xpad.c          reference implementation, consumer and provider helpers
src/drivers/joystick  joyvec provider: joystick 1 as one pad
src/drivers/keyboard  kbdvec provider: DOOM controls as one pad
src/tools/          standalone programs, eg the diagnostic viewer
test/abi.c          ABI assertions, mostly static; host and ST builds
test/mint/osbind.h  host stand-in for <mint/osbind.h>
test/run-hatari.py  boots an ST program under Hatari, relays its output
Makefile            test, check, st and hatari targets
README.md           the specification
LICENSE             BSD-2-Clause
```

`src/xpad.h` and `src/xpad.c` are the only files a port copies. Everything
under `src/drivers` and `src/tools` is a standalone program that links them,
never something a consumer takes.

## Building and testing

There are two targets, and the harness is already written: do not
improvise a throwaway one.

```
make                               host tests, no toolchain
STCMD_NO_TTY=1 stcmd make check    compile for the ST, warnings fatal
STCMD_NO_TTY=1 stcmd make st       link everything that runs on an ST
make hatari                        ABI assertions on an emulated ST
make hatari-joystick               joystick driver self test
make hatari-keyboard               keyboard driver self test
```

`test` is the default goal and needs nothing but a host compiler, so run
it on every change. `check` builds `src/xpad.c` with `m68k-atari-mint-gcc`
from `atarist-toolkit-docker` and syntax checks `test/abi.c` there too,
so the frozen layout is asserted for the target and not only for the
host.

`st` and `hatari` are two commands rather than one because the build
needs the container and the emulator does not. `hatari` boots EmuTOS
headless with `build` as drive C, autostarts the harness, and exits with
its status. It needs Hatari and a TOS image; set `$HATARI` or `$TOS` if
they are not found. Note that `make` reports a failing recipe as exit 2,
so check the printed verdict rather than the code.

Target Atari Mega STE, but everything must work correctly on ST and STE.
`hatari` covers the emulated ST, so run it before claiming anything
works; nothing here runs on real hardware, and there is no CI that can
catch a 68000-specific mistake for you.

Drivers follow the same split. Anything with logic worth getting wrong
lives in a TOS-free header the host build tests (`translate.h`), and
whatever needs an ST gets a self test in the driver itself, built as a
separate binary because Hatari's `--auto` takes a path and no arguments.

`test/abi.c` builds for both and switches on `__MINT__`. The pure logic
runs either way. The cookie jar cannot: the host substitutes a fake jar
for `0x5A0`, which is the only way to reach a full or absent jar, while
the ST uses the real one under real supervisor mode, which is the only
way a published pointer survives the jar's `uint32_t`. Keep both. A
change to the jar code that only passes one of them is not finished.

## Hard invariants

Breaking any of these breaks binaries already in the wild. Do not change
them without an explicit instruction and a major version bump.

- **Button bit values are frozen.** Never reorder, renumber or reuse a
  bit. New buttons take bits 17 and up.
- **`XPAD_PAD` is 12 bytes** in v1, named by `XPAD_PAD_SIZE_V1`. Growing
  it is allowed only via the `pad_size` mechanism, never by silently
  changing the struct. `XPAD_PAD_SIZE_V1` is the floor `xpad_valid()`
  enforces and does not move when `XPAD_PAD` grows.
- **`XPAD_HDR_FIXED` is 18**, the part of `XPAD` whose offsets are the
  same on every architecture: `magic` through `active`. Everything after
  it holds pointers, so it is not portable and is not asserted. Adding a
  field inside the fixed part breaks every shipped binary.
- **Consumers locate pads through `XPAD_PAD_AT()`**, using `pads_offset`,
  `pad_size` and `pad_count`. Never index `x->pads[][]` directly in
  consumer code, even though the array is declared. Provider code may,
  via `xpad_back()`.
- **`active` is a single byte** because a byte write is atomic on 68000.
  Do not widen it, and do not replace the double buffer with a lock.
- **`xpad_commit()` bumps `seq` before flipping `active`.** Order matters
  for the consumer's consistency check.
- **The two `pads` buffers sit `pad_count * pad_size` apart.**
  `xpad_back()` derives this through `XPAD_PAD_AT()` so the provider side
  cannot drift from the consumer side. Do not reintroduce a fixed
  `XPAD_MAX_PADS` stride: the two agree only when `pad_count` happens to
  be `XPAD_MAX_PADS`.
- **Version policy** is in README's Forward compatibility section. Do not
  bump the major version without being asked to.

## The X/Y trap

`XPAD_X` aliases **north** and `XPAD_Y` aliases **west**, following
`linux/input-event-codes.h`. README's Conventions section explains why,
and `xpad.h` warns about it at the point of use.

This is not a bug. Do not "fix" it. Use `XPAD_NORTH` / `XPAD_WEST` in new
code, and when writing a provider that translates from Gamepad API index
order, map by position and never by the letter on the button.

## Deliberate decisions

Listed so they do not get helpfully undone:

- D-pad bits sit at 0 to 3 rather than mirroring the kernel's
  `BTN_DPAD_*` range, because ST software reads directions constantly.
- Axes are 8-bit. 16-bit resolution is pointless on this hardware.
- `xpad_read()` copies rather than returning a pointer, so the caller
  holds a stable snapshot for a whole frame.
- The retry loop is three attempts. Two buffers plus a `seq` recheck
  handles the pathological case; it is not meant to be a seqlock.
- There is no edge detection helper. Games want that per action, with
  their own repeat timing.
- `xpad_valid()` is separate from `xpad_find()` so the layout rules can
  be tested without a cookie jar and supervisor mode, and so a provider
  can check its own block before publishing. Do not fold it back in.
- `xpad_read()` takes both buffer addresses before the retry loop and
  copies by struct assignment on the full-pad path. That is what keeps a
  multiply and a `memcpy` libcall out of a per-frame path; it is smaller
  as well as faster, so there is nothing to win by simplifying it back
  to a `memcpy` through `XPAD_PAD_AT()` inside the loop.
- `xpad_req()` is the only supported route to the request area, which is
  the sole direction a consumer can corrupt a provider. Do not encourage
  reading `XPAD.req` directly.
- Neither driver needs a periodic hook: `joyvec` and `kbdvec` are both
  called for us when something changes. Where a driver does need one,
  prefer ETV (`$400`) over VBL (`$70`): VBL is far more likely to be
  clobbered by an existing game.
- The keyboard driver needs TOS 2.0 or later and refuses below it. Key
  releases only reach `kbdvec`, four bytes below `Kbdvbase()`, which
  older TOS lacks; getting them from TOS 1.x means driving the ACIA
  through `ikbdsys`, where reading the data register destroys the status
  bits, so a driver cannot both see a byte and let TOS have it. EmuTOS
  reports 2.06, so the emulator does not warn you about this.
- A driver must always chain to the handler it displaced. One that
  swallows packets breaks the desktop and every existing game, so the
  joystick self test checks for it explicitly.
- **System variables below `$800` need `Supexec`.** The ST bus errors on
  user mode access down there, which is why reading `_sysbase` at `$4f2`
  and the cookie jar at `$5a0` both go through it.
- `xpad_fold_stick()` uses a radial deadzone, three word multiplies,
  which is acceptable at VBL rate. Do not "optimise" it into a box test:
  a box test rejects diagonals whose magnitude clears the threshold, so
  the stick goes dead in the corners. `test/abi.c` pins this with the
  (30, 30) case.
- The deadzone arithmetic is deliberately 16 bit. A 68000 has no 32-bit
  multiply, so widening any operand turns each `MULU.W` into a
  `__mulsi3` libcall. The same applies to `XPAD_PAD_AT()`.

## Style

Follow the existing file exactly rather than any general convention.

- C, tight and allocation-free. No dynamic memory anywhere.
- Allman braces, four spaces, no tabs.
- Types `XPAD`, `XPAD_PAD`, `XPAD_REQ`. Functions `xpad_lower_snake()`.
- Trailing comments aligned in a column, as in the existing code.
- Keep dependencies to `<stdint.h>`, `<string.h>` and `<mint/osbind.h>`.
  Anything more makes the file harder to drop into a port.
- Prose in comments and docs avoids em dashes. Use colons, commas or
  shorter sentences.
- ASM only where extreme optimisation is genuinely needed, which so far
  is nowhere in this repo.

## Licensing

New and significantly modified files carry an SPDX header:

```c
/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */
```

`src/xpad.h` and `src/xpad.c` must stay BSD-2-Clause. They are compiled into
other people's programs, several of which are GPL-2-only, and a copyleft
core would lock those out. Do not introduce a dependency under a licence
that would compromise this.

Standalone tools and providers in this repo (a fallback shim, a
diagnostic program, a Hatari stub) are programs rather than libraries
linked into consumers, so they may carry a different licence. Check the
file's own SPDX header rather than assuming.

## Scope discipline

- Change only what was asked. No opportunistic refactoring, no renaming,
  no reformatting of untouched code.
- Do not change behaviour unless explicitly asked to.
- If a change would alter the ABI, stop and say so rather than
  proceeding.

## Planned work

Not yet written, listed so contributions land in the right place:

- **IKBD fallback shim**: publishes an `XPAD` block synthesised from
  joystick 0, joystick 1's fire bit as a second button, and the keyboard.
  Lets a port implement only the xpad path and still work on a bare ST.
- **Diagnostic TOS program**: live state for all four pads.
- **Hatari stub provider**: driven by emulated joysticks, so the spec can
  be developed against without hardware.
