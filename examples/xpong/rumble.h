/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Rumble pulses on any number of pads and motors at once, and the
 * promise that every one of them stops.
 *
 * The request area carries a strength and no duration, and a provider
 * holds a rumble until something replaces it: COMpad sends the new
 * strength to the pad and the pad keeps turning. So a pulse is two
 * requests, a start and a stop, and the stop is this file's whole job.
 * Free of TOS so the host build can prove it, since a motor left
 * running after the game exits is the one mistake here a player
 * cannot fix without switching the pad off.
 *
 * Lengths are counted in game frames. The game draws the same amount
 * every frame, so a frame is a steady clock here, unlike the viewer,
 * whose frames vary with how much changed and which times its pulses
 * with _hz_200 for that reason.
 */

#ifndef XPONG_RUMBLE_H
#define XPONG_RUMBLE_H

#include <stdint.h>

#include "../../src/xpad.h"

typedef struct
{
    /*
     * NULL when the provider offers no request area. Volatile because
     * the provider reads it from an interrupt, and the order of the
     * writes is the protocol: magnitudes first, seq last, so a
     * provider that samples between the two sees the old seq and comes
     * back. Without it the compiler may store seq first, and a
     * provider that sampled in the gap would mark the change seen
     * before it had happened.
     */
    volatile XPAD_REQ *req;

    /* Frames left to run on each motor of each pad, 0 when it is still.
     * Motor 0 is the low frequency one, as in XPAD_REQ. */
    uint8_t left[XPAD_MAX_PADS][2];
} RUMBLE;

static void rumble_init(RUMBLE *r, XPAD_REQ *req)
{
    int i;

    r->req = req;

    for (i = 0; i < XPAD_MAX_PADS; i++)
        r->left[i][0] = r->left[i][1] = 0;
}

/* Stop whatever this started on pad, without bumping seq. Returns 1 if
 * anything was turning. */
static int rumble_still(RUMBLE *r, int pad)
{
    int motor, stopped = 0;

    for (motor = 0; motor < 2; motor++)
    {
        if (r->left[pad][motor])
        {
            r->req->rumble[pad][motor] = 0;
            r->left[pad][motor] = 0;
            stopped = 1;
        }
    }

    return stopped;
}

/*
 * Start a pulse on pad: lo drives the low frequency motor, the heavy
 * one in the left grip, and hi the high frequency one in the right.
 * Each motor keeps its own time, and a strength of 0 leaves that motor
 * as it was, so two players sharing a pad can each have one without
 * cutting the other short. Asking for nothing at all, no strength or
 * no length, stops the pad. Returns 1 if a pulse was started.
 *
 * caps is read at every pulse rather than once, because a provider
 * may gain or lose rumble as pads come and go. A refusal still stops
 * what is already running on that pad: losing XPAD_CAP_RUMBLE does not
 * clear the strengths already written, and nothing else will.
 */
static int rumble_pulse(RUMBLE *r, uint16_t caps, int pad, uint8_t lo,
                        uint8_t hi, uint8_t frames)
{
    if (!r->req || pad < 0 || pad >= XPAD_MAX_PADS)
        return 0;

    if (!(caps & XPAD_CAP_RUMBLE) || !frames || !(lo || hi))
    {
        if (rumble_still(r, pad))
            r->req->seq++;
        return 0;
    }

    if (lo)
    {
        r->req->rumble[pad][0] = lo;
        r->left[pad][0] = frames;
    }

    if (hi)
    {
        r->req->rumble[pad][1] = hi;
        r->left[pad][1] = frames;
    }

    r->req->seq++; /* last, so the provider sees it all */

    return 1;
}

/* Once a frame: stop every motor whose time is up, with one bump of
 * seq however many that is. */
static void rumble_tick(RUMBLE *r)
{
    int i, motor, stopped = 0;

    for (i = 0; i < XPAD_MAX_PADS; i++)
    {
        for (motor = 0; motor < 2; motor++)
        {
            if (r->left[i][motor] && !--r->left[i][motor])
            {
                r->req->rumble[i][motor] = 0;
                stopped = 1;
            }
        }
    }

    if (stopped)
        r->req->seq++;
}

/* Stop everything now: pausing, leaving a match, and above all exiting.
 * Only motors this started are touched, so a request some other
 * program made is left alone. */
static void rumble_stop_all(RUMBLE *r)
{
    int i, stopped = 0;

    for (i = 0; i < XPAD_MAX_PADS; i++)
        stopped |= rumble_still(r, i);

    if (stopped)
        r->req->seq++;
}

#endif /* XPONG_RUMBLE_H */
