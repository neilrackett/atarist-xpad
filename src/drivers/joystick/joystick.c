/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad joystick driver: publishes both joystick ports as two pads.
 *
 * EXAMPLE DRIVER. Written to be read and copied as much as to be used:
 * it is the smallest complete provider, showing the whole shape of one
 * in as little code as possible. Start here if you are writing a driver
 * for a transport of your own. See "Example drivers" in README for the
 * full list of what it does not do.
 *
 * Four directions and one button, which is the whole of what the IKBD
 * reports for a port. It needs no periodic hook at all, because joyvec
 * is called for us whenever the joystick changes.
 *
 * Because MD/Sidepad synthesises joyvec packets to inject its Bluetooth
 * controller, this driver picks that up with no firmware change.
 *
 *   XPADJOY        install and stay resident
 *   XPADJOY -t     run the self test and exit, installing nothing
 *
 * Limits, all inherent to the IKBD rather than to this driver:
 *
 *   - one button, because the IKBD reports one fire bit per port
 *   - no analogue, so XPAD_CAP_ANALOG is not claimed
 *   - no presence detection: an ST cannot tell whether a joystick is
 *     plugged in, so both slots are always reported connected, and pad
 *     0 usually sits idle because port 0 is the mouse
 *   - seq advances on change, not per frame, because that is when the
 *     IKBD reports. A still joystick is not a stalled provider.
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <stdio.h>
#include <string.h>

#include "../../xpad.h"
#include "translate.h"

#define PROVIDER "IKBD joystick 1.0"

/*
 * Both bytes of the packet, one pad each. Port 1 is where a stick
 * actually plugs in, and where MD/Sidepad injects; port 0 is normally
 * the mouse, so pad 0 usually sits idle. Publishing it anyway is what
 * makes this a two pad provider, which is worth having as an example:
 * a driver with one pad never exercises the buffer stride, because the
 * two buffers only sit pad_count entries apart.
 */
#define PACKET_JOY0 1
#define PACKET_JOY1 2

#define PAD_COUNT 2

static XPAD block;

/* Read by the trampoline in joyvec.s. */
void (*xpad_joyvec_chain)(void *);

extern void xpad_joyvec_entry(void *pkt);
extern void xpad_joyvec_call(void (*fn)(void *), const void *pkt);

/*
 * Called from the trampoline, in interrupt context. Nothing here may
 * allocate, do I/O, or call anything that is not reentrant.
 */
void xpad_joyvec_update(const uint8_t *pkt)
{
    XPAD_PAD *back = xpad_back(&block);

    back[0].buttons = xpad_joystick_translate(pkt[PACKET_JOY0]);
    back[1].buttons = xpad_joystick_translate(pkt[PACKET_JOY1]);

    xpad_commit(&block);
}

