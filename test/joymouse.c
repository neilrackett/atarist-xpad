/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Joystick and mouse driver tests.
 *
 * Which report owns button 2 is the only part of that driver with logic
 * worth getting wrong, and it is deliberately free of TOS dependencies,
 * so it is tested here on the host. The trampolines and the commit path
 * are covered by the driver's own self test, which needs an ST.
 */

#include <stdio.h>

#include "../src/drivers/joymouse/combine.h"

static int failures;
static XPAD_JOYMOUSE s;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void joy(uint8_t header, uint8_t joy0, uint8_t joy1)
{
    uint8_t pkt[3] = {header, joy0, joy1};

    xpad_joymouse_joy(&s, pkt);
}

static int mouse(uint8_t header)
{
    uint8_t pkt[3] = {header, 0, 0};

    return xpad_joymouse_mouse(&s, pkt);
}

int main(void)
{
    unsigned i;
    int clean = 1;

    printf("xpad joystick and mouse\n\n");

    check(xpad_joymouse_buttons(&s) == 0, "idle maps to nothing");

    joy(0xff, 0, 0x8f);
    check(xpad_joymouse_buttons(&s) == (XPAD_DPAD | XPAD_SOUTH),
          "joystick 1 translates as the joystick driver");
    joy(0xff, 0, 0);

    check(mouse(0xfa) && xpad_joymouse_buttons(&s) == XPAD_EAST,
          "left click is button 2, and a change");
    check(!mouse(0xfa), "holding it is not a change");
    check(!mouse(0xfb) && xpad_joymouse_buttons(&s) == XPAD_EAST,
          "the right button changes nothing");
    check(mouse(0xf8) && xpad_joymouse_buttons(&s) == 0,
          "releasing it is a change");
    check(!mouse(0xf9) && xpad_joymouse_buttons(&s) == 0,
          "the right button alone is not button 2");

    /* $F7 is an absolute report, $F6 a status reply and $FE a joystick
     * header. None is a relative mouse packet, so none may move it. */
    check(!mouse(0xf7) && !mouse(0xf6) && !mouse(0xfe) &&
              xpad_joymouse_buttons(&s) == 0,
          "only relative mouse headers are read");

    joy(0xfe, 0x80, 0);
    check(xpad_joymouse_buttons(&s) == XPAD_EAST,
          "joystick 0 fire is button 2");
    joy(0xfe, 0x0f, 0);
    check(xpad_joymouse_buttons(&s) == 0,
          "joystick 0 directions are ignored");
    joy(0xff, 0x80, 0);
    check(xpad_joymouse_buttons(&s) == 0,
          "a stale joystick 0 byte is ignored");

    /* One wire, so the last report wins, whichever vector it came
     * through. Two flags ORed would leave this stuck down. */
    mouse(0xfa);
    joy(0xfe, 0x00, 0);
    check(xpad_joymouse_buttons(&s) == 0,
          "a joystick report releases a click");
    joy(0xfe, 0x80, 0);
    check(mouse(0xf8) && xpad_joymouse_buttons(&s) == 0,
          "and a mouse report releases joystick 0");

    /* Nothing may ever set a bit the provider does not own. */
    for (i = 0; i <= 0xff; i++)
    {
        joy(0xfe, (uint8_t)i, (uint8_t)i);
        mouse((uint8_t)i);

        if (xpad_joymouse_buttons(&s) & ~(XPAD_DPAD | XPAD_SOUTH | XPAD_EAST))
            clean = 0;
    }
    check(clean, "no input sets a bit it does not own");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
