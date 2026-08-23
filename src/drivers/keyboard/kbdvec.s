| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett

| kbdvec trampoline.
|
| TOS calls kbdvec with the key code in d0.b, bit 7 set for a release.
| That is a register argument, which is not the m68k C calling
| convention, so this shim moves it onto the stack. ABI glue, not
| optimisation.
|
| Chaining is not optional here, and matters more than it does for
| joyvec: kbdvec is where TOS turns scancodes into console input, so a
| handler that swallows them stops the keyboard working for everything
| else on the machine.

        .text
        .even

        .globl  _xpad_kbdvec_entry
        .extern _xpad_kbdvec_chain      | previous vector, set by the C side
        .extern _xpad_kbdvec_update     | void (*)(int code)

_xpad_kbdvec_entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        moveq   #0,%d1
        move.b  %d0,%d1                 | zero extend: C promotes to int
        move.l  %d1,-(%sp)
        jsr     _xpad_kbdvec_update
        addq.l  #4,%sp
        movem.l (%sp)+,%d0-%d7/%a0-%a6

        move.l  _xpad_kbdvec_chain,-(%sp)
        rts

| void xpad_kbdvec_call(void (*fn)(void), int code);
|
| Call a kbdvec-convention handler from C, for tests: the callee wants
| the code in d0, and C would put it on the stack.

        .globl  _xpad_kbdvec_call

_xpad_kbdvec_call:
        move.l  %a2,-(%sp)
        move.l  8(%sp),%a2              | fn
        move.l  12(%sp),%d0             | code
        jsr     (%a2)
        move.l  (%sp)+,%a2
        rts
