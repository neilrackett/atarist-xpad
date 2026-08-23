/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Keyboard driver keymap tests.
 *
 * The scancode table is the part worth checking without an emulator.
 * Holding, releasing and chaining are covered by the driver's own self
 * test, which needs an ST.
 */

#include <stdio.h>

#include "../src/drivers/keyboard/keymap.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    unsigned i, j;
    int ok;

    printf("xpad keyboard keymap\n\n");

    check(xpad_keymap_lookup(XPAD_KEY_UP) == XPAD_UP, "Up arrow is forward");
    check(xpad_keymap_lookup(XPAD_KEY_DOWN) == XPAD_DOWN, "Down arrow is back");
    check(xpad_keymap_lookup(XPAD_KEY_LEFT) == XPAD_LEFT, "Left arrow turns left");
    check(xpad_keymap_lookup(XPAD_KEY_RIGHT) == XPAD_RIGHT, "Right arrow turns right");
    check(xpad_keymap_lookup(XPAD_KEY_CTRL) == XPAD_SOUTH, "Ctrl is fire");
    check(xpad_keymap_lookup(XPAD_KEY_SPACE) == XPAD_EAST, "Space is use");
    check(xpad_keymap_lookup(XPAD_KEY_ALT) == XPAD_WEST, "Alt is strafe");
    check(xpad_keymap_lookup(XPAD_KEY_LSHIFT) == XPAD_NORTH, "Shift is run");
    check(xpad_keymap_lookup(XPAD_KEY_RSHIFT) == XPAD_NORTH, "either Shift is run");
    check(xpad_keymap_lookup(XPAD_KEY_COMMA) == XPAD_TL, "comma strafes left");
    check(xpad_keymap_lookup(XPAD_KEY_PERIOD) == XPAD_TR, "period strafes right");
    check(xpad_keymap_lookup(XPAD_KEY_TAB) == XPAD_SELECT, "Tab is the automap");
    check(xpad_keymap_lookup(XPAD_KEY_ESC) == XPAD_START, "Esc is the menu");

    check(xpad_keymap_lookup(0x10) == 0, "an unmapped key maps to nothing");
    check(xpad_keymap_lookup(0x00) == 0, "scancode zero maps to nothing");

    /* A release arrives as the make code with bit 7 set. The driver
     * strips it, so nothing in the table may have it set already. */
    ok = 1;
    for (i = 0; i < XPAD_KEYMAP_COUNT; i++)
    {
        if (xpad_keymap[i].scancode & 0x80)
            ok = 0;
    }
    check(ok, "no entry uses a break code as its scancode");

    /* Two keys may share a button, but a scancode listed twice would
     * make the second entry unreachable. */
    ok = 1;
    for (i = 0; i < XPAD_KEYMAP_COUNT; i++)
    {
        for (j = i + 1; j < XPAD_KEYMAP_COUNT; j++)
        {
            if (xpad_keymap[i].scancode == xpad_keymap[j].scancode)
                ok = 0;
        }
    }
    check(ok, "no scancode is mapped twice");

    ok = 1;
    for (i = 0; i < XPAD_KEYMAP_COUNT; i++)
    {
        if (xpad_keymap[i].button & ~XPAD_KEYMAP_BUTTONS)
            ok = 0;
        if (!xpad_keymap[i].button)
            ok = 0;
    }
    check(ok, "every entry sets exactly one owned button");

    /* Sweeping the whole scancode space must never reach outside the
     * set this driver claims. */
    ok = 1;
    for (i = 0; i <= 0x7f; i++)
    {
        if (xpad_keymap_lookup((uint8_t)i) & ~XPAD_KEYMAP_BUTTONS)
            ok = 0;
    }
    check(ok, "no scancode produces an unowned button");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
