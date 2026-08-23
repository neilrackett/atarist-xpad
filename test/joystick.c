/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Joystick driver translation tests.
 *
 * The IKBD to XPad mapping is the only part of that driver with logic
 * worth getting wrong, and it is deliberately free of TOS dependencies,
 * so it is tested here on the host rather than in the emulator. The
 * trampoline and the commit path are covered by the driver's own self
 * test, which needs an ST.
 */

#include <stdio.h>

#include "../src/drivers/joystick/translate.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void expect(uint8_t ikbd, uint32_t want, const char *what)
{
    check(xpad_joystick_translate(ikbd) == want, what);
}

int main(void)
{
    unsigned i;
    int clean = 1;

    printf("xpad joystick translation\n\n");

    expect(0x00, 0, "idle maps to nothing");
    expect(0x01, XPAD_UP, "bit 0 is UP");
    expect(0x02, XPAD_DOWN, "bit 1 is DOWN");
    expect(0x04, XPAD_LEFT, "bit 2 is LEFT");
    expect(0x08, XPAD_RIGHT, "bit 3 is RIGHT");
    expect(0x80, XPAD_SOUTH, "bit 7 is the button");

    expect(0x09, XPAD_UP | XPAD_RIGHT, "up and right combine");
    expect(0x06, XPAD_DOWN | XPAD_LEFT, "down and left combine");
    expect(0x81, XPAD_UP | XPAD_SOUTH, "direction and button combine");
    expect(0x8f, XPAD_DPAD | XPAD_SOUTH, "everything at once");

    /* The mapping exists because these two agree; if the d-pad bits ever
     * move, this is the check that says so before hardware does. */
    expect(0x0f, XPAD_DPAD, "the direction nibble carries across intact");

    /* Bits 4 to 6 are not reported by the IKBD. Whatever appears there
     * must not leak into the button set. */
    for (i = 0x10; i <= 0x70; i += 0x10)
    {
        if (xpad_joystick_translate((uint8_t)i) != 0)
            clean = 0;
    }
    check(clean, "unused IKBD bits are ignored");

    /* Nothing may ever set a bit the provider does not own. */
    clean = 1;
    for (i = 0; i <= 0xff; i++)
    {
        if (xpad_joystick_translate((uint8_t)i) & ~(XPAD_DPAD | XPAD_SOUTH))
            clean = 0;
    }
    check(clean, "no input produces a bit outside DPAD and SOUTH");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
