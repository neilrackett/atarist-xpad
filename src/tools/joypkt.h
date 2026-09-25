/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad state to IKBD packets: the parts with logic that can be wrong.
 *
 * Free of TOS dependencies so the host build tests it without an
 * emulator, the same split the drivers use for translate.h and
 * keymap.h. What is left in xpademu.c is vector surgery and file
 * reading, neither of which a host can check.
 *
 * Inspired by, and ported from, the assembly injector in MD/Sidepad:
 * https://github.com/neilrackett/md-sidepad
 * (target/atarist/src/userfw.s). The packet shapes, the live vector
 * read and the interrupt masking are all its discoveries, made on real
 * hardware. What changes here is the source of the input: Sidepad
 * reads a byte its own cartridge publishes, this reads any XPAD block,
 * so it works with any provider.
 */

#ifndef XPAD_JOYPKT_H
#define XPAD_JOYPKT_H

#include "../xpad.h"

/*
 * The IKBD packs a joystick into one byte: bit0 up, bit1 down, bit2
 * left, bit3 right, bit7 fire.
 *
 * XPAD_UP, DOWN, LEFT and RIGHT are 0x01, 0x02, 0x04 and 0x08, so the
 * direction nibble carries across untouched and only fire has to move.
 * That is the d-pad bit placement in AGENTS.md paying off a second
 * time: src/drivers/joystick/translate.h is this same mapping in the
 * other direction.
 */
#define JOYPKT_DIRS 0x0f
#define JOYPKT_FIRE 0x80

/*
 * A relative mouse packet's header is 0xF8 with the button bits in the
 * bottom two: bit 0 right, bit 1 left. Mouse packets are three bytes,
 * header then signed dx then signed dy.
 */
#define JOYPKT_MOUSE_HDR 0xF8
#define JOYPKT_MOUSE_RIGHT 0x01
#define JOYPKT_MOUSE_LEFT 0x02

/*
 * Translate a pad into an IKBD joystick byte.
 *
 * fire is a mask rather than one button so a pad can have several, and
 * jump is the button that reads as up, which is how most ST games ask
 * you to jump. Both come from the config file.
 *
 * The directions arrive already folded: a provider is required to fold
 * its analogue stick into the d-pad bits, so an analogue pad drives a
 * game that predates analogue sticks without anything here knowing.
 */
static uint8_t joypkt_joystick(uint32_t buttons, uint32_t fire, uint32_t jump)
{
    uint8_t b = (uint8_t)(buttons & JOYPKT_DIRS);

    if (buttons & fire)
        b |= JOYPKT_FIRE;

    if (jump && (buttons & jump))
        b |= (uint8_t)XPAD_UP;

    return b;
}

/*
 * Every button that is not a direction, which is every button a
 * joystick could plausibly want to be fire.
 *
 * Named rather than written as a mask so that a button added at bit 17
 * has an obvious place to join, and so the exclusion of the d-pad is
 * visible rather than arithmetic. The directions are not in it because
 * they already have a job: putting one in the fire mask would fire the
 * gun whenever you walked left.
 */
#define JOYPKT_ANY_BUTTON                                                 \
    (XPAD_SOUTH | XPAD_EAST | XPAD_NORTH | XPAD_WEST | XPAD_TL |          \
     XPAD_TR | XPAD_TL2 | XPAD_TR2 | XPAD_SELECT | XPAD_START |           \
     XPAD_MODE | XPAD_THUMBL | XPAD_THUMBR)

/*
 * What is left over once the named jobs have taken what they want.
 *
 * A joystick has one button, a pad has thirteen, and a game only ever
 * asks about fire. So anything not doing something else is fire, and
 * the pad has no dead buttons: whichever one you press, it shoots.
 * That is a better default than picking two and leaving eleven inert,
 * because a player's first instinct with an unfamiliar pad is to press
 * things until something happens.
 */
static uint32_t joypkt_spare(uint32_t taken)
{
    return JOYPKT_ANY_BUTTON & ~taken;
}

