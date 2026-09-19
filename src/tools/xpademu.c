/* SPDX-License-Identifier: GPL-3.0-or-later */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * XPADEMU: any Xpad provider, as joystick 1 and the GEM mouse.
 *
 *   XPADEMU        install, stay resident, read XPADEMU.CFG if present
 *   XPADEMU -t     run the self test and exit
 *
 * EXAMPLE CONSUMER, and the one people actually run. It publishes
 * nothing and owns nothing, so it composes with every provider: a
 * Bluetooth pad through COMpad, a Mega Drive pad through MD/Sidepad,
 * an STE joypad, the keyboard shim. Whatever put the block in the
 * cookie jar, this turns it into input a 1987 game understands.
 *
 * Inspired by, and ported from, the assembly injector in MD/Sidepad:
 * https://github.com/neilrackett/md-sidepad
 * (target/atarist/src/userfw.s). The mechanism below is its work, made
 * to run on real hardware; this is the same idea reading an XPAD block
 * instead of one cartridge's private byte, written in C because
 * nothing here has to live in a read-only ROM window.
 *
 * MECHANISM
 *
 * A hook on etv_timer samples the block 50 times a second, builds a
 * real IKBD packet, and calls the system's joystick or mouse vector
 * with it. To whatever is listening, the packets are indistinguishable
 * from a stick somebody moved.
 *
 * The vectors are read live, out of KBDVECS, every single time rather
 * than saved at install. This is the whole trick and it is easy to get
 * wrong: a game or a test tool installs its own joyvec AFTER this
 * program, so a pointer saved at boot points at whatever was there
 * before the game started and the game never hears a thing. Reading
 * the slot at call time reaches whoever is listening now.
 *
 * The real vectors are left in place, so a physical joystick on port 1
 * keeps working. Whichever one is moved drives the game.
 *
 * WHAT THIS CANNOT DO
 *
 * A game that drives the IKBD ACIA at $FFFC00 itself, rather than
 * going through TOS' vectors, never sees any of this. There is no
 * software fix for that: you cannot write to a receive register. This
 * reaches games that let TOS deliver their input, which is most but
 * emphatically not all of them.
 */

#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "../xpad.h"
#include "joypkt.h"

#define PROGRAM "Xpad joystick and mouse emulation 1.0"
#define CFG_FILE "XPADEMU.CFG"

/*
 * etv_timer runs at about 200 Hz and the IKBD reports at 50, so inject
 * on every fourth tick and chain the rest. Matching the real rate
 * matters: a game that counts packets to time an autofire or a repeat
 * would run four times fast otherwise.
 */
#define ETV_DIVISOR 4

/*
 * Autofire is configured in shots per second, because that is what a
 * person means, and stored as the tick period the state machine wants.
 * Injection runs at 50 Hz, so eight shots a second is a six tick
 * cycle: three held, three released.
 */
#define INJECT_HZ 50
#define AUTOFIRE_TICKS(hz) ((uint8_t)(INJECT_HZ / (hz)))
#define AUTOFIRE_MIN_HZ 1
#define AUTOFIRE_MAX_HZ 25 /* faster than this has no off half at all */

/*
 * KBDVECS field offsets, in bytes from what Kbdvbase() returns.
 * joyvec really is at 24 and mousevec at 16; the struct has midivec,
 * vkbderr, vmiderr and statvec ahead of them.
 */
#define KBDVECS_MOUSEVEC 16
#define KBDVECS_JOYVEC 24

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

/*
 * Read once, at install, and never again. A resident handler runs in
 * interrupt context where GEMDOS is not reentrant, so a file call from
 * there would eventually hang the machine: everything the tick needs
 * is settled here and copied into plain integers.
 */
typedef struct
{
    int pad;            /* which pad drives the joystick        */
    uint32_t fire;      /* buttons that read as fire            */
    uint32_t autofire;  /* buttons that read as repeating fire  */
    uint8_t autoperiod; /* one on-and-off cycle, in ticks       */
    uint32_t jump;      /* button that reads as up, or 0        */
    int mouse;          /* stick as mouse at all                */
    int mouse_pad;      /* which pad drives the mouse           */
    uint32_t mleft;     /* buttons that read as left click      */
    uint32_t mright;    /* and as right                         */
    uint8_t deadzone;   /* below this the stick is at rest      */
    uint8_t speed;      /* mouse pixels per unit of deflection  */
} CFG;

