/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpong's rules, with no screen and no ST in them.
 *
 * Everything here is a decision worth getting wrong, kept free of TOS
 * so the host build tests it the way it tests the drivers' translate.h
 * and keymap.h: how a pad moves a paddle, how the computer plays, how
 * the ball bounces, who scores, and what each player feels. xpong.c
 * reads the pads, draws what this decides and sends the rumble.
 *
 * Positions and speeds are fixed point, in sixteenths of a pixel, so a
 * stick pushed a little way moves its paddle less than a pixel a frame.
 * Every value fits in 16 bits.
 */

#ifndef XPONG_PONG_H
#define XPONG_PONG_H

#include <stdint.h>

#include "../../src/xpad.h"

#define PONG_FP 4 /* sixteenths of a pixel */

/* The court, in pixels, on a 320 x 200 low resolution screen. */
#define PONG_TOP 18      /* first row inside the walls      */
#define PONG_BOTTOM 196  /* first row past them             */
#define PONG_LEFT_X 8    /* left paddle's left edge         */
#define PONG_RIGHT_X 308 /* right paddle's left edge        */
#define PONG_PADDLE_H 32 /* paddles are 4 pixels wide       */
#define PONG_BALL 4      /* the ball is a square this size  */
#define PONG_SERVE_X 158 /* where a served ball waits, in   */
                         /* the middle of the court         */
#define PONG_SERVE_Y ((PONG_TOP + PONG_BOTTOM - PONG_BALL) / 2)

#define PONG_WIN 11          /* first to this many wins          */
#define PONG_SERVE_FRAMES 50 /* a served ball waits this long    */

/* Ball speeds, sixteenths of a pixel a frame. */
#define PONG_SERVE_VX 40 /* 2.5 pixels                       */
#define PONG_VX_STEP 3   /* faster by this on every return   */
#define PONG_VX_MAX 80   /* 5 pixels, which still cannot     */
                         /* step over a paddle in one frame  */

/* Paddle speeds, sixteenths of a pixel a frame. */
#define PONG_DIGITAL_SPEED 80 /* 5 pixels: a d-pad or joystick    */
#define PONG_ANALOG_SPEED 96  /* 6 pixels: a stick pushed fully   */

/*
 * The computer. It moves at 1.75 pixels a frame, watches the ball only
 * for the last 160 pixels of its way, about half the court, and means
 * to take every ball within 14 pixels of its paddle's middle, which is
 * short of the very end, so its sharpest returns are not the sharpest
 * there are.
 *
 * Tuned in test/xpong.c, against itself and against players modelled
 * there: a perfect one who never misses never concedes, so it has no
 * unreturnable shot, and one who plays the angles usually wins. One
 * more like a person, who sees the ball a fifth of a second late and
 * aims roughly, wins about one match in five. The aim is what makes it
 * hard to beat, more than its speed: aiming for the very end, it beat
 * that person by 10 points to 6, whether it moved at 1.75 pixels or 2.
 */
#define PONG_CPU_SPEED 28  /* sixteenths of a pixel a frame     */
#define PONG_CPU_SIGHT 160 /* pixels from its paddle            */
#define PONG_CPU_AIM 14    /* pixels from its paddle's middle   */

/*
 * How far a stick must move before its paddle does, of 127. One axis,
 * so a plain threshold is right here: the corner problem the radial
 * deadzone in xpad_fold_stick() solves needs two.
 */
#define PONG_DEADZONE 24

#define PONG_LEFT 0
#define PONG_RIGHT 1

/* What a frame did, as pong_step() reports it. */
#define PONG_HIT(side) (0x01 << (side))   /* side returned the ball */
#define PONG_POINT(side) (0x04 << (side)) /* side scored            */

typedef struct
{
    int16_t y[2];     /* paddle tops, by PONG_LEFT and PONG_RIGHT    */
    int16_t bx, by;   /* the ball's top left corner                  */
    int16_t vx, vy;   /* its velocity                                */
    int16_t aim;      /* where on its paddle the computer means to   */
                      /* take the ball that is heading its way       */
    uint8_t score[2];
    uint8_t serve;    /* frames before a served ball moves           */
    uint16_t rng;
} PONG;

/* The rules are random in two places, the serve and the computer's
 * aim, and a 16 bit generator is plenty for both. Seeded by the caller,
 * so a test or a demo can replay a match exactly. */
static int pong_random(PONG *g, int n)
{
    g->rng = (uint16_t)(g->rng * 25173u + 13849u);

    return (int)((g->rng >> 4) % (unsigned)n);
}

/* A new aim for the computer, anywhere up to PONG_CPU_AIM either side
 * of its paddle's middle, so it returns at an angle as well as flat. */
static void pong_aim(PONG *g)
{
    g->aim = (int16_t)((pong_random(g, 2 * PONG_CPU_AIM + 1) << PONG_FP) -
                       (PONG_CPU_AIM << PONG_FP));
}

/* Put the ball in the middle, heading for side, after a pause. Up or
 * down is the only thing left to chance. */
