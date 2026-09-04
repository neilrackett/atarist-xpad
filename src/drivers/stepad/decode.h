/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * STE enhanced joystick port sample to Xpad buttons.
 *
 * Deliberately free of TOS dependencies: this is the only part of the
 * driver with logic that can be wrong, so the host build tests it
 * without an emulator and without an STE.
 *
 * The ports are a scanned matrix rather than a set of registers. A row
 * is selected by pulling one bit of $FF9202 low, and both pads answer
 * at once: pad 0 in the low nibble, pad 1 in the high one. Four rows
 * cover everything the pad has. Everything is active low, so a bit
 * reads 0 while its button is held.
 *
 *   row 0   directions, plus A and Pause on $FF9200
 *   row 1   keypad * 7 4 1, plus B on $FF9200
 *   row 2   keypad 0 8 5 2, plus C on $FF9200
 *   row 3   keypad # 9 6 3, plus Option on $FF9200
 *
 * The keypad columns are sampled and then ignored: see README's
 * "Example drivers" for why they are not mapped.
 */

#ifndef XPAD_STEPAD_DECODE_H
#define XPAD_STEPAD_DECODE_H

#include "../../xpad.h"

#define XPAD_STE_ROWS 4
#define XPAD_STE_PADS 2

/*
 * One poll: both registers read once per row select, in row order.
 * Kept as raw bytes so the decode below can be tested without any
 * hardware, which is the whole point of this file.
 */
typedef struct
{
    uint8_t dir[XPAD_STE_ROWS];  /* $FF9202: pad 0 low nibble, pad 1 high */
    uint8_t fire[XPAD_STE_ROWS]; /* $FF9200 low byte, same four reads     */
} XPAD_STE_SAMPLE;

/*
 * $FF9200 carries one fire line and one pause line per pad. The fire
 * line means a different button in each row, which is what makes the
 * four reads necessary rather than merely tidy.
 */
#define XPAD_STE_FIRE0 0x02
#define XPAD_STE_PAUSE0 0x01
#define XPAD_STE_FIRE1 0x08
#define XPAD_STE_PAUSE1 0x04

/* Written to $FF9202 to select row r on both pads at once. */
#define XPAD_STE_SELECT(r) ((uint16_t)(~(0x11u << (r)) & 0xffu))

/*
 * A, B and C sit in a row on the pad, and Xpad's face buttons are a
 * diamond, so there is no mapping that preserves position the way the
 * X/Y rule in README asks for. This is a convention instead, and it is
 * the conventional one: A is the primary button, so it lands where a
 * modern pad's primary button is.
 */
static uint32_t xpad_stepad_decode(const XPAD_STE_SAMPLE *s, int pad)
{
    uint8_t fire = pad ? XPAD_STE_FIRE1 : XPAD_STE_FIRE0;
    uint8_t pause = pad ? XPAD_STE_PAUSE1 : XPAD_STE_PAUSE0;
    uint8_t dirs = pad ? (uint8_t)(s->dir[0] >> 4) : s->dir[0];
    uint32_t buttons;

    /* XPAD_UP, DOWN, LEFT and RIGHT are 0x01, 0x02, 0x04 and 0x08, and
     * the port reports the four directions in that same order, so the
     * nibble carries across once inverted. The d-pad bit placement in
     * AGENTS.md paying off twice. */
    buttons = (uint32_t)(~dirs & 0x0f);

    if (!(s->fire[0] & fire))
        buttons |= XPAD_SOUTH; /* A      */
    if (!(s->fire[1] & fire))
        buttons |= XPAD_EAST; /* B      */
    if (!(s->fire[2] & fire))
        buttons |= XPAD_WEST; /* C      */
    if (!(s->fire[3] & fire))
        buttons |= XPAD_SELECT; /* Option */
    if (!(s->fire[0] & pause))
        buttons |= XPAD_START; /* Pause  */

    return buttons;
}

/* Every bit this driver is entitled to set, and nothing else. */
#define XPAD_STE_BUTTONS \
    (XPAD_DPAD | XPAD_SOUTH | XPAD_EAST | XPAD_WEST | XPAD_SELECT | XPAD_START)

#endif /* XPAD_STEPAD_DECODE_H */
