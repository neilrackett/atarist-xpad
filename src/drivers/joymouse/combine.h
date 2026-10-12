/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Joystick 1 and the left mouse button, as one pad.
 *
 * Deliberately free of TOS dependencies, like the translate.h it builds
 * on: deciding what button 2 is doing is the only logic in the driver
 * worth getting wrong, so the host build tests it without an emulator.
 *
 * Button 2 is port 0's fire line, which is the left mouse button. The
 * IKBD reports that one wire in two different ways, depending on the
 * mode a program has put it in:
 *
 *   mouse mode      bit 1 of a relative mouse header, %111110LR
 *   joystick mode   bit 7 of joystick 0, in a $FE packet
 *
 * So it is held in one variable that both write, and whichever report
 * came last wins. Two flags ORed together would stick: a game that
 * switches the IKBD to joystick mode while the button is held never
 * sends another mouse packet to release it.
 *
 * The right button is not used. It shares a line with joystick 1's
 * fire, so it could only ever repeat button 1.
 */

#ifndef XPAD_JOYMOUSE_COMBINE_H
#define XPAD_JOYMOUSE_COMBINE_H

#include "../joystick/translate.h"

#define XPAD_IKBD_JOY0       0xfe /* joyvec header: joystick 0 changed */
#define XPAD_IKBD_MOUSE_MASK 0xfc
#define XPAD_IKBD_MOUSE_REL  0xf8 /* relative mouse header, %111110LR  */
#define XPAD_IKBD_MOUSE_LEFT 0x02

typedef struct
{
    uint8_t joy1;   /* joystick 1, as the IKBD packs it */
    uint8_t second; /* port 0's fire line, 0 or 1       */
} XPAD_JOYMOUSE;

/*
 * A joyvec packet, [header, joy0, joy1]. Joystick 1 always counts.
 * Joystick 0 only speaks for button 2 when the header says it is the
 * one that changed: on a $FF packet its byte is whatever TOS last
 * buffered, which in mouse mode is nothing at all.
 */
static void xpad_joymouse_joy(XPAD_JOYMOUSE *s, const uint8_t *pkt)
{
    s->joy1 = pkt[2];

    if (pkt[0] == XPAD_IKBD_JOY0)
        s->second = (pkt[1] & XPAD_IKBD_FIRE) != 0;
}

/*
 * A mousevec packet, [header, dx, dy]. Returns nonzero only when button
 * 2 changed, so a moving mouse, which reports constantly, commits
 * nothing. Absolute reports reach mousevec too, and are not read.
 */
static int xpad_joymouse_mouse(XPAD_JOYMOUSE *s, const uint8_t *pkt)
{
    uint8_t second;

    if ((pkt[0] & XPAD_IKBD_MOUSE_MASK) != XPAD_IKBD_MOUSE_REL)
        return 0;

    second = (pkt[0] & XPAD_IKBD_MOUSE_LEFT) != 0;

    if (second == s->second)
        return 0;

    s->second = second;

    return 1;
}

static uint32_t xpad_joymouse_buttons(const XPAD_JOYMOUSE *s)
{
    uint32_t buttons = xpad_joystick_translate(s->joy1);

    if (s->second)
        buttons |= XPAD_EAST;

    return buttons;
}

#endif /* XPAD_JOYMOUSE_COMBINE_H */