static void pong_serve(PONG *g, int side)
{
    g->bx = PONG_SERVE_X << PONG_FP;
    g->by = PONG_SERVE_Y << PONG_FP;
    g->vx = side == PONG_LEFT ? -PONG_SERVE_VX : PONG_SERVE_VX;
    g->vy = pong_random(g, 2) ? 16 : -16;
    g->serve = PONG_SERVE_FRAMES;

    pong_aim(g);
}

static void pong_reset(PONG *g, uint16_t seed)
{
    g->rng = seed;
    g->score[PONG_LEFT] = 0;
    g->score[PONG_RIGHT] = 0;
    g->y[PONG_LEFT] = ((PONG_TOP + PONG_BOTTOM - PONG_PADDLE_H) / 2)
                      << PONG_FP;
    g->y[PONG_RIGHT] = g->y[PONG_LEFT];

    pong_serve(g, pong_random(g, 2));
}

/* The side that has won, or -1 while the match is still on. */
static int pong_winner(const PONG *g)
{
    if (g->score[PONG_LEFT] >= PONG_WIN)
        return PONG_LEFT;

    if (g->score[PONG_RIGHT] >= PONG_WIN)
        return PONG_RIGHT;

    return -1;
}

/*
 * How far a pad moves its paddle this frame, played with one hand.
 *
 * hand is PONG_LEFT for the left stick and the d-pad, which is how a
 * player with a pad of their own plays, or PONG_RIGHT for the right
 * stick and the north and south face buttons, which is the second
 * player's half of a shared pad.
 *
 * A pad that reports analogue axes moves in proportion to how far the
 * stick is pushed, which is the point of reading Xpad rather than a
 * joystick. Everything else uses the buttons, and so does an analogue
 * pad whose stick is at rest, so a pad with both works either way. The
 * flag is checked rather than assumed: a provider may not know yet
 * what kind of pad it has, and COMpad reports a plain gamepad until
 * the adapter has described it. The left stick still works then,
 * because providers fold it into the d-pad bits, and the right hand
 * still has its buttons.
 */
static int pong_pad_speed(const XPAD_PAD *p, int hand)
{
    int8_t stick = hand == PONG_LEFT ? p->ly : p->ry;
    uint32_t up = hand == PONG_LEFT ? XPAD_UP : XPAD_NORTH;
    uint32_t down = hand == PONG_LEFT ? XPAD_DOWN : XPAD_SOUTH;

    if ((p->flags & XPAD_PAD_ANALOG) &&
        (stick > PONG_DEADZONE || stick < -PONG_DEADZONE))
        return stick * PONG_ANALOG_SPEED / 127;

    if (p->buttons & up)
        return -PONG_DIGITAL_SPEED;

    if (p->buttons & down)
        return PONG_DIGITAL_SPEED;

    return 0;
}

/*
 * How far the computer moves its paddle this frame, playing side.
 *
 * It follows the ball once the ball is coming its way and within
 * PONG_CPU_SIGHT, and waits in the middle otherwise. It is beaten by
 * speed rather than by chance: a fast ball arriving far from the
 * middle gets there before it can, and so does one coming back off
 * the end of a paddle, steeper than PONG_CPU_SPEED can follow.
 */
static int pong_cpu(const PONG *g, int side)
{
    int coming = side == PONG_LEFT ? g->vx < 0 : g->vx > 0;
    int away = side == PONG_LEFT ? g->bx - (PONG_LEFT_X << PONG_FP)
                                 : (PONG_RIGHT_X << PONG_FP) - g->bx;
    int want;

    if (g->serve || !coming || away > PONG_CPU_SIGHT << PONG_FP)
        want = ((PONG_TOP + PONG_BOTTOM - PONG_PADDLE_H) / 2) << PONG_FP;
    else
        want = g->by - (((PONG_PADDLE_H - PONG_BALL) / 2) << PONG_FP) +
               g->aim;

    want -= g->y[side];

    if (want > PONG_CPU_SPEED)
        return PONG_CPU_SPEED;

    if (want < -PONG_CPU_SPEED)
        return -PONG_CPU_SPEED;

    return want;
}

/* The ball met side's paddle: send it back faster, and steeper the
 * further from the paddle's middle it struck. */
static void pong_return(PONG *g, int side)
{
    int off = (g->by + ((PONG_BALL / 2) << PONG_FP)) -
              (g->y[side] + ((PONG_PADDLE_H / 2) << PONG_FP));
    int speed = (g->vx < 0 ? -g->vx : g->vx) + PONG_VX_STEP;

    if (speed > PONG_VX_MAX)
        speed = PONG_VX_MAX;

    g->vx = (int16_t)(side == PONG_LEFT ? speed : -speed);
    g->vy = (int16_t)(off * 3 / 16);

    pong_aim(g); /* the other side's computer, if it has one */
}

/* Whether the ball overlaps side's paddle vertically. */
static int pong_level(const PONG *g, int side)
{
    return g->by + (PONG_BALL << PONG_FP) > g->y[side] &&
           g->by < g->y[side] + (PONG_PADDLE_H << PONG_FP);
}