/*
 * South is fire because it is where a thumb rests, west is autofire
 * because it is the far face button and therefore the deliberate one,
 * and the right stick drives the mouse so the left can stay on the
 * d-pad. A deadzone of 40 is what the COMpad provider folds at, so the
 * mouse goes to sleep at the same deflection the d-pad bits do.
 *
 * West is the LEFT face button, not the one with Y on an Xbox pad's
 * legend. See the X/Y trap in xpad.h.
 */
static CFG cfg = {
    0,
    XPAD_SOUTH | XPAD_EAST,
    XPAD_WEST,
    AUTOFIRE_TICKS(8),
    0,
    1,
    0,
    XPAD_THUMBR | XPAD_TR,
    XPAD_TL,
    40,
    24,
};

/* Names a person would write in a config file, mapped to the bits. */
static const struct
{
    const char *name;
    uint32_t bit;
} button_names[] = {
    {"south", XPAD_SOUTH}, {"east", XPAD_EAST},
    {"north", XPAD_NORTH}, {"west", XPAD_WEST},
    {"a", XPAD_SOUTH},     {"b", XPAD_EAST},
    {"tl", XPAD_TL},       {"tr", XPAD_TR},
    {"select", XPAD_SELECT}, {"start", XPAD_START},
    {"thumbl", XPAD_THUMBL}, {"thumbr", XPAD_THUMBR},
    {"none", 0},
};

#define BUTTON_COUNT (sizeof(button_names) / sizeof(button_names[0]))

/*
 * Deliberately by position, never by the letter printed on a pad. "a"
 * and "b" are accepted as aliases for south and east because that is
 * what somebody writing a config file will reach for, but north and
 * west have no such aliases: XPAD_X is north and XPAD_Y is west,
 * following the kernel, and accepting "x" here would hand people the
 * X/Y trap in a file they cannot see the warning in. See the
 * Conventions section of README.
 */
static uint32_t parse_buttons(const char *v)
{
    uint32_t mask = 0;

    while (*v)
    {
        unsigned i;
        const char *start = v;
        unsigned len = 0;

        while (v[len] && v[len] != '+')
            len++;

        v += len;
        if (*v == '+')
            v++;

        for (i = 0; i < BUTTON_COUNT; i++)
        {
            if (strlen(button_names[i].name) == len &&
                strncmp(button_names[i].name, start, len) == 0)
            {
                mask |= button_names[i].bit;
                break;
            }
        }
    }

    return mask;
}

static int parse_int(const char *v)
{
    int n = 0;

    while (*v >= '0' && *v <= '9')
        n = n * 10 + (*v++ - '0');

    return n;
}

static void apply(const char *key, const char *value)
{
    if (strcmp(key, "pad") == 0)
        cfg.pad = parse_int(value) & 3;
    else if (strcmp(key, "fire") == 0)
        cfg.fire = parse_buttons(value);
    else if (strcmp(key, "autofire") == 0)
        cfg.autofire = parse_buttons(value);
    else if (strcmp(key, "autorate") == 0)
    {
        int hz = parse_int(value);

        /* Clamped rather than rejected: a rate of zero would divide by
         * zero, and one above half the tick rate has no released half
         * to offer, so it would be indistinguishable from holding
         * fire down. */
        if (hz < AUTOFIRE_MIN_HZ)
            hz = AUTOFIRE_MIN_HZ;
        if (hz > AUTOFIRE_MAX_HZ)
            hz = AUTOFIRE_MAX_HZ;

        cfg.autoperiod = AUTOFIRE_TICKS(hz);
    }
    else if (strcmp(key, "jump") == 0)
        cfg.jump = parse_buttons(value);
    else if (strcmp(key, "mouse") == 0)
        cfg.mouse = strcmp(value, "off") != 0 && strcmp(value, "0") != 0;
    else if (strcmp(key, "mousepad") == 0)
        cfg.mouse_pad = parse_int(value) & 3;
    else if (strcmp(key, "left") == 0)
        cfg.mleft = parse_buttons(value);
    else if (strcmp(key, "right") == 0)
        cfg.mright = parse_buttons(value);
    else if (strcmp(key, "deadzone") == 0)
        cfg.deadzone = (uint8_t)parse_int(value);
    else if (strcmp(key, "speed") == 0)
        cfg.speed = (uint8_t)parse_int(value);
}

