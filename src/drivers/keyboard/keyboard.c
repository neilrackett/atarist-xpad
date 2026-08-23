/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad keyboard driver: publishes the ST keyboard as a single pad,
 * using DOOM's default controls.
 *
 * EXAMPLE DRIVER. Written to be read and copied as much as to be used.
 * Its counterpart in drivers/joystick is smaller; this one additionally
 * shows tracking held state across separate press and release events,
 * and refusing to install where the machine cannot support it. See
 * "Example drivers" in README for the full list of what it does not do.
 *
 * Useful on a machine with no joystick at all, and as something to test
 * a consumer against when no controller is to hand.
 *
 *   XPADKEY        install and stay resident
 *   XPADKEY -t     run the self test and exit, installing nothing
 *
 * Requires TOS 2.0 or later, and refuses to install below it. Reading
 * which keys are held needs make and break codes, and the only vector
 * that carries them is kbdvec, four bytes below Kbdvbase(), which older
 * TOS does not have. Getting them out of TOS 1.x means driving the
 * ACIA through ikbdsys, where reading the data register destroys the
 * status bits, so a driver cannot both look at a byte and let TOS have
 * it. EmuTOS reports 2.06 and does have kbdvec.
 *
 * Limits:
 *
 *   - no analogue, so XPAD_CAP_ANALOG is not claimed
 *   - XPAD_TL2, XPAD_TR2, XPAD_MODE, XPAD_THUMBL and XPAD_THUMBR are
 *     unmapped: DOOM has no key that means them
 *   - seq advances when a mapped key changes, not per frame
 *   - a keyboard is not a pad: keyboard matrices limit which
 *     combinations of keys register together, so some multi-key holds a
 *     game would expect from a controller will not all arrive
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <stdio.h>
#include <string.h>

#include "../../xpad.h"
#include "keymap.h"

#define PROVIDER "IKBD keyboard 1.0"

/* kbdvec sets bit 7 of the code to mean the key came up. */
#define KEY_RELEASED 0x80
#define KEY_CODE 0x7f

static XPAD block;
static uint32_t held;

/* Read by the trampoline in kbdvec.s. */
void (*xpad_kbdvec_chain)(void);

extern void xpad_kbdvec_entry(void);
extern void xpad_kbdvec_call(void (*fn)(void), int code);

/*
 * Called from the trampoline, in interrupt context. Nothing here may
 * allocate, do I/O, or call anything that is not reentrant.
 */
void xpad_kbdvec_update(int code)
{
    uint32_t button = xpad_keymap_lookup((uint8_t)(code & KEY_CODE));
    XPAD_PAD *back;

    /* Most keys are not mapped. Leaving the block alone saves a commit
     * and keeps seq meaningful: it counts changes we care about. */
    if (!button)
        return;

    if (code & KEY_RELEASED)
        held &= ~button;
    else
        held |= button;

    back = xpad_back(&block);
    back[0].buttons = held;

    xpad_commit(&block);
}

static void init_block(void)
{
    int i;

    held = 0;

    xpad_init(&block, 1, 0, PROVIDER, 0);

    /* Both buffers, once, so the interrupt path only writes buttons. */
    for (i = 0; i < 2; i++)
    {
        xpad_back(&block)[0].type = XPAD_TYPE_KEYBOARD;
        xpad_commit(&block);
    }
}

/* ------------------------------------------------------------------ */
/* TOS version                                                         */
/* ------------------------------------------------------------------ */

static unsigned short tos_version;

/*
 * The OS header pointer lives at 0x4f2, below 0x800, and the ST bus
 * errors on user mode access down there. Hence Supexec.
 */