/*
 * One frame. move is how far each paddle wants to go, from
 * pong_pad_speed() or pong_cpu(). Returns PONG_HIT and PONG_POINT bits
 * for whatever happened, which is what the caller rumbles on.
 */
static int pong_step(PONG *g, int move_left, int move_right)
{
    const int16_t top = PONG_TOP << PONG_FP;
    const int16_t bottom = (PONG_BOTTOM - PONG_PADDLE_H) << PONG_FP;
    int move[2];
    int side;

    move[PONG_LEFT] = move_left;
    move[PONG_RIGHT] = move_right;

    for (side = PONG_LEFT; side <= PONG_RIGHT; side++)
    {
        int y = g->y[side] + move[side];

        g->y[side] = (int16_t)(y < top ? top : y > bottom ? bottom : y);
    }

    if (pong_winner(g) >= 0)
        return 0;

    if (g->serve)
    {
        g->serve--;
        return 0;
    }

    g->bx = (int16_t)(g->bx + g->vx);
    g->by = (int16_t)(g->by + g->vy);

    if (g->by < PONG_TOP << PONG_FP)
    {
        g->by = PONG_TOP << PONG_FP;
        g->vy = (int16_t)-g->vy;
    }

    if (g->by > (PONG_BOTTOM - PONG_BALL) << PONG_FP)
    {
        g->by = (PONG_BOTTOM - PONG_BALL) << PONG_FP;
        g->vy = (int16_t)-g->vy;
    }

    /* Each paddle is only tested while the ball is heading for it, and
     * the window is wider than the fastest ball moves in a frame, so a
     * return can neither be missed nor taken twice. */
    if (g->vx < 0 && g->bx <= (PONG_LEFT_X + 4) << PONG_FP &&
        g->bx >= (PONG_LEFT_X - 2) << PONG_FP && pong_level(g, PONG_LEFT))
    {
        g->bx = (PONG_LEFT_X + 4) << PONG_FP;
        pong_return(g, PONG_LEFT);
        return PONG_HIT(PONG_LEFT);
    }

    if (g->vx > 0 && g->bx + (PONG_BALL << PONG_FP) >= PONG_RIGHT_X << PONG_FP &&
        g->bx <= (PONG_RIGHT_X + 2) << PONG_FP && pong_level(g, PONG_RIGHT))
    {
        g->bx = (PONG_RIGHT_X - PONG_BALL) << PONG_FP;
        pong_return(g, PONG_RIGHT);
        return PONG_HIT(PONG_RIGHT);
    }

    /* Off the screen: a point to the other side, and the next serve
     * goes to whoever lost it. */
    if (g->bx < -(16 << PONG_FP))
        side = PONG_RIGHT;
    else if (g->bx > 336 << PONG_FP)
        side = PONG_LEFT;
    else
        return 0;

    g->score[side]++;

    if (pong_winner(g) < 0)
        pong_serve(g, !side);

    return PONG_POINT(side);
}

/*
 * Rumble: strengths of 0 to 255, and lengths in frames. A return is a
 * tap on the high frequency motor, a lost point a thump on the low
 * frequency one, and a lost match both, for longer.
 */
#define PONG_TAP 140
#define PONG_TAP_FRAMES 5
#define PONG_THUMP 200
#define PONG_THUMP_FRAMES 15
#define PONG_LOSS 255
#define PONG_LOSS_FRAMES 40

typedef struct
{
    uint8_t lo, hi; /* each motor's strength, 0 to leave it alone */
    uint8_t frames; /* how long, and 0 when there is nothing      */
} PONG_FEEL;

/*
 * What side's player should feel after a frame that returned events
 * from pong_step(): the return they made, or the point or the match
 * they lost. Nobody feels the other side's events.
 *
 * shared is a match on one pad, a player in each grip, and then each
 * feels only their own hand: the left player on the low frequency
 * motor, which on every pad with two is the heavy weight in the left
 * grip, and the right player on the high frequency one in the right.
 */
static PONG_FEEL pong_feel(const PONG *g, int events, int side, int shared)
{
    PONG_FEEL f = {0, 0, 0};
    uint8_t most;

    if (events & PONG_HIT(side))
    {
        f.hi = PONG_TAP;
        f.frames = PONG_TAP_FRAMES;
    }
    else if (events & PONG_POINT(!side))
    {
        if (pong_winner(g) == !side)
        {
            f.lo = f.hi = PONG_LOSS;
            f.frames = PONG_LOSS_FRAMES;
        }
        else
        {
            f.lo = PONG_THUMP;
            f.frames = PONG_THUMP_FRAMES;
        }
    }

    if (shared && f.frames)
    {
        most = f.lo > f.hi ? f.lo : f.hi;
        f.lo = side == PONG_LEFT ? most : 0;
        f.hi = side == PONG_RIGHT ? most : 0;
    }

    return f;
}

#endif /* XPONG_PONG_H */
