/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * IKBD joystick byte to XPad buttons.
 *
 * Deliberately free of TOS dependencies: this is the only part of the
 * driver with logic that can be wrong, so the host build tests it
 * without an emulator.
 */

#ifndef XPAD_JOYSTICK_TRANSLATE_H
#define XPAD_JOYSTICK_TRANSLATE_H

#include "../../xpad.h"

/*
 * The IKBD packs a joystick into one byte:
 *
 *   bit0 up, bit1 down, bit2 left, bit3 right, bit7 fire
 *
 * XPAD_UP, DOWN, LEFT and RIGHT are 0x01, 0x02, 0x04 and 0x08: the same
 * four directions, in the same order, in the same nibble. So the
 * directions carry across untouched and only fire has to move. That is
 * the d-pad bit placement in AGENTS.md paying off.
 */

#define XPAD_IKBD_DIRS 0x0f
#define XPAD_IKBD_FIRE 0x80

static uint32_t xpad_joystick_translate(uint8_t ikbd)
{
    uint32_t buttons = (uint32_t)(ikbd & XPAD_IKBD_DIRS);

    if (ikbd & XPAD_IKBD_FIRE)
        buttons |= XPAD_SOUTH;

    return buttons;
}

#endif /* XPAD_JOYSTICK_TRANSLATE_H */
