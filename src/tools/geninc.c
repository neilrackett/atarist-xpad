/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Generates the assembler equates from xpad.h, so an assembly consumer
 * does not transcribe offsets by eye.
 *
 * Everything emitted here is architecture independent: the fixed header
 * (magic through active), the whole of XPAD_PAD, and the constants. The
 * fields past XPAD_HDR_FIXED hold pointers and are deliberately absent,
 * which is not a gap: a consumer locates pads through pads_offset,
 * pad_size and pad_count, and those are all inside the fixed part. An
 * assembly consumer therefore never needs to reach past it.
 *
 * Two dialects, because the m68k world has two. Pass -gas for GNU as,
 * anything else for vasm in devpac mode.
 *
 *   make inc     regenerate src/xpad.inc and src/xpad_gas.inc
 *   make test    fails if the committed files are stale
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../xpad.h"

static int gas;

/*
 * hex is for the values a reader checks against a bitmask or a magic
 * number; plain decimal is for offsets and sizes, where hex would just
 * be arithmetic in the way. The two dialects spell hex differently:
 * $1234 for devpac, 0x1234 for gas.
 */
static void emit(const char *name, unsigned long value, int hex,
                 const char *comment)
{
    char rendered[24];

    if (hex)
        snprintf(rendered, sizeof(rendered), gas ? "0x%lx" : "$%lx", value);
    else
        snprintf(rendered, sizeof(rendered), "%lu", value);

    /* Pad the value only when a comment follows, so lines without one
     * do not carry trailing blanks. */
    if (gas)
        printf("        .equ    %-20s, %s", name, rendered);
    else
        printf("%-24s equ     %s", name, rendered);

    if (comment && *comment)
    {
        int pad = 10 - (int)strlen(rendered);

        printf("%*s %s %s", pad > 0 ? pad : 1, "", gas ? "|" : ";", comment);
    }

    printf("\n");
}

/* An offset or a count. */
static void def(const char *name, unsigned long value, const char *comment)
{
    emit(name, value, 0, comment);
}

/* A bitmask or a magic number. */
static void defx(const char *name, unsigned long value, const char *comment)
{
    emit(name, value, 1, comment);
}

static void heading(const char *text)
{
    printf("\n%s %s\n", gas ? "|" : ";", text);
}