/*
 * key=value a line at a time, # for a comment. Missing file is not an
 * error: the defaults are meant to be usable, and a config file is for
 * people who disagree with them.
 */
static int read_config(void)
{
    static char buf[1024];
    long fh = Fopen(CFG_FILE, 0);
    long n;
    long i, start = 0;

    if (fh < 0)
        return 0;

    n = Fread((int)fh, (long)sizeof(buf) - 1, buf);
    Fclose((int)fh);

    if (n <= 0)
        return 0;

    buf[n] = 0;

    for (i = 0; i <= n; i++)
    {
        char *line, *eq;

        if (buf[i] != '\n' && buf[i] != '\r' && buf[i] != 0)
            continue;

        buf[i] = 0;
        line = buf + start;
        start = i + 1;

        while (*line == ' ' || *line == '\t')
            line++;

        if (*line == '#' || *line == 0)
            continue;

        eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = 0;
        apply(line, eq + 1);
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* The resident half                                                   */
/* ------------------------------------------------------------------ */

void xpademu_etv_entry(void);
void xpademu_etv_call(void);
void xpademu_call_vec(void (*vec)(void), void *packet);

/* Read by the trampoline in emuetv.s. */
void (*xpademu_etv_chain)(void);

static const XPAD *pads;
static void **joyslot;
static void **mouseslot;

static uint8_t divider = 1;
static uint8_t prev_joy;
static uint8_t prev_mbtn;
static JOYPKT_MOUSE macc;
static JOYPKT_AUTO autostate;

/*
 * The joystick packet is three bytes and TOS delivers it with the 0xFF
 * header at an ODD address, so that a consumer reading a word from
 * header+1 gets joy0 and joy1 as an aligned pair. PP's JOYMOUT tester
 * does exactly that. So the buffer is even-aligned and the header goes
 * at index 1, which puts it on the odd address the vector expects.
 */
static uint8_t joy_packet[4];
static uint8_t mouse_packet[3];

/* Called from the trampoline, in interrupt context, 200 times a
 * second. Allocates nothing and calls nothing that could block. */
void xpademu_tick(void)
{
    XPAD_PAD pad;
    uint8_t joy;

    if (--divider)
        return;

    divider = ETV_DIVISOR;

    if (!pads)
        return;

    if (xpad_read(pads, cfg.pad, &pad))
    {
        joy = joypkt_joystick(pad.buttons, cfg.fire, cfg.jump);

        /* Called every tick whether or not the button is held, because
         * the phase has to advance and releasing has to reset it. OR
         * rather than replace, so holding fire and autofire together
         * gives a steady fire rather than a gap. */
        if (joypkt_autofire(&autostate, (pad.buttons & cfg.autofire) != 0,
                            cfg.autoperiod))
            joy |= JOYPKT_FIRE;

        /* On change only, because that is what the IKBD does: a game
         * hooking joyvec expects an event, not a stream. */
        if (joy != prev_joy)
        {
            void (*vec)(void);

            prev_joy = joy;

            joy_packet[1] = 0xFF; /* header, on the odd address */
            joy_packet[2] = 0;    /* joystick 0 idle: we drive 1 */
            joy_packet[3] = joy;

            vec = (void (*)(void)) * joyslot;
            if (vec)
                xpademu_call_vec(vec, &joy_packet[1]);
        }
    }

    if (!cfg.mouse)
        return;

    if (xpad_read(pads, cfg.mouse_pad, &pad))
    {
        uint8_t btn = joypkt_buttons(pad.buttons, cfg.mleft, cfg.mright);
        int8_t dx, dy;
        int moved = joypkt_mouse(&macc, pad.rx, pad.ry, cfg.deadzone,
                                 cfg.speed, &dx, &dy);

        if (moved || btn != prev_mbtn)
        {
            void (*vec)(void);

            prev_mbtn = btn;

            mouse_packet[0] = (uint8_t)(JOYPKT_MOUSE_HDR | btn);
            mouse_packet[1] = (uint8_t)dx;
            mouse_packet[2] = (uint8_t)dy;

            vec = (void (*)(void)) * mouseslot;
            if (vec)
                xpademu_call_vec(vec, mouse_packet);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

/* Vector surgery belongs in supervisor mode: user mode gets away with
 * it on a bare ST, but not under a memory protected kernel. */
static long install_vector(void)
{
    void (**etv)(void) = (void (**)(void))0x400L;

    xpademu_etv_chain = *etv;
    *etv = xpademu_etv_entry;

    return 0;
}

static void find_vectors(void)
{
    uint8_t *kb = (uint8_t *)Kbdvbase();

    /* The ADDRESSES of the slots, not their contents. Everything in
     * this program's design depends on that distinction. */
    joyslot = (void **)(kb + KBDVECS_JOYVEC);
    mouseslot = (void **)(kb + KBDVECS_MOUSEVEC);
}

static void describe(void)
{
    unsigned i;

    printf("%s\n", PROGRAM);
    printf("Pad %d drives joystick 1. Fire is ", cfg.pad);

    for (i = 0; i < BUTTON_COUNT; i++)
    {
        if (button_names[i].bit && (cfg.fire & button_names[i].bit) &&
            strlen(button_names[i].name) > 1)
            printf("%s ", button_names[i].name);
    }

    printf("\n");

    if (cfg.mouse)
        printf("Pad %d's right stick drives the mouse.\n", cfg.mouse_pad);
    else
        printf("Mouse emulation is off.\n");
}

static int install(void)
{
    pads = xpad_find();

    if (!pads)
    {
        printf("No Xpad provider is installed, so there is nothing\n");
        printf("to emulate a joystick from. Load a provider first.\n");
        return 0;
    }

    if (read_config())
        printf("Read %s.\n", CFG_FILE);

    find_vectors();
    describe();

    /* Last, so a tick cannot arrive before the block and the vector
     * slots are both known. */
    Supexec(install_vector);

    return 1;
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

#define XPAD_MAX_TEST_PADS 2

static int failures;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

/* A stand-in for whatever a game would install, so the test can see
 * what a game would see. */
static uint8_t seen_joy[4];
static int joy_calls;
static uint8_t seen_mouse[3];
static int mouse_calls;
static int chain_calls;

static void fake_joyvec(void)
{
    /* The trampoline hands the packet in a0, which C cannot name. The
     * real vector convention is the reason emuetv.s exists; here the
     * packet is read from the buffer directly, which is the same
     * memory the vector was pointed at. */
    seen_joy[0] = joy_packet[1];
    seen_joy[1] = joy_packet[2];
    seen_joy[2] = joy_packet[3];
    joy_calls++;
}

static void fake_mousevec(void)
{
    seen_mouse[0] = mouse_packet[0];
    seen_mouse[1] = mouse_packet[1];
    seen_mouse[2] = mouse_packet[2];
    mouse_calls++;
}

static void fake_chain(void)
{
    chain_calls++;
}

static XPAD demo;
static void *demo_joyslot;
static void *demo_mouseslot;

/* Pad 0 only, which is all this test drives. */
static void set_pad(uint32_t buttons, int8_t rx, int8_t ry)
{
    XPAD_PAD *p = xpad_back(&demo);

    memset(p, 0, sizeof(*p));
    p->type = XPAD_TYPE_GAMEPAD;
    p->buttons = buttons;
    p->rx = rx;
    p->ry = ry;

    xpad_commit(&demo);
}

/* Drive the real trampoline, in supervisor mode: xpademu_call_vec
 * raises the interrupt level, which user mode may not do. */
static long drive_tick(void)
{
    xpademu_etv_call();
    return 0;
}

static void ticks(int n)
{
    while (n-- > 0)
        Supexec(drive_tick);
}

static int selftest(void)
{
    int i;

    printf("Xpad joystick and mouse emulation\n\n");

    xpad_init(&demo, XPAD_MAX_TEST_PADS, XPAD_CAP_ANALOG, PROGRAM, 0);

    pads = &demo;
    demo_joyslot = (void *)fake_joyvec;
    demo_mouseslot = (void *)fake_mousevec;
    joyslot = &demo_joyslot;
    mouseslot = &demo_mouseslot;
    xpademu_etv_chain = fake_chain;

    cfg.mouse = 1;
    cfg.speed = 24;
    cfg.deadzone = 40;

    /*
     * The divider starts at 1 so the very first tick injects, rather
     * than making whatever the pad already holds wait a frame for the
     * count to come round. After that it is four ETV ticks per packet,
     * because the IKBD reports at 50 Hz and etv_timer runs at 200.
     */
    set_pad(XPAD_RIGHT | XPAD_SOUTH, 0, 0);
    joy_calls = 0;
    ticks(1);
    check(joy_calls == 1, "the first tick injects at once");

    set_pad(XPAD_LEFT | XPAD_SOUTH, 0, 0);
    joy_calls = 0;
    ticks(3);
    check(joy_calls == 0, "then three ticks inject nothing");
    ticks(1);
    check(joy_calls == 1, "and the fourth injects once");
    check(chain_calls == 5, "every tick reaches the displaced handler");

    /* Put the state back for the packet checks below. */
    set_pad(XPAD_RIGHT | XPAD_SOUTH, 0, 0);
    ticks(4);

    /* The packet a game sees. Directions pass through the bottom
     * nibble unchanged and fire moves to bit 7. */
    check(seen_joy[0] == 0xFF, "the packet carries the 0xFF header");
    check(seen_joy[1] == 0, "joystick 0 is left idle");
    check(seen_joy[2] == (XPAD_RIGHT | JOYPKT_FIRE),
          "joystick 1 carries the direction and fire");

    /* The header has to sit at an odd address, or a consumer reading a
     * word from header+1 takes an address error on a 68000. */
    check((((unsigned long)&joy_packet[1]) & 1) == 1,
          "and the header is at an odd address");

    /* On change only, the way the IKBD reports. */
    joy_calls = 0;
    ticks(8);
    check(joy_calls == 0, "an unchanged pad injects nothing");

    set_pad(XPAD_LEFT, 0, 0);
    ticks(4);
    check(joy_calls == 1, "a change injects again");
    check(seen_joy[2] == XPAD_LEFT, "with fire released");

    /*
     * Autofire, end to end: holding it must produce a stream of
     * packets with fire going on and off, not one packet with fire
     * held. A game counts the transitions, so a steady bit is one
     * shot however long you lean on it.
     */
    {
        int with_fire = 0, without = 0;

        cfg.autofire = XPAD_WEST;
        set_pad(XPAD_WEST, 0, 0);
        joy_calls = 0;

        for (i = 0; i < 40; i++)
        {
            int before = joy_calls;

            ticks(4);

            if (joy_calls == before)
                continue;

            if (seen_joy[2] & JOYPKT_FIRE)
                with_fire++;
            else
                without++;
        }

        check(joy_calls > 8, "autofire keeps sending packets while held");
        check(with_fire > 0 && without > 0,
              "with fire going on and off, not held down");
    }

    /* Releasing it stops, rather than leaving fire stuck either way. */
    set_pad(0, 0, 0);
    ticks(4);
    joy_calls = 0;
    ticks(40);
    check(joy_calls == 0, "and letting go stops the stream");

    cfg.autofire = 0;

    /* The mouse. A stick inside the deadzone must not creep: a cursor
     * that drifts on its own is worse than one that is slow. */
    mouse_calls = 0;
    set_pad(0, 20, 0);
    ticks(40);
    check(mouse_calls == 0, "a stick inside the deadzone moves nothing");

    set_pad(0, 127, 0);
    ticks(4);
    check(mouse_calls == 1, "a deflected stick moves the mouse");
    check((seen_mouse[0] & 0xFC) == JOYPKT_MOUSE_HDR,
          "with a relative mouse header");
    check((int8_t)seen_mouse[1] > 0, "rightwards for a rightward stick");

    /* The IKBD's y axis points up and the screen's points down. */
    set_pad(0, 0, 127);
    mouse_calls = 0;
    ticks(4);
    check((int8_t)seen_mouse[2] < 0, "and down the screen for a down stick");

    /* Buttons reach the header even when the stick is still. */
    set_pad(XPAD_THUMBR, 0, 0);
    mouse_calls = 0;
    ticks(4);
    check(mouse_calls == 1, "a click alone still sends a packet");
    check(seen_mouse[0] & JOYPKT_MOUSE_LEFT, "carrying the left button");

    /* A slot with nothing in it is not a crash. Games do uninstall. */
    demo_joyslot = 0;
    demo_mouseslot = 0;
    set_pad(XPAD_UP, 0, 0);
    ticks(4);
    check(1, "an empty vector slot is survivable");

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
    static char *test_args[] = {"XPADEMU", "-t"};

    argc = 2;
    argv = test_args;
#endif

    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        return selftest();

    if (!install())
        return 1;

    /* Keep the whole image. Being clever about the resident size is how
     * TSRs corrupt memory in ways that surface an hour later. */
    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
}
