/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * XPad joystick driver: publishes joystick 1 as a single pad.
 *
 * Proof of concept, and the simplest possible provider: four directions
 * and one button, which is the whole of what the IKBD reports for a
 * port. It needs no periodic hook at all, because joyvec is called for
 * us whenever the joystick changes.
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
 *     plugged in, so the slot is always reported connected
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

/* Joystick 1: the port a stick actually plugs into, and the one
 * MD/Sidepad injects into. Port 0 is the mouse. */
#define PACKET_JOY1 2

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

    back[0].buttons = xpad_joystick_translate(pkt[PACKET_JOY1]);

    xpad_commit(&block);
}

static void init_block(void)
{
    int i;

    xpad_init(&block, 1, 0, PROVIDER, 0);

    /* Mark the slot occupied in both buffers, once, so the interrupt
     * path only ever has to write buttons. Two commits also leave
     * active back where xpad_init put it. */
    for (i = 0; i < 2; i++)
    {
        xpad_back(&block)[0].type = XPAD_TYPE_JOYSTICK;
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
static void fire(uint8_t joy1)
{
    /* The $FF header sits at an odd address, as TOS delivers it. A word
     * array is even, so one byte in is odd. */
    static uint16_t aligned[4];
    uint8_t *pkt = (uint8_t *)aligned + 1;

    pkt[0] = 0xff;
    pkt[1] = 0; /* joystick 0, the mouse port */
    pkt[2] = joy1;

    xpad_joyvec_call(xpad_joyvec_entry, pkt);
}

static int expect(uint8_t joy1, uint32_t want, const char *what)
{
    XPAD_PAD pad;

    fire(joy1);

    if (!xpad_read(&block, 0, &pad))
        return 0;

    check(pad.buttons == want, what);

    return pad.buttons == want;
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

    check(xpad_read(&block, 0, &pad) && pad.type == XPAD_TYPE_JOYSTICK,
          "the slot reports a joystick");
    check(xpad_connected(&block) == 1, "one pad is connected");

    /* The regression that would break every existing game. */
    check(chain_calls == 10, "every packet reached the displaced handler");

    before = block.seq;
    fire(0x01);
    check(block.seq == (uint16_t)(before + 1), "each packet commits once");

    /* Nothing analogue is claimed, so nothing analogue should appear. */
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

#ifndef XPAD_SELFTEST

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
    /* XPad is single provider and the last one to publish wins, so a
     * shim like this must not displace something better. */
    if (xpad_find())
    {
        printf("An XPad provider is already installed. Leaving it alone.\n");
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

    printf("%s installed: joystick 1 as pad 0.\n", PROVIDER);

    return 1;
}

#endif /* !XPAD_SELFTEST */

int main(int argc, char **argv)
{
#ifdef XPAD_SELFTEST
    /* Built as the harness binary: Hatari's --auto takes a path and no
     * arguments, so this build cannot be told to test, only be it. */
    (void)argc;
    (void)argv;

    return selftest();
#else
    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        return selftest();

    if (!install())
        return 1;

    /* Keep the whole image. Being clever about the resident size is how
     * TSRs corrupt memory in ways that surface an hour later. */
    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
#endif
}