static long read_tos_version(void)
{
    char *sysbase = *(char **)0x4f2L;

    tos_version = *(unsigned short *)(sysbase + 2);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

static int failures;
static int chain_calls;

static void check(int ok, const char *what)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void chain_stub(void)
{
    chain_calls++;
}

static void press(uint8_t scancode)
{
    xpad_kbdvec_call(xpad_kbdvec_entry, scancode);
}

static void release(uint8_t scancode)
{
    xpad_kbdvec_call(xpad_kbdvec_entry, scancode | KEY_RELEASED);
}

static uint32_t buttons(void)
{
    XPAD_PAD pad;

    if (!xpad_read(&block, 0, &pad))
        return 0xffffffffUL; /* cannot happen; fails the check loudly */

    return pad.buttons;
}

static int selftest(void)
{
    XPAD_PAD pad;
    uint16_t before;
    unsigned i;
    int ok;

    init_block();
    xpad_kbdvec_chain = chain_stub;

    Supexec(read_tos_version);
    printf("TOS %x.%02x\n\n", tos_version >> 8, tos_version & 0xff);

    press(XPAD_KEY_CTRL);
    check(buttons() == XPAD_SOUTH, "Ctrl is fire");
    release(XPAD_KEY_CTRL);
    check(buttons() == 0, "releasing Ctrl clears it");

    press(XPAD_KEY_UP);
    check(buttons() == XPAD_UP, "Up arrow is forward");
    press(XPAD_KEY_CTRL);
    check(buttons() == (XPAD_UP | XPAD_SOUTH), "keys accumulate while held");
    release(XPAD_KEY_UP);
    check(buttons() == XPAD_SOUTH, "releasing one leaves the other held");
    release(XPAD_KEY_CTRL);
    check(buttons() == 0, "releasing both clears everything");

    press(XPAD_KEY_SPACE);
    check(buttons() == XPAD_EAST, "Space is use");
    release(XPAD_KEY_SPACE);

    press(XPAD_KEY_COMMA);
    press(XPAD_KEY_PERIOD);
    check(buttons() == (XPAD_TL | XPAD_TR), "comma and period are the strafes");
    release(XPAD_KEY_COMMA);
    release(XPAD_KEY_PERIOD);

    /* Both shift keys mean run, so either alone sets it and releasing
     * one while the other is held still clears it: one bit, two keys. */
    press(XPAD_KEY_LSHIFT);
    check(buttons() == XPAD_NORTH, "left shift is run");
    release(XPAD_KEY_LSHIFT);
    press(XPAD_KEY_RSHIFT);
    check(buttons() == XPAD_NORTH, "right shift is run too");
    release(XPAD_KEY_RSHIFT);
    check(buttons() == 0, "run clears");

    /* An unmapped key must not disturb anything. */
    press(XPAD_KEY_CTRL);
    before = block.seq;
    press(0x10); /* Q */
    release(0x10);
    check(block.seq == before, "unmapped keys do not commit");
    check(buttons() == XPAD_SOUTH, "unmapped keys do not change the pad");
    release(XPAD_KEY_CTRL);

    /* The regression that would stop the keyboard working machine wide. */
    check(chain_calls > 0, "every key reached the displaced handler");

    check(xpad_read(&block, 0, &pad) && pad.type == XPAD_TYPE_KEYBOARD,
          "the slot reports a keyboard");
    check(xpad_connected(&block) == 1, "one pad is connected");

    check(xpad_read(&block, 0, &pad) &&
              !pad.lx && !pad.ly && !pad.rx && !pad.ry && !pad.lt && !pad.rt,
          "axes and triggers stay zero");

    /* Every mapped key must round trip, and none may set a bit outside
     * what this driver owns. */
    ok = 1;
    for (i = 0; i < XPAD_KEYMAP_COUNT; i++)
    {
        uint32_t held;

        press(xpad_keymap[i].scancode);
        held = buttons();

        if ((held & xpad_keymap[i].button) != xpad_keymap[i].button)
            ok = 0;
        if (held & ~XPAD_KEYMAP_BUTTONS)
            ok = 0;

        release(xpad_keymap[i].scancode);
    }
    check(ok, "every mapped key sets its own button and no other");
    check(buttons() == 0, "the keymap sweep leaves nothing held");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    printf("XPAD-DONE %d\n", failures ? 1 : 0);
    fflush(stdout);

    return failures ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

static _KBDVECS *vecs;

/* kbdvec sits four bytes below the documented KBDVECS block. Vector
 * surgery belongs in supervisor mode. */
static long install_vector(void)
{
    long *slot = &((long *)vecs)[-1];

    xpad_kbdvec_chain = (void (*)(void))*slot;
    *slot = (long)xpad_kbdvec_entry;

    return 0;
}

static int install(void)
{
    Supexec(read_tos_version);

    if (tos_version < 0x0200)
    {
        printf("Needs TOS 2.0 or later; this is %x.%02x.\n",
               tos_version >> 8, tos_version & 0xff);
        printf("Older TOS has no kbdvec, so key releases cannot be seen.\n");
        return 0;
    }

    if (xpad_find())
    {
        printf("An Xpad provider is already installed. Leaving it alone.\n");
        return 0;
    }

    init_block();

    if (!xpad_publish(&block))
    {
        printf("Could not install the XPAD cookie. Is the jar full?\n");
        return 0;
    }

    vecs = Kbdvbase();
    Supexec(install_vector);

    printf("%s installed: DOOM controls as pad 0.\n", PROVIDER);

    return 1;
}


int main(int argc, char **argv)
{
#ifdef XPAD_SELFTEST
    /*
     * Hatari's --auto takes a path and no arguments, so the harness has
     * no way to ask for a mode. This build supplies the command line it
     * would have passed and then runs the ordinary parsing below, so
     * the only thing the tested binary does differently from the
     * shipped one is where argv came from. Replacing the parsing here
     * instead, which is what this used to do, left the flags README
     * documents with no test at all.
     */
    static char *test_args[] = {"XPADKEY", "-t"};

    argc = 2;
    argv = test_args;
#endif

    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        return selftest();

    if (!install())
        return 1;

    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
}
