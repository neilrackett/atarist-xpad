; SPDX-License-Identifier: BSD-2-Clause
; SPDX-FileCopyrightText: 2026 Neil Rackett
;
; A worked Xpad consumer in m68k assembly, devpac dialect.
;
; Instruction for instruction the same as xpadread.s, which is the GNU
; as version; only the syntax differs. Both are assembled by `make
; check`, so neither can drift from the other or from the header.
;
; Read xpadread.s for the reasoning: it is written out there in full and
; is not repeated here. The short version:
;
;   discovery once, at boot, in supervisor
;   precompute pad 0's address in each buffer, so no per-frame multiply
;   per frame: sample, select, copy, recheck BOTH active and seq
;
; Precomputing is legal because pads_offset, pad_size and pad_count are
; immutable for the life of a published block. See README, "What a block
; promises for its lifetime".

        include "xpad.inc"

; ----------------------------------------------------------------------
; Discovery. Out: a0 = block, or 0. Clobbers d0/a0/a1.

find_xpad:
        move.l  XPAD_JAR.w,a1           ; the jar, or 0 if none
        move.l  a1,d0
        beq.s   .no_jar

.next:  move.l  (a1),d0                 ; tag
        beq.s   .no_jar                 ; tag 0 ends the jar
        cmpi.l  #XPAD_COOKIE,d0
        beq.s   .found
        addq.l  #8,a1                   ; tag, value: eight bytes a slot
        bra.s   .next

.found: move.l  4(a1),a0                ; the block
        move.l  a0,d0
        beq.s   .no_jar

        ; Alignment before the first dereference: a word read from an
        ; odd address bus errors, and there is no recovering from that
        ; inside a VBL handler.
        btst    #0,d0
        bne.s   .no_jar

        cmpi.l  #XPAD_MAGIC,XPAD_MAGIC_OFF(a0)
        bne.s   .no_jar
        cmpi.b  #XPAD_VER_MAJOR,XPAD_VERSION_OFF(a0) ; high byte
        bne.s   .no_jar
        rts

.no_jar:
        sub.l   a0,a0                   ; a0 = 0
        rts

; ----------------------------------------------------------------------
; Precompute, once, after discovery.
; In: a0 = block. Out: xpad_buf0 / xpad_buf1 = pad 0 in each buffer.

precompute_pad0:
        move.l  a0,xpad_block

        moveq   #0,d0
        move.w  XPAD_PADSOFF_OFF(a0),d0
        lea     0(a0,d0.l),a1           ; a1 = &pads[0][0]
        move.l  a1,xpad_buf0

        moveq   #0,d0
        move.b  XPAD_PADCOUNT_OFF(a0),d0
        move.w  XPAD_PADSIZE_OFF(a0),d1
        mulu    d1,d0                   ; pad_count * pad_size
        add.l   d0,a1                   ; one whole buffer along
        move.l  a1,xpad_buf1
        rts

; ----------------------------------------------------------------------
; The per-frame read. Drops a torn frame rather than retrying, which the
; spec blesses for a consumer that will sample again next frame.
;
; Out: d0 = buttons, Z set if torn or no pad (d0 meaningless, keep the
;      previous frame's). Clobbers d0/d1/d2/a1.

read_pad0:
        move.l  xpad_block,a1
        move.w  XPAD_SEQ_OFF(a1),d1     ; seq before
        move.b  XPAD_ACTIVE_OFF(a1),d2  ; active before

        tst.b   d2                      ; promised 0 or 1, so branch
        bne.s   .use1
        move.l  xpad_buf0,a1
        bra.s   .grab
.use1:  move.l  xpad_buf1,a1

.grab:  tst.b   XPAD_PAD_TYPE(a1)       ; 0 means no pad in this slot
        beq.s   .torn
        move.l  XPAD_PAD_BUTTONS(a1),d0

        move.l  xpad_block,a1           ; recheck BOTH
        cmp.b   XPAD_ACTIVE_OFF(a1),d2
        bne.s   .torn
        cmp.w   XPAD_SEQ_OFF(a1),d1
        bne.s   .torn

        moveq   #-1,d1                  ; clear Z: the read stands
        rts

.torn:  moveq   #0,d0
        rts                             ; Z set: discard

; ----------------------------------------------------------------------

        section bss

xpad_block:     ds.l 1
xpad_buf0:      ds.l 1
xpad_buf1:      ds.l 1
