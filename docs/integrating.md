<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# Integrating into a port

[← Back to the README](../README.md)

How to add Xpad to a game or a provider of your own, what to do when no
provider is installed, and the one line of documentation the licence
asks for. How the block itself works is in
[the specification](specification.md).

## Adding Xpad to your project

**Add this repository as a submodule**, and compile the two or three
files you need straight out of it:

```
git submodule add https://github.com/neilrackett/atarist-xpad lib/xpad
```

Then build `lib/xpad/src/xpad.c` alongside your own sources, with
`-Ilib/xpad/src`. A consumer needs `xpad.h` and `xpad.c`. A provider
takes `xpad_provider.c` as well: `xpad.c` is the consumer half, and
`xpad_provider.c` adds the helpers for owning and publishing a block.
An assembly consumer wants `xpad.inc` or `xpad_gas.inc` instead, which
are generated from the header and would be the third and fourth things
to keep in sync by hand: see [Consuming from assembly](assembly.md).

The split is there because there is no section garbage collection on
`m68k-atari-mint`, so the linker's unit is the object file. A game that
linked one file with both halves in it would carry the provider helpers
into every binary without ever calling them: 684 bytes against 1252,
measured, which is real money on a 512K machine.

Xpad's README used to say that at around 500 lines these files were not
worth a submodule. That was true when the ABI was settled and there
were two files; it stopped being true. A submodule pins an exact commit,
makes an update a deliberate act with a diff to read, and keeps the
generated assembler equates matching the header they came from. Copies
drift silently, and a consumer holding a stale `xpad.h` gets a struct
layout that still compiles.

Vendoring is still reasonable for a port that is frozen, or one whose
build genuinely cannot take a submodule. If you do copy, record the
commit you copied in your own README, and treat `XPAD_VERSION` as the
thing to check when something behaves oddly.

## Cross-compiling with atarist-toolkit-docker

**There is one trap if you cross-compile through
`atarist-toolkit-docker`.** `stcmd` runs `docker run -v ${ST_WORKING_FOLDER}:/tmp`, mounting that
one directory and nothing else, and `ST_WORKING_FOLDER` defaults to the
current directory. Put the submodule inside whatever gets mounted and
everything works; put it above, which is what happens when the ST build
lives in a subdirectory like `target/atarist` and that is the working
folder, and the compiler cannot see it at all. No `-I` reaches outside
the mount.

Two ways out. Either mount the repository root, which is what
atarist-stdl and pico-compad do and why neither hit this; or keep the
submodule as the source of truth and have the build copy what it needs
into the mounted directory as a generated artifact. The second is right
for a project whose ST target is one of several: MD/Lynx does that for
`xpad.inc` while its RP2040 half includes `xpad.h` from the submodule
directly.

## The licence notice

Because the licence is BSD-2-Clause, a port that ships a binary must
reproduce the copyright notice in its accompanying documentation. One
line in the port's own README or docs covers it:

```
Controller input via Xpad, Copyright (c) 2026 Neil Rackett,
BSD-2-Clause. See XPAD.TXT.
```

Easy to overlook, so handle it as part of the initial integration
rather than at release time.

## Falling back

`xpad` is an enhancement, never a requirement. A port should degrade in
this order:

1. `xpad`, when a provider is present.
2. STE enhanced joystick ports, on machines that have them.
3. Joystick 0 for movement and button 1, with joystick 1's fire bit as
   button 2. These are the same two physical lines the ST shares between
   its mouse buttons and joystick fire buttons, so this works either
   through a mouse-port adapter or through a provider that synthesises
   the bits in software.
4. Plain joystick plus keyboard.

A shim that publishes an `XPAD` block populated from steps 2 to 4 lets a
port implement only step 1 and get the rest for nothing. Three such
drivers ship here, covering a plain joystick, the keyboard and the STE
enhanced ports: see [Example drivers](drivers.md). Step 3's trick of
borrowing joystick 1's fire bit as a second button is still to write:
the joystick driver publishes each port as its own pad rather than
merging them.