static void init_block(void)
{
    int i, j;

    xpad_init(&block, PAD_COUNT, 0, PROVIDER, 0);

    /* Mark both slots occupied in both buffers, once, so the interrupt
     * path only ever has to write buttons. Two commits also leave
     * active back where xpad_init put it. */
    for (i = 0; i < 2; i++)
    {
        XPAD_PAD *back = xpad_back(&block);

        for (j = 0; j < PAD_COUNT; j++)
            back[j].type = XPAD_TYPE_JOYSTICK;

        xpad_commit(&block);
    }
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

static int failures;
static int chain_calls;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void chain_stub(void *pkt)
{
    (void)pkt;
    chain_calls++;
}

/*
 * Drive the real trampoline with fabricated packets, with a stub in
 * place of the displaced handler so nothing reaches TOS. That covers
 * the assembly, the argument passing, the translation and the commit,
 * without installing anything or making the desktop think the joystick
 * moved.
 */
static void fire(uint8_t joy0, uint8_t joy1)
{
    /* The $FF header sits at an odd address, as TOS delivers it. A word
     * array is even, so one byte in is odd. */
    static uint16_t aligned[4];
    uint8_t *pkt = (uint8_t *)aligned + 1;

    pkt[0] = 0xff;
    pkt[1] = joy0;
    pkt[2] = joy1;

    xpad_joyvec_call(xpad_joyvec_entry, pkt);
}

static uint32_t pad_buttons(int index)
{
    XPAD_PAD pad;

    if (!xpad_read(&block, index, &pad))
        return 0xffffffffUL; /* cannot happen; fails the check loudly */

    return pad.buttons;
}

/* Drive port 1 only, which is the common case, and check pad 1. */
static void expect(uint8_t joy1, uint32_t want, const char *what)
{
    fire(0, joy1);
    check(pad_buttons(1) == want, what);
}

static int selftest(void)
{
    XPAD_PAD pad;
    uint16_t before;

    init_block();
    xpad_joyvec_chain = chain_stub;

    expect(0x00, 0, "centred stick reads no buttons");
    expect(0x01, XPAD_UP, "bit 0 is UP");
    expect(0x02, XPAD_DOWN, "bit 1 is DOWN");
    expect(0x04, XPAD_LEFT, "bit 2 is LEFT");
    expect(0x08, XPAD_RIGHT, "bit 3 is RIGHT");
    expect(0x80, XPAD_SOUTH, "bit 7 is the button");
    expect(0x09, XPAD_UP | XPAD_RIGHT, "diagonals combine");
    expect(0x81, XPAD_UP | XPAD_SOUTH, "direction and button combine");
    expect(0x8f, XPAD_DPAD | XPAD_SOUTH, "everything at once");
    expect(0x00, 0, "releasing clears the buttons");

    /*
     * Two ports, and they must not bleed into each other. This is also
     * the only test here that depends on the buffer stride being right:
     * pad 1 only lands where xpad_read() looks for it if xpad_back()
     * and XPAD_PAD_AT() agree at a pad_count below XPAD_MAX_PADS.
     */
    fire(0x04, 0x08);
    check(pad_buttons(0) == XPAD_LEFT, "joystick 0 lands in pad 0");
    check(pad_buttons(1) == XPAD_RIGHT, "joystick 1 lands in pad 1");

    fire(0x01, 0x00);
    check(pad_buttons(0) == XPAD_UP, "port 0 moves on its own");
    check(pad_buttons(1) == 0, "and leaves port 1 alone");

    fire(0x00, 0x02);
    check(pad_buttons(0) == 0, "port 1 moves on its own");
    check(pad_buttons(1) == XPAD_DOWN, "and leaves port 0 alone");

    fire(0x8f, 0x8f);
    check(pad_buttons(0) == (XPAD_DPAD | XPAD_SOUTH) &&
              pad_buttons(1) == (XPAD_DPAD | XPAD_SOUTH),
          "both ports at once");
    fire(0x00, 0x00);

    check(xpad_read(&block, 0, &pad) && pad.type == XPAD_TYPE_JOYSTICK,
          "pad 0 reports a joystick");
    check(xpad_read(&block, 1, &pad) && pad.type == XPAD_TYPE_JOYSTICK,
          "pad 1 reports a joystick");
    check(xpad_connected(&block) == PAD_COUNT, "both pads are connected");
    check(xpad_read(&block, PAD_COUNT, &pad) == 0,
          "there is no third pad");

    /* The regression that would break every existing game. */
    check(chain_calls > 0, "every packet reached the displaced handler");

    before = block.seq;
    fire(0, 0x01);
    check(block.seq == (uint16_t)(before + 1), "each packet commits once");

    /* Nothing analogue is claimed, so nothing analogue should appear. */
    check(xpad_read(&block, 1, &pad) &&
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
static long install_vector(void)
{
    xpad_joyvec_chain = vecs->joyvec;
    vecs->joyvec = xpad_joyvec_entry;

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
    Supexec(install_vector);

    printf("%s installed: ports 0 and 1 as pads 0 and 1.\n", PROVIDER);

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
    static char *test_args[] = {"XPADJOY", "-t"};

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
