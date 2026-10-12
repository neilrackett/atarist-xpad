/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad joystick and mouse driver: joystick 1 as one pad, with the left
 * mouse button as its second button.
 *
 * EXAMPLE DRIVER. Written to be read and copied as much as to be used.
 * It is the joystick driver plus one thing: a pad assembled from two
 * vectors rather than one, with the joyvec trampoline and the IKBD
 * translation taken from drivers/joystick as they are. See
 * docs/drivers.md for the full list of what it does not do.
 *
 * Port 0's fire line is the left mouse button, so button 2 is a mouse
 * clicked in port 0, or a two button joystick wired so its second
 * button reaches pin 6 of port 0.
 *
 *   XPADJM         install and stay resident
 *   XPADJM -t      run the self test and exit, installing nothing
 *
 * Limits:
 *
 *   - two buttons: fire is XPAD_SOUTH and the left button XPAD_EAST.
 *     The right button shares joystick 1's fire line, so it adds none
 *   - no analogue: moving the mouse does nothing
 *   - no presence detection, so the pad is always reported connected
 *   - seq advances on every joystick packet, but only on a change of
 *     button 2 from the mouse, which reports constantly while it moves
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <stdio.h>
#include <string.h>

#include "../../xpad.h"
#include "combine.h"

#define PROVIDER "IKBD joystick and mouse 1.0"

#define PAD_COUNT 1

static XPAD block;
static XPAD_JOYMOUSE state;

/* Read by the trampolines in joyvec.s and mousevec.s. */
void (*xpad_joyvec_chain)(void *);
void (*xpad_mousevec_chain)(void *);

extern void xpad_joyvec_entry(void *pkt);
extern void xpad_mousevec_entry(void *pkt);
extern void xpad_joyvec_call(void (*fn)(void *), const void *pkt);

static void publish(void)
{
    XPAD_PAD *back = xpad_back(&block);

    back[0].buttons = xpad_joymouse_buttons(&state);

    xpad_commit(&block);
}

/*
 * Both called from their trampolines, in interrupt context. Nothing
 * here may allocate, do I/O, or call anything that is not reentrant.
 */
void xpad_joyvec_update(const uint8_t *pkt)
{
    xpad_joymouse_joy(&state, pkt);
    publish();
}

void xpad_mousevec_update(const uint8_t *pkt)
{
    if (xpad_joymouse_mouse(&state, pkt))
        publish();
}

