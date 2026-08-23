/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad ABI assertions.
 *
 * Builds on the host with plain gcc; no ST toolchain and no Hatari
 * needed. Everything checkable at compile time is a static assertion,
 * so a violation fails the build rather than the run. The check target
 * also runs this file through the m68k compiler, so the frozen values
 * are asserted for the architecture the ABI is actually for.
 *
 * The values below are the published v1 ABI. If a change here is needed
 * to make the build pass, that change breaks binaries already in the
 * wild. See "Hard invariants" in AGENTS.md.
 */

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../src/xpad.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define XPAD_ASSERT(c, m) _Static_assert(c, m)
#else
#define XPAD_CAT_(a, b) a##b
#define XPAD_CAT(a, b) XPAD_CAT_(a, b)
#define XPAD_ASSERT(c, m) \
    typedef char XPAD_CAT(xpad_assert_, __LINE__)[(c) ? 1 : -1]
#endif

/* ------------------------------------------------------------------ */
/* Frozen button bits                                                  */
/* ------------------------------------------------------------------ */

XPAD_ASSERT(XPAD_UP == 0x00000001UL, "XPAD_UP moved");
XPAD_ASSERT(XPAD_DOWN == 0x00000002UL, "XPAD_DOWN moved");
XPAD_ASSERT(XPAD_LEFT == 0x00000004UL, "XPAD_LEFT moved");
XPAD_ASSERT(XPAD_RIGHT == 0x00000008UL, "XPAD_RIGHT moved");

XPAD_ASSERT(XPAD_SOUTH == 0x00000010UL, "XPAD_SOUTH moved");
XPAD_ASSERT(XPAD_EAST == 0x00000020UL, "XPAD_EAST moved");
XPAD_ASSERT(XPAD_NORTH == 0x00000040UL, "XPAD_NORTH moved");
XPAD_ASSERT(XPAD_WEST == 0x00000080UL, "XPAD_WEST moved");

XPAD_ASSERT(XPAD_TL == 0x00000100UL, "XPAD_TL moved");
XPAD_ASSERT(XPAD_TR == 0x00000200UL, "XPAD_TR moved");
XPAD_ASSERT(XPAD_TL2 == 0x00000400UL, "XPAD_TL2 moved");
XPAD_ASSERT(XPAD_TR2 == 0x00000800UL, "XPAD_TR2 moved");

XPAD_ASSERT(XPAD_SELECT == 0x00001000UL, "XPAD_SELECT moved");
XPAD_ASSERT(XPAD_START == 0x00002000UL, "XPAD_START moved");
XPAD_ASSERT(XPAD_MODE == 0x00004000UL, "XPAD_MODE moved");

XPAD_ASSERT(XPAD_THUMBL == 0x00008000UL, "XPAD_THUMBL moved");
XPAD_ASSERT(XPAD_THUMBR == 0x00010000UL, "XPAD_THUMBR moved");

/*
 * Every defined bit is distinct and bits 17 and up are still free. The
 * per-bit assertions above already pin each value, so this only has to
 * catch a new bit landing on top of an old one.
 */
XPAD_ASSERT((XPAD_UP | XPAD_DOWN | XPAD_LEFT | XPAD_RIGHT |
             XPAD_SOUTH | XPAD_EAST | XPAD_NORTH | XPAD_WEST |
             XPAD_TL | XPAD_TR | XPAD_TL2 | XPAD_TR2 |
             XPAD_SELECT | XPAD_START | XPAD_MODE |
             XPAD_THUMBL | XPAD_THUMBR) == 0x0001ffffUL,
            "button mask must stay 0x0001ffff");

/*
 * The kernel aliases BTN_X to north and BTN_Y to west. This is
 * deliberate and faithful; see "The X/Y trap" in AGENTS.md. If these
 * two fail, someone has "fixed" it.
 */
