/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * The viewer's keys.
 *
 * Hatari's --auto takes a path and no arguments, so no automated run
 * can press a key in the live loop: hatari-view exercises the one-shot
 * snapshot path and nothing else. These keys were therefore the only
 * part of the program with no coverage at all, which is how Q came to
 * be reported doing nothing on a real machine while Escape worked.
 *
 * Host only, like the driver tests: the decision has no TOS in it.
 */

#include <stdio.h>

#include "../src/tools/viewkeys.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

int main(void)
{
    int i;

    printf("viewer keys\n\n");

    /* Quitting, by both keys and by both routes. The scancode route is
     * the one that matters: TOS maps a key to ASCII through the
     * national keyboard table in ROM, so the key with Q printed on it
     * does not send 'q' on every machine, and the legend on screen is
     * naming the printed letter. */
    check(view_key('q', 0) == VIEW_QUIT, "q quits");
    check(view_key('Q', 0) == VIEW_QUIT, "and so does shifted Q");
    check(view_key(27, 0) == VIEW_QUIT, "Escape quits");
    check(view_key(0, VIEW_SCAN_Q) == VIEW_QUIT,
          "the Q key quits whatever ASCII it sends");
    check(view_key(0, VIEW_SCAN_ESC) == VIEW_QUIT, "and so does its Escape");

    /* Selecting a pad. Four keys, and the caller subtracts. */
    for (i = 0; i < 4; i++)
        if (view_key((char)('1' + i), 0) != VIEW_PAD_0 + i)
            break;

    check(i == 4, "1 to 4 select the four pads, in order");
    check(view_key('5', 0) == VIEW_NOTHING, "5 selects nothing");
    check(view_key('0', 0) == VIEW_NOTHING, "nor does 0");

    /* The rumble keys, which must not be confusable with each other:
     * the whole reason they are separate is to tell one motor from the
     * other, so a test that let them collide would defeat the point. */
    check(view_key('(', 0) - VIEW_RUMBLE_0 == VIEW_RUMBLE_LEFT,
          "( rumbles the left motor");
    check(view_key(')', 0) - VIEW_RUMBLE_0 == VIEW_RUMBLE_RIGHT,
          "and ) the right");
    check(view_key('*', 0) - VIEW_RUMBLE_0 == VIEW_RUMBLE_BOTH,
          "* rumbles both");

    check(view_key(0, VIEW_SCAN_KP_LPAREN) - VIEW_RUMBLE_0 ==
              VIEW_RUMBLE_LEFT,
          "the keypad's ( works without its ASCII");
    check(view_key(0, VIEW_SCAN_KP_RPAREN) - VIEW_RUMBLE_0 ==
              VIEW_RUMBLE_RIGHT,
          "and so does its )");
    check(view_key(0, VIEW_SCAN_KP_STAR) - VIEW_RUMBLE_0 == VIEW_RUMBLE_BOTH,
          "and its *");

    check(VIEW_RUMBLE_LEFT != VIEW_RUMBLE_RIGHT,
          "the two motors are not the same request");
    check(VIEW_RUMBLE_BOTH == (VIEW_RUMBLE_LEFT | VIEW_RUMBLE_RIGHT),
          "and both is the two of them together");

    /* The verdict carries the mask the viewer writes, so a key that
     * asked for one motor cannot reach the request area as the other.
     * Nothing enforced that while the two were separate enumerations
     * joined by a switch. */
    check((view_key('(', 0) - VIEW_RUMBLE_0) == VIEW_RUMBLE_LEFT &&
              (view_key(')', 0) - VIEW_RUMBLE_0) == VIEW_RUMBLE_RIGHT,
          "the mask survives the trip from key to request");

    /* Anything else is ignored rather than mistaken for something. A
     * viewer that quit on a stray byte would be worse than one that
     * ignored a key. */
    check(view_key('a', 0) == VIEW_NOTHING, "an unbound letter does nothing");
    check(view_key(0, 0) == VIEW_NOTHING, "nor does a null");
    check(view_key(' ', 0) == VIEW_NOTHING, "nor space");
    check(view_key('\r', 0) == VIEW_NOTHING, "nor return");

    /* The two payload-carrying verdicts have to stay clear of each
     * other and of everything below them, or a pad selection would be
     * read as a rumble or the other way round. */
    check(VIEW_PAD_0 > VIEW_RUMBLE_0 + VIEW_RUMBLE_BOTH,
          "pad selections sort above every rumble");
    check(VIEW_RUMBLE_0 > VIEW_QUIT && VIEW_RUMBLE_0 > VIEW_NOTHING,
          "and rumbles above the verdicts that carry nothing");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
