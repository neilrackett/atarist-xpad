#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

"""Run an Atari ST program under Hatari and relay its console output.

Boots EmuTOS with a temporary directory holding only the program as
drive C, autostarts it, and pipes the ST console back to stdout.

Hatari cannot pass an exit status out, and sits at the desktop once the
program returns, so the program prints a final "XPAD-DONE <rc>" line.
This waits for that, stops the emulator, and exits with the same code.

Override the emulator, TOS image and machine with $HATARI, $TOS and
$MACHINE if the guesses below are wrong.
"""

import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading

TIMEOUT = 120  # seconds of wall clock before giving up on the sentinel
SENTINEL = "XPAD-DONE"

HATARI_GUESSES = [
    "/Applications/Hatari.app/Contents/MacOS/Hatari",
    "/usr/local/bin/hatari",
    "/opt/homebrew/bin/hatari",
]

# EmuTOS is the easiest image to obtain and is free to redistribute, so
# it is what this looks for. A real TOS ROM works just as well.
TOS_GLOBS = [
    "~/Downloads/emutos-*/etos256us.img",
    "~/Downloads/emutos-*/etos*.img",
    "~/emutos*/etos*.img",
    "/usr/local/share/emutos/etos*.img",
    "/opt/homebrew/share/emutos/etos*.img",
]


def find_hatari():
    if os.environ.get("HATARI"):
        return os.environ["HATARI"]

    for path in HATARI_GUESSES:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path

    return shutil.which("hatari")


def find_tos():
    import glob

    if os.environ.get("TOS"):
        return os.environ["TOS"]

    for pattern in TOS_GLOBS:
        hits = sorted(glob.glob(os.path.expanduser(pattern)))
        if hits:
            return hits[0]

    return None


def main():
    if len(sys.argv) not in (2, 3):
        sys.exit("usage: run-hatari.py <program.tos> [auto-program]")

    program = sys.argv[1]
    # Optional second program, dropped in AUTO\ so EmuTOS runs it at boot.
    # That is how a resident driver gets installed before the program
    # under test looks for it.
    auto_first = sys.argv[2] if len(sys.argv) > 2 else None

    if not os.path.isfile(program):
        sys.exit("no such program: %s\n"
                 "build it first: STCMD_NO_TTY=1 stcmd make st" % program)

    hatari = find_hatari()
    if not hatari:
        sys.exit("Hatari not found. Install it, or set $HATARI to the binary.")

    tos = find_tos()
    if not tos:
        sys.exit("No TOS image found. Set $TOS to an EmuTOS or TOS image.\n"
                 "EmuTOS: https://emutos.sourceforge.io/")

    # Drive C holds only the program: GEMDOS filenames are 8.3, and an
    # unrelated file with a long name would draw a warning per boot.
    with tempfile.TemporaryDirectory() as drive_c:
        name = os.path.basename(program).upper()
        shutil.copy(program, os.path.join(drive_c, name))

        if auto_first:
            if not os.path.isfile(auto_first):
                sys.exit("no such program: %s" % auto_first)
            auto_dir = os.path.join(drive_c, "AUTO")
            os.mkdir(auto_dir)
            shutil.copy(auto_first,
                        os.path.join(auto_dir,
                                     os.path.basename(auto_first).upper()))

        cmd = [
            hatari,
            "--tos", tos,
            "--machine", os.environ.get("MACHINE", "st"),
            "--memsize", "4",
            "--harddrive", drive_c,
            "--auto", "C:\\" + name,
            "--conout", "2",         # ST console through to our stdout
            "--sound", "off",
            "--fast-forward", "on",
            "--confirm-quit", "off",
        ]

        env = dict(os.environ,
                   SDL_VIDEODRIVER="dummy",  # headless
                   SDL_AUDIODRIVER="dummy")

        print("hatari: %s" % hatari)
        print("tos:    %s" % tos)
        if auto_first:
            print("auto:   %s" % os.path.basename(auto_first).upper())
        print("running %s\n" % name)

        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, env=env)

        # Read on a thread so the sentinel can be spotted as it arrives
        # rather than after the emulator is killed.
        lines = queue.Queue()

        def reader():
            for raw in proc.stdout:
                lines.put(raw.decode("latin-1", "replace").rstrip("\r\n"))
            lines.put(None)

        threading.Thread(target=reader, daemon=True).start()

        rc = None
        try:
            while True:
                try:
                    line = lines.get(timeout=TIMEOUT)
                except queue.Empty:
                    print("\ntimed out after %ds with no verdict" % TIMEOUT)
                    rc = 2
                    break

                if line is None:  # emulator exited on its own
                    break

                print(line)

                if line.startswith(SENTINEL):
                    rc = int(line.split()[1])
                    break
        finally:
            proc.kill()
            proc.wait()

    if rc is None:
        print("\nemulator exited before reporting a verdict")
        rc = 2

    return rc


if __name__ == "__main__":
    sys.exit(main())
