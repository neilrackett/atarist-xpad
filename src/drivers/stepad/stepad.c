/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Xpad STE driver: publishes both enhanced joystick ports as two pads.
 *
 * EXAMPLE DRIVER. Written to be read and copied as much as to be used.
 * See "Example drivers" in README for the full list of what it does
 * not do.
 *
 * This is the only driver here that reaches a real gamepad on stock
 * hardware: the STE's 15 pin ports take an Atari Jaguar style pad, so
 * a d-pad, three fire buttons and two menu buttons arrive without any
 * adapter, cartridge or Bluetooth. It is also the first driver here
 * that has to poll, because the ports are registers rather than an
 * interrupt source, which is why it is the one with an ETV hook.
 *
 *   XPADSTE        install and stay resident
 *   XPADSTE -t     run the self test and exit, installing nothing
 *
 * Limits:
 *
 *   - STE and Falcon only. A plain ST bus errors on $FF9200, so the
 *     machine is checked through _MCH before anything is read and the
 *     driver refuses rather than taking the machine down. A Mega STE is
 *     refused too, for the opposite reason: the registers are there but
 *     the sockets are not, so it would publish pads nothing can move.
 *   - the 12 key keypad is sampled but not mapped, because Xpad v1 has
 *     no home for it and inventing twelve button bits is not an
 *     example's decision to make
 *   - no analogue, so XPAD_CAP_ANALOG is not claimed. The same ports
 *     carry paddles at $FF9211 and up, which would map onto the axes
 *     nicely, but a paddle is a different peripheral and would be a
 *     different example.
 *   - no presence detection. The ports cannot say whether anything is
 *     plugged in, so both slots always report connected and an empty
 *     port reads as a pad with nothing pressed.
 *   - seq advances on every tick rather than on change, because a poll
 *     has no way to know a button changed without looking.
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <stdio.h>
#include <string.h>

#include "../../xpad.h"
#include "decode.h"

#define PROVIDER "STE joypad 1.0"

#define PAD_COUNT XPAD_STE_PADS

/*
 * The I/O area is at $FF8000 in the 68000's 24 bit address space, which
 * is $FFFF8000 once sign extended, the form ST code conventionally
 * writes. $FF9200 must be read as a word: a byte read of it bus errors
 * on STE and Mega STE, which is a hardware quirk rather than a
 * convention, and Hatari models it because somebody met it.
 */
#define STE_FIRE (*(volatile uint16_t *)0xFFFF9200UL)
#define STE_MULTI (*(volatile uint16_t *)0xFFFF9202UL)

#define COOKIE_MCH 0x5F4D4348UL /* '_MCH' */

/*
 * _MCH, in full rather than by family, because the family is not enough
 * here: the low word is the sub model and it is what separates an STE
 * from a Mega STE. Values confirmed from EmuTOS under Hatari rather
 * than from memory.
 */
#define MCH_STE 0x00010000UL
#define MCH_MEGA_STE 0x00010010UL
#define MCH_FAMILY_FALCON 3

static XPAD block;

/* Cleared until the machine has been checked, so the sample below can
 * never reach $FF9200 on a machine that does not have it. */
static int hardware_ok;

/* Read by the trampoline in etv.s. */
void (*xpad_etv_chain)(void);

extern void xpad_etv_entry(void);
extern void xpad_etv_call(void (*fn)(void));

/* ------------------------------------------------------------------ */
/* Hardware                                                            */
/* ------------------------------------------------------------------ */

/*
 * Supervisor only: both the cookie jar pointer at $5A0 and the jar
 * itself sit below $800, where the ST bus errors on a user mode read.
 * Returns the _MCH value, or -1 when there is no jar and no cookie,
 * which is what TOS 1.0 looks like and is correctly treated as an ST.
 */
static long mch_probe(void)
{
    uint32_t *jar = *(uint32_t **)0x5A0L;

    if (!jar)
        return -1;

    while (jar[0])
    {
        if (jar[0] == COOKIE_MCH)
            return (long)jar[1];

        jar += 2;
    }

    return -1;
}

/*
 * An STE has the ports and so does a Falcon. A Mega STE does not: it is
 * an STE on a board in a Mega case, and the case has nowhere to plug a
 * joypad in. That one matters more than it looks, because the registers
 * are still there. $FF9200 on a Mega STE reads the motherboard DIP
 * switches in its upper byte, so a driver that gated on the family
 * would find no bus error, install happily, and then publish two pads
 * that could never move. A silently idle provider is worse than a
 * refusal, and Xpad is single provider, so it would be sitting where a
 * working one could have been.
 *
 * Anything not known to have the ports is refused rather than probed.
 * The cost of guessing wrong in the other direction is a bus error
 * inside a timer interrupt, which takes the machine with it.
 */
static int stepad_present(void)
{
    long mch = Supexec(mch_probe);

    if (mch < 0)
        return 0;

    if ((unsigned long)mch == MCH_MEGA_STE)
        return 0;

    return (unsigned long)mch == MCH_STE ||
           ((mch >> 16) & 0xffff) == MCH_FAMILY_FALCON;
}

