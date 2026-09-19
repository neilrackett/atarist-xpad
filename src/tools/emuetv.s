| SPDX-License-Identifier: GPL-3.0-or-later
| SPDX-FileCopyrightText: 2026 Neil Rackett

| ABI glue for XPADEMU: an etv_timer trampoline, and a safe way to call
| an IKBD vector from C.
|
| Ported from the assembly injector in MD/Sidepad:
| https://github.com/neilrackett/md-sidepad
| (target/atarist/src/userfw.s). Everything here is its discovery; only
| the surrounding program is new.
|
| This is ABI glue rather than optimisation, which is why it does not
| contradict AGENTS.md's "no ASM" guidance. Both routines exist because
| C cannot say what they say: one is entered with the system's
| registers live, and the other calls code that does not honour the C
| calling convention.

        .text
        .even

        .globl  _xpademu_etv_entry
        .extern _xpademu_etv_chain      | void (*)(void), set by the C side
        .extern _xpademu_tick           | void (*)(void)

| etv_timer ($400) entry. TOS' MFP Timer C calls this about 200 times a
| second; the C side divides it down to the 50 Hz the IKBD is expected
| to report at.
|
| The whole register set is saved rather than the ABI scratch set,
| because this ends up inside joyvec and mousevec. Those reach line-A
| and the AES cursor machinery, which clobber registers C assumes are
| preserved, and restoring only the scratch set would leak that
| corruption into whatever program was interrupted. That shows up as
| intermittent bombs an hour later, which is the worst way to find it.

_xpademu_etv_entry:
        movem.l %d0-%d7/%a0-%a6,-(%sp)
        jsr     _xpademu_tick
        movem.l (%sp)+,%d0-%d7/%a0-%a6

        | Tail call whatever we displaced. Pushing its address and
        | returning to it leaves every register untouched. Chaining is
        | not optional: etv_timer drives parts of the BIOS, so a hook
        | that swallows ticks breaks far more than this program.
        move.l  _xpademu_etv_chain,-(%sp)
        rts

| void xpademu_call_vec(void (*vec)(void), void *packet);
|
| Call an IKBD vector the way the IKBD's own interrupt handler would:
| the packet in a0, and the ACIA masked.
|
| The masking is the part that is not obvious. Driving joyvec or
| mousevec leaves the IKBD ACIA interrupt enabled, so a real packet
| arriving mid-call would re-enter the very handler we are inside and
| corrupt its state. Raising to IPL 7 for the duration is what stops
| that. It is safe here because etv_timer is already interrupt context
| and therefore supervisor; called from user mode this would be a
| privilege violation, which is why the self test drives it through
| Supexec.
|
| d2-d7 and a2-a6 are saved because C expects this function to preserve
| them and the vector we call will not.

        .globl  _xpademu_call_vec

_xpademu_call_vec:
        movem.l %d2-%d7/%a2-%a6,-(%sp)  | 11 registers, 44 bytes
        move.l  48(%sp),%a1             | vec
        move.l  52(%sp),%a0             | packet
        move.w  %sr,-(%sp)
        ori.w   #0x0700,%sr
        jsr     (%a1)
        move.w  (%sp)+,%sr
        movem.l (%sp)+,%d2-%d7/%a2-%a6
        rts

| void xpademu_etv_call(void);
|
| Drive the trampoline the way etv_timer does, for the self test, so it
| exercises the real entry point and the real chain rather than a
| version of them written out again in the test.

        .globl  _xpademu_etv_call

_xpademu_etv_call:
        jsr     _xpademu_etv_entry
        rts
