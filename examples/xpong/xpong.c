/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpong: Pong for one player against the computer, or two against each
 * other, on a pad each or sharing one, on whatever pads the installed
 * Xpad provider has.
 *
 *   XPONG       play
 *   XPONG -d    publish a demo provider and watch it play itself
 *
 * EXAMPLE GAME. Ported from MD/Sidepad's SIDEPONG, which hooked the
 * IKBD's joystick and mouse vectors; this reads nothing but Xpad. Find
 * the block once, read every pad once a frame, and work out presses by
 * comparing with the frame before, since Xpad leaves edge detection to
 * the game on purpose.
 *
 * Controls, on any pad:
 *
 *   up, down       move your paddle. A pad with analogue sticks uses
 *                  the left one, as far as it is pushed
 *   south, start   choose on the menus; start pauses a match
 *   select         back: from a match to the menu, from the menu to
 *                  the desktop. Escape on the keyboard does the same,
 *                  which is the way out with a joystick
 *
 * Two players sharing a pad play left stick against right. Without
 * analogue sticks, the left player has the d-pad and the right player
 * north and south of the face buttons, and only start plays again.
 *
 * Rumble, wherever the provider offers it: a tap on the high frequency
 * motor when you return the ball, a thump on the low frequency one when
 * you lose a point, and both when you lose the match. Sharing a pad,
 * each player feels only the motor in their own grip.
 *
 * The rules are in pong.h and the rumble bookkeeping in rumble.h, both
 * free of TOS so the host build tests them. This file reads the pads
 * and draws.
 *
 * Low resolution, so it needs a colour monitor.
 */

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "../../src/xpad.h"
#include "pong.h"
#include "rumble.h"

#define DEMO_PROVIDER "Xpong demo provider 1.0"

/* _frclock, the frames the display has shown since boot. A system
 * variable below $800, so supervisor only. Read to seed the rules and
 * to time the demo, never per frame. */
#define FRCLOCK ((volatile uint32_t *)0x466)

#define SCAN_ESC 0x01

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/* ------------------------------------------------------------------ */

#define LINE_BYTES 160
#define SCREEN_BYTES 32000L

static uint8_t *screen[2]; /* double buffered */
static int drawing;        /* which of them is being drawn */
static uint8_t *back;      /* and where it is */

/*
 * The court is grey, which is bitplane 3 alone, and colours 9 to 15
 * repeat 1 to 7, so anything drawn across a line of the court keeps
 * its own colour where they cross.
 */
#define WHITE 1
#define RED 2
#define ORANGE 3
#define YELLOW 4
#define GREEN 5 /* the left player  */
#define CYAN 6  /* the right player */
#define MAGENTA 7
#define GREY 8

static const uint16_t palette[16] = {
    0x000, 0x777, 0x700, 0x750, 0x770, 0x070, 0x077, 0x707,
    0x444, 0x777, 0x700, 0x750, 0x770, 0x070, 0x077, 0x707};

static const int rainbow[6] = {RED, ORANGE, YELLOW, GREEN, CYAN, MAGENTA};

static const int side_colour[2] = {GREEN, CYAN};

/* 8x8, only the glyphs the game needs. */
static const char font_chars[] = "0123456789ABCDEFGHIJKLMNOPRSTUVWXY";
static const uint8_t font[][8] = {
    {0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00}, /* 0 */
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, /* 1 */
    {0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x60, 0xFE, 0x00}, /* 2 */
    {0x7C, 0xC6, 0x06, 0x1C, 0x06, 0xC6, 0x7C, 0x00}, /* 3 */
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x00}, /* 4 */
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00}, /* 5 */
    {0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00}, /* 6 */
    {0xFE, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00}, /* 7 */
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00}, /* 8 */
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00}, /* 9 */
    {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00}, /* A */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00}, /* B */
    {0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00}, /* C */
    {0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00}, /* D */
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFE, 0x00}, /* E */
    {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00}, /* F */
    {0x7C, 0xC6, 0xC0, 0xDE, 0xC6, 0xC6, 0x7C, 0x00}, /* G */
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, /* H */
    {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00}, /* I */
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, 0x78, 0x00}, /* J */
    {0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00}, /* K */
    {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00}, /* L */
    {0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00}, /* M */
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, /* N */
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, /* O */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00}, /* P */
    {0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00}, /* R */
    {0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00}, /* S */
    {0xFE, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x00}, /* T */
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, /* U */
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00}, /* V */
    {0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00}, /* W */
    {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00}, /* X */
    {0xC6, 0xC6, 0x6C, 0x38, 0x38, 0x38, 0x38, 0x00}, /* Y */
};

/* 3x5, for the credit. Low three bits of each row, left pixel first. */
static const char tiny_chars[] = ".COMNEILRAKT";
static const uint8_t tiny[][5] = {
    {0, 0, 0, 0, 2}, /* . */
    {7, 4, 4, 4, 7}, /* C */
    {7, 5, 5, 5, 7}, /* O */
    {5, 7, 7, 5, 5}, /* M */
    {6, 5, 5, 5, 5}, /* N */
    {7, 4, 6, 4, 7}, /* E */
    {7, 2, 2, 2, 7}, /* I */
    {4, 4, 4, 4, 7}, /* L */
    {6, 5, 6, 5, 5}, /* R */
    {2, 5, 7, 5, 5}, /* A */
    {5, 6, 4, 6, 5}, /* K */
    {7, 2, 2, 2, 2}, /* T */
};

