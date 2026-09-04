| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett

| etv_timer trampoline.
|
| Unlike joyvec and kbdvec, nothing calls a driver when a joypad button
| changes: the enhanced ports are registers, not an interrupt source, so
| this driver is the one that has to poll. AGENTS.md says to prefer ETV
| at $400 over the VBL at $70 when a hook is needed, because a game is
| far more likely to have taken the VBL for itself.
|
| This is ABI glue rather than optimisation, which is why it does not
| contradict the "no ASM" guidance: the vector is entered with the
| system's registers live and must be left exactly as it was found, and
| the displaced handler still expects its own arguments in registers
| that C would happily reuse.
|
| The whole register set is saved rather than the ABI scratch set. This
| runs inside the timer interrupt, on somebody else's stack, and the
| cost is irrelevant next to being certain we disturbed nothing.

        .text
        .even

        .globl  _xpad_etv_entry
        .extern _xpad_etv_chain         | void (*)(void), set by the C side
        .extern _xpad_etv_update        | void (*)(void)

_xpad_etv_entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        jsr     _xpad_etv_update
        movem.l (%sp)+,%d0-%d7/%a0-%a6

        | Tail call whatever we displaced. Pushing its address and
        | returning to it leaves every register untouched, including
        | anything the old handler was passed. Chaining is not optional:
        | etv_timer drives parts of the BIOS, so a hook that swallows
        | ticks breaks far more than this driver.
        move.l  _xpad_etv_chain,-(%sp)
        rts

| void xpad_etv_call(void (*fn)(void));
|
| Call an etv_timer-convention handler from C, for tests: it lets the
| self test drive the real trampoline, chain included, rather than a
| version of it written out again in the test.

        .globl  _xpad_etv_call

_xpad_etv_call:
        move.l  %a2,-(%sp)
        move.l  8(%sp),%a2
        jsr     (%a2)
        move.l  (%sp)+,%a2
        rts