XPAD_ASSERT(XPAD_X == XPAD_NORTH, "XPAD_X must alias north, as BTN_X does");
XPAD_ASSERT(XPAD_Y == XPAD_WEST, "XPAD_Y must alias west, as BTN_Y does");

XPAD_ASSERT(XPAD_A == XPAD_SOUTH, "XPAD_A alias broken");
XPAD_ASSERT(XPAD_B == XPAD_EAST, "XPAD_B alias broken");

XPAD_ASSERT(XPAD_LB == XPAD_TL, "XPAD_LB alias broken");
XPAD_ASSERT(XPAD_RB == XPAD_TR, "XPAD_RB alias broken");
XPAD_ASSERT(XPAD_LT == XPAD_TL2, "XPAD_LT alias broken");
XPAD_ASSERT(XPAD_RT == XPAD_TR2, "XPAD_RT alias broken");
XPAD_ASSERT(XPAD_BACK == XPAD_SELECT, "XPAD_BACK alias broken");
XPAD_ASSERT(XPAD_GUIDE == XPAD_MODE, "XPAD_GUIDE alias broken");
XPAD_ASSERT(XPAD_L3 == XPAD_THUMBL, "XPAD_L3 alias broken");
XPAD_ASSERT(XPAD_R3 == XPAD_THUMBR, "XPAD_R3 alias broken");

XPAD_ASSERT(XPAD_DPAD == 0x0000000fUL, "XPAD_DPAD wrong");
XPAD_ASSERT(XPAD_FACE == 0x000000f0UL, "XPAD_FACE wrong");

/* ------------------------------------------------------------------ */
/* Frozen layout                                                       */
/* ------------------------------------------------------------------ */

XPAD_ASSERT(sizeof(XPAD_PAD) == 12, "XPAD_PAD must stay 12 bytes in v1");
XPAD_ASSERT(XPAD_PAD_SIZE_V1 == 12, "the v1 pad size is frozen at 12");
XPAD_ASSERT(sizeof(XPAD_PAD) >= XPAD_PAD_SIZE_V1,
            "XPAD_PAD may grow past the v1 size but never shrink below it");

XPAD_ASSERT(offsetof(XPAD_PAD, buttons) == 0, "buttons must lead XPAD_PAD");
XPAD_ASSERT(offsetof(XPAD_PAD, lx) == 4, "lx moved");
XPAD_ASSERT(offsetof(XPAD_PAD, lt) == 8, "lt moved");
XPAD_ASSERT(offsetof(XPAD_PAD, type) == 10, "type moved");
XPAD_ASSERT(offsetof(XPAD_PAD, flags) == 11, "flags moved");

/*
 * The XPAD header up to and including active. Everything past it holds
 * pointers, so its offsets and sizeof(XPAD) differ between the ST and an
 * LP64 host and cannot be frozen here. That is exactly why the block
 * publishes hdr_size and pads_offset as runtime fields: consumers read
 * where pads is rather than assuming.
 */
XPAD_ASSERT(offsetof(XPAD, magic) == 0, "magic must lead XPAD");
XPAD_ASSERT(offsetof(XPAD, version) == 4, "version moved");
XPAD_ASSERT(offsetof(XPAD, hdr_size) == 6, "hdr_size moved");
XPAD_ASSERT(offsetof(XPAD, pads_offset) == 8, "pads_offset moved");
XPAD_ASSERT(offsetof(XPAD, pad_size) == 10, "pad_size moved");
XPAD_ASSERT(offsetof(XPAD, caps) == 12, "caps moved");
XPAD_ASSERT(offsetof(XPAD, seq) == 14, "seq moved");
XPAD_ASSERT(offsetof(XPAD, pad_count) == 16, "pad_count moved");
XPAD_ASSERT(offsetof(XPAD, active) == 17, "active moved");

XPAD_ASSERT(sizeof(((XPAD *)0)->active) == 1,
            "active must stay one byte; the flip relies on it being atomic");