/* Left edge that centres s, at w pixels a character. */
static int centre(const char *s, int w)
{
    return (320 - (int)strlen(s) * w) / 2;
}

/* Rows of 16 pixels into every bitplane that colour has set: ORed for
 * the still picture, exclusive ORed for what moves, so that drawing it
 * a second time takes it off again. */
static void draw_rows(int x, int y, const uint16_t *rows, int h, int colour,
                      int xor)
{
    uint8_t *p = back + (long)y * LINE_BYTES + ((x >> 4) << 3);
    int shift = x & 15;
    int last = (x >> 4) >= 19; /* nothing to the right to spill into */
    int i, plane;

    for (i = 0; i < h; i++, p += LINE_BYTES)
    {
        uint32_t d = ((uint32_t)rows[i] << 16) >> shift;
        uint16_t hi = (uint16_t)(d >> 16);
        uint16_t lo = (uint16_t)d;

        for (plane = 0; plane < 4; plane++)
        {
            uint16_t *w = (uint16_t *)(p + (plane << 1));

            if (!(colour & (1 << plane)))
                continue;

            if (xor)
            {
                w[0] ^= hi;
                if (lo && !last)
                    w[4] ^= lo;
            }
            else
            {
                w[0] |= hi;
                if (lo && !last)
                    w[4] |= lo;
            }
        }
    }
}

/* A bar 4 pixels wide: a paddle, the ball, a dash of the net. */
static void draw_bar(int x, int y, int h, int colour, int xor)
{
    static const uint16_t solid[PONG_PADDLE_H] = {
        0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000,
        0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000,
        0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000,
        0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000, 0xF000};

    draw_rows(x, y, solid, h, colour, xor);
}

/* The full width, 2 pixels tall, in grey: bitplane 3 alone. */
static void draw_wall(int y)
{
    uint16_t *p = (uint16_t *)(back + (long)y * LINE_BYTES);
    int i;

    for (i = 0; i < 2 * 20; i++, p += 4)
        p[3] = 0xFFFF;
}

static int glyph(const char *chars, char c)
{
    const char *at = strchr(chars, c);

    return c && at ? (int)(at - chars) : -1;
}

static void draw_text(int x, int y, const char *s, int colour)
{
    for (; *s; s++, x += 8)
    {
        int g = glyph(font_chars, *s);
        uint16_t rows[8];
        int i;

        if (g < 0)
            continue;

        for (i = 0; i < 8; i++)
            rows[i] = (uint16_t)(font[g][i] << 8);

        draw_rows(x, y, rows, 8, colour, 0);
    }
}

/* Every pixel of b, doubled. */
static uint16_t widen(uint8_t b)
{
    uint16_t r = 0;
    int i;

    for (i = 0; i < 8; i++)
        if (b & (0x80 >> i))
            r |= (uint16_t)(0xC000 >> (i * 2));

    return r;
}

/* draw_text at twice the size. colour < 0 cycles through the rainbow. */
static void draw_big(int x, int y, const char *s, int colour)
{
    int n;

    for (n = 0; *s; s++, x += 16, n++)
    {
        int g = glyph(font_chars, *s);
        uint16_t rows[16];
        int i;

        if (g < 0)
            continue;

        for (i = 0; i < 8; i++)
            rows[i * 2] = rows[i * 2 + 1] = widen(font[g][i]);

        draw_rows(x, y, rows, 16, colour < 0 ? rainbow[n % 6] : colour, 0);
    }
}

static void draw_tiny(int x, int y, const char *s, int colour)
{
    for (; *s; s++, x += 4)
    {
        int g = glyph(tiny_chars, *s);
        uint16_t rows[5];
        int i;

        if (g < 0)
            continue;

        for (i = 0; i < 5; i++)
            rows[i] = (uint16_t)(tiny[g][i] << 13);

        draw_rows(x, y, rows, 5, colour, 0);
    }
}

/* Show what was just drawn and start on the other buffer. The new base
 * is latched at the next frame, so wait for it before drawing over the
 * one that was showing. */
static void flip(void)
{
    Setscreen(screen[drawing ^ 1], screen[drawing], -1);
    Vsync();

    drawing ^= 1;
    back = screen[drawing];
}

/* ------------------------------------------------------------------ */
/* Pads                                                                */
/* ------------------------------------------------------------------ */

static const XPAD *xpad;                /* found once, at startup  */
static XPAD_PAD pads[XPAD_MAX_PADS];    /* this frame's state      */
static uint32_t pressed[XPAD_MAX_PADS]; /* went down this frame    */
static RUMBLE rumble;

#define FIRE (XPAD_SOUTH | XPAD_START)

/*
 * Every pad, once a frame. A read that could not be made consistent
 * keeps last frame's state. It goes through a copy because xpad_read()
 * writes its output on every attempt, so a failed read leaves a torn
 * one behind, and a torn read can show a button pressed that never was.
 */
