/* SPDX-License-Identifier: BSD-2-Clause */
/* SPDX-FileCopyrightText: 2026 Neil Rackett */

/*
 * Host stand-in for <mint/osbind.h>, used only by the ABI harness so
 * that xpad.c compiles unmodified with a native compiler.
 *
 * Supexec has no privilege to model here, so it just calls through.
 * XPAD_JAR is redirected at a variable the harness owns, since 0x5A0
 * only means anything on an ST: that puts the jar walking under test
 * instead of leaving it to hardware. Only the jar layout is checked
 * this way, never a block address round trip, because casting a host
 * pointer through the jar's uint32_t truncates it.
 */

#ifndef XPAD_TEST_OSBIND_H
#define XPAD_TEST_OSBIND_H

#include <stdint.h>

extern uint32_t *xpad_test_jar;

#define XPAD_JAR xpad_test_jar

static inline long Supexec(long (*fn)(void))
{
    return fn();
}

#endif