XPAD_ASSERT(offsetof(XPAD, active) + 1 == XPAD_HDR_FIXED,
            "XPAD_HDR_FIXED must cover exactly magic through active");

/* XPAD_REQ is the only consumer writable region, and holds no pointers,
 * so its whole layout is frozen on every architecture. */
XPAD_ASSERT(sizeof(XPAD_REQ) == 16, "XPAD_REQ must stay 16 bytes in v1");
XPAD_ASSERT(offsetof(XPAD_REQ, size) == 0, "size must lead XPAD_REQ");
XPAD_ASSERT(offsetof(XPAD_REQ, seq) == 2, "XPAD_REQ seq moved");
XPAD_ASSERT(offsetof(XPAD_REQ, rumble) == 4, "rumble moved");
XPAD_ASSERT(offsetof(XPAD_REQ, led) == 12, "led moved");

XPAD_ASSERT(XPAD_MAGIC == 0x58504144UL, "magic must be 'XPAD'");
XPAD_ASSERT(XPAD_COOKIE == 0x58504144UL, "cookie must be 'XPAD'");
XPAD_ASSERT(XPAD_VER_MAJOR == 1, "major version changed; was that intended?");
XPAD_ASSERT(XPAD_MAX_PADS == 4, "XPAD_MAX_PADS changed");

/* ------------------------------------------------------------------ */
/* Runtime checks                                                      */
/* ------------------------------------------------------------------ */

static int failures;

#ifndef __MINT__
/* Backs XPAD_JAR for the host build; see test/mint/osbind.h. On an ST
 * xpad.c reads the real jar at 0x5A0 and this does not exist. */
uint32_t *xpad_test_jar;
#endif

static void check(int ok, const char *what)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* xpad_fold_stick() must reach the diagonals a box deadzone would miss. */
static void check_fold_stick(void)
{
    XPAD_PAD pad;

    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, 0, 0, 40);
    check(pad.buttons == 0, "centred stick folds to nothing");

    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, -100, 0, 40);
    check(pad.buttons == XPAD_LEFT, "full left folds to LEFT only");

    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, 0, 100, 40);
    check(pad.buttons == XPAD_DOWN, "full down folds to DOWN only");

    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, 70, -70, 40);
    check(pad.buttons == (XPAD_RIGHT | XPAD_UP),
          "diagonal folds to both axes");

    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, 20, 20, 40);
    check(pad.buttons == 0, "inside radial deadzone folds to nothing");

    /* The case that separates the two tests: a box deadzone rejects
     * (30,30) at threshold 40 because neither axis clears it, while the
     * radial test passes it because the magnitude is about 42. Swap the
     * radial test for a box one and this is the check that fails. */
    memset(&pad, 0, sizeof(pad));
    xpad_fold_stick(&pad, 30, 30, 40);
    check(pad.buttons == (XPAD_RIGHT | XPAD_DOWN),
          "diagonal a box deadzone would reject still folds");
}

/* The double buffer must alternate and never hand back the front one. */
static void check_double_buffer(void)
{
    XPAD x;
    uint8_t first;
    XPAD_PAD *back;
    int ok = 1, i;

    xpad_init(&x, XPAD_MAX_PADS, XPAD_CAP_ANALOG, "test", 0);

    check(x.magic == XPAD_MAGIC, "xpad_init sets magic");
    check(x.pad_size == sizeof(XPAD_PAD), "xpad_init sets pad_size");
    check(x.pads_offset == offsetof(XPAD, pads), "xpad_init sets pads_offset");
    check(x.active == 0, "xpad_init starts on buffer 0");

    first = x.active;

    for (i = 0; i < 4; i++)
    {
        back = xpad_back(&x);

        if (back == (XPAD_PAD *)XPAD_PAD_AT(&x, x.active, 0))
            ok = 0; /* handed out the buffer being read */

        xpad_commit(&x);
    }

    check(ok, "xpad_back never returns the front buffer");
    check(x.active == first, "four commits return to the starting buffer");
    check(x.seq == 4, "seq counts commits");
}

