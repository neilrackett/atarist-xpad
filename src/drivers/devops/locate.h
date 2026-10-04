/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Where the DevOps driver's Xpad block is.
 *
 * Deliberately free of TOS dependencies: deciding whether what the
 * cartridge says is usable is the part of the driver with logic worth
 * getting wrong, so the host build tests it without an emulator.
 *
 * md-devops, a SidecarTridge Multi-device microfirmware, keeps 1 KB of
 * the cartridge window that the workstation writes over HTTP and any ST
 * program reads: its rom4 area. Programs find it through three longs
 * the firmware publishes at a fixed address:
 *
 *   $FA2860  'R4A1' when the area exists
 *   $FA2864  the area's address
 *   $FA2868  its size in bytes
 *
 * xpaddev.py writes an XPAD block at the start of the area and the
 * provider string after it, so the block's address is the area's.
 */

#ifndef XPAD_DEVOPS_LOCATE_H
#define XPAD_DEVOPS_LOCATE_H

#include <stdint.h>

#define XPAD_DEVOPS_SLOTS 0xFA2860UL   /* md-devops shared variable 20 */
#define XPAD_DEVOPS_MAGIC 0x52344131UL /* 'R4A1'                       */

/*
 * How much of the area xpaddev.py uses: the block with room for every
 * pad, then a 32 byte provider string. SPAN in xpaddev.py says the
 * same, and test/devops.py holds the two together.
 */
#define XPAD_DEVOPS_SPAN 160

#define XPAD_DEVOPS_FOUND 0
#define XPAD_DEVOPS_NO_AREA 1   /* no md-devops, or one without rom4 */
#define XPAD_DEVOPS_TOO_SMALL 2 /* an area the block does not fit in */

/*
 * slots holds the three longs as read from XPAD_DEVOPS_SLOTS. Sets
 * *block to the block's address and returns XPAD_DEVOPS_FOUND, or
 * returns why not and leaves *block alone. Whether a block is really
 * there, and at an address a 68000 can read, is xpad_valid()'s call.
 */
static int xpad_devops_locate(const uint32_t slots[3], uint32_t *block)
{
    if (slots[0] != XPAD_DEVOPS_MAGIC)
        return XPAD_DEVOPS_NO_AREA;

    if (slots[2] < XPAD_DEVOPS_SPAN)
        return XPAD_DEVOPS_TOO_SMALL;

    *block = slots[1];

    return XPAD_DEVOPS_FOUND;
}

#endif /* XPAD_DEVOPS_LOCATE_H */