/*
 * Autofire: a button that holds fire down and lets it go, repeatedly,
 * for as long as you hold it.
 *
 * period is one full on-and-off cycle in ticks, so at the 50 Hz this
 * injects at, 6 is about eight shots a second. Fire is asserted for
 * the first half and released for the second.
 *
 * It asserts on the very first tick the button is held, rather than
 * after half a cycle, so a tap still shoots. Letting go resets the
 * phase, so every press starts with a shot rather than wherever the
 * counter happened to be.
 */
typedef struct
{
    uint8_t phase;
} JOYPKT_AUTO;

static int joypkt_autofire(JOYPKT_AUTO *a, int held, uint8_t period)
{
    uint8_t half;

    /* A period under two cannot have an off half, and a held button
     * that never releases is not autofire, it is fire. */
    if (!held || period < 2)
    {
        a->phase = 0;
        return 0;
    }

    half = (uint8_t)(period >> 1);

    if (a->phase >= period)
        a->phase = 0;

    return a->phase++ < half;
}

/* The mouse button byte, which is the header's bottom two bits. */
static uint8_t joypkt_buttons(uint32_t buttons, uint32_t left, uint32_t right)
{
    uint8_t b = 0;

    if (buttons & left)
        b |= JOYPKT_MOUSE_LEFT;

    if (buttons & right)
        b |= JOYPKT_MOUSE_RIGHT;

    return b;
}

/*
 * A stick position, carried across ticks so slow movement still moves.
 *
 * The IKBD reports whole pixels, and a stick pushed a tenth of the way
 * over wants a fraction of one per tick. Truncating that to zero would
 * make the bottom of the stick's range dead and the mouse usable only
 * at a sprint, so the remainder is kept and added to the next tick.
 */
typedef struct
{
    int16_t fx, fy;
} JOYPKT_MOUSE;

/*
 * One tick of stick-as-mouse. Returns 1 when either axis produced a
 * whole pixel, which is when a packet is worth sending.
 *
 * Deliberately 16 bit throughout. A 68000 has no 32-bit multiply, so a
 * wider operand here would turn this into a __mulsi3 libcall inside a
 * timer interrupt. The widest product is 127 * 255, which is 32385 and
 * fits, and the accumulator is reduced below 256 every tick, so the
 * sum cannot overflow either.
 */
static int joypkt_mouse(JOYPKT_MOUSE *m, int8_t x, int8_t y,
                        uint8_t deadzone, uint8_t speed,
                        int8_t *dx, int8_t *dy)
{
    int16_t ax = x < 0 ? (int16_t)-x : (int16_t)x;
    int16_t ay = y < 0 ? (int16_t)-y : (int16_t)y;
    int16_t px, py;

    /* Inside the deadzone the stick is at rest, and a resting stick
     * must not creep: a cursor that drifts is worse than one that is
     * slow. The remainder is dropped with it, so letting go stops the
     * pointer rather than letting it coast a pixel. */
    if (ax < (int16_t)deadzone)
    {
        x = 0;
        m->fx = 0;
    }

    if (ay < (int16_t)deadzone)
    {
        y = 0;
        m->fy = 0;
    }

    m->fx = (int16_t)(m->fx + (int16_t)x * (int16_t)speed);
    m->fy = (int16_t)(m->fy + (int16_t)y * (int16_t)speed);

    px = (int16_t)(m->fx >> 8);
    py = (int16_t)(m->fy >> 8);

    /* Clamp to what one packet can carry rather than wrapping: a
     * stick slammed to the corner should move fast, not backwards. */
    if (px > 127)
        px = 127;
    if (px < -127)
        px = -127;
    if (py > 127)
        py = 127;
    if (py < -127)
        py = -127;

    m->fx = (int16_t)(m->fx - (int16_t)(px << 8));
    m->fy = (int16_t)(m->fy - (int16_t)(py << 8));

    *dx = (int8_t)px;

    /*
     * No sign flip. xpad's +y is down the screen, and TOS runs the IKBD
     * with its y origin at the top, so a positive dy is down as well.
     * This once negated it, and the pointer went the wrong way
     * vertically on a real machine: the tests had been written to the
     * same wrong belief, so they passed.
     */
    *dy = (int8_t)py;

    return px != 0 || py != 0;
}

#endif /* XPAD_JOYPKT_H */