static void read_pads(void)
{
    XPAD_PAD now;
    int i;

    for (i = 0; i < xpad->pad_count; i++)
    {
        if (!xpad_read(xpad, i, &now))
        {
            pressed[i] = 0;
            continue;
        }

        pressed[i] = now.buttons & ~pads[i].buttons;
        pads[i] = now;
    }
}

/* The first pad other than not that pressed any of mask this frame,
 * or -1. Pass -1 as not to accept any pad. */
static int pad_pressing(uint32_t mask, int not)
{
    int i;

    for (i = 0; i < xpad->pad_count; i++)
        if (i != not && (pressed[i] & mask))
            return i;

    return -1;
}

/*
 * Escape on the keyboard, which means back, so a joystick with one
 * button can still leave. Read through the BIOS and every key taken,
 * so none pile up in the buffer while a keyboard provider is chaining
 * to TOS. Matched on the scancode too: that is the key's position, and
 * the same whichever national keyboard table the ROM carries.
 */
static int escape_pressed(void)
{
    int esc = 0;

    while (Bconstat(2))
    {
        long key = Bconin(2);

        if ((key & 0xff) == 27 || ((key >> 16) & 0xff) == SCAN_ESC)
            esc = 1;
    }

    return esc;
}

/* ------------------------------------------------------------------ */
/* The game                                                            */
/* ------------------------------------------------------------------ */

#define CPU (-1) /* a side the computer plays */

enum
{
    TITLE,  /* the menu, over a rally the computer plays itself */
    JOIN,   /* two players chosen, waiting for the second       */
    PLAY,
    PAUSED, /* by start, or because a player's pad went away    */
    OVER,   /* somebody won                                     */
    QUIT
};

/* The menu, in order. */
enum
{
    VS_CPU,     /* one player, against the computer          */
    VS_PAD,     /* two players, a pad each                   */
    VS_SHARED,  /* two players on one pad, a stick each      */
    MENU_ITEMS
};

static const char *const menu_items[MENU_ITEMS] = {
    "1 PLAYER", "2 PLAYERS", "2 PLAYERS 1 PAD"};

static int state;
static int menu;      /* which item is chosen            */
static int player[2]; /* each side's pad, or CPU         */
static int hand[2];   /* and which half of it they use   */
static PONG game;     /* the match, or the title's rally */
static unsigned long frame;

/* Counted for the demo's verdict, and harmless otherwise. */
static struct
{
    unsigned hits, points, pauses, lost, matches;
} stats;

static int human(int side)
{
    return player[side] != CPU;
}

/* Both players on one pad, a hand each. */
static int shared(void)
{
    return human(PONG_RIGHT) && player[PONG_LEFT] == player[PONG_RIGHT];
}

/* The first side whose player's pad has gone, or -1. A provider marks
 * a pad that has disconnected as XPAD_TYPE_NONE rather than leaving
 * stale state in it, so this is how a game notices. */
static int pad_lost(void)
{
    int side;

    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
        if (human(side) && pads[player[side]].type == XPAD_TYPE_NONE)
            return side;

    return -1;
}

/* Whether either player, and nobody else, pressed any of mask. */
static int players_pressed(uint32_t mask)
{
    int side;

    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
        if (human(side) && (pressed[player[side]] & mask))
            return 1;

    return 0;
}

static int move_for(int side)
{
    return human(side) ? pong_pad_speed(&pads[player[side]], hand[side])
                       : pong_cpu(&game, side);
}

/*
 * What a frame felt like, decided by pong_feel() and sent to each
 * player's pad. The computer's side has no pad, which rumble_pulse()
 * refuses, and on a shared pad each player has a motor of their own.
 */
static void feel(int events)
{
    int side;

    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
    {
        PONG_FEEL f = pong_feel(&game, events, side, shared());

        if (f.frames)
            rumble_pulse(&rumble, xpad->caps, player[side], f.lo, f.hi,
                         f.frames);
    }

    if (events & (PONG_HIT(PONG_LEFT) | PONG_HIT(PONG_RIGHT)))
        stats.hits++;
    if (events & (PONG_POINT(PONG_LEFT) | PONG_POINT(PONG_RIGHT)))
        stats.points++;
}

static void start_match(void)
{
    pong_reset(&game, game.rng);
    state = PLAY;
}

/* Back to the menu from anywhere, motors off. The rally starts over
 * for the menu to sit on. */
static void to_title(void)
{
    rumble_stop_all(&rumble);
    pong_reset(&game, game.rng);
    state = TITLE;
}

static void title(void)
{
    int p;

    pong_step(&game, pong_cpu(&game, PONG_LEFT), pong_cpu(&game, PONG_RIGHT));
    if (pong_winner(&game) >= 0)
        pong_reset(&game, game.rng);

    if (pad_pressing(XPAD_UP, -1) >= 0 && menu > 0)
        menu--;
    if (pad_pressing(XPAD_DOWN, -1) >= 0 && menu < MENU_ITEMS - 1)
        menu++;

    if (pad_pressing(XPAD_SELECT, -1) >= 0)
    {
        state = QUIT;
        return;
    }

    p = pad_pressing(FIRE, -1);
    if (p < 0)
        return;

    /* Whoever chose plays on the left, with the left hand. On a shared
     * pad the right player has the right hand of the same pad. */
    player[PONG_LEFT] = p;
    hand[PONG_LEFT] = PONG_LEFT;
    hand[PONG_RIGHT] = PONG_RIGHT;

    if (menu == VS_PAD)
    {
        state = JOIN;
        return;
    }

    player[PONG_RIGHT] = menu == VS_SHARED ? p : CPU;
    start_match();
}

