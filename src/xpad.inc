; SPDX-License-Identifier: BSD-2-Clause
; SPDX-FileCopyrightText: 2026 Neil Rackett
;
; Xpad ABI equates for vasm/devpac. GENERATED from src/xpad.h by
; src/tools/geninc.c: do not edit, run `make inc`. `make test`
; fails if this file drifts from the header.
;
; Offsets past XPAD_HDR_FIXED are omitted on purpose: they hold
; pointers, and nothing an assembly consumer needs lives there.
;
; XPAD_COOKIE and XPAD_MAGIC hold the same value today but are
; separate constants: match the jar tag with the first and the
; block sentinel with the second. Do not rely on them being equal.

; Identity
XPAD_COOKIE              equ     $58504144  ; jar tag: what a jar walk matches
XPAD_MAGIC               equ     $58504144  ; block sentinel: XPAD.magic
XPAD_VER_MAJOR           equ     1          ; what validation compares
XPAD_VER_MINOR           equ     0
XPAD_VERSION             equ     $100       ; major<<8 | minor
XPAD_JAR                 equ     $5a0       ; cookie jar pointer, needs supervisor

; XPAD header, the architecture independent part
XPAD_MAGIC_OFF           equ     0          ; long
XPAD_VERSION_OFF         equ     4          ; word
XPAD_HDRSIZE_OFF         equ     6          ; word
XPAD_PADSOFF_OFF         equ     8          ; word
XPAD_PADSIZE_OFF         equ     10         ; word
XPAD_CAPS_OFF            equ     12         ; word
XPAD_SEQ_OFF             equ     14         ; word
XPAD_PADCOUNT_OFF        equ     16         ; byte
XPAD_ACTIVE_OFF          equ     17         ; byte, 0 or 1
XPAD_HDR_FIXED           equ     18         ; bytes frozen everywhere

; XPAD_PAD
XPAD_PAD_BUTTONS         equ     0          ; long
XPAD_PAD_LX              equ     4          ; byte, signed
XPAD_PAD_LY              equ     5          ; byte, signed
XPAD_PAD_RX              equ     6          ; byte, signed
XPAD_PAD_RY              equ     7          ; byte, signed
XPAD_PAD_LT              equ     8          ; byte, unsigned
XPAD_PAD_RT              equ     9          ; byte, unsigned
XPAD_PAD_TYPE            equ     10         ; byte, 0 = absent
XPAD_PAD_FLAGS           equ     11         ; byte
XPAD_PAD_SIZE_V1         equ     12         ; floor, never shrinks
XPAD_MAX_PADS            equ     4

; Buttons. Bit values are frozen; see README.
XPAD_UP                  equ     $1
XPAD_DOWN                equ     $2
XPAD_LEFT                equ     $4
XPAD_RIGHT               equ     $8
XPAD_SOUTH               equ     $10        ; A on an Xbox pad
XPAD_EAST                equ     $20        ; B
XPAD_NORTH               equ     $40        ; aliased XPAD_X: see the X/Y trap
XPAD_WEST                equ     $80        ; aliased XPAD_Y: see the X/Y trap
XPAD_TL                  equ     $100
XPAD_TR                  equ     $200
XPAD_TL2                 equ     $400
XPAD_TR2                 equ     $800
XPAD_SELECT              equ     $1000
XPAD_START               equ     $2000
XPAD_MODE                equ     $4000
XPAD_THUMBL              equ     $8000
XPAD_THUMBR              equ     $10000     ; bit 16: the only one above a word

; Pad types
XPAD_TYPE_NONE           equ     0          ; slot empty
XPAD_TYPE_JOYSTICK       equ     1
XPAD_TYPE_GAMEPAD        equ     2
XPAD_TYPE_XBOX           equ     3
XPAD_TYPE_PLAYSTATION    equ     4
XPAD_TYPE_NINTENDO       equ     5
XPAD_TYPE_KEYBOARD       equ     6

; Capabilities
XPAD_CAP_ANALOG          equ     $1
XPAD_CAP_RUMBLE          equ     $2
XPAD_CAP_LED             equ     $4
XPAD_CAP_HOTPLUG         equ     $8

; Pad flags
XPAD_PAD_ANALOG          equ     $1
XPAD_PAD_WIRELESS        equ     $2
XPAD_PAD_LOWBATT         equ     $4
