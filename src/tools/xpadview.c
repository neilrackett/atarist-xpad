/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad viewer: live input state for whichever provider is installed.
 *
 *   XPADVIEW       view the installed provider
 *   XPADVIEW -d    publish a demo provider and view that
 *   XPADVIEW -1    print one frame as plain text and exit
 *
 * EXAMPLE CONSUMER, and the reference one: written exactly the way
 * README tells a consumer to write one, so the advice there cannot
 * drift away from something that compiles. Find once, cache the
 * pointer, read per frame.
 *
 * The live display has only been checked at 80 columns, so the 40
 * column layout low resolution gives you is unverified.
 *
 * Buttons are labelled by position, never by the letter printed on a
 * pad. See the X/Y trap in xpad.h for why that distinction matters.
 */

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "../xpad.h"

#define DEMO_PROVIDER "Xpad demo provider 1.0"
#define SAMPLE_FRAMES 50 /* one second at 50 Hz, for the seq rate */

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/* ------------------------------------------------------------------ */

static void put(const char *s)
{
    (void)Cconws(s);
}

/* VT52, so this works in every resolution rather than banging a
 * framebuffer that only exists in one of them. */
static void at(int row, int col)
{
    char seq[5];

    seq[0] = 27;
    seq[1] = 'Y';
    seq[2] = (char)(32 + row);
    seq[3] = (char)(32 + col);
    seq[4] = 0;

    put(seq);
}

static void cls(void)
{
    put("\033E");
}

