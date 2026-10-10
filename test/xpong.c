/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpong's rules and its rumble, on the host.
 *
 * The game itself draws to an ST screen, and hatari-xpong plays it
 * there. Everything it decides is in pong.h and rumble.h, free of TOS,
 * so it is tested here instead: how a pad moves a paddle, the ball,
 * the score, whether the computer can be beaten, who feels what, and
 * that every motor the game starts is stopped again.
 *
 * Host only, like the driver tests: the decisions have no TOS in them.
 */

#include <stdio.h>
#include <string.h>

#include "../examples/xpong/pong.h"
#include "../examples/xpong/rumble.h"

static int failures;

static void check(int ok, const char *what)
{
    printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

#define TOP (PONG_TOP << PONG_FP)
#define BOTTOM ((PONG_BOTTOM - PONG_PADDLE_H) << PONG_FP)
#define MIDDLE (((PONG_TOP + PONG_BOTTOM - PONG_PADDLE_H) / 2) << PONG_FP)

static XPAD_PAD pad(uint8_t flags, uint32_t buttons, int8_t ly, int8_t ry)
{
    XPAD_PAD p;

    memset(&p, 0, sizeof(p));
    p.type = flags ? XPAD_TYPE_XBOX : XPAD_TYPE_GAMEPAD;
    p.flags = flags;
    p.buttons = buttons;
    p.ly = ly;
    p.ry = ry;

    return p;
}

/* The usual way to play: the left stick and the d-pad. */
static int speed(uint8_t flags, uint32_t buttons, int8_t ly)
{
    XPAD_PAD p = pad(flags, buttons, ly, 0);

    return pong_pad_speed(&p, PONG_LEFT);
}

/* The second player's half of a shared pad. */
static int right(uint8_t flags, uint32_t buttons, int8_t ly, int8_t ry)
{
    XPAD_PAD p = pad(flags, buttons, ly, ry);

    return pong_pad_speed(&p, PONG_RIGHT);
}

static void paddles(void)
{
    printf("moving a paddle\n\n");

    check(speed(0, XPAD_UP, 0) == -PONG_DIGITAL_SPEED, "up on a d-pad moves up");
    check(speed(0, XPAD_DOWN, 0) == PONG_DIGITAL_SPEED, "and down moves down");
    check(speed(0, 0, 0) == 0, "and nothing stays put");
    check(speed(0, XPAD_SOUTH, 0) == 0, "and a button is not a direction");

    /* A pad that has not said it is analogue is read by its d-pad bits,
     * even with numbers in its stick fields: COMpad reports a plain
     * gamepad until the adapter describes it. */
    check(speed(0, 0, 127) == 0, "a stick is ignored without XPAD_PAD_ANALOG");
    check(speed(0, XPAD_DOWN, 127) == PONG_DIGITAL_SPEED,
          "where the folded d-pad bits still work");

    check(speed(XPAD_PAD_ANALOG, 0, 127) == PONG_ANALOG_SPEED,
          "an analogue stick pushed fully down");
    check(speed(XPAD_PAD_ANALOG, 0, -127) == -PONG_ANALOG_SPEED,
          "and fully up");
    check(speed(XPAD_PAD_ANALOG, 0, 64) == PONG_ANALOG_SPEED / 2,
          "and halfway goes half as fast");
    check(speed(XPAD_PAD_ANALOG, 0, PONG_DEADZONE) == 0,
          "and the edge of the deadzone does not move it");
    check(speed(XPAD_PAD_ANALOG, 0, PONG_DEADZONE + 1) > 0,
          "but just past it does");
    check(speed(XPAD_PAD_ANALOG, XPAD_UP, 0) == -PONG_DIGITAL_SPEED,
          "an analogue pad's d-pad works with the stick at rest");
    check(speed(XPAD_PAD_ANALOG, XPAD_UP, 100) > 0,
          "and the stick wins when it is pushed");
    check(PONG_ANALOG_SPEED > PONG_DIGITAL_SPEED,
          "a stick pushed fully beats a d-pad");

    /* Sharing a pad: the right hand has the right stick, and without
     * analogue the two face buttons that sit one above the other. */
    check(right(XPAD_PAD_ANALOG, 0, 0, 127) == PONG_ANALOG_SPEED,
          "the right hand uses the right stick");
    check(right(XPAD_PAD_ANALOG, 0, -127, 0) == 0,
          "and not the left one");
    check(right(XPAD_PAD_ANALOG, XPAD_DOWN, 0, 0) == 0,
          "nor the d-pad, which is the left hand's");
    check(right(0, XPAD_NORTH, 0, 0) == -PONG_DIGITAL_SPEED,
          "north moves it up");
    check(right(0, XPAD_SOUTH, 0, 0) == PONG_DIGITAL_SPEED,
          "and south down");
    check(right(0, 0, 0, 127) == 0,
          "and its stick is ignored without XPAD_PAD_ANALOG");
    check(speed(XPAD_PAD_ANALOG, XPAD_SOUTH, 0) == 0 &&
              pong_pad_speed(&(XPAD_PAD){.flags = XPAD_PAD_ANALOG, .ry = 127},
                             PONG_LEFT) == 0,
          "while the left hand ignores both");
}

/* A match with the ball on its way to side, already served. */
static void rally(PONG *g, int side)
{
    pong_reset(g, 1);
    pong_serve(g, side);
    g->serve = 0;
}

/* Frames until something happens, or 0 after max of nothing. */
static int until_event(PONG *g, int left, int right, int max, int *events)
{
    int i;

    for (i = 1; i <= max; i++)
    {
        *events = pong_step(g, left, right);
        if (*events)
            return i;
    }

    return 0;
}

static void ball(void)
{
    PONG g;
    int i, events, bx, ok;

    printf("\nthe ball\n\n");

    pong_reset(&g, 7);
    check(g.bx == PONG_SERVE_X << PONG_FP && g.by == PONG_SERVE_Y << PONG_FP,
          "a serve starts in the middle of the court");
    check(g.by + ((PONG_BALL / 2) << PONG_FP) == MIDDLE +
                                                   ((PONG_PADDLE_H / 2)
                                                    << PONG_FP),
          "level with the middle of a paddle at rest");
    check(g.vy == 16 || g.vy == -16, "heading up or down");

    pong_reset(&g, 7);
    bx = g.bx;
    for (i = 0; i < PONG_SERVE_FRAMES; i++)
        pong_step(&g, 0, 0);
    check(g.bx == bx && g.serve == 0, "a served ball waits before it moves");
    pong_step(&g, 0, 0);
    check(g.bx != bx, "and then it does");

    pong_reset(&g, 7);
    for (i = 0; i < 100; i++)
        pong_step(&g, -PONG_ANALOG_SPEED, PONG_ANALOG_SPEED);
    check(g.y[PONG_LEFT] == TOP && g.y[PONG_RIGHT] == BOTTOM,
          "paddles stop at the walls");

    /* Straight at the left paddle's middle. */
    rally(&g, PONG_LEFT);
    g.by = g.y[PONG_LEFT] + (((PONG_PADDLE_H - PONG_BALL) / 2) << PONG_FP);
    g.vy = 0;
    until_event(&g, 0, 0, 500, &events);
    check(events == PONG_HIT(PONG_LEFT), "a ball reaching a paddle is returned");
    check(g.vx == PONG_SERVE_VX + PONG_VX_STEP, "faster, and the other way");
    check(g.vy == 0, "flat, off the paddle's middle");

    /* Off the very end of the right paddle. */
    rally(&g, PONG_RIGHT);
    g.by = g.y[PONG_RIGHT] + ((PONG_PADDLE_H - 1) << PONG_FP);
    g.vy = 0;
    until_event(&g, 0, 0, 500, &events);
    check(events == PONG_HIT(PONG_RIGHT) && g.vx < 0,
          "the end of a paddle returns it too");
    check(g.vy > PONG_CPU_SPEED, "steep enough to outrun the computer");

    rally(&g, PONG_LEFT);
    g.vx = -PONG_VX_MAX + 1;
    g.by = g.y[PONG_LEFT];
    g.vy = 0;
    until_event(&g, 0, 0, 500, &events);
    check(g.vx == PONG_VX_MAX, "no faster than the maximum");

    /* The fastest ball, from every starting offset in a frame's travel,
     * must still meet the paddle rather than step over it. */
    ok = 1;
    for (i = 0; i < PONG_VX_MAX; i++)
    {
        rally(&g, PONG_LEFT);
        g.bx = (int16_t)((100 << PONG_FP) + i);
        g.vx = -PONG_VX_MAX;
        g.by = g.y[PONG_LEFT];
        g.vy = 0;
        until_event(&g, 0, 0, 500, &events);
        ok &= events == PONG_HIT(PONG_LEFT);
    }
    check(ok, "the fastest ball cannot step over a paddle");

    rally(&g, PONG_LEFT);
    g.by = (PONG_TOP + 20) << PONG_FP;
    g.vy = -40;
    for (i = 0; i < 200 && g.vy < 0; i++)
        pong_step(&g, 0, 0);
    check(g.vy == 40 && g.by >= TOP, "the top wall turns it back");

    rally(&g, PONG_LEFT);
    g.y[PONG_LEFT] = TOP;
    g.by = (PONG_BOTTOM - PONG_BALL) << PONG_FP;
    g.vy = 0;
    until_event(&g, -PONG_DIGITAL_SPEED, 0, 500, &events);
    check(events == PONG_POINT(PONG_RIGHT), "a miss is the other side's point");
    check(g.score[PONG_RIGHT] == 1 && g.score[PONG_LEFT] == 0,
          "and it is counted");
    check(g.vx < 0 && g.serve == PONG_SERVE_FRAMES,
          "and the next serve goes to whoever missed");

    rally(&g, PONG_RIGHT);
    g.score[PONG_LEFT] = PONG_WIN - 1;
    g.y[PONG_RIGHT] = TOP;
    g.by = (PONG_BOTTOM - PONG_BALL) << PONG_FP;
    g.vy = 0;
    until_event(&g, 0, -PONG_DIGITAL_SPEED, 500, &events);
    check(events == PONG_POINT(PONG_LEFT) && pong_winner(&g) == PONG_LEFT,
          "the last point wins the match");
    check(until_event(&g, 0, 0, 500, &events) == 0,
          "and nothing happens after it");
    check(pong_step(&g, PONG_DIGITAL_SPEED, 0) == 0 && g.y[PONG_LEFT] > TOP,
          "though the paddles still move");
}

/*
 * A perfect player, at d-pad speed, so anyone with a joystick could
 * play this way. Taking every ball on the middle of the paddle, it
 * never misses; taking it on the end sends it back steeper than the
 * computer can follow, and sometimes misses, because the end is a
 * single row.
 */
static int perfect(const PONG *g, int side, int on)
{
    int coming = side == PONG_LEFT ? g->vx < 0 : g->vx > 0;
    int want = coming ? g->by - (on << PONG_FP) : MIDDLE;

    want -= g->y[side];

    if (want > PONG_DIGITAL_SPEED)
        return PONG_DIGITAL_SPEED;
    if (want < -PONG_DIGITAL_SPEED)
        return -PONG_DIGITAL_SPEED;
    return want;
}

#define MATCHES 50
#define LONGEST 30000 /* frames, ten minutes: a match this long never ends */

#define END (PONG_PADDLE_H - 1)                  /* the paddle's last row */
#define FLAT ((PONG_PADDLE_H - PONG_BALL) / 2)   /* its middle            */

/*
 * A player more like a person: sees the ball a fifth of a second late,
 * aims for anywhere within 10 pixels of the paddle's middle, and moves
 * with a d-pad, so only at full speed or not at all.
 */
#define PERSON_LATE 10 /* frames */
#define PERSON_SLOP 10 /* pixels */

static struct
{
    int16_t seen[PERSON_LATE]; /* where the ball was, oldest first */
    int16_t aim;
    int coming;
    uint16_t rng;
} person;

static int person_move(const PONG *g)
{
    int coming = g->vx < 0;
    int want, i;

    for (i = 0; i < PERSON_LATE - 1; i++)
        person.seen[i] = person.seen[i + 1];
    person.seen[PERSON_LATE - 1] = g->by;

    if (coming && !person.coming)
    {
        person.rng = (uint16_t)(person.rng * 25173u + 13849u);
        person.aim = (int16_t)((((person.rng >> 4) % (2 * PERSON_SLOP + 1))
                                << PONG_FP) -
                               (PERSON_SLOP << PONG_FP));
    }
    person.coming = coming;

    want = coming ? person.seen[0] - (FLAT << PONG_FP) + person.aim : MIDDLE;
    want -= g->y[PONG_LEFT];

    if (want > PONG_DIGITAL_SPEED / 2)
        return PONG_DIGITAL_SPEED;
    if (want < -PONG_DIGITAL_SPEED / 2)
        return -PONG_DIGITAL_SPEED;
    return 0;
}

typedef struct
{
    int won, lost; /* matches, of MATCHES; the rest ran out of time */
    int scored;    /* points the left side scored, in all of them   */
    int conceded;  /* and the computer                              */
} RECORD;

#define IDLE (-1)
#define PERSON (-2)

/* MATCHES against the computer, the left side taking every ball on
 * row on of its paddle, or standing still, or playing like a person. */
static RECORD against_cpu(int on)
{
    RECORD r = {0, 0, 0, 0};
    PONG g;
    int m, frames, move;

    memset(&person, 0, sizeof(person));
    person.rng = 1;

    for (m = 0; m < MATCHES; m++)
    {
        pong_reset(&g, (uint16_t)(m + 1));
        for (frames = 0; frames < LONGEST && pong_winner(&g) < 0; frames++)
        {
            move = on == IDLE     ? 0
                   : on == PERSON ? person_move(&g)
                                  : perfect(&g, PONG_LEFT, on);
            pong_step(&g, move, pong_cpu(&g, PONG_RIGHT));
        }
        r.won += pong_winner(&g) == PONG_LEFT;
        r.lost += pong_winner(&g) == PONG_RIGHT;
        r.scored += g.score[PONG_LEFT];
        r.conceded += g.score[PONG_RIGHT];
    }

    return r;
}

static void computer(void)
{
    PONG g;
    RECORD r;
    int m, frames, events, finished, longest = 0;
    long hits = 0, points = 0;

    printf("\nthe computer\n\n");

    rally(&g, PONG_LEFT);
    g.by = (PONG_BOTTOM - PONG_BALL) << PONG_FP;
    g.bx = ((PONG_LEFT_X + PONG_CPU_SIGHT) << PONG_FP) + 1;
    check(pong_cpu(&g, PONG_LEFT) == 0,
          "it ignores a ball until the ball is in sight");
    g.bx = (PONG_LEFT_X + PONG_CPU_SIGHT) << PONG_FP;
    check(pong_cpu(&g, PONG_LEFT) == PONG_CPU_SPEED,
          "and then chases it");
    g.y[PONG_LEFT] = TOP;
    check(pong_cpu(&g, PONG_RIGHT) == 0,
          "and waits in the middle for one that is not");
    g.y[PONG_RIGHT] = TOP;
    check(pong_cpu(&g, PONG_RIGHT) == PONG_CPU_SPEED,
          "going back there when it has strayed");
    g.serve = 1;
    check(pong_cpu(&g, PONG_LEFT) == PONG_CPU_SPEED &&
              g.y[PONG_LEFT] < MIDDLE,
          "and while a ball waits to be served");
    g.serve = 0;
    g.y[PONG_LEFT] = MIDDLE;
    g.by = g.y[PONG_LEFT] + (((PONG_PADDLE_H - PONG_BALL) / 2) << PONG_FP);
    g.aim = 0;
    check(pong_cpu(&g, PONG_LEFT) == 0,
          "and stays put on one it is already in front of");

    /* Against itself it has to lose points, or a match would never
     * end, and the title screen plays exactly that. */
    finished = 0;
    for (m = 0; m < MATCHES; m++)
    {
        pong_reset(&g, (uint16_t)(m + 1));
        for (frames = 0; frames < LONGEST && pong_winner(&g) < 0; frames++)
        {
            events = pong_step(&g, pong_cpu(&g, PONG_LEFT),
                               pong_cpu(&g, PONG_RIGHT));
            hits += (events & (PONG_HIT(0) | PONG_HIT(1))) != 0;
            points += (events & (PONG_POINT(0) | PONG_POINT(1))) != 0;
        }
        finished += pong_winner(&g) >= 0;
        if (frames > longest)
            longest = frames;
    }
    printf("  against itself: %ld returns a point, longest match %d frames\n",
           points ? hits / points : 0, longest);
    check(finished == MATCHES, "against itself, every match ends");
    check(hits > points, "with rallies, not a point a serve");

    r = against_cpu(IDLE);
    check(r.lost == MATCHES, "a player who does nothing loses");

    /* Fair: every ball it sends can be reached at d-pad speed, so a
     * player who never misses never concedes a point. */
    r = against_cpu(FLAT);
    check(r.conceded == 0, "it has no shot that cannot be returned");

    /* Beatable, and not a pushover: a player playing the angles, which
     * is how it loses, still takes risks doing so. */
    r = against_cpu(END);
    printf("  playing the angles: won %d, lost %d of %d\n", r.won, r.lost,
           MATCHES);
    check(r.won >= MATCHES / 5, "it can be beaten, by playing the angles");
    check(r.lost >= MATCHES / 5, "but not every time");

    /* And by people: often enough to be worth playing, not so often
     * that the computer is a pushover. */
    r = against_cpu(PERSON);
    printf("  a person: won %d, lost %d of %d, %d points to %d a match\n",
           r.won, r.lost, MATCHES, r.scored / MATCHES, r.conceded / MATCHES);
    check(r.won >= MATCHES / 10, "a person beats it sometimes");
    check(r.lost >= MATCHES / 2, "but it wins more often than not");
}

/* What one side feels after a frame with these events, packed into an
 * int as lo, hi and frames so a check can compare it in one go. */
static long felt(const PONG *g, int events, int side, int shared)
{
    PONG_FEEL f = pong_feel(g, events, side, shared);

    return ((long)f.lo << 16) | ((long)f.hi << 8) | f.frames;
}

#define FELT(lo, hi, frames) (((long)(lo) << 16) | ((long)(hi) << 8) | (frames))

static void feeling(void)
{
    PONG g;

    printf("\nwho feels what\n\n");

    pong_reset(&g, 1);

    check(felt(&g, 0, PONG_LEFT, 0) == 0, "nothing happening is felt by nobody");

    check(felt(&g, PONG_HIT(PONG_LEFT), PONG_LEFT, 0) ==
              FELT(0, PONG_TAP, PONG_TAP_FRAMES),
          "a return taps the high motor");
    check(felt(&g, PONG_HIT(PONG_LEFT), PONG_RIGHT, 0) == 0,
          "of the player who made it, only");

    g.score[PONG_RIGHT] = 1;
    check(felt(&g, PONG_POINT(PONG_RIGHT), PONG_LEFT, 0) ==
              FELT(PONG_THUMP, 0, PONG_THUMP_FRAMES),
          "a lost point thumps the low motor");
    check(felt(&g, PONG_POINT(PONG_RIGHT), PONG_RIGHT, 0) == 0,
          "and the winner of it feels nothing");

    g.score[PONG_RIGHT] = PONG_WIN;
    check(felt(&g, PONG_POINT(PONG_RIGHT), PONG_LEFT, 0) ==
              FELT(PONG_LOSS, PONG_LOSS, PONG_LOSS_FRAMES),
          "a lost match shakes both");
    check(felt(&g, PONG_POINT(PONG_RIGHT), PONG_RIGHT, 0) == 0,
          "and the winner of that feels nothing either");

    /* Sharing a pad: the left player's grip has the low motor and the
     * right player's the high, whatever the event. */
    g.score[PONG_RIGHT] = 1;
    check(felt(&g, PONG_HIT(PONG_LEFT), PONG_LEFT, 1) ==
              FELT(PONG_TAP, 0, PONG_TAP_FRAMES),
          "sharing, the left player's return is low");
    check(felt(&g, PONG_HIT(PONG_RIGHT), PONG_RIGHT, 1) ==
              FELT(0, PONG_TAP, PONG_TAP_FRAMES),
          "and the right player's high");
    check(felt(&g, PONG_POINT(PONG_LEFT), PONG_RIGHT, 1) ==
              FELT(0, PONG_THUMP, PONG_THUMP_FRAMES),
          "the right player's lost point is high");
    check(felt(&g, PONG_POINT(PONG_RIGHT), PONG_LEFT, 1) ==
              FELT(PONG_THUMP, 0, PONG_THUMP_FRAMES),
          "and the left player's low");

    g.score[PONG_LEFT] = PONG_WIN;
    check(felt(&g, PONG_POINT(PONG_LEFT), PONG_RIGHT, 1) ==
              FELT(0, PONG_LOSS, PONG_LOSS_FRAMES),
          "and a lost match, in one hand only");
    check(felt(&g, PONG_POINT(PONG_LEFT), PONG_LEFT, 1) == 0,
          "which the winner does not feel");
}

static void rumble(void)
{
    RUMBLE r;
    XPAD_REQ req;
    uint8_t seq;
    int i, ok;

    printf("\nrumble\n\n");

    rumble_init(&r, 0);
    check(!rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 200, 200, 5),
          "nothing is asked of a provider with no request area");
    rumble_tick(&r);
    rumble_stop_all(&r);
    check(1, "and ticking or stopping it is harmless");

    memset(&req, 0, sizeof(req));
    req.size = sizeof(req);
    rumble_init(&r, &req);

    check(!rumble_pulse(&r, 0, 0, 200, 200, 5) && req.seq == 0 &&
              !req.rumble[0][0],
          "nor of one without XPAD_CAP_RUMBLE");
    check(!rumble_pulse(&r, XPAD_CAP_RUMBLE, -1, 200, 200, 5) &&
              !rumble_pulse(&r, XPAD_CAP_RUMBLE, XPAD_MAX_PADS, 200, 200, 5) &&
              req.seq == 0,
          "nor for the computer's side, which has no pad");

    check(rumble_pulse(&r, XPAD_CAP_RUMBLE, 1, 10, 20, 3) &&
              req.rumble[1][0] == 10 && req.rumble[1][1] == 20 &&
              req.seq == 1,
          "a pulse sets both motors and bumps seq");

    rumble_tick(&r);
    rumble_tick(&r);
    check(req.rumble[1][0] == 10 && req.seq == 1, "it runs for its length");
    rumble_tick(&r);
    check(!req.rumble[1][0] && !req.rumble[1][1] && req.seq == 2,
          "and then stops, with seq bumped again");
    rumble_tick(&r);
    check(req.seq == 2, "after which ticking writes nothing");

    /* Two players feel their own events at once. */
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 50, 0, 2);
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 2, 0, 60, 2);
    seq = req.seq;
    rumble_tick(&r);
    rumble_tick(&r);
    check(!req.rumble[0][0] && !req.rumble[2][1] && req.seq == (uint8_t)(seq + 1),
          "two pads stop together, for one bump of seq");

    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 50, 0, 2);
    rumble_tick(&r);
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 90, 0, 2);
    rumble_tick(&r);
    check(req.rumble[0][0] == 90, "a new pulse replaces one running");
    rumble_tick(&r);
    check(!req.rumble[0][0], "and runs its own full length");

    /* Two players sharing a pad, a motor each: neither cuts the other
     * short, and each stops on its own time. */
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 70, 0, 4);
    rumble_tick(&r);
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 0, 80, 2);
    check(req.rumble[0][0] == 70 && req.rumble[0][1] == 80,
          "a pulse on one motor leaves the other");
    rumble_tick(&r);
    rumble_tick(&r);
    check(req.rumble[0][0] == 70 && !req.rumble[0][1],
          "and the two stop on their own time");
    rumble_tick(&r);
    check(!req.rumble[0][0], "each of them");

    /* The provider loses rumble while a motor is turning: the refusal
     * has to stop it, since a provider holds what was last written. */
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 3, 255, 255, 10);
    seq = req.seq;
    check(!rumble_pulse(&r, 0, 3, 100, 100, 10) && !req.rumble[3][0] &&
              !req.rumble[3][1] && req.seq == (uint8_t)(seq + 1),
          "a refusal stops a pulse already running");

    rumble_pulse(&r, XPAD_CAP_RUMBLE, 1, 255, 255, 10);
    check(!rumble_pulse(&r, XPAD_CAP_RUMBLE, 1, 255, 255, 0) &&
              !req.rumble[1][0],
          "and so does a pulse of no length");

    /* Leaving: everything this started stops, and nothing else is
     * touched, since another program may own those requests. */
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 0, 1, 2, 100);
    rumble_pulse(&r, XPAD_CAP_RUMBLE, 2, 3, 4, 100);
    req.rumble[1][0] = 77;
    seq = req.seq;
    rumble_stop_all(&r);
    ok = 1;
    for (i = 0; i < XPAD_MAX_PADS; i++)
        if (i != 1)
            ok &= !req.rumble[i][0] && !req.rumble[i][1];
    check(ok && req.seq == (uint8_t)(seq + 1), "stopping all stops every pulse");
    check(req.rumble[1][0] == 77, "and only the ones it started");

    seq = req.seq;
    rumble_stop_all(&r);
    check(req.seq == seq, "and stopping again writes nothing");
}

int main(void)
{
    printf("xpong\n\n");

    paddles();
    ball();
    computer();
    feeling();
    rumble();

    printf("\n%s\n", failures ? "FAILED" : "all passed");

    return failures ? 1 : 0;
}
