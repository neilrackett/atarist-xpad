/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * DevOps driver location tests.
 *
 * Deciding whether what the cartridge publishes is an area the block
 * fits in is that driver's only logic, and it is free of TOS, so it is
 * tested here on the host. Whether the block itself is acceptable is
 * xpad_valid()'s call, covered by the driver's self test against an
 * image the workstation tool made; the tool has test/devops.py.
 */

#include <stdio.h>

#include "../src/drivers/devops/locate.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static int locate(uint32_t magic, uint32_t address, uint32_t size,
                  uint32_t *block)
{
    uint32_t slots[3];

    slots[0] = magic;
    slots[1] = address;
    slots[2] = size;

    return xpad_devops_locate(slots, block);
}

int main(void)
{
    uint32_t block = 0;

    printf("xpad DevOps location\n\n");

    check(locate(XPAD_DEVOPS_MAGIC, 0xFA8B00UL, 1024, &block) ==
                  XPAD_DEVOPS_FOUND &&
              block == 0xFA8B00UL,
          "md-devops' area is found where it says");

    block = 0;
    check(locate(0, 0xFA8B00UL, 1024, &block) == XPAD_DEVOPS_NO_AREA &&
              block == 0,
          "no magic, no area, nothing set");
    check(locate(0x52344132UL, 0xFA8B00UL, 1024, &block) ==
              XPAD_DEVOPS_NO_AREA,
          "a different magic is not this area");

    /* Whatever an ST without md-devops happens to read there. */
    check(locate(0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, &block) ==
              XPAD_DEVOPS_NO_AREA,
          "an empty cartridge port is not an area");

    check(locate(XPAD_DEVOPS_MAGIC, 0xFA8B00UL, XPAD_DEVOPS_SPAN - 1,
                 &block) == XPAD_DEVOPS_TOO_SMALL,
          "one byte short is too small");
    check(locate(XPAD_DEVOPS_MAGIC, 0xFA8B00UL, XPAD_DEVOPS_SPAN, &block) ==
              XPAD_DEVOPS_FOUND,
          "exactly the span is enough");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

    return failures ? 1 : 0;
}
