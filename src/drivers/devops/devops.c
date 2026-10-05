/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad DevOps driver: publishes pads driven from the workstation.
 *
 * EXAMPLE DRIVER, and the odd one out: it never touches a pad. The
 * block lives in the cartridge window of a SidecarTridge running
 * md-devops, in the 1 KB rom4 area the workstation writes over HTTP.
 * xpaddev.py, next to this file, writes a whole XPAD block there and
 * then commits pad state into it exactly as xpad_commit() would: back
 * buffer, then seq, then active. The ST cannot write the cartridge, so
 * all this driver can do, and all it needs to, is point the cookie at
 * the block. Nothing stays resident and nothing is hooked.
 *
 *   XPADDEV        publish the cookie and exit
 *   XPADDEV -t     run the self test and exit, installing nothing
 *
 * Limits:
 *
 *   - xpaddev.py init must have run since the SidecarTridge started,
 *     because the driver refuses an area with no valid block in it.
 *     The area survives an ST reset, so init then reset is enough.
 *   - no rumble or LEDs: req is NULL, because a consumer could never
 *     write a request into a cartridge
 *   - an RP reset empties the area, which consumers then read as four
 *     disconnected pads, the same as a provider going away
 */

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "../../xpad.h"
#include "locate.h"

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

/*
 * where is XPAD_DEVOPS_SLOTS everywhere but the self test, which hands
 * in a table of its own so the same code runs against a known area.
 * Reading the cartridge needs no Supexec: the ROM port is not one of
 * the places user mode is barred from.
 */