/* xpad_init() owns the header's 1..XPAD_MAX_PADS invariant, so a block
 * it produces is always one xpad_find() will accept. */
static void check_init_clamps(void)
{
    XPAD x;

    xpad_init(&x, 0, 0, "test", 0);
    check(x.pad_count == 1, "xpad_init raises pad_count 0 to 1");

    xpad_init(&x, XPAD_MAX_PADS + 1, 0, "test", 0);
    check(x.pad_count == XPAD_MAX_PADS,
          "xpad_init clamps pad_count to XPAD_MAX_PADS");
}

/*
 * XPAD_PAD_AT strides by pad_count, so buffer 1 begins pad_count entries
 * in, not XPAD_MAX_PADS entries in. At pad_count == XPAD_MAX_PADS that
 * coincides with the declared v1 array.
 */
static void check_accessor(void)
{
    XPAD x;
    int ok = 1, packed = 1, n, b, i;

    for (n = 1; n <= XPAD_MAX_PADS; n++)
    {
        xpad_init(&x, (uint8_t)n, 0, "test", 0);

        for (b = 0; b < 2; b++)
        {
            for (i = 0; i < n; i++)
            {
                if (XPAD_PAD_AT(&x, b, i) != &x.pads[0][0] + b * n + i)
                    packed = 0;

                if (n == XPAD_MAX_PADS &&
                    XPAD_PAD_AT(&x, b, i) != &x.pads[b][i])
                    ok = 0;
            }
        }
    }

    check(packed, "XPAD_PAD_AT strides by pad_count");
    check(ok, "XPAD_PAD_AT matches the declared v1 array");
}

/*
 * The provider writes through xpad_back(), the consumer reads through
 * xpad_read(). They must agree about where a buffer starts at every
 * legal pad_count, not only at XPAD_MAX_PADS.
 */
static void check_write_read_agree(void)
{
    XPAD x;
    XPAD_PAD out;
    int ok = 1, n, i;

    for (n = 1; n <= XPAD_MAX_PADS; n++)
    {
        xpad_init(&x, (uint8_t)n, 0, "test", 0);

        for (i = 0; i < n; i++)
        {
            XPAD_PAD *back = xpad_back(&x);

            back[i].type = XPAD_TYPE_GAMEPAD;
            back[i].buttons = (uint32_t)(i + 1);
        }

        xpad_commit(&x);

        for (i = 0; i < n; i++)
        {
            if (!xpad_read(&x, i, &out) || out.buttons != (uint32_t)(i + 1))
                ok = 0;
        }

        if (xpad_connected(&x) != n)
            ok = 0;
    }

    check(ok, "xpad_back and xpad_read agree at every pad_count");
}

static void check_read_bounds(void)
{
    XPAD x;
    XPAD_PAD out;

    xpad_init(&x, 2, 0, "test", 0);

    check(xpad_read(&x, -1, &out) == 0, "xpad_read rejects a negative index");
    check(xpad_read(&x, 2, &out) == 0, "xpad_read rejects index >= pad_count");
    check(xpad_read(0, 0, &out) == 0, "xpad_read rejects a NULL block");
    check(xpad_read(&x, 0, 0) == 0, "xpad_read rejects a NULL destination");
    check(xpad_connected(0) == 0, "xpad_connected rejects a NULL block");
}

/*
 * README promises that a field added in a later revision reads as zero
 * against an older provider rather than as garbage. That case needs a
 * consumer whose XPAD_PAD has grown past the provider's pad_size, which
 * cannot happen while both are v1, so drive the zero-fill mechanism
 * directly with a short pad instead. A real v1 provider never publishes
 * below XPAD_PAD_SIZE_V1, and xpad_valid() refuses one that does.
 */