int main(int argc, char **argv)
{
    gas = argc > 1 && strcmp(argv[1], "-gas") == 0;

    printf("%s SPDX-License-Identifier: BSD-2-Clause\n", gas ? "|" : ";");
    printf("%s SPDX-FileCopyrightText: 2026 Neil Rackett\n", gas ? "|" : ";");
    printf("%s\n", gas ? "|" : ";");
    printf("%s Xpad ABI equates for %s. GENERATED from src/xpad.h by\n",
           gas ? "|" : ";", gas ? "GNU as" : "vasm/devpac");
    printf("%s src/tools/geninc.c: do not edit, run `make inc`. `make test`\n",
           gas ? "|" : ";");
    printf("%s fails if this file drifts from the header.\n", gas ? "|" : ";");
    printf("%s\n", gas ? "|" : ";");
    printf("%s Offsets past XPAD_HDR_FIXED are omitted on purpose: they hold\n",
           gas ? "|" : ";");
    printf("%s pointers, and nothing an assembly consumer needs lives there.\n",
           gas ? "|" : ";");
    printf("%s\n", gas ? "|" : ";");
    printf("%s XPAD_COOKIE and XPAD_MAGIC hold the same value today but are\n",
           gas ? "|" : ";");
    printf("%s separate constants: match the jar tag with the first and the\n",
           gas ? "|" : ";");
    printf("%s block sentinel with the second. Do not rely on them being equal.\n",
           gas ? "|" : ";");

    heading("Identity");
    defx("XPAD_COOKIE", XPAD_COOKIE, "jar tag: what a jar walk matches");
    defx("XPAD_MAGIC", XPAD_MAGIC, "block sentinel: XPAD.magic");
    def("XPAD_VER_MAJOR", XPAD_VER_MAJOR, "what validation compares");
    def("XPAD_VER_MINOR", XPAD_VER_MINOR, NULL);
    defx("XPAD_VERSION", XPAD_VERSION, "major<<8 | minor");
    defx("XPAD_JAR", 0x5a0, "cookie jar pointer, needs supervisor");

    heading("XPAD header, the architecture independent part");
    def("XPAD_MAGIC_OFF", offsetof(XPAD, magic), "long");
    def("XPAD_VERSION_OFF", offsetof(XPAD, version), "word");
    def("XPAD_HDRSIZE_OFF", offsetof(XPAD, hdr_size), "word");
    def("XPAD_PADSOFF_OFF", offsetof(XPAD, pads_offset), "word");
    def("XPAD_PADSIZE_OFF", offsetof(XPAD, pad_size), "word");
    def("XPAD_CAPS_OFF", offsetof(XPAD, caps), "word");
    def("XPAD_SEQ_OFF", offsetof(XPAD, seq), "word");
    def("XPAD_PADCOUNT_OFF", offsetof(XPAD, pad_count), "byte");
    def("XPAD_ACTIVE_OFF", offsetof(XPAD, active), "byte, 0 or 1");
    def("XPAD_HDR_FIXED", XPAD_HDR_FIXED, "bytes frozen everywhere");

    heading("XPAD_PAD");
    def("XPAD_PAD_BUTTONS", offsetof(XPAD_PAD, buttons), "long");
    def("XPAD_PAD_LX", offsetof(XPAD_PAD, lx), "byte, signed");
    def("XPAD_PAD_LY", offsetof(XPAD_PAD, ly), "byte, signed");
    def("XPAD_PAD_RX", offsetof(XPAD_PAD, rx), "byte, signed");
    def("XPAD_PAD_RY", offsetof(XPAD_PAD, ry), "byte, signed");
    def("XPAD_PAD_LT", offsetof(XPAD_PAD, lt), "byte, unsigned");
    def("XPAD_PAD_RT", offsetof(XPAD_PAD, rt), "byte, unsigned");
    def("XPAD_PAD_TYPE", offsetof(XPAD_PAD, type), "byte, 0 = absent");
    def("XPAD_PAD_FLAGS", offsetof(XPAD_PAD, flags), "byte");
    def("XPAD_PAD_SIZE_V1", XPAD_PAD_SIZE_V1, "floor, never shrinks");
    def("XPAD_MAX_PADS", XPAD_MAX_PADS, NULL);

    heading("Buttons. Bit values are frozen; see README.");
    defx("XPAD_UP", XPAD_UP, NULL);
    defx("XPAD_DOWN", XPAD_DOWN, NULL);
    defx("XPAD_LEFT", XPAD_LEFT, NULL);
    defx("XPAD_RIGHT", XPAD_RIGHT, NULL);
    defx("XPAD_SOUTH", XPAD_SOUTH, "A on an Xbox pad");
    defx("XPAD_EAST", XPAD_EAST, "B");
    defx("XPAD_NORTH", XPAD_NORTH, "aliased XPAD_X: see the X/Y trap");
    defx("XPAD_WEST", XPAD_WEST, "aliased XPAD_Y: see the X/Y trap");
    defx("XPAD_TL", XPAD_TL, NULL);
    defx("XPAD_TR", XPAD_TR, NULL);
    defx("XPAD_TL2", XPAD_TL2, NULL);
    defx("XPAD_TR2", XPAD_TR2, NULL);
    defx("XPAD_SELECT", XPAD_SELECT, NULL);
    defx("XPAD_START", XPAD_START, NULL);
    defx("XPAD_MODE", XPAD_MODE, NULL);
    defx("XPAD_THUMBL", XPAD_THUMBL, NULL);
    defx("XPAD_THUMBR", XPAD_THUMBR, "bit 16: the only one above a word");

    heading("Pad types");
    def("XPAD_TYPE_NONE", XPAD_TYPE_NONE, "slot empty");
    def("XPAD_TYPE_JOYSTICK", XPAD_TYPE_JOYSTICK, NULL);
    def("XPAD_TYPE_GAMEPAD", XPAD_TYPE_GAMEPAD, NULL);
    def("XPAD_TYPE_XBOX", XPAD_TYPE_XBOX, NULL);
    def("XPAD_TYPE_PLAYSTATION", XPAD_TYPE_PLAYSTATION, NULL);
    def("XPAD_TYPE_NINTENDO", XPAD_TYPE_NINTENDO, NULL);
    def("XPAD_TYPE_KEYBOARD", XPAD_TYPE_KEYBOARD, NULL);

    heading("Capabilities");
    defx("XPAD_CAP_ANALOG", XPAD_CAP_ANALOG, NULL);
    defx("XPAD_CAP_RUMBLE", XPAD_CAP_RUMBLE, NULL);
    defx("XPAD_CAP_LED", XPAD_CAP_LED, NULL);
    defx("XPAD_CAP_HOTPLUG", XPAD_CAP_HOTPLUG, NULL);

    heading("Pad flags");
    defx("XPAD_PAD_ANALOG", XPAD_PAD_ANALOG, NULL);
    defx("XPAD_PAD_WIRELESS", XPAD_PAD_WIRELESS, NULL);
    defx("XPAD_PAD_LOWBATT", XPAD_PAD_LOWBATT, NULL);

    return 0;
}