/* The second player is whichever other pad presses fire first, so it
 * makes no difference which slots the two pads landed in. */
static void join(void)
{
    int p;

    if (pad_pressing(XPAD_SELECT, -1) >= 0)
    {
        to_title();
        return;
    }

    p = pad_pressing(FIRE, player[PONG_LEFT]);
    if (p < 0)
        return;

    player[PONG_RIGHT] = p;
    hand[PONG_RIGHT] = PONG_LEFT; /* their own pad, played the usual way */
    start_match();
}

static void play(void)
{
    if (players_pressed(XPAD_SELECT))
    {
        to_title();
        return;
    }

    if (pad_lost() >= 0)
    {
        rumble_stop_all(&rumble);
        state = PAUSED;
        stats.lost++;
        return;
    }

    if (players_pressed(XPAD_START))
    {
        rumble_stop_all(&rumble);
        state = PAUSED;
        stats.pauses++;
        return;
    }

    feel(pong_step(&game, move_for(PONG_LEFT), move_for(PONG_RIGHT)));

    if (pong_winner(&game) >= 0)
    {
        state = OVER;
        stats.matches++;
    }
}

/* Paused stays paused while a pad is missing, so a player whose pad
 * dropped out is not ambushed by the serve when it comes back. */
static void paused(void)
{
    if (players_pressed(XPAD_SELECT))
        to_title();
    else if (pad_lost() < 0 && players_pressed(XPAD_START))
        state = PLAY;
}

/* Fire plays again, except on a shared pad, where south is the right
 * player's down button: still pressing it when the match ended would
 * start the next one before anyone had seen the result. */
static uint32_t again(void)
{
    return shared() ? XPAD_START : FIRE;
}