static void check_read_forward_compat(void)
{
    const uint16_t old_size = 8;
    XPAD x;
    XPAD_PAD out;
    uint8_t *raw;
    unsigned i;
    int ok = 1;

    xpad_init(&x, 1, 0, "test", 0);
    x.pad_size = old_size;

    raw = (uint8_t *)&x + x.pads_offset;
    for (i = 0; i < sizeof(XPAD_PAD); i++)
        raw[i] = 0xaa;

    memset(&out, 0xff, sizeof(out));

    check(xpad_read(&x, 0, &out) == 1, "xpad_read succeeds on a short pad");

    for (i = 0; i < old_size; i++)
    {
        if (((const uint8_t *)&out)[i] != 0xaa)
            ok = 0;
    }
    check(ok, "xpad_read copies the provider's bytes");

    ok = 1;
    for (i = old_size; i < sizeof(XPAD_PAD); i++)
    {
        if (((const uint8_t *)&out)[i] != 0)
            ok = 0;
    }
    check(ok, "xpad_read zeroes beyond the provider's pad_size");
}

/*
 * xpad_valid() is the gate every consumer passes through, so a block
 * xpad_init() produced must always clear it, and a block with layout
 * fields that do not describe a pad area inside itself must not.
 * Testable here only because the predicate is separate from
 * xpad_find(), which needs a cookie jar and supervisor mode.
 */
static void check_valid(void)
{
    XPAD x;
    int ok = 1, n;

    for (n = 1; n <= XPAD_MAX_PADS; n++)
    {
        xpad_init(&x, (uint8_t)n, 0, "test", 0);

        if (!xpad_valid(&x))
            ok = 0;
    }

    check(ok, "xpad_init always produces a block xpad_valid accepts");
    check(xpad_valid(0) == 0, "xpad_valid rejects NULL");

    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.magic = 0;
    check(xpad_valid(&x) == 0, "xpad_valid rejects a bad magic");

    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.version = (XPAD_VER_MAJOR + 1) << 8;
    check(xpad_valid(&x) == 0, "xpad_valid rejects a newer major version");

    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.pad_size = XPAD_PAD_SIZE_V1 - 1;
    check(xpad_valid(&x) == 0, "xpad_valid rejects pad_size below the v1 size");

    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.pads_offset = XPAD_HDR_FIXED - 1;
    check(xpad_valid(&x) == 0, "xpad_valid rejects pads inside the header");

    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.pads_offset = (uint16_t)(x.hdr_size + 2);
    check(xpad_valid(&x) == 0, "xpad_valid rejects pads past the block");

    /* The pad area must fit: one byte too much on the last pad is
     * enough to put it outside the block the provider claims. */
    xpad_init(&x, XPAD_MAX_PADS, 0, "test", 0);
    x.pad_size = (uint16_t)((x.hdr_size - x.pads_offset) / 8 + 1);
    check(xpad_valid(&x) == 0, "xpad_valid rejects a pad area that overruns");
}

/*
 * The request area is the only thing a consumer writes, so reaching it
 * through xpad_req() is what stops a consumer built against a later
 * header from writing fields an older provider never allocated.
 */
static void check_req(void)
{
    XPAD x;
    XPAD_REQ req;

    xpad_init(&x, 1, 0, "test", 0);
    check(xpad_req(&x) == 0, "xpad_req returns NULL when there is no req");
    check(xpad_req(0) == 0, "xpad_req rejects a NULL block");

    xpad_init(&x, 1, XPAD_CAP_RUMBLE, "test", &req);
    check(req.size == sizeof(XPAD_REQ), "xpad_init sets req->size");
    check(xpad_req(&x) == &req, "xpad_req returns a req of the right size");

    req.size = (uint16_t)(sizeof(XPAD_REQ) - 1);
    check(xpad_req(&x) == 0, "xpad_req refuses a req smaller than this header");

    req.size = (uint16_t)(sizeof(XPAD_REQ) + 8);
    check(xpad_req(&x) == &req, "xpad_req accepts a larger req from a newer provider");
}

