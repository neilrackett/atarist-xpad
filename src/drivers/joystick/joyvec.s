| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett

| joyvec trampoline.
|
| TOS calls joyvec from the IKBD interrupt with a0 pointing at the three
| byte packet [$FF, joy0, joy1]. That is a register argument, which is
| not the m68k C calling convention, and gcc may clobber a0 in a
| prologue before any C code could read it. Hence this file: it is ABI
| glue, not optimisation, which is why it does not contradict the "no
| ASM" guidance in AGENTS.md. Keep it to that one job.
|
| The whole register set is saved rather than just the ABI scratch
| registers. This runs inside someone else's interrupt handler, and the
| cost on an event that only fires when a joystick moves is irrelevant
| next to being certain we disturbed nothing.

        .text
        .even

        .globl  _xpad_joyvec_entry
        .extern _xpad_joyvec_chain      | void (*)(void *), set by the C side
        .extern _xpad_joyvec_update     | void (*)(const unsigned char *)

_xpad_joyvec_entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.l  %a0,-(%sp)              | the packet, as a normal C argument
        jsr     _xpad_joyvec_update
        addq.l  #4,%sp
        movem.l (%sp)+,%d0-%d7/%a0-%a6

        | Tail call whatever we displaced. Pushing its address and
        | returning to it leaves every register untouched, including a0,
        | which it still needs pointing at the packet. Chaining is not
        | optional: a hook that swallows packets breaks the desktop and
        | every existing game.
        move.l  _xpad_joyvec_chain,-(%sp)
        rts

| void xpad_joyvec_call(void (*fn)(void *), const void *pkt);
|
| Call a joyvec-convention handler from C, for tests. Same problem in
| reverse: the callee wants the packet in a0, and C would put it on the
| stack.

        .globl  _xpad_joyvec_call

_xpad_joyvec_call:
        move.l  %a2,-(%sp)
        move.l  8(%sp),%a2              | fn
        move.l  12(%sp),%a0             | pkt
        jsr     (%a2)
        move.l  (%sp)+,%a2
        rts