static void over(void)
{
    if (players_pressed(XPAD_SELECT))
        to_title();
    else if (players_pressed(again()))
        start_match();
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

/*
 * Clearing the screen and drawing everything again every frame ran at
 * 28 frames a second on an emulated ST, about half the display's rate,
 * so this does what the viewer does: keeps a note of what is on the
 * screen and redraws only what changed.
 *
 * The picture is in two layers. The still one, the court and every
 * word on it, is drawn from scratch only when still() says it has
 * changed: a score, a menu choice, a message. The paddles and the ball
 * move every frame, so they are drawn with exclusive OR and taken off
 * the same way, which leaves what was under them exactly as it was.
 * Each buffer keeps its own note, because the one being drawn was last
 * drawn two frames ago.
 */

/* Something that moves. colour 0 means it is not drawn. */
typedef struct
{
    int x, y, h, colour;
} BAR;

#define BARS 3 /* the two paddles, then the ball */

static struct
{
    uint32_t still; /* still()'s answer when it was drawn, 0 for never */
    BAR bars[BARS];
} shown[2];

static const char *name(int side)
{
    return side == PONG_LEFT ? "P1" : human(side) ? "P2" : "CPU";
}

/* For the messages that blink. */
static int blink(void)
{
    return (frame & 63) < 44;
}

/*
 * Everything the still picture depends on, packed into one word, so a
 * changed word means a changed picture. Only what each screen shows
 * goes in: the title's rally has a score too, but nothing draws it.
 */
static uint32_t still(void)
{
    uint32_t key = 0x80000000UL | (uint32_t)state; /* never 0 */

    if (state == TITLE)
    {
        key |= (uint32_t)menu << 4;
        key |= (uint32_t)xpad_connected(xpad) << 6;
        key |= (uint32_t)((xpad->caps & XPAD_CAP_RUMBLE) != 0) << 9;
    }
    else if (state == JOIN)
    {
        key |= (uint32_t)blink() << 4;
    }
    else
    {
        key |= (uint32_t)game.score[PONG_LEFT] << 4;
        key |= (uint32_t)game.score[PONG_RIGHT] << 9;
        key |= (uint32_t)human(PONG_RIGHT) << 14;
        key |= (uint32_t)(pad_lost() + 1) << 15;
        key |= (uint32_t)(state == OVER && blink()) << 17;
        key |= (uint32_t)shared() << 18;
    }

    return key;
}

/* The walls, and the net everywhere but the title, where it would run
 * straight through the menu. */
static void draw_court(void)
{
    int y;

    draw_wall(PONG_TOP - 2);
    draw_wall(PONG_BOTTOM);

    if (state == TITLE)
        return;

    for (y = PONG_TOP + 2; y < PONG_BOTTOM - 4; y += 8)
        draw_bar(159, y, 4, GREY, 0);
}

static void draw_title(void)
{
    char status[24];
    int n = xpad_connected(xpad);
    int i;

    draw_tiny(centre("NEILRACKETT.COM", 4), 6, "NEILRACKETT.COM", WHITE);
    draw_big(centre("XPONG", 16), 40, "XPONG", -1);

    /* Left aligned, as a list, with the longest item centred. */
    for (i = 0; i < MENU_ITEMS; i++)
        draw_text(centre(menu_items[VS_SHARED], 8), 88 + 14 * i,
                  menu_items[i], menu == i ? YELLOW : GREY);

    draw_text(centre("PRESS FIRE", 8), 140, "PRESS FIRE", WHITE);

    /* What the provider has, so a second player can see whether their
     * pad has arrived before choosing two players. */
    if (n == 0)
        strcpy(status, "NO PADS");
    else
        sprintf(status, "%d PAD%s", n, n == 1 ? "" : "S");
    if (xpad->caps & XPAD_CAP_RUMBLE)
        strcat(status, "  RUMBLE");

    draw_text(centre(status, 8), 176, status, WHITE);
}

static void draw_join(void)
{
    draw_big(centre("PLAYER 2", 16), 80, "PLAYER 2", CYAN);
    if (blink())
        draw_text(centre("PRESS FIRE", 8), 112, "PRESS FIRE", WHITE);
}

static void draw_match(void)
{
    char s[24];
    int side;

    /* Scores either side of the net, the left one right aligned. */
    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
    {
        int x = side == PONG_LEFT ? 144 : 176;

        sprintf(s, "%d", game.score[side]);
        draw_big(side == PONG_LEFT ? x - 16 * (int)strlen(s) : x, 24, s,
                 side_colour[side]);

        draw_text(side == PONG_LEFT ? x - 8 * (int)strlen(name(side)) : x,
                  42, name(side), side_colour[side]);
    }

    if (state == PAUSED)
    {
        side = pad_lost();

        if (side >= 0)
        {
            if (shared())
                strcpy(s, "PAD LOST");
            else
                sprintf(s, "%s PAD LOST", name(side));
            draw_big(centre(s, 16), 88, s, RED);
        }
        else
        {
            draw_big(centre("PAUSED", 16), 88, "PAUSED", WHITE);
        }
    }
    else if (state == OVER)
    {
        side = pong_winner(&game);

        sprintf(s, "%s WINS", name(side));
        draw_big(centre(s, 16), 88, s, side_colour[side]);

        if (blink())
        {
            strcpy(s, shared() ? "START TO PLAY AGAIN" : "FIRE TO PLAY AGAIN");
            draw_text(centre(s, 8), 120, s, WHITE);
        }
    }
}

/* Where the paddles and the ball should be this frame, and in what
 * colour: grey for the title's rally, the players' own colours in a
 * match, and nothing at all for what a screen does not show. */
static void place_bars(BAR *bars)
{
    int match = state == PLAY || state == PAUSED || state == OVER;
    int side, x, y;

    memset(bars, 0, BARS * sizeof(*bars));

    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
    {
        bars[side].x = side == PONG_LEFT ? PONG_LEFT_X : PONG_RIGHT_X;
        bars[side].y = game.y[side] >> PONG_FP;
        bars[side].h = PONG_PADDLE_H;

        if (state == TITLE)
            bars[side].colour = GREY;
        else if (match || side == PONG_LEFT)
            bars[side].colour = side_colour[side];
    }

    /* The ball blinks while it waits to be served, is gone once the
     * match is won, and is not drawn once it is off the court. */
    x = game.bx >> PONG_FP;
    y = game.by >> PONG_FP;

    if ((state == TITLE || state == PLAY || state == PAUSED) &&
        !(game.serve && (frame & 4)) && x >= 0 && x <= 316)
    {
        bars[2].x = x;
        bars[2].y = y;
        bars[2].h = PONG_BALL;
        bars[2].colour = state == TITLE ? GREY : WHITE;
    }
}

static void draw(void)
{
    BAR want[BARS];
    uint32_t now = still();
    int i;

    place_bars(want);

    /* A new still picture means a clear screen, with nothing moving on
     * it yet. */
    if (shown[drawing].still != now)
    {
        memset(back, 0, SCREEN_BYTES);
        draw_court();

        if (state == TITLE)
            draw_title();
        else if (state == JOIN)
            draw_join();
        else
            draw_match();

        shown[drawing].still = now;
        memset(shown[drawing].bars, 0, sizeof(shown[drawing].bars));
    }

    for (i = 0; i < BARS; i++)
    {
        BAR *was = &shown[drawing].bars[i];

        if (memcmp(was, &want[i], sizeof(*was)) == 0)
            continue;

        if (was->colour)
            draw_bar(was->x, was->y, was->h, was->colour, 1);
        if (want[i].colour)
            draw_bar(want[i].x, want[i].y, want[i].h, want[i].colour, 1);

        *was = want[i];
    }

    flip();
}

/* ------------------------------------------------------------------ */
/* Demo provider                                                       */
/* ------------------------------------------------------------------ */

/*
 * Two pads playing a scripted session, so the game can be watched,
 * developed and tested with no hardware and nothing else installed.
 *
 * Pad 0 is an analogue pad and pad 1 a digital one, so both ways of
 * moving a paddle are driven, and the request area keeps count of
 * every pulse the game asks for, which is how the verdict knows each
 * pad was rumbled and that every motor was stopped again.
 *
 * The demo seeds the rules with a constant, so the session is the same
 * every time it runs.
 */

#define DEMO_SEED 1
#define DEMO_MATCH_FRAMES 30000 /* longest a match may take: ten minutes */

#define DEMO_UNPLUG 0x01 /* pad 1 disconnected                          */
#define DEMO_MARK 0x02   /* note the paddles as this step begins         */
#define DEMO_CHASE 0x04  /* both players go after the ball, as people would */
#define DEMO_SHARED 0x08 /* pad 0 is both players, a stick each          */

#define DEMO_MARKS 2 /* how many steps are marked */

static XPAD demo;
static XPAD_REQ demo_req;

typedef struct
{
    uint16_t frames;  /* how long it lasts, or 0 for until a match is won */
    uint32_t held[2]; /* the buttons each pad holds                       */
    int8_t ly, ry;    /* pad 0's sticks                                   */
    uint8_t flags;    /* DEMO_*                                           */
} DEMO_STEP;

#define S DEMO_SHARED

static const DEMO_STEP script[] = {
    {25, {0, 0}, 0, 0, 0},                       /* the menu             */
    {4, {XPAD_DOWN, 0}, 0, 0, 0},                /* two players          */
    {4, {0, 0}, 0, 0, 0},
    {4, {XPAD_SOUTH, 0}, 0, 0, 0},
    {4, {0, 0}, 0, 0, 0},
    {4, {0, XPAD_SOUTH}, 0, 0, 0},               /* pad 1 joins          */
    {300, {0, XPAD_DOWN}, -127, 0, 0},           /* stick up, d-pad down */
    {4, {XPAD_START, 0}, 0, 0, DEMO_MARK},       /* pause                */
    {30, {0, 0}, 0, 0, 0},
    {4, {XPAD_START, 0}, 0, 0, 0},               /* and on again         */
    {300, {0, 0}, 0, 0, DEMO_CHASE},             /* a rally              */
    {40, {0, 0}, 0, 0, DEMO_UNPLUG},             /* pad 1 goes: a pause  */
    {10, {0, 0}, 0, 0, 0},                       /* it comes back        */
    {4, {0, XPAD_START}, 0, 0, 0},               /* and its player goes on */
    {100, {0, 0}, 0, 0, DEMO_CHASE},
    {4, {XPAD_SELECT, 0}, 0, 0, 0},              /* back to the menu     */
    {10, {0, 0}, 0, 0, 0},
    {4, {XPAD_UP, 0}, 0, 0, 0},                  /* one player           */
    {4, {0, 0}, 0, 0, 0},
    {4, {XPAD_SOUTH, 0}, 0, 0, 0},
    {0, {0, 0}, 0, 0, 0},                        /* until the computer wins */
    {50, {0, 0}, 0, 0, 0},
    {4, {XPAD_SELECT, 0}, 0, 0, 0},              /* to the menu          */
    {10, {0, 0}, 0, 0, 0},
    {4, {XPAD_DOWN, 0}, 0, 0, 0},                /* down twice, to       */
    {4, {0, 0}, 0, 0, 0},
    {4, {XPAD_DOWN, 0}, 0, 0, 0},
    {4, {0, 0}, 0, 0, 0},
    {4, {XPAD_SOUTH, 0}, 0, 0, 0},               /* two players, one pad */
    {4, {0, 0}, 0, 0, S},
    {200, {0, 0}, -127, 127, S},                 /* left up, right down  */
    {4, {XPAD_START, 0}, 0, 0, S | DEMO_MARK},   /* pause                */
    {30, {0, 0}, 0, 0, S},
    {4, {XPAD_START, 0}, 0, 0, S},
    {300, {0, 0}, 0, 0, S | DEMO_CHASE},         /* a rally on one pad   */
    {4, {XPAD_SELECT, 0}, 0, 0, S},              /* to the menu          */
    {10, {0, 0}, 0, 0, 0},
    {4, {XPAD_SELECT, 0}, 0, 0, 0},              /* and out              */
    {50, {0, 0}, 0, 0, 0},
};

#undef S

#define SCRIPT_STEPS (sizeof(script) / sizeof(script[0]))

static struct
{
    unsigned step;
    uint16_t frames;       /* into this step                          */
    uint8_t seq;           /* the request area's, when last looked    */
    uint8_t on[2][2];      /* which motors of which pads are turning  */
    unsigned pulses[2][2]; /* how many times each was started         */
    unsigned shared[2];    /* pad 0's, while it was both players      */
    int16_t mark[DEMO_MARKS][2]; /* the paddles, at each DEMO_MARK    */
    unsigned marks;
    int ran_out;           /* the script ended and the game had not   */
} run;

/*
 * Whether any XPAD cookie is in the jar, valid or not. Supervisor only.
 *
 * Publishing repoints a cookie that is already there and withdrawing
 * removes it, so a demo run over a real provider would leave that
 * provider unreachable until the machine was reset. The demo is for
 * trying the game without one, so it refuses rather than borrow it.
 */
static long cookie_present(void)
{
    uint32_t *slot = xpad_jar_seek();

    return slot && slot[0];
}

static void demo_init(void)
{
    xpad_init(&demo, 2, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE | XPAD_CAP_HOTPLUG,
              DEMO_PROVIDER, &demo_req);
}

/* What a provider does with the request area, reduced to counting
 * every time a motor starts. */
static void demo_listen(void)
{
    int sharing = run.step < SCRIPT_STEPS &&
                  (script[run.step].flags & DEMO_SHARED);
    int i, motor;

    if (demo_req.seq == run.seq)
        return;

    run.seq = demo_req.seq;

    for (i = 0; i < 2; i++)
    {
        for (motor = 0; motor < 2; motor++)
        {
            int on = demo_req.rumble[i][motor] != 0;

            if (on && !run.on[i][motor])
            {
                run.pulses[i][motor]++;
                if (sharing && i == 0)
                    run.shared[motor]++;
            }

            run.on[i][motor] = (uint8_t)on;
        }
    }
}

/* How far side's paddle is from meeting the ball on its middle. Pad 0
 * plays the left in a two player match and pad 1 the right, because
 * that is the order the script chose them in. */
static int demo_chase(int side)
{
    return game.by - (((PONG_PADDLE_H - PONG_BALL) / 2) << PONG_FP) -
           game.y[side];
}

/* A chase as a stick: as far as it has to go, and no further. */
static int8_t demo_stick(int side)
{
    int want = demo_chase(side);

    return (int8_t)(want > 127 ? 127 : want < -127 ? -127 : want);
}

static void demo_frame(void)
{
    const DEMO_STEP *s;
    XPAD_PAD *pad;
    int want;

    demo_listen();

    if (run.step >= SCRIPT_STEPS)
    {
        run.ran_out = 1;
        return;
    }

    s = &script[run.step];

    if (run.frames == 0 && (s->flags & DEMO_MARK) && run.marks < DEMO_MARKS)
    {
        run.mark[run.marks][PONG_LEFT] = game.y[PONG_LEFT];
        run.mark[run.marks][PONG_RIGHT] = game.y[PONG_RIGHT];
        run.marks++;
    }

    pad = xpad_back(&demo);
    memset(pad, 0, 2 * sizeof(*pad));

    pad[0].type = XPAD_TYPE_XBOX;
    pad[0].flags = XPAD_PAD_ANALOG;
    pad[0].buttons = s->held[0];
    pad[0].ly = s->ly;
    pad[0].ry = s->ry;

    if (!(s->flags & DEMO_UNPLUG))
    {
        pad[1].type = XPAD_TYPE_GAMEPAD;
        pad[1].buttons = s->held[1];
    }

    /* The left player by pad 0's left stick. The right player by pad 0's
     * right stick when they share it, or else by pad 1's d-pad, which
     * moves further than a stick's deadzone, so it stops short rather
     * than shaking either side of the ball. */
    if (s->flags & DEMO_CHASE)
    {
        pad[0].ly = demo_stick(PONG_LEFT);

        if (s->flags & DEMO_SHARED)
        {
            pad[0].ry = demo_stick(PONG_RIGHT);
        }
        else
        {
            want = demo_chase(PONG_RIGHT);
            if (want > PONG_DIGITAL_SPEED / 2)
                pad[1].buttons |= XPAD_DOWN;
            else if (want < -PONG_DIGITAL_SPEED / 2)
                pad[1].buttons |= XPAD_UP;
        }
    }

    xpad_fold_stick(&pad[0], pad[0].lx, pad[0].ly, 40);
    xpad_commit(&demo);

    run.frames++;

    if (s->frames ? run.frames >= s->frames
                  : state == OVER || run.frames >= DEMO_MATCH_FRAMES)
    {
        run.step++;
        run.frames = 0;
    }
}

static int failures;

/* 40 columns wide at most, which is all low resolution has. */
static void check(int ok, const char *what)
{
    printf("%-34s %s\r\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* The demo's verdict, printed once the screen is back. shown is how
 * many frames the display showed while the game ran. */
static void demo_verdict(uint32_t shown)
{
    const int16_t top = PONG_TOP << PONG_FP;
    const int16_t bottom = (PONG_BOTTOM - PONG_PADDLE_H) << PONG_FP;

    printf("Xpong demo\r\n\r\n");
    printf("%lu frames drawn, %lu shown\r\n", frame, (unsigned long)shown);
    printf("returns %u, points %u, matches %u\r\n", stats.hits,
           stats.points, stats.matches);
    printf("pulses, low and high: pad 0 %u %u,\r\n", run.pulses[0][0],
           run.pulses[0][1]);
    printf("  pad 1 %u %u, shared pad 0 %u %u\r\n\r\n", run.pulses[1][0],
           run.pulses[1][1], run.shared[0], run.shared[1]);

    check(!run.ran_out, "the script quit, through select");
    check(run.marks == DEMO_MARKS, "every mark was reached");
    check(run.mark[0][PONG_LEFT] == top,
          "a stick moved a paddle to the top");
    check(run.mark[0][PONG_RIGHT] == bottom,
          "a d-pad moved one to the bottom");
    check(run.mark[1][PONG_LEFT] == top && run.mark[1][PONG_RIGHT] == bottom,
          "one pad: the right stick worked");
    check(stats.pauses >= 1, "start paused a match");
    check(stats.lost >= 1, "and so did a pad going away");
    check(stats.matches >= 1, "a match was played to the end");
    check(run.pulses[0][0] + run.pulses[0][1] >= 1, "pad 0 was rumbled");
    check(run.pulses[1][0] + run.pulses[1][1] >= 1,
          "and pad 1, for its own player");
    check(run.shared[0] >= 1 && run.shared[1] >= 1,
          "one pad: each player a motor");
    check(!run.on[0][0] && !run.on[0][1] && !run.on[1][0] && !run.on[1][1],
          "and no motor was left turning");

    /* Counted in the display's own frames, so this holds at 50 or 60
     * a second. A frame that misses costs a whole one, so a game that
     * never kept up would draw half of them; this allows the odd miss,
     * for a new still picture, and no more. */
    check(frame * 10 >= shown * 9UL, "the game kept up with the display");
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static long read_frclock(void)
{
    return (long)*FRCLOCK;
}

static uint32_t now(void)
{
    return (uint32_t)Supexec(read_frclock);
}

static void game_loop(int demo_mode)
{
    menu = 0;
    game.rng = demo_mode ? DEMO_SEED : (uint16_t)now();
    to_title();

    while (state != QUIT)
    {
        if (demo_mode)
        {
            demo_frame();
            if (run.ran_out)
                break;
        }

        read_pads();

        if (escape_pressed())
        {
            if (state == TITLE)
                state = QUIT;
            else
                to_title();
        }
        else if (state == TITLE)
            title();
        else if (state == JOIN)
            join();
        else if (state == PLAY)
            play();
        else if (state == PAUSED)
            paused();
        else if (state == OVER)
            over();

        rumble_tick(&rumble);

        if (state != QUIT)
            draw();

        frame++;
    }

    /* Never walk away leaving a motor turning: a provider holds a
     * rumble until something replaces it. */
    rumble_stop_all(&rumble);

    if (demo_mode)
        demo_listen();
}

/*
 * Say why, and wait for a key: started from the desktop, a message
 * that is not waited for is gone before anyone can read it. The demo
 * does not wait, since the harness has nobody to press one, but it
 * prints the sentinel the harness is waiting for instead, which would
 * otherwise time out.
 */
static int fail(int demo_mode, const char *why)
{
    printf("%s\r\n", why);

    if (demo_mode)
    {
        printf("\r\nXPAD-DONE 1\r\n");
    }
    else
    {
        printf("\r\nPress a key.\r\n");
        fflush(stdout);
        (void)Cnecin();
    }

    return 1;
}

int main(int argc, char **argv)
{
    void *old_phys, *old_log, *block;
    uint16_t old_palette[16];
    uint32_t took;
    int old_rez, demo_mode = 0, i;

#ifdef XPAD_SELFTEST
    /*
     * Hatari's --auto takes a path and no arguments, so the harness has
     * no way to ask for the demo. This build supplies the command line
     * it would have passed and then runs the ordinary parsing below, so
     * the only thing the tested binary does differently from the
     * shipped one is where argv came from.
     */
    static char *test_args[] = {"XPONG", "-d"};

    argc = 2;
    argv = test_args;
#endif

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-d") == 0)
            demo_mode = 1;

    old_rez = Getrez();
    if (old_rez == 2)
        return fail(demo_mode, "Xpong needs a colour monitor.");

    if (demo_mode)
    {
        if (Supexec(cookie_present))
            return fail(demo_mode, "A provider is already installed. "
                                   "XPONG -d is for trying the game "
                                   "without one.");

        demo_init();
        demo_frame();

        if (!xpad_publish(&demo))
            return fail(demo_mode, "Could not install the XPAD cookie. "
                                   "Is the jar full?");
    }

    xpad = xpad_find();
    if (!xpad)
    {
        if (demo_mode)
            xpad_unpublish();
        return fail(demo_mode, "No Xpad provider found. Load a driver from "
                               "AUTO, or run XPONG -d to watch a demo.");
    }

    rumble_init(&rumble, xpad_req(xpad));

    block = (void *)Malloc(SCREEN_BYTES + 256);
    if (!block)
    {
        if (demo_mode)
            xpad_unpublish();
        return fail(demo_mode, "Not enough memory for a second screen.");
    }

    old_phys = Physbase();
    old_log = Logbase();
    for (i = 0; i < 16; i++)
        old_palette[i] = (uint16_t)Setcolor(i, -1);

    /* Video memory starts on a 256 byte boundary on an ST. */
    screen[0] = (uint8_t *)old_phys;
    screen[1] = (uint8_t *)(((unsigned long)block + 255) & ~255UL);
    memset(screen[0], 0, SCREEN_BYTES);
    memset(screen[1], 0, SCREEN_BYTES);
    drawing = 1;
    back = screen[1];

    Setscreen(screen[0], screen[0], 0);
    for (i = 0; i < 16; i++)
        (void)Setcolor(i, palette[i]);

    took = now();
    game_loop(demo_mode);
    took = now() - took;

    Setscreen(old_log, old_phys, old_rez);
    for (i = 0; i < 16; i++)
        (void)Setcolor(i, old_palette[i]);
    Mfree(block);

    if (demo_mode)
    {
        /* Leave the jar as we found it. */
        xpad_unpublish();

        demo_verdict(took);
        printf("\r\n%s\r\n", failures ? "FAILED" : "all passed");
        printf("\r\nXPAD-DONE %d\r\n", failures ? 1 : 0);
        fflush(stdout);

        return failures ? 1 : 0;
    }

    return 0;
}
