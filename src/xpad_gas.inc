| SPDX-License-Identifier: BSD-2-Clause
| SPDX-FileCopyrightText: 2026 Neil Rackett
|
| Xpad ABI equates for GNU as. GENERATED from src/xpad.h by
| src/tools/geninc.c: do not edit, run `make inc`. `make test`
| fails if this file drifts from the header.
|
| Offsets past XPAD_HDR_FIXED are omitted on purpose: they hold
| pointers, and nothing an assembly consumer needs lives there.
|
| XPAD_COOKIE and XPAD_MAGIC hold the same value today but are
| separate constants: match the jar tag with the first and the
| block sentinel with the second. Do not rely on them being equal.

| Identity
        .equ    XPAD_COOKIE         , 0x58504144  | jar tag: what a jar walk matches
        .equ    XPAD_MAGIC          , 0x58504144  | block sentinel: XPAD.magic
        .equ    XPAD_VER_MAJOR      , 1          | what validation compares
        .equ    XPAD_VER_MINOR      , 0
        .equ    XPAD_VERSION        , 0x100      | major<<8 | minor
        .equ    XPAD_JAR            , 0x5a0      | cookie jar pointer, needs supervisor

| XPAD header, the architecture independent part
        .equ    XPAD_MAGIC_OFF      , 0          | long
        .equ    XPAD_VERSION_OFF    , 4          | word
        .equ    XPAD_HDRSIZE_OFF    , 6          | word
        .equ    XPAD_PADSOFF_OFF    , 8          | word
        .equ    XPAD_PADSIZE_OFF    , 10         | word
        .equ    XPAD_CAPS_OFF       , 12         | word
        .equ    XPAD_SEQ_OFF        , 14         | word
        .equ    XPAD_PADCOUNT_OFF   , 16         | byte
        .equ    XPAD_ACTIVE_OFF     , 17         | byte, 0 or 1
        .equ    XPAD_HDR_FIXED      , 18         | bytes frozen everywhere

| XPAD_PAD
        .equ    XPAD_PAD_BUTTONS    , 0          | long
        .equ    XPAD_PAD_LX         , 4          | byte, signed
        .equ    XPAD_PAD_LY         , 5          | byte, signed
        .equ    XPAD_PAD_RX         , 6          | byte, signed
        .equ    XPAD_PAD_RY         , 7          | byte, signed
        .equ    XPAD_PAD_LT         , 8          | byte, unsigned
        .equ    XPAD_PAD_RT         , 9          | byte, unsigned
        .equ    XPAD_PAD_TYPE       , 10         | byte, 0 = absent
        .equ    XPAD_PAD_FLAGS      , 11         | byte
        .equ    XPAD_PAD_SIZE_V1    , 12         | floor, never shrinks
        .equ    XPAD_MAX_PADS       , 4

| Buttons. Bit values are frozen; see README.
        .equ    XPAD_UP             , 0x1
        .equ    XPAD_DOWN           , 0x2
        .equ    XPAD_LEFT           , 0x4
        .equ    XPAD_RIGHT          , 0x8
        .equ    XPAD_SOUTH          , 0x10       | A on an Xbox pad
        .equ    XPAD_EAST           , 0x20       | B
        .equ    XPAD_NORTH          , 0x40       | aliased XPAD_X: see the X/Y trap
        .equ    XPAD_WEST           , 0x80       | aliased XPAD_Y: see the X/Y trap
        .equ    XPAD_TL             , 0x100
        .equ    XPAD_TR             , 0x200
        .equ    XPAD_TL2            , 0x400
        .equ    XPAD_TR2            , 0x800
        .equ    XPAD_SELECT         , 0x1000
        .equ    XPAD_START          , 0x2000
        .equ    XPAD_MODE           , 0x4000
        .equ    XPAD_THUMBL         , 0x8000
        .equ    XPAD_THUMBR         , 0x10000    | bit 16: the only one above a word

| Pad types
        .equ    XPAD_TYPE_NONE      , 0          | slot empty
        .equ    XPAD_TYPE_JOYSTICK  , 1
        .equ    XPAD_TYPE_GAMEPAD   , 2
        .equ    XPAD_TYPE_XBOX      , 3
        .equ    XPAD_TYPE_PLAYSTATION, 4
        .equ    XPAD_TYPE_NINTENDO  , 5
        .equ    XPAD_TYPE_KEYBOARD  , 6

| Capabilities
        .equ    XPAD_CAP_ANALOG     , 0x1
        .equ    XPAD_CAP_RUMBLE     , 0x2
        .equ    XPAD_CAP_LED        , 0x4
        .equ    XPAD_CAP_HOTPLUG    , 0x8

| Pad flags
        .equ    XPAD_PAD_ANALOG     , 0x1
        .equ    XPAD_PAD_WIRELESS   , 0x2
        .equ    XPAD_PAD_LOWBATT    , 0x4