static int install(const volatile uint32_t *where)
{
    uint32_t slots[3];
    uint32_t addr;
    XPAD *x;

    /* Xpad is single provider and the last one to publish wins, so a
     * driver must not displace something better. */
    if (xpad_find())
    {
        printf("An Xpad provider is already installed. Leaving it alone.\n");
        return 0;
    }

    slots[0] = where[0];
    slots[1] = where[1];
    slots[2] = where[2];

    switch (xpad_devops_locate(slots, &addr))
    {
    case XPAD_DEVOPS_FOUND:
        break;

    case XPAD_DEVOPS_TOO_SMALL:
        printf("The DevOps rom4 area is too small\n"
               "for an Xpad block.\n");
        return 0;

    default:
        printf("No DevOps rom4 area. XPADDEV needs a\n"
               "SidecarTridge running md-devops.\n");
        return 0;
    }

    x = (XPAD *)addr;

    if (!xpad_valid(x))
    {
        printf("No Xpad block in the rom4 area yet.\n"
               "Run xpaddev.py init on the workstation,\n"
               "then reset the ST.\n");
        return 0;
    }

    if (!xpad_publish(x))
    {
        printf("Could not install the XPAD cookie. Is the jar full?\n");
        return 0;
    }

    printf("Xpad from the workstation, %d pads\n"
           "in the cartridge at $%06lX.\n",
           x->pad_count, (unsigned long)addr);

    return 1;
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

/*
 * The harness puts DEVOPS.IMG on drive C: the area as an ST would read
 * it, written by xpaddev.py --file. So what is checked here is that the
 * consumer code accepts and reads exactly what the workstation tool
 * produces, with the real byte order and the real m68k layout, which
 * no host test can show.
 *
 * The Makefile makes the image with init and then
 *
 *   set --pad 1 south start --lx 100 --ly -20 --lt 255
 *
 * and the expectations below are that state. Change one, change both.
 */
#define IMAGE "\\DEVOPS.IMG"
#define AREA_SIZE 1024

/* The address the image's pointers were made for: md-devops' area. */
#define IMAGE_ADDRESS 0xFA8B00UL

/* Word arrays, so both areas sit at an even address as the real one
 * does. */
static uint16_t area[AREA_SIZE / 2];
static uint16_t empty[XPAD_DEVOPS_SPAN / 2];

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static int load_image(void)
{
    long fd = Fopen(IMAGE, 0);
    long got;

    if (fd < 0)
        return 0;

    got = Fread((short)fd, AREA_SIZE, area);
    Fclose((short)fd);

    return got == AREA_SIZE;
}

static int selftest(void)
{
    uint32_t slots[3];
    const XPAD *x = (const XPAD *)area;
    XPAD_PAD pad;

    check(load_image(), "the workstation's image loads");

    if (failures)
    {
        printf("\nFAILED\nXPAD-DONE 1\n");
        fflush(stdout);
        return 1;
    }

    /* Everything that should be refused, before anything is installed. */
    slots[0] = 0;
    slots[1] = (uint32_t)area;
    slots[2] = AREA_SIZE;
    check(!install(slots) && !xpad_find(),
          "refuses a cartridge without rom4");

    slots[0] = XPAD_DEVOPS_MAGIC;
    slots[2] = XPAD_DEVOPS_SPAN - 1;
    check(!install(slots) && !xpad_find(),
          "refuses an area that is too small");

    slots[1] = (uint32_t)empty;
    slots[2] = XPAD_DEVOPS_SPAN;
    check(!install(slots) && !xpad_find(),
          "refuses an area with no block yet");

    /* The real cartridge address. Under the harness that is Hatari's
     * own GEMDOS cartridge, which is not md-devops, so this proves the
     * read is harmless as well as that it is refused. */
    check(!install((const volatile uint32_t *)XPAD_DEVOPS_SLOTS) &&
              !xpad_find(),
          "refuses this machine's cartridge");

    slots[1] = (uint32_t)area;
    slots[2] = AREA_SIZE;
    check(install(slots), "installs over a block xpaddev.py wrote");
    check(xpad_find() == x, "the cookie points at the area");

    check(x->pad_count == XPAD_MAX_PADS, "four pads");
    check(x->caps == (XPAD_CAP_ANALOG | XPAD_CAP_HOTPLUG),
          "analogue and hotplug, nothing else");
    check(x->req == 0, "no request area");
    check(x->seq == 1 && x->active == 1, "one commit after init");
    check((uint32_t)x->provider == IMAGE_ADDRESS + XPAD_DEVOPS_PROVIDER_OFFSET,
          "the provider points past the block");
    check(strcmp((const char *)area + XPAD_DEVOPS_PROVIDER_OFFSET,
                 XPAD_DEVOPS_PROVIDER) == 0,
          "and names the workstation");

    check(xpad_read(x, 0, &pad) && pad.type == XPAD_TYPE_GAMEPAD &&
              pad.flags == XPAD_PAD_ANALOG && pad.buttons == 0,
          "pad 0 is connected and idle");

    /* Pad 1 lands where xpad_read() looks only if the tool's buffer
     * stride agrees with XPAD_PAD_AT(). */
    check(xpad_read(x, 1, &pad) &&
              pad.buttons == (XPAD_SOUTH | XPAD_START | XPAD_RIGHT),
          "pad 1 holds south and start, stick right");
    check(pad.lx == 100 && pad.ly == -20 && pad.rx == 0 && pad.ry == 0,
          "pad 1's sticks");
    check(pad.lt == 255 && pad.rt == 0, "pad 1's triggers");
    check(pad.type == XPAD_TYPE_GAMEPAD && pad.flags == XPAD_PAD_ANALOG,
          "pad 1 is a connected analogue pad");

    check(xpad_read(x, 2, &pad) && pad.type == XPAD_TYPE_NONE &&
              xpad_read(x, 3, &pad) && pad.type == XPAD_TYPE_NONE,
          "pads 2 and 3 are not connected");
    check(xpad_connected(x) == 2, "two pads connected");

    check(!install(slots) && xpad_find() == x,
          "leaves an installed provider alone");

    /* Leave the jar as we found it. */
    check(xpad_unpublish() && !xpad_find(), "unpublishes cleanly");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    printf("XPAD-DONE %d\n", failures ? 1 : 0);
    fflush(stdout);

    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
#ifdef XPAD_SELFTEST
    /*
     * Hatari's --auto takes a path and no arguments, so the harness has
     * no way to ask for a mode. This build supplies the command line it
     * would have passed and then runs the ordinary parsing below, so
     * the only thing the tested binary does differently from the
     * shipped one is where argv came from.
     */
    static char *test_args[] = {"XPADDEV", "-t"};

    argc = 2;
    argv = test_args;
#endif

    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        return selftest();

    /* No Ptermres: the block is in the cartridge, so nothing here has to
     * outlive the program. */
    return install((const volatile uint32_t *)XPAD_DEVOPS_SLOTS) ? 0 : 1;
}
