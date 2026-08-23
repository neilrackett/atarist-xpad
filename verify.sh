#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

#
# Run every test this repo has.
#
# There is no single make target for this, and there cannot be: the ST
# builds need the atarist-toolkit-docker container and Hatari must not
# run inside it. This script straddles the two, calling stcmd for the
# build half the way md-sidepad's build.sh does.
#
# Run it on the host. Takes a couple of minutes, mostly booting EmuTOS
# six times.
#

set -u

cd "$(dirname "$0")" || exit 1

export STCMD_NO_TTY=1

failed=""

step() {
    name=$1
    shift

    printf '\n\033[1m=== %s ===\033[0m\n' "$name"

    if "$@"; then
        return 0
    fi

    # make reports a failing recipe as exit 2, so any non-zero is a fail.
    failed="$failed
  $name"
    return 1
}

if [ -f /.dockerenv ]; then
    echo "Run this on the host, not under stcmd: it needs Hatari, which the"
    echo "container does not have. The container half is invoked from here."
    exit 1
fi

if ! command -v stcmd >/dev/null 2>&1; then
    echo "stcmd not found, so the ST builds cannot run."
    echo "Host tests alone: make"
    exit 1
fi

# Host first: it needs no toolchain and fails fastest.
step "host tests" make

# The ST toolchain half. Everything after this needs the binaries it
# produces, so a failure here makes the rest meaningless.
step "m68000 compile, ABI assertions on the target" stcmd make check

if step "link everything that runs on an ST" stcmd make st; then
    # Emulated ST. Each is independent, so run them all and report
    # together rather than stopping at the first.
    step "ABI assertions on an emulated ST" make hatari
    step "joystick driver self test" make hatari-joystick
    step "keyboard driver self test" make hatari-keyboard
    step "viewer against its demo provider" make hatari-view
    step "drivers resident from AUTO, read by another process" \
        make hatari-integration
else
    failed="$failed
  (emulated ST tests skipped: nothing to run)"
fi

echo
if [ -n "$failed" ]; then
    printf '\033[1mFAILED\033[0m%s\n' "$failed"
    exit 1
fi

printf '\033[1mEverything passed\033[0m\n'