/*
 * One poll of the matrix: select each row in turn and read both
 * registers. Runs in interrupt context, so it allocates nothing and
 * calls nothing.
 */
static void stepad_sample(XPAD_STE_SAMPLE *s)
{
    int r;

    for (r = 0; r < XPAD_STE_ROWS; r++)
    {
        STE_MULTI = XPAD_STE_SELECT(r);
        s->dir[r] = (uint8_t)STE_MULTI;
        s->fire[r] = (uint8_t)STE_FIRE;
    }

    /* Leave the matrix unselected. Holding a row low is visible to
     * anything else that reads these ports, the paddle and lightpen
     * registers included, and this driver is a guest here. */
    STE_MULTI = 0xff;
}

/* Split from the sample so the self test can drive it with fabricated
 * rows on a machine that has no ports to read. */
static void stepad_apply(const XPAD_STE_SAMPLE *s)
{
    XPAD_PAD *back = xpad_back(&block);
    int i;

    for (i = 0; i < PAD_COUNT; i++)
        back[i].buttons = xpad_stepad_decode(s, i);

    xpad_commit(&block);
}

/*
 * Called from the trampoline, in interrupt context. Nothing here may
 * allocate, do I/O, or call anything that is not reentrant.
 */
void xpad_etv_update(void)
{
    XPAD_STE_SAMPLE s;

    if (!hardware_ok)
        return;

    stepad_sample(&s);
    stepad_apply(&s);
}

static void init_block(void)
{
    int i, j;

    xpad_init(&block, PAD_COUNT, 0, PROVIDER, 0);

    /* Mark both slots occupied in both buffers, once, so the interrupt
     * path only ever has to write buttons. Two commits also leave
     * active back where xpad_init put it. */
    for (i = 0; i < 2; i++)
    {
        XPAD_PAD *back = xpad_back(&block);

        for (j = 0; j < PAD_COUNT; j++)
            back[j].type = XPAD_TYPE_GAMEPAD;

        xpad_commit(&block);
    }
}

/* ------------------------------------------------------------------ */
/* Self test                                                           */
/* ------------------------------------------------------------------ */

static int failures;
static int chain_calls;

