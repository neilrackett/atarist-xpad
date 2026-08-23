/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * ST keyboard scancodes to XPad buttons, using DOOM's default controls.
 *
 * Free of TOS dependencies so the host build tests it without an
 * emulator, the same split the joystick driver uses.
 */

#ifndef XPAD_KEYBOARD_KEYMAP_H
#define XPAD_KEYBOARD_KEYMAP_H

#include "../../xpad.h"

/* ST scancodes. Make codes as sent; a release is the same value with
 * bit 7 set, which the caller strips before looking up. */
#define XPAD_KEY_ESC     0x01
#define XPAD_KEY_TAB     0x0f
#define XPAD_KEY_CTRL    0x1d
#define XPAD_KEY_LSHIFT  0x2a
#define XPAD_KEY_COMMA   0x33
#define XPAD_KEY_PERIOD  0x34
#define XPAD_KEY_RSHIFT  0x36
#define XPAD_KEY_ALT     0x38
#define XPAD_KEY_SPACE   0x39
#define XPAD_KEY_UP      0x48
#define XPAD_KEY_LEFT    0x4b
#define XPAD_KEY_RIGHT   0x4d
#define XPAD_KEY_DOWN    0x50

typedef struct
{
    uint8_t scancode;
    uint32_t button;
} XPAD_KEYMAP;

/*
 * DOOM's keyboard defaults, mapped by what the key does rather than
 * where it sits: fire is the primary button, use is secondary, and the
 * two strafe keys become shoulders.
 *
 * Deliberately unmapped, because DOOM has no equivalent: XPAD_TL2,
 * XPAD_TR2, XPAD_MODE, XPAD_THUMBL and XPAD_THUMBR. A keyboard pad that
 * claimed them would be inventing controls.
 */
static const XPAD_KEYMAP xpad_keymap[] = {
    {XPAD_KEY_UP, XPAD_UP},         /* forward           */
    {XPAD_KEY_DOWN, XPAD_DOWN},     /* back              */
    {XPAD_KEY_LEFT, XPAD_LEFT},     /* turn left         */
    {XPAD_KEY_RIGHT, XPAD_RIGHT},   /* turn right        */
    {XPAD_KEY_CTRL, XPAD_SOUTH},    /* fire              */
    {XPAD_KEY_SPACE, XPAD_EAST},    /* use, open         */
    {XPAD_KEY_ALT, XPAD_WEST},      /* strafe modifier   */
    {XPAD_KEY_LSHIFT, XPAD_NORTH},  /* run               */
    {XPAD_KEY_RSHIFT, XPAD_NORTH},  /* run               */
    {XPAD_KEY_COMMA, XPAD_TL},      /* strafe left       */
    {XPAD_KEY_PERIOD, XPAD_TR},     /* strafe right      */
    {XPAD_KEY_TAB, XPAD_SELECT},    /* automap           */
    {XPAD_KEY_ESC, XPAD_START}      /* menu              */
};

#define XPAD_KEYMAP_COUNT (sizeof(xpad_keymap) / sizeof(xpad_keymap[0]))

/* Buttons this driver can ever set. Nothing outside it may appear. */
#define XPAD_KEYMAP_BUTTONS                                        \
    (XPAD_DPAD | XPAD_SOUTH | XPAD_EAST | XPAD_WEST | XPAD_NORTH | \
     XPAD_TL | XPAD_TR | XPAD_SELECT | XPAD_START)

/* The XPad button for a make code, or 0 when the key is not mapped. */
static uint32_t xpad_keymap_lookup(uint8_t scancode)
{
    unsigned i;

    for (i = 0; i < XPAD_KEYMAP_COUNT; i++)
    {
        if (xpad_keymap[i].scancode == scancode)
            return xpad_keymap[i].button;
    }

    return 0;
}

#endif /* XPAD_KEYBOARD_KEYMAP_H */
