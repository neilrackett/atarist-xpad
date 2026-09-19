/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * What a keypress means to the viewer.
 *
 * Free of TOS dependencies so the host build tests it without an
 * emulator, the same split the drivers use for translate.h and
 * keymap.h. Hatari's --auto takes a path and no arguments, so nothing
 * automated can press a key in the live loop: without this the viewer's
 * keys were the one part of the program with no coverage at all.
 *
 * Every key is matched on the ASCII byte and on the scancode. TOS maps
 * a key to ASCII through the national keyboard table in ROM, so the key
 * with Q printed on it does not send 'q' on every machine, while the
 * scancode is the physical position and is the same everywhere. The
 * legend on screen names positions, so the scancode is what it is
 * really promising.
 */

#ifndef XPAD_VIEWKEYS_H
#define XPAD_VIEWKEYS_H

/* ST scancodes. The keypad's top row runs ( ) / * left to right. */
#define VIEW_SCAN_ESC 0x01
#define VIEW_SCAN_Q 0x10
#define VIEW_SCAN_KP_LPAREN 0x63
#define VIEW_SCAN_KP_RPAREN 0x64
#define VIEW_SCAN_KP_STAR 0x66

/* What a key asks for. VIEW_PAD_0 through VIEW_PAD_0 + 3 select a pad,
 * so the caller subtracts rather than switching on four values. */
#define VIEW_NOTHING 0
#define VIEW_QUIT 1
#define VIEW_RUMBLE_LEFT 2
#define VIEW_RUMBLE_RIGHT 3
#define VIEW_RUMBLE_BOTH 4
#define VIEW_PAD_0 16

/*
 * c is the ASCII byte of Bconin's return and scan the scancode, which
 * is bits 16 to 23 of it.
 */
static int view_key(char c, unsigned scan)
{
    if (c == 'q' || c == 'Q' || c == 27 || scan == VIEW_SCAN_Q ||
        scan == VIEW_SCAN_ESC)
        return VIEW_QUIT;

    if (c >= '1' && c <= '4')
        return VIEW_PAD_0 + (c - '1');

    if (c == '(' || scan == VIEW_SCAN_KP_LPAREN)
        return VIEW_RUMBLE_LEFT;

    if (c == ')' || scan == VIEW_SCAN_KP_RPAREN)
        return VIEW_RUMBLE_RIGHT;

    if (c == '*' || scan == VIEW_SCAN_KP_STAR)
        return VIEW_RUMBLE_BOTH;

    return VIEW_NOTHING;
}

#endif /* XPAD_VIEWKEYS_H */
