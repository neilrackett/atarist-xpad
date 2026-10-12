| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett

| mousevec trampoline.
|
| The same job as joyvec.s, which this driver links alongside it: TOS
| calls mousevec from the IKBD interrupt with a0 pointing at the three
| byte packet [header, dx, dy], a register argument that is not the
| m68k C calling convention. ABI glue, not optimisation.
|
| The whole register set is saved for the same reason as there, and it
| matters slightly more here: a moving mouse calls this constantly, and
| AES and VDI are listening behind it.

        .text
        .even

        .globl  _xpad_mousevec_entry
        .extern _xpad_mousevec_chain    | void (*)(void *), set by the C side
        .extern _xpad_mousevec_update   | void (*)(const unsigned char *)

_xpad_mousevec_entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        move.l  %a0,-(%sp)              | the packet, as a normal C argument
        jsr     _xpad_mousevec_update
        addq.l  #4,%sp
        movem.l (%sp)+,%d0-%d7/%a0-%a6

        | Tail call whatever we displaced, with a0 still on the packet.
        | Swallowing it would freeze the GEM mouse.
        move.l  _xpad_mousevec_chain,-(%sp)
        rts
