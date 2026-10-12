<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# Consuming from assembly

[← Back to the README](../README.md)

[`src/xpad.inc`](../src/xpad.inc) (vasm/devpac) and
[`src/xpad_gas.inc`](../src/xpad_gas.inc) (GNU as) carry the offsets
and constants as equates. Both are **generated from `xpad.h`** by
`make inc`, and `make test` fails if they drift, so nothing is
transcribed by eye.

They deliberately stop at `XPAD_HDR_FIXED`. Everything past it holds
pointers, and nothing an assembly consumer needs lives there: the three
fields that locate pads are all inside the fixed 18 bytes.

[`examples/asm/xpadread.s`](../examples/asm/xpadread.s) is a worked
consumer: the jar walk, the precompute, and the consistent read, with
the reasoning written down.
[`examples/asm/xpadread_devpac.s`](../examples/asm/xpadread_devpac.s)
is the same in the vasm/devpac dialect. `make check` assembles both, so
neither can rot into a comment that merely looks like code.

The precompute is legal because of what a block promises for its
lifetime: see
[the specification](specification.md#what-a-block-promises-for-its-lifetime).

There are deliberately **no callable assembly routines**. A consumer
with hard register constraints, which describes most reasons to be
writing assembly at all, is better served by constants and a recipe
than by a subroutine with a calling convention it has to work around.

Two things worth knowing before you start:

- **Check alignment before the first dereference.** A 68000 bus errors
  on a word read from an odd address, and a consumer polling from a VBL
  handler has nowhere to recover to. `xpad_valid()` now tests this, and
  the example does the same `btst #0` on the cookie value.
- **Supervisor already?** Then skip `xpad_find()`, which wraps itself in
  `Supexec`, and walk the jar directly. If you are probing for other
  cookies as well, walk it once and test each tag as you pass.
