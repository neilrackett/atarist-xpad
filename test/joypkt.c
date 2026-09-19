/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * XPADEMU's packet logic, on the host.
 *
 * The half that can be wrong without anything crashing: a direction
 * that comes out as the wrong bit, a mouse that creeps when the stick
 * is at rest, a y axis pointing the wrong way. All of those look like
 * hardware faults on a real machine, so they are worth settling here.
 */

#include <stdio.h>
#include <string.h>

#include "../src/tools/joypkt.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    JOYPKT_MOUSE m;
    int8_t dx, dy;
    int i;

    printf("XPADEMU packets\n\n");

    /*
     * The direction nibble is the whole point of xpad's bit placement:
     * it crosses into the IKBD byte untouched, so this is a check that
     * the two definitions still agree rather than arithmetic.
     */
    check(joypkt_joystick(XPAD_UP, 0, 0) == 0x01, "up is bit 0");
    check(joypkt_joystick(XPAD_DOWN, 0, 0) == 0x02, "down is bit 1");
    check(joypkt_joystick(XPAD_LEFT, 0, 0) == 0x04, "left is bit 2");
    check(joypkt_joystick(XPAD_RIGHT, 0, 0) == 0x08, "right is bit 3");

    check(joypkt_joystick(XPAD_UP | XPAD_LEFT, 0, 0) == 0x05,
          "and diagonals are just both of them");

    /* Fire is the one bit that has to move. */
    check(joypkt_joystick(XPAD_SOUTH, XPAD_SOUTH, 0) == JOYPKT_FIRE,
          "the fire button reaches bit 7");
    check(joypkt_joystick(XPAD_SOUTH, XPAD_EAST, 0) == 0,
          "and a button that is not fire does not");

    /* A mask, so a pad can have more than one fire button. */
    check(joypkt_joystick(XPAD_EAST, XPAD_SOUTH | XPAD_EAST, 0) ==
              JOYPKT_FIRE,
          "either of two fire buttons will do");

    /* Jump-on-up, which is how most ST games ask for it. */
    check(joypkt_joystick(XPAD_NORTH, XPAD_SOUTH, XPAD_NORTH) == 0x01,
          "a jump button reads as up");
    check(joypkt_joystick(XPAD_NORTH | XPAD_DOWN, XPAD_SOUTH, XPAD_NORTH) ==
              0x03,
          "alongside a direction already held");

    /* Nothing above the four directions may leak into the nibble: the
     * IKBD byte has no room for them and a game would read a
     * direction nobody pressed. */
    check(joypkt_joystick(XPAD_START | XPAD_SELECT | XPAD_MODE, 0, 0) == 0,
          "buttons that are not directions stay out of the nibble");

    /*
     * Spare buttons become fire, which is what stops a pad having ten
     * dead ones. The rule that matters is that a direction never
     * does: firing the gun whenever you walk left would be a fault
     * nobody would think to look for in a fire mask.
     */
    check((joypkt_spare(0) & XPAD_DPAD) == 0,
          "no direction is ever handed out as fire");
    check(joypkt_spare(0) == JOYPKT_ANY_BUTTON,
          "with nothing allocated, every button is fire");

    check((joypkt_spare(XPAD_WEST) & XPAD_WEST) == 0,
          "an allocated button is not also fire");
    check(joypkt_spare(XPAD_WEST) & XPAD_SOUTH,
          "but its neighbours still are");

    check((joypkt_spare(XPAD_WEST | XPAD_THUMBR) &
           (XPAD_WEST | XPAD_THUMBR)) == 0,
          "and so with several allocated");

    /* The shipped defaults: autofire on west, the mouse clicked in on
     * the right stick, everything else shooting. */
    {
        uint32_t fire = joypkt_spare(XPAD_WEST | XPAD_THUMBR);

        check(fire & XPAD_SOUTH, "by default south fires");
        check(fire & XPAD_NORTH, "and north");
        check(fire & XPAD_START, "and even start");
        check(!(fire & XPAD_WEST), "west does not, it autofires");
        check(!(fire & XPAD_THUMBR), "nor the stick you click to click");
    }

    check(joypkt_spare(JOYPKT_ANY_BUTTON) == 0,
          "allocating everything leaves no fire at all");

    /*
     * Autofire. The shape that matters is that a held button produces
     * an alternating stream rather than a steady one: a "fire" that
     * never releases is just fire, and the game never registers a
     * second shot.
     */
    {
        JOYPKT_AUTO a;
        int on = 0, off = 0, changes = 0, last = -1;

        memset(&a, 0, sizeof(a));
        check(joypkt_autofire(&a, 1, 6) == 1,
              "autofire shoots on the first tick it is held");

        memset(&a, 0, sizeof(a));
        for (i = 0; i < 60; i++)
        {
            int f = joypkt_autofire(&a, 1, 6);

            if (f)
                on++;
            else
                off++;

            if (last >= 0 && f != last)
                changes++;

            last = f;
        }

        check(on > 0 && off > 0, "and both holds and releases");
        check(on == off, "evenly, over a whole number of cycles");
        check(changes == 19, "ten shots in sixty ticks at a six tick period");

        /* Letting go resets the phase, so the next press starts with a
         * shot rather than wherever the counter had got to. */
        (void)joypkt_autofire(&a, 0, 6);
        check(joypkt_autofire(&a, 1, 6) == 1, "a fresh press shoots at once");

        /* Not held is not firing, however long you wait. */
        memset(&a, 0, sizeof(a));
        on = 0;
        for (i = 0; i < 60; i++)
            on += joypkt_autofire(&a, 0, 6);

        check(on == 0, "a button nobody is holding never fires");

        /* A period too short to have an off half would be steady fire
         * wearing a disguise, so it is refused outright. */
        memset(&a, 0, sizeof(a));
        on = 0;
        for (i = 0; i < 20; i++)
            on += joypkt_autofire(&a, 1, 1);

        check(on == 0, "a period with no released half does nothing");
    }

    /* Mouse buttons live in the header's bottom two bits. */
    check(joypkt_buttons(XPAD_TR, XPAD_TR, XPAD_TL) == JOYPKT_MOUSE_LEFT,
          "the left click is bit 1");
    check(joypkt_buttons(XPAD_TL, XPAD_TR, XPAD_TL) == JOYPKT_MOUSE_RIGHT,
          "and the right is bit 0");
    check(joypkt_buttons(XPAD_TL | XPAD_TR, XPAD_TR, XPAD_TL) ==
              (JOYPKT_MOUSE_LEFT | JOYPKT_MOUSE_RIGHT),
          "both at once is both bits");

    /*
     * The mouse. A resting stick must produce nothing at all: a cursor
     * that drifts on its own is the single most obvious way this
     * feature can be worse than not having it.
     */
    memset(&m, 0, sizeof(m));
    for (i = 0; i < 200; i++)
        if (joypkt_mouse(&m, 0, 0, 40, 24, &dx, &dy))
            break;

    check(i == 200, "a centred stick never moves the pointer");

    memset(&m, 0, sizeof(m));
    for (i = 0; i < 200; i++)
        if (joypkt_mouse(&m, 39, 39, 40, 24, &dx, &dy))
            break;

    check(i == 200, "nor does one just inside the deadzone");

    /* And just outside it, it must move eventually: a range that is
     * dead at the bottom is what dropping the remainder would give. */
    memset(&m, 0, sizeof(m));
    for (i = 0; i < 200; i++)
        if (joypkt_mouse(&m, 41, 0, 40, 24, &dx, &dy))
            break;

    check(i < 200, "a stick just outside it does move, eventually");

    /* Direction. The IKBD's y axis points up and the screen's points
     * down, so dy is negated on the way out. */
    memset(&m, 0, sizeof(m));
    (void)joypkt_mouse(&m, 127, 0, 40, 24, &dx, &dy);
    check(dx > 0, "a rightward stick moves right");

    memset(&m, 0, sizeof(m));
    (void)joypkt_mouse(&m, -127, 0, 40, 24, &dx, &dy);
    check(dx < 0, "and a leftward one left");

    memset(&m, 0, sizeof(m));
    (void)joypkt_mouse(&m, 0, 127, 40, 24, &dx, &dy);
    check(dy < 0, "a stick pushed down moves down the screen");

    memset(&m, 0, sizeof(m));
    (void)joypkt_mouse(&m, 0, -127, 40, 24, &dx, &dy);
    check(dy > 0, "and one pushed up moves up it");

    /* Faster deflection moves further, or the analogue stick is a
     * digital one with extra steps. */
    {
        int slow = 0, fast = 0;

        memset(&m, 0, sizeof(m));
        for (i = 0; i < 50; i++)
        {
            (void)joypkt_mouse(&m, 60, 0, 40, 24, &dx, &dy);
            slow += dx;
        }

        memset(&m, 0, sizeof(m));
        for (i = 0; i < 50; i++)
        {
            (void)joypkt_mouse(&m, 127, 0, 40, 24, &dx, &dy);
            fast += dx;
        }

        check(fast > slow * 2 - 1, "a further push moves further");
    }

    /* Letting go stops the pointer rather than letting it coast: the
     * accumulated remainder is dropped with the deflection. */
    memset(&m, 0, sizeof(m));
    (void)joypkt_mouse(&m, 127, 127, 40, 24, &dx, &dy);
    (void)joypkt_mouse(&m, 0, 0, 40, 24, &dx, &dy);
    check(dx == 0 && dy == 0, "letting go stops the pointer dead");

    /*
     * Nothing may exceed what one packet carries. A stick slammed into
     * the corner should move fast; wrapping would move it backwards,
     * which is the kind of fault that reads as a broken pad.
     */
    memset(&m, 0, sizeof(m));
    for (i = 0; i < 100; i++)
    {
        (void)joypkt_mouse(&m, 127, -127, 1, 255, &dx, &dy);
        if (dx < 0 || dy < 0)
            break;
    }

    check(i == 100, "a slammed stick never moves backwards");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