/* ------------------------------------------------------------------ */
/* Cookie jar                                                          */
/* ------------------------------------------------------------------ */

/*
 * The jar is tested two ways, because neither way covers the other.
 *
 * On the host a fake jar is substituted for 0x5A0, which is the only
 * way to reach a full jar, a jar that is absent, or exact capacity
 * arithmetic. It cannot check a published address, because
 * xpad_publish() stores one through a uint32_t and a host pointer does
 * not survive that.
 *
 * On an ST the real jar is used, under real supervisor mode, where a
 * pointer is 32 bits and does survive. That is the round trip a
 * consumer actually performs, and only the emulator can run it.
 */

#ifndef __MINT__

/*
 * A jar is pairs of (id, value) ending in a (0, slots_left) terminator.
 */

#define JAR_SLOTS 8

static uint32_t jar[JAR_SLOTS * 2];

static void jar_reset(uint32_t used_pairs)
{
    uint32_t i;

    memset(jar, 0, sizeof(jar));

    for (i = 0; i < used_pairs; i++)
    {
        jar[i * 2] = 0x11110000UL + i; /* some other subsystem's cookie */
        jar[i * 2 + 1] = 0xdeadbeefUL;
    }

    jar[used_pairs * 2] = 0;                          /* terminator      */
    jar[used_pairs * 2 + 1] = JAR_SLOTS - used_pairs; /* slots remaining */

    xpad_test_jar = jar;
}

static void check_jar_publish(void)
{
    XPAD x;

    xpad_init(&x, 1, 0, "test", 0);

    /* Append into an empty jar. */
    jar_reset(0);
    check(xpad_publish(&x) == 1, "xpad_publish appends to an empty jar");
    check(jar[0] == XPAD_COOKIE, "xpad_publish writes the cookie id");
    check(jar[2] == 0, "xpad_publish lays a new terminator");
    check(jar[3] == JAR_SLOTS - 1, "xpad_publish consumes one slot");

    /* Publishing twice must repoint the entry, not append a second. */
    check(xpad_publish(&x) == 1, "xpad_publish succeeds when already present");
    check(jar[2] == 0 && jar[3] == JAR_SLOTS - 1,
          "xpad_publish replaces rather than appending twice");

    /* Append past existing cookies belonging to other subsystems. */
    jar_reset(3);
    check(xpad_publish(&x) == 1, "xpad_publish appends past other cookies");
    check(jar[0] == 0x11110000UL, "xpad_publish leaves earlier cookies alone");
    check(jar[6] == XPAD_COOKIE, "xpad_publish appends at the terminator");
    check(jar[8] == 0 && jar[9] == JAR_SLOTS - 4,
          "xpad_publish moves the terminator along");

    /* A jar with only the terminator slot left cannot take an entry. */
    jar_reset(JAR_SLOTS - 1);
    check(xpad_publish(&x) == 0, "xpad_publish refuses a full jar");
    check(jar[(JAR_SLOTS - 1) * 2] == 0, "a refused publish leaves the jar alone");

    /* No jar at all. */
    xpad_test_jar = 0;
    check(xpad_publish(&x) == 0, "xpad_publish fails with no jar");
    check(xpad_unpublish() == 0, "xpad_unpublish fails with no jar");
    check(xpad_find() == 0, "xpad_find returns NULL with no jar");
}