static void check(int ok, const char *what)
{
    printf("%-44s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        failures++;
}

static void chain_stub(void)
{
    chain_calls++;
}

/*
 * Fabricate a poll. Everything is active low, so an idle sample is all
 * ones and a test presses buttons by clearing bits.
 */
static void idle(XPAD_STE_SAMPLE *s)
{
    memset(s, 0xff, sizeof(*s));
}

static uint32_t pad_buttons(int index)
{
    XPAD_PAD pad;

    if (!xpad_read(&block, index, &pad))
        return 0xffffffffUL; /* cannot happen; fails the check loudly */

    return pad.buttons;
}

/*
 * Drive the real trampoline the way the timer does, in supervisor mode.
 * That is not a formality: the poll writes $FF9202 to select a row, and
 * the I/O area bus errors on a user mode access exactly as the cookie
 * jar below $800 does. Calling this straight from main() is a bus error
 * on the first row select, which is how this comment came to be here.
 */
static long drive_tick(void)
{
    xpad_etv_call(xpad_etv_entry);

    return 0;
}

static int selftest(void)
{
    XPAD_STE_SAMPLE s;
    XPAD_PAD pad;
    uint16_t before;
    int present;
    unsigned r, bit;
    int clean;

    init_block();
    xpad_etv_chain = chain_stub;

    idle(&s);
    stepad_apply(&s);
    check(pad_buttons(0) == 0 && pad_buttons(1) == 0,
          "an idle sample reads no buttons");

    /* Directions, active low, in the nibble the port reports them in. */
    idle(&s);
    s.dir[0] = 0xfe;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_UP, "pad 0 bit 0 is UP");

    idle(&s);
    s.dir[0] = 0xf7;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_RIGHT, "pad 0 bit 3 is RIGHT");

    idle(&s);
    s.dir[0] = 0xef;
    stepad_apply(&s);
    check(pad_buttons(1) == XPAD_UP, "pad 1 reads the high nibble");

    /* One fire line, four rows, four different buttons. */
    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_FIRE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_SOUTH, "row 0 on the fire line is A");

    idle(&s);
    s.fire[1] = (uint8_t)~XPAD_STE_FIRE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_EAST, "row 1 on the fire line is B");

    idle(&s);
    s.fire[2] = (uint8_t)~XPAD_STE_FIRE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_WEST, "row 2 on the fire line is C");

    idle(&s);
    s.fire[3] = (uint8_t)~XPAD_STE_FIRE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_SELECT, "row 3 on the fire line is Option");

    idle(&s);
    s.fire[0] = (uint8_t)~XPAD_STE_PAUSE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_START, "the pause line is Pause");

    /* Two ports, and they must not bleed into each other. This is also
     * the only test here that depends on the buffer stride being right:
     * pad 1 only lands where xpad_read() looks for it if xpad_back()
     * and XPAD_PAD_AT() agree at a pad_count below XPAD_MAX_PADS. */
    idle(&s);
    s.dir[0] = 0xfb;                            /* pad 0 LEFT  */
    s.fire[0] = (uint8_t)~XPAD_STE_FIRE1;       /* pad 1 A     */
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_LEFT, "port 0 lands in pad 0");
    check(pad_buttons(1) == XPAD_SOUTH, "port 1 lands in pad 1");

    idle(&s);
    s.dir[0] = 0xf0;
    s.fire[0] = (uint8_t)~(XPAD_STE_FIRE0 | XPAD_STE_PAUSE0);
    s.fire[1] = (uint8_t)~XPAD_STE_FIRE0;
    s.fire[2] = (uint8_t)~XPAD_STE_FIRE0;
    s.fire[3] = (uint8_t)~XPAD_STE_FIRE0;
    stepad_apply(&s);
    check(pad_buttons(0) == XPAD_STE_BUTTONS, "everything at once on pad 0");
    check(pad_buttons(1) == 0, "and pad 1 stays clear");

    idle(&s);
    stepad_apply(&s);
    check(pad_buttons(0) == 0, "releasing clears the buttons");

    /* No sample may ever set a bit this driver does not own. The keypad
     * columns share the rows with the fire line, so this is the check
     * that they stay ignored rather than leaking into the mask. */
    clean = 1;
    for (r = 0; r < XPAD_STE_ROWS; r++)
    {
        for (bit = 0; bit < 8; bit++)
        {
            idle(&s);
            s.dir[r] = (uint8_t)~(1u << bit);
            s.fire[r] = (uint8_t)~(1u << bit);

            if (xpad_stepad_decode(&s, 0) & ~XPAD_STE_BUTTONS)
                clean = 0;
            if (xpad_stepad_decode(&s, 1) & ~XPAD_STE_BUTTONS)
                clean = 0;
        }
    }
    check(clean, "no sample sets a bit outside this driver's own");

    /* The row select has to reach both pads in one write, or pad 1
     * would need a second pass over the matrix. */
    check(XPAD_STE_SELECT(0) == 0xee && XPAD_STE_SELECT(3) == 0x77,
          "one select reads the same row on both pads");

    check(xpad_read(&block, 0, &pad) && pad.type == XPAD_TYPE_GAMEPAD,
          "pad 0 reports a gamepad");
    check(xpad_connected(&block) == PAD_COUNT, "both pads are connected");
    check(xpad_read(&block, PAD_COUNT, &pad) == 0, "there is no third pad");

    /* Nothing analogue is claimed, so nothing analogue should appear. */
    check(xpad_read(&block, 1, &pad) &&
              !pad.lx && !pad.ly && !pad.rx && !pad.ry && !pad.lt && !pad.rt,
          "axes and triggers stay zero");
    check(block.caps == 0, "no capabilities are claimed");

    before = block.seq;
    stepad_apply(&s);
    check(block.seq == (uint16_t)(before + 1), "each poll commits once");

    /*
     * The machine gate, and then the real trampoline. On a plain ST
     * hardware_ok stays clear and the update does nothing, which is the
     * behaviour being tested: reaching $FF9200 there is a bus error in
     * an interrupt handler. On an STE the same call samples the real
     * ports, so this covers the register access too.
     */
    present = stepad_present();
    printf("\n_MCH is %08lx, enhanced ports: %s\n",
           (unsigned long)Supexec(mch_probe), present ? "yes" : "no");

    hardware_ok = present;
    Supexec(drive_tick);
    check(chain_calls > 0, "every tick reached the displaced handler");

    hardware_ok = 0;
    before = block.seq;
    Supexec(drive_tick);
    check(block.seq == before, "without the ports, a tick commits nothing");
    check(chain_calls > 1, "and still chains");

    printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    printf("XPAD-DONE %d\n", failures ? 1 : 0);
    fflush(stdout);

    return failures ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Install                                                             */
/* ------------------------------------------------------------------ */

/* Vector surgery belongs in supervisor mode: user mode gets away with
 * it on a bare ST, but not under a memory protected kernel. */
static long install_vector(void)
{
    void (**etv)(void) = (void (**)(void))0x400L;

    xpad_etv_chain = *etv;
    *etv = xpad_etv_entry;

    return 0;
}

static int install(void)
{
    /* Xpad is single provider and the last one to publish wins, so a
     * shim like this must not displace something better. */
    if (xpad_find())
    {
        printf("An Xpad provider is already installed. Leaving it alone.\n");
        return 0;
    }

    if (!stepad_present())
    {
        printf("This machine has no enhanced joystick ports.\n");
        printf("An STE or a Falcon is needed: a Mega STE has the\n");
        printf("registers but no sockets to plug a joypad into.\n");
        return 0;
    }

    init_block();

    if (!xpad_publish(&block))
    {
        printf("Could not install the XPAD cookie. Is the jar full?\n");
        return 0;
    }

    /* Only now, with the block published and about to be driven. The
     * hook goes in last so a tick cannot arrive before there is
     * anything for it to fill. */
    hardware_ok = 1;
    Supexec(install_vector);

    printf("%s installed: both enhanced ports as pads 0 and 1.\n", PROVIDER);

    return 1;
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
    static char *test_args[] = {"XPADSTE", "-t"};

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