static void cursor(int on)
{
    put(on ? "\033e" : "\033f");
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

static const char *type_name(uint8_t type)
{
    switch (type)
    {
    case XPAD_TYPE_NONE:
        return "empty";
    case XPAD_TYPE_JOYSTICK:
        return "joystick";
    case XPAD_TYPE_GAMEPAD:
        return "gamepad";
    case XPAD_TYPE_XBOX:
        return "Xbox";
    case XPAD_TYPE_PLAYSTATION:
        return "PlayStation";
    case XPAD_TYPE_NINTENDO:
        return "Nintendo";
    case XPAD_TYPE_KEYBOARD:
        return "keyboard";
    default:
        return "unknown";
    }
}

/*
 * Positional labels, in bit order. S/E/N/W are south, east, north and
 * west: the physical corners, not the legends, which is the whole point
 * of the naming in xpad.h.
 */
static const struct
{
    uint32_t bit;
    const char *label;
} buttons[] = {
    {XPAD_UP, "UP"}, {XPAD_DOWN, "DN"}, {XPAD_LEFT, "LF"}, {XPAD_RIGHT, "RT"},
    {XPAD_SOUTH, "S"}, {XPAD_EAST, "E"}, {XPAD_NORTH, "N"}, {XPAD_WEST, "W"},
    {XPAD_TL, "TL"}, {XPAD_TR, "TR"}, {XPAD_TL2, "L2"}, {XPAD_TR2, "R2"},
    {XPAD_SELECT, "SE"}, {XPAD_START, "ST"}, {XPAD_MODE, "MO"},
    {XPAD_THUMBL, "L3"}, {XPAD_THUMBR, "R3"}};

#define BUTTON_COUNT (sizeof(buttons) / sizeof(buttons[0]))

/* First eight on one row, the rest on the next: 40 columns is all low
 * resolution gives us. */
#define ROW_SPLIT 8

static void button_labels(unsigned from, unsigned to)
{
    unsigned i;

    for (i = from; i < to; i++)
        printf("%3s", buttons[i].label);
}

static void button_states(uint32_t held, unsigned from, unsigned to)
{
    unsigned i;

    for (i = from; i < to; i++)
        printf("%3s", (held & buttons[i].bit) ? "*" : ".");
}

/* ------------------------------------------------------------------ */
/* Demo provider                                                       */
/* ------------------------------------------------------------------ */

/*
 * A fake provider, so the viewer can be developed, demonstrated and
 * tested with no hardware and nothing else installed. It exercises the
 * provider half of the API as well as the consumer half.
 */

static XPAD demo;
static XPAD_REQ demo_req;

/* Quarter turn of a circle in eighths, scaled to a stick's range. */
static const int8_t wave[8] = {0, 90, 127, 90, 0, -90, -127, -90};

static void demo_init(void)
{
    xpad_init(&demo, 2, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE | XPAD_CAP_LED,
              DEMO_PROVIDER, &demo_req);
}

static void demo_frame(unsigned tick)
{
    XPAD_PAD *back = xpad_back(&demo);
    unsigned i;

    for (i = 0; i < 2; i++)
    {
        unsigned phase = (tick / 4 + i * 2) & 7;

        memset(&back[i], 0, sizeof(back[i]));

        back[i].type = i ? XPAD_TYPE_PLAYSTATION : XPAD_TYPE_XBOX;
        back[i].flags = XPAD_PAD_ANALOG | (i ? XPAD_PAD_WIRELESS : 0);

        back[i].lx = wave[phase];
        back[i].ly = wave[(phase + 2) & 7];
        back[i].rx = wave[(phase + 4) & 7];
        back[i].ry = wave[(phase + 6) & 7];
        back[i].lt = (uint8_t)(tick * 4);
        back[i].rt = (uint8_t)(255 - (uint8_t)(tick * 4));

        /* Walk one button at a time so every bit gets exercised. */
        back[i].buttons = buttons[(tick / 8 + i) % BUTTON_COUNT].bit;

        /* Fold the left stick in, exactly as a real provider should. */
        xpad_fold_stick(&back[i], back[i].lx, back[i].ly, 40);
    }

    xpad_commit(&demo);
}

/* ------------------------------------------------------------------ */
/* Diagnosis when nothing is found                                     */
/* ------------------------------------------------------------------ */

/*
 * xpad_find() returns NULL both when no provider is installed and when
 * one is installed but malformed, and those are completely different
 * problems. Walk the jar directly to tell them apart: worth duplicating
 * ten lines so a provider author gets a useful message.
 */

#define JAR (*(uint32_t **)0x5A0L)

static void *raw_cookie;

static long find_raw(void)
{
    uint32_t *jar = JAR;

    raw_cookie = 0;

    if (jar)
    {
        while (jar[0])
        {
            if (jar[0] == XPAD_COOKIE)
            {
                raw_cookie = (void *)jar[1];
                break;
            }
            jar += 2;
        }
    }

    return 0;
}

static void explain_absence(void)
{
    const XPAD *x;

    Supexec(find_raw);
    x = (const XPAD *)raw_cookie;

    if (!x)
    {
        put("No Xpad provider found.\r\n\r\n");
        put("No XPAD cookie is installed. Load a driver,\r\n");
        put("or run XPADVIEW -d to see the viewer work.\r\n");
        return;
    }

    put("An XPAD cookie is installed, but the block it\r\n");
    put("points at is not one this build can read.\r\n\r\n");
    printf("  magic    %08lx (want %08lx)\r\n",
           (unsigned long)x->magic, (unsigned long)XPAD_MAGIC);
    printf("  version  %d.%d (want major %d)\r\n",
           x->version >> 8, x->version & 0xff, XPAD_VER_MAJOR);
    printf("  pads     %d, %d bytes each\r\n", x->pad_count, x->pad_size);
    put("\r\nThat is a provider bug, not a missing provider.\r\n");
}

/* ------------------------------------------------------------------ */
/* Rendering                                                           */
/* ------------------------------------------------------------------ */

static void draw_header(const XPAD *x, int sel, unsigned rate)
{
    int i;

    at(0, 0);
    printf("Xpad viewer            v%d.%d  %3u/s ",
           x->version >> 8, x->version & 0xff, rate);

    at(1, 0);
    printf("%-32s", x->provider ? x->provider : "(unnamed)");

    at(2, 0);
    put("caps ");
    printf("%s%s%s%s      ",
           (x->caps & XPAD_CAP_ANALOG) ? "ANALOG " : "",
           (x->caps & XPAD_CAP_RUMBLE) ? "RUMBLE " : "",
           (x->caps & XPAD_CAP_LED) ? "LED " : "",
           (x->caps & XPAD_CAP_HOTPLUG) ? "HOTPLUG" : "");

    /* Which slots hold something, so nobody has to cycle all four to
     * find out whether a second controller was seen. */
    at(3, 0);
    put("pads ");
    for (i = 0; i < XPAD_MAX_PADS; i++)
    {
        XPAD_PAD pad;
        int live = i < x->pad_count && xpad_read(x, i, &pad) &&
                   pad.type != XPAD_TYPE_NONE;

        if (i == sel)
            printf("[%d]%c", i, live ? '*' : ' ');
        else
            printf(" %d %c", i, live ? '*' : ' ');
    }
    printf("  buf %d      ", x->active);
}

static void draw_pad(const XPAD *x, int sel)
{
    XPAD_PAD pad;

    at(5, 0);

    if (!xpad_read(x, sel, &pad))
    {
        printf("Pad %d is not present.%-18s", sel, "");
        at(6, 0);
        printf("%-40s", "");
        at(7, 0);
        printf("%-40s", "");
        at(8, 0);
        printf("%-40s", "");
        at(9, 0);
        printf("%-40s", "");
        at(10, 0);
        printf("%-40s", "");
        return;
    }

    printf("Pad %d  %-12s %s%s%s     ", sel, type_name(pad.type),
           (pad.flags & XPAD_PAD_ANALOG) ? "ANALOG " : "",
           (pad.flags & XPAD_PAD_WIRELESS) ? "BT " : "",
           (pad.flags & XPAD_PAD_LOWBATT) ? "LOWBATT" : "");

    at(6, 0);
    printf("buttons %08lx", (unsigned long)pad.buttons);

    at(7, 0);
    button_labels(0, ROW_SPLIT);
    at(8, 0);
    button_states(pad.buttons, 0, ROW_SPLIT);

    at(9, 0);
    button_labels(ROW_SPLIT, BUTTON_COUNT);
    at(10, 0);
    button_states(pad.buttons, ROW_SPLIT, BUTTON_COUNT);

    at(12, 0);
    printf("stick L %+4d,%+4d   R %+4d,%+4d", pad.lx, pad.ly, pad.rx, pad.ry);

    at(13, 0);
    printf("trig  L %3u  R %3u        ", pad.lt, pad.rt);
}

static void snapshot(const XPAD *x, int sel)
{
    XPAD_PAD pad;

    printf("provider %s\n", x->provider ? x->provider : "(unnamed)");
    printf("version  %d.%d\n", x->version >> 8, x->version & 0xff);
    printf("caps     %04x\n", x->caps);
    printf("pads     %d\n", x->pad_count);
    printf("connected %d\n", xpad_connected(x));

    if (!xpad_read(x, sel, &pad))
    {
        printf("pad %d   unreadable\n", sel);
        return;
    }

    printf("pad %d    type %s flags %02x\n", sel, type_name(pad.type),
           pad.flags);
    printf("buttons  %08lx\n", (unsigned long)pad.buttons);
    printf("sticks   %d,%d %d,%d\n", pad.lx, pad.ly, pad.rx, pad.ry);
    printf("triggers %u %u\n", pad.lt, pad.rt);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static int view(const XPAD *x, int demo_mode)
{
    unsigned tick = 0, frames = 0, rate = 0;
    uint16_t last_seq = x->seq;
    int sel = 0;
    int running = 1;

    cls();
    cursor(0);

    at(23, 0);
    put("1-4 pad   Q quit");

    while (running)
    {
        if (demo_mode)
            demo_frame(tick);

        draw_header(x, sel, rate);
        draw_pad(x, sel);

        while (Bconstat(2))
        {
            long key = Bconin(2);
            char c = (char)(key & 0xff);

            if (c == 'q' || c == 'Q' || c == 27)
                running = 0;
            else if (c >= '1' && c <= '4')
                sel = c - '1';
        }

        Vsync();
        tick++;

        if (++frames >= SAMPLE_FRAMES)
        {
            rate = (uint16_t)(x->seq - last_seq);
            last_seq = x->seq;
            frames = 0;
        }
    }

    cursor(1);
    cls();

    return 0;
}

int main(int argc, char **argv)
{
    const XPAD *x;
    int demo_mode = 0;
    int once = 0;

#ifdef XPAD_SELFTEST
    /* Hatari's --auto passes no arguments, so the harness build cannot
     * be asked for a mode: it reports on whatever provider is already
     * installed, and only publishes the demo when there is none. That
     * makes one binary cover both the standalone test and the
     * integration test, where a driver is loaded from AUTO first. */
    (void)argc;
    (void)argv;
    demo_mode = (xpad_find() == 0);
    once = 1;
#else
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-d") == 0)
            demo_mode = 1;
        else if (strcmp(argv[i], "-1") == 0)
            once = 1;
    }
#endif

    if (demo_mode)
    {
        demo_init();
        demo_frame(0);

        if (!xpad_publish(&demo))
        {
            put("Could not install the XPAD cookie. Is the jar full?\r\n");
            return 1;
        }
    }

    x = xpad_find();

    if (!x)
    {
        explain_absence();
        if (demo_mode)
            xpad_unpublish();
        return 1;
    }

    if (once)
    {
        snapshot(x, 0);
        printf("\nXPAD-DONE 0\n");
        fflush(stdout);
    }
    else
    {
        view(x, demo_mode);
    }

    /* Leave the jar as we found it. */
    if (demo_mode)
        xpad_unpublish();

    return 0;
}
