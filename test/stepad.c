/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * STE joypad driver decode tests.
 *
 * The matrix to Xpad mapping is the only part of that driver with logic
 * worth getting wrong, and it is deliberately free of TOS dependencies,
 * so it is tested here on the host rather than in the emulator. The
 * trampoline, the machine gate and the commit path are covered by the
 * driver's own self test, which needs an ST.
 */

#include <stdio.h>
#include <string.h>

#include "../src/drivers/stepad/decode.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* Everything is active low, so an idle sample is all ones. */
static void idle(XPAD_STE_SAMPLE *s)
{
    memset(s, 0xff, sizeof(*s));
}

static void expect(const XPAD_STE_SAMPLE *s, int pad, uint32_t want,
                   const char *what)
{
    check(xpad_stepad_decode(s, pad) == want, what);
}

int main(void)
{
    XPAD_STE_SAMPLE s;
    unsigned r, bit, i;
    int clean;

    printf("xpad STE joypad decode\n\n");

    idle(&s);
    expect(&s, 0, 0, "an idle sample maps to nothing");
    expect(&s, 1, 0, "on both pads");

    /* Directions. The port reports up, down, left, right in the same
     * order and the same nibble as Xpad, so this is the mapping that
     * had better stay free. */
    idle(&s);
    s.dir[0] = 0xfe;
    expect(&s, 0, XPAD_UP, "bit 0 is UP");
    idle(&s);
    s.dir[0] = 0xfd;
    expect(&s, 0, XPAD_DOWN, "bit 1 is DOWN");
    idle(&s);
    s.dir[0] = 0xfb;
    expect(&s, 0, XPAD_LEFT, "bit 2 is LEFT");
    idle(&s);
    s.dir[0] = 0xf7;
    expect(&s, 0, XPAD_RIGHT, "bit 3 is RIGHT");
    idle(&s);
    s.dir[0] = 0xf6;
    expect(&s, 0, XPAD_UP | XPAD_RIGHT, "diagonals combine");
    idle(&s);
    s.dir[0] = 0xf0;
    expect(&s, 0, XPAD_DPAD, "all four at once");

    /* The high nibble is the other pad, and the two must not bleed. */
    idle(&s);
    s.dir[0] = 0xef;
    expect(&s, 1, XPAD_UP, "pad 1 reads the high nibble");
    expect(&s, 0, 0, "and pad 0 is unaffected");
    idle(&s);
    s.dir[0] = 0xfe;
    expect(&s, 1, 0, "pad 1 ignores the low nibble");

    /* One fire line per pad, meaning a different button in each row. */
    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_FIRE0;
    expect(&s, 0, XPAD_SOUTH, "row 0 on the fire line is A");
    idle(&s);
    s.fire[1] = (uint8_t)~XPAD_STE_FIRE0;
    expect(&s, 0, XPAD_EAST, "row 1 on the fire line is B");
    idle(&s);
    s.fire[2] = (uint8_t)~XPAD_STE_FIRE0;
    expect(&s, 0, XPAD_WEST, "row 2 on the fire line is C");
    idle(&s);
    s.fire[3] = (uint8_t)~XPAD_STE_FIRE0;
    expect(&s, 0, XPAD_SELECT, "row 3 on the fire line is Option");
    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_PAUSE0;
    expect(&s, 0, XPAD_START, "the pause line is Pause");

    /* Pad 1 uses different lines in the same byte. */
    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_FIRE1;
    expect(&s, 1, XPAD_SOUTH, "pad 1 has its own fire line");
    expect(&s, 0, 0, "which pad 0 does not see");
    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_PAUSE1;
    expect(&s, 1, XPAD_START, "pad 1 has its own pause line");
    expect(&s, 0, 0, "which pad 0 does not see either");

    /* Both pads, both lines, one sample. */
    idle(&s);
    s.fire[0] = (uint8_t)~(XPAD_STE_FIRE0 | XPAD_STE_FIRE1);
    expect(&s, 0, XPAD_SOUTH, "both pads press A together, pad 0");
    expect(&s, 1, XPAD_SOUTH, "both pads press A together, pad 1");

    idle(&s);
    s.dir[0] = 0xf0;
    s.fire[0] = (uint8_t)~(XPAD_STE_FIRE0 | XPAD_STE_PAUSE0);
    s.fire[1] = (uint8_t)~XPAD_STE_FIRE0;
    s.fire[2] = (uint8_t)~XPAD_STE_FIRE0;
    s.fire[3] = (uint8_t)~XPAD_STE_FIRE0;
    expect(&s, 0, XPAD_STE_BUTTONS, "every mapped button at once");

    /*
     * The keypad columns share their rows with the fire line and are
     * deliberately unmapped, so no bit of any sample may produce
     * anything outside the set this driver owns.
     */
    clean = 1;
    for (r = 0; r < XPAD_STE_ROWS; r++)
    {
        for (bit = 0; bit < 8; bit++)
        {
            idle(&s);
            s.dir[r] = (uint8_t)~(1u << bit);
            s.fire[r] = (uint8_t)~(1u << bit);

            if (xpad_stepad_decode(&s, 0) & ~XPAD_STE_BUTTONS)
                clean = 0;
            if (xpad_stepad_decode(&s, 1) & ~XPAD_STE_BUTTONS)
                clean = 0;
        }
    }
    check(clean, "no sample sets a bit outside the mapped set");

    /* Rows 1 to 3 of $FF9202 are keypad only, so they must not move a
     * direction however they read. */
    clean = 1;
    for (i = 0; i <= 0xff; i++)
    {
        idle(&s);
        s.dir[1] = (uint8_t)i;
        s.dir[2] = (uint8_t)i;
        s.dir[3] = (uint8_t)i;

        if (xpad_stepad_decode(&s, 0) || xpad_stepad_decode(&s, 1))
            clean = 0;
    }
    check(clean, "the keypad rows never move a direction");

    /* One write has to select the same row on both pads, or pad 1 would
     * need a second pass over the matrix. Active low, so the two bits
     * for a row are the only ones clear. */
    check(XPAD_STE_SELECT(0) == 0xee, "row 0 selects bits 0 and 4");
    check(XPAD_STE_SELECT(1) == 0xdd, "row 1 selects bits 1 and 5");
    check(XPAD_STE_SELECT(2) == 0xbb, "row 2 selects bits 2 and 6");
    check(XPAD_STE_SELECT(3) == 0x77, "row 3 selects bits 3 and 7");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