static void check_jar_unpublish(void)
{
    XPAD x;
    int i;

    xpad_init(&x, 1, 0, "test", 0);

    /* Removing the only entry empties the jar. */
    jar_reset(0);
    xpad_publish(&x);
    check(xpad_unpublish() == 1, "xpad_unpublish removes the entry");
    check(jar[0] == 0, "xpad_unpublish restores the terminator");
    check(jar[1] == JAR_SLOTS, "xpad_unpublish gives the slot back");
    check(xpad_unpublish() == 0, "xpad_unpublish fails when absent");
    check(xpad_find() == 0, "xpad_find returns NULL when the cookie is absent");

    /* Removing from the middle must shuffle the tail down intact. */
    jar_reset(1);
    xpad_publish(&x);      /* jar: other, XPAD, terminator */
    jar[4] = 0x22220000UL; /* append another by hand       */
    jar[5] = 0x12345678UL;
    jar[6] = 0;
    jar[7] = JAR_SLOTS - 3;

    check(xpad_unpublish() == 1, "xpad_unpublish removes a middle entry");
    check(jar[0] == 0x11110000UL, "the cookie before it is untouched");
    check(jar[2] == 0x22220000UL && jar[3] == 0x12345678UL,
          "the cookie after it shuffles down intact");
    check(jar[4] == 0 && jar[5] == JAR_SLOTS - 3 + 1,
          "the terminator shuffles down, one slot richer");

    /* The count must survive repeated cycles: a TSR that reinstalls
     * itself is the case that used to walk the jar down to nothing. */
    jar_reset(0);
    for (i = 0; i < 5; i++)
    {
        xpad_publish(&x);
        xpad_unpublish();
    }
    check(jar[1] == JAR_SLOTS, "install and remove cycles leak no slots");
}

#else /* __MINT__ */

/*
 * The round trip through TOS's own jar: publish, find it again, read a
 * pad through the pointer that came back, then withdraw. This is what
 * the host build cannot reach, so a failure here is a failure nothing
 * else in this harness would catch.
 */

static XPAD live;

static void check_jar_live(void)
{
    const XPAD *found;
    XPAD_PAD *back;
    XPAD_PAD out;

    xpad_init(&live, 2, XPAD_CAP_ANALOG, "hatari harness 1.0", 0);
    check(xpad_valid(&live), "the block to publish is valid");

    if (!xpad_publish(&live))
    {
        /* Not a defect in xpad: the jar had no room for another entry. */
        check(0, "xpad_publish installs the cookie (jar too small?)");
        return;
    }

    check(1, "xpad_publish installs the cookie");

    found = xpad_find();
    check(found == &live, "xpad_find returns the block that was published");

    if (found)
    {
        back = xpad_back(&live);
        back[1].type = XPAD_TYPE_XBOX;
        back[1].buttons = XPAD_SOUTH | XPAD_START;
        back[1].lx = -42;
        xpad_commit(&live);

        check(xpad_read(found, 1, &out),
              "xpad_read succeeds through the found block");
        check(out.type == XPAD_TYPE_XBOX &&
                  out.buttons == (XPAD_SOUTH | XPAD_START) &&
                  out.lx == -42,
              "a pad survives publish, find and read");
        check(xpad_connected(found) == 1, "xpad_connected sees the pad");
    }

    check(xpad_publish(&live), "xpad_publish repoints an existing cookie");
    check(xpad_unpublish(), "xpad_unpublish withdraws the cookie");
    check(xpad_find() == 0, "xpad_find returns NULL once withdrawn");
    check(xpad_unpublish() == 0, "xpad_unpublish fails when already gone");
}

#endif /* __MINT__ */

int main(void)
{
    printf("xpad ABI checks, version %d.%d\n\n",
           XPAD_VER_MAJOR, XPAD_VER_MINOR);

    check_fold_stick();
    check_double_buffer();
    check_init_clamps();
    check_accessor();
    check_write_read_agree();
    check_read_bounds();
    check_read_forward_compat();
    check_valid();
    check_req();
#ifndef __MINT__
    check_jar_publish();
    check_jar_unpublish();
#else
    check_jar_live();
#endif

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");

#ifdef __MINT__
    /* Hatari has no way to hand an exit status back, and sits at the
     * desktop once this returns. The runner watches for this line so it
     * can stop the emulator as soon as there is a verdict. */
    printf("XPAD-DONE %d\n", failures ? 1 : 0);
#endif
    fflush(stdout);

    return failures ? 1 : 0;
}