static void init_block(void)
{
    int i;

    xpad_init(&block, PAD_COUNT, 0, PROVIDER, 0);

    /* Mark the slot occupied in both buffers, once, so the interrupt
     * path only ever has to write buttons. Two commits also leave
     * active back where xpad_init put it. */
    for (i = 0; i < 2; i++)
    {
        xpad_back(&block)[0].type = XPAD_TYPE_GAMEPAD;
        xpad_commit(&block);
    }
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

static int failures;
static int joy_chain_calls;
static int mouse_chain_calls;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void joy_chain_stub(void *pkt)
{
    (void)pkt;
    joy_chain_calls++;
}

static void mouse_chain_stub(void *pkt)
{
    (void)pkt;
    mouse_chain_calls++;
}

/*
 * Drive the real trampolines with fabricated packets, with stubs in
 * place of the displaced handlers so nothing reaches TOS. The joyvec
 * test helper suits both: all it does is call a handler with the
 * packet in a0, which is the convention mousevec shares.
 */
static void joy(uint8_t header, uint8_t joy0, uint8_t joy1)
{
    /* The header sits at an odd address, as TOS delivers it. */
    static uint16_t aligned[4];
    uint8_t *pkt = (uint8_t *)aligned + 1;

    pkt[0] = header;
    pkt[1] = joy0;
    pkt[2] = joy1;

    xpad_joyvec_call(xpad_joyvec_entry, pkt);
}

static void mouse(uint8_t header, int8_t dx, int8_t dy)
{
    static uint8_t pkt[3];

    pkt[0] = header;
    pkt[1] = (uint8_t)dx;
    pkt[2] = (uint8_t)dy;

    xpad_joyvec_call(xpad_mousevec_entry, pkt);
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

    init_block();
    xpad_joyvec_chain = joy_chain_stub;
    xpad_mousevec_chain = mouse_chain_stub;

    joy(0xff, 0, 0x00);
    check(buttons() == 0, "centred stick reads no buttons");
    joy(0xff, 0, 0x09);
    check(buttons() == (XPAD_UP | XPAD_RIGHT), "joystick 1 is the d-pad");
    joy(0xff, 0, 0x80);
    check(buttons() == XPAD_SOUTH, "joystick 1 fire is button 1");
    joy(0xff, 0, 0x00);

    mouse(0xfa, 0, 0);
    check(buttons() == XPAD_EAST, "left click is button 2");
    mouse(0xfa, 5, -3);
    check(buttons() == XPAD_EAST, "and stays held while the mouse moves");
    mouse(0xf8, 0, 0);
    check(buttons() == 0, "releasing it clears button 2");
    mouse(0xf9, 0, 0);
    check(buttons() == 0, "the right button is not button 2");
    mouse(0xf8, 0, 0);

    mouse(0xfa, 0, 0);
    joy(0xff, 0, 0x84);
    check(buttons() == (XPAD_LEFT | XPAD_SOUTH | XPAD_EAST),
          "stick, fire and click combine");
    joy(0xff, 0, 0x00);
    check(buttons() == XPAD_EAST, "a joystick packet keeps the click");
    mouse(0xf8, 0, 0);

    /* Moving the mouse is most of what it reports. None of it may
     * cost a commit. */
    before = block.seq;
    mouse(0xf8, 10, 10);
    mouse(0xf8, -10, 0);
    check(block.seq == before, "mouse movement alone does not commit");

    mouse(0xf7, 0, 0);
    check(buttons() == 0 && block.seq == before,
          "an absolute report is ignored");

    /* Joystick mode: the same wire, reported as joystick 0's fire. */
    joy(0xfe, 0x80, 0);
    check(buttons() == XPAD_EAST, "joystick 0 fire is button 2");
    joy(0xfe, 0x8f, 0);
    check(buttons() == XPAD_EAST, "joystick 0 directions are ignored");
    joy(0xfe, 0x00, 0);
    check(buttons() == 0, "and its release clears it");

    /* A $FF packet carries joystick 0's buffered byte too, but only a
     * $FE packet means it changed. */
    joy(0xff, 0x80, 0);
    check(buttons() == 0, "a stale joystick 0 byte is ignored");

    /* Held as a game switches the IKBD to joystick mode: the release
     * arrives as a joystick report and must still clear it. */
    mouse(0xfa, 0, 0);
    joy(0xfe, 0x00, 0);
    check(buttons() == 0, "a click released in joystick mode clears");

    check(joy_chain_calls > 0, "every joystick packet was chained");
    check(mouse_chain_calls > 0, "every mouse packet was chained");

    check(xpad_read(&block, 0, &pad) && pad.type == XPAD_TYPE_GAMEPAD,
          "the slot reports a gamepad");
    check(xpad_connected(&block) == PAD_COUNT, "one pad is connected");
    check(xpad_read(&block, PAD_COUNT, &pad) == 0, "there is no second pad");

    check(xpad_read(&block, 0, &pad) &&
              !pad.lx && !pad.ly && !pad.rx && !pad.ry && !pad.lt && !pad.rt,
          "axes and triggers stay zero");
    check(block.caps == 0, "no capabilities are claimed");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    printf("XPAD-DONE %d\n", failures ? 1 : 0);
    fflush(stdout);

    return failures ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

static _KBDVECS *vecs;

/* Vector surgery belongs in supervisor mode: user mode gets away with
 * it on a bare ST, but not under a memory protected kernel. */
static long install_vectors(void)
{
    xpad_joyvec_chain = vecs->joyvec;
    vecs->joyvec = xpad_joyvec_entry;

    xpad_mousevec_chain = vecs->mousevec;
    vecs->mousevec = xpad_mousevec_entry;

    return 0;
}

static int install(void)
{
    /* Xpad is single provider and the last one to publish wins, so a
     * shim like this must not displace something better. */
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
    Supexec(install_vectors);

    printf("%s installed: joystick 1 and the left button as pad 0.\n",
           PROVIDER);

    return 1;
}


int main(int argc, char **argv)
{
#ifdef XPAD_SELFTEST
    /* As in the joystick driver: supply the command line Hatari cannot
     * pass, then run the ordinary parsing below. */
    static char *test_args[] = {"XPADJM", "-t"};

    argc = 2;
    argv = test_args;
#endif

    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        return selftest();

    if (!install())
        return 1;

    /* Keep the whole image. Being clever about the resident size is how
     * TSRs corrupt memory in ways that surface an hour later. */
    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
}
