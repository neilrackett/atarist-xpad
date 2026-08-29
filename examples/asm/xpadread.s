| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett
|
| A worked Xpad consumer in m68k assembly.
|
| This is the example the C API cannot be: the consistent read is the
| part that is easy to get subtly wrong and never notice, because a torn
| read looks like a dropped input rather than like a bug.
|
| Written for GNU as (see xpad_gas.inc). The same code in vasm/devpac
| dialect is the same instructions with xpad.inc and `equ`.
|
| Shape: find the block once at boot, precompute pad 0's address in each
| buffer, then read every frame with no multiply and no subroutine call.
| That is what a per-frame budget wants, and it is only legal because
| pads_offset, pad_size and pad_count are immutable for the life of a
| published block. See xpad.h.
|
| This is NOT a library. Consumers with hard register constraints, which
| is most of the ones written in assembly, want constants and a recipe
| rather than a routine with a calling convention. Copy what you need.

        .include "xpad_gas.inc"        | -I src, see the Makefile

| ---------------------------------------------------------------------
| Discovery: once, at boot, in supervisor mode.
|
| Reads the cookie jar directly rather than calling xpad_find(), which
| wraps itself in Supexec. Code already running supervisor (a cartridge
| app, a VBL handler, a TSR) does not need that, and the jar walk is
| eight instructions. If you are also probing for other cookies, walk
| the jar once and test each tag as you go.
|
| Out: a0 = block, or 0. Clobbers d0/a0/a1.

find_xpad:
        movea.l XPAD_JAR,%a1            | the jar, or 0 if none
        move.l  %a1,%d0
        beq.s   .no_jar

.next:  move.l  (%a1),%d0               | tag
        beq.s   .no_jar                 | tag 0 ends the jar
        cmpi.l  #XPAD_COOKIE,%d0
        beq.s   .found
        addq.l  #8,%a1                  | tag, value: eight bytes a slot
        bra.s   .next

.found: movea.l 4(%a1),%a0              | the block
        move.l  %a0,%d0
        beq.s   .no_jar

        | Alignment before the first dereference: a word read from an
        | odd address bus errors, and there is no recovering from that
        | inside a VBL handler.
        btst    #0,%d0
        bne.s   .no_jar

        | Then the two fields worth checking before trusting anything.
        cmpi.l  #XPAD_MAGIC,XPAD_MAGIC_OFF(%a0)
        bne.s   .no_jar
        cmpi.b  #XPAD_VER_MAJOR,XPAD_VERSION_OFF(%a0)   | high byte
        bne.s   .no_jar
        rts

.no_jar:
        suba.l  %a0,%a0                 | a0 = 0
        rts

| ---------------------------------------------------------------------
| Precompute: once, after discovery.
|
| pad n in buffer b sits at pads_offset + (b * pad_count + n) * pad_size.
| Doing that per frame would be a multiply per frame; doing it once is
| free. mulu on a 68000 is 16x16 -> 32, which is all this needs: the C
| macro's 16-bit arithmetic exists to dodge a __mulsi3 libcall that
| assembly never risks.
|
| In: a0 = block. Out: xpad_buf0 / xpad_buf1 = pad 0 in each buffer.

precompute_pad0:
        move.l  %a0,xpad_block

        moveq   #0,%d0
        move.w  XPAD_PADSOFF_OFF(%a0),%d0
        lea     0(%a0,%d0.l),%a1        | a1 = &pads[0][0]
        move.l  %a1,xpad_buf0

        moveq   #0,%d0
        move.b  XPAD_PADCOUNT_OFF(%a0),%d0
        move.w  XPAD_PADSIZE_OFF(%a0),%d1
        mulu    %d1,%d0                 | pad_count * pad_size
        adda.l  %d0,%a1                 | one whole buffer along
        move.l  %a1,xpad_buf1
        rts

| ---------------------------------------------------------------------
| The per-frame read.
|
| The consistent read, in order:
|
|   sample seq, sample active   (either order: the recheck covers both)
|   select the buffer
|   copy what you need
|   recheck active AND seq      (both, or a commit can slip past)
|
| On mismatch this drops the frame rather than retrying. That is a
| legitimate strategy and the spec blesses it: in a VBL handler the next
| sample is 20ms away, so a retry loop buys nothing and costs worst-case
| time where there is none to spare. xpad.c retries three times because
| a caller asking for a snapshot has no next frame to fall back on.
| Neither is more correct than the other.
|
| active is 0 or 1, promised by the spec, so branching on zero is safe
| and reaches the same buffer the C reaches.
|
| Out: d0 = buttons, and Z set if the read was torn or the pad absent
|      (in which case d0 is meaningless: keep last frame's).
| Clobbers d0/d1/d2/a1.

read_pad0:
        movea.l xpad_block,%a1
        move.w  XPAD_SEQ_OFF(%a1),%d1           | seq before
        move.b  XPAD_ACTIVE_OFF(%a1),%d2        | active before

        tst.b   %d2
        bne.s   .use1
        movea.l xpad_buf0,%a1
        bra.s   .grab
.use1:  movea.l xpad_buf1,%a1

.grab:  tst.b   XPAD_PAD_TYPE(%a1)              | 0 means no pad here
        beq.s   .torn
        move.l  XPAD_PAD_BUTTONS(%a1),%d0

        | Recheck both. seq alone is not enough: it is a word, and a
        | provider that commits twice while you are reading wraps it
        | only after 65536 commits, but active flipping is the cheap
        | tell that a commit happened at all.
        movea.l xpad_block,%a1
        cmp.b   XPAD_ACTIVE_OFF(%a1),%d2
        bne.s   .torn
        cmp.w   XPAD_SEQ_OFF(%a1),%d1
        bne.s   .torn

        moveq   #-1,%d1                 | clear Z: the read stands
        rts

.torn:  moveq   #0,%d0
        rts                             | Z set: discard, keep last frame

| ---------------------------------------------------------------------

        .bss
        .even
xpad_block:     .ds.l 1
xpad_buf0:      .ds.l 1
xpad_buf1:      .ds.l 1
