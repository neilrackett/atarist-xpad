<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# Xpad for Atari ST

Extended controller input for the Atari ST family,
by [Neil Rackett](https://neilrackett.com/atarist)

## Introduction

A small, transport-neutral shared state block that exposes every button,
stick and trigger of a modern gamepad to native ST software. Named after
the Linux `xpad` driver, whose button naming it follows.

The ST has no way to describe a controller with more than one button.
`xpad` adds one, without replacing anything: a provider publishes a
struct, consumers poll it, and neither end needs to know how the other
works.

## Why a struct rather than a vector

`joyvec` is a callback because the IKBD is event driven. Games poll once
per frame, so a callback only adds indirection and re-entrancy hazards.
A block in RAM, refreshed by whoever owns the hardware, is simpler at
both ends and costs a consumer nothing when no provider is present.

## Discovery

The provider installs cookie `XPAD`, whose value is the address of an
`XPAD` block. A consumer calls `xpad_find()` once at startup:

```c
const XPAD *pads = xpad_find();     /* NULL when unavailable */
```

`xpad_find()` validates the magic and the major version, so a consumer
built against v1 will safely decline a hypothetical v2 provider rather
than misread it. It also checks that the block's own layout fields
describe a pad area lying inside the block: `pad_size` is at least the
v1 pad, and `pads_offset` plus the two buffers fits within `hdr_size`.
Those are the values `XPAD_PAD_AT()` trusts on every read, so they are
established once at discovery rather than defended against per frame.

The predicate is available on its own as `xpad_valid()`. A provider
should run it over a block it has just built, before publishing
something no consumer would accept.

## Reading

```c
XPAD_PAD pad;

if (pads && xpad_read(pads, 0, &pad))
{
    if (pad.buttons & XPAD_A)     jump();
    if (pad.buttons & XPAD_RT)    fire();
    if (pad.buttons & XPAD_LEFT)  walk(-1);

    if (pad.flags & XPAD_PAD_ANALOG)
        aim(pad.rx, pad.ry);
}
else
{
    read_ikbd_joystick();           /* fall back, see below */
}
```

`xpad_read()` copies rather than handing out a pointer, so the caller
holds a stable snapshot for the whole frame. It is cheap: twelve bytes
and two consistency checks.

## Writing

```c
static XPAD     state;
static XPAD_REQ req;

xpad_init(&state, 4, XPAD_CAP_ANALOG | XPAD_CAP_RUMBLE,
          "MD/Sidepad 1.1", &req);
xpad_publish(&state);

/* once per refresh, typically from a VBL handler */
{
    XPAD_PAD *pads = xpad_back(&state);

    pads[0].type    = XPAD_TYPE_XBOX;
    pads[0].flags   = XPAD_PAD_ANALOG | XPAD_PAD_WIRELESS;
    pads[0].buttons = translate(raw);
    pads[0].lx      = raw.lx;
    pads[0].ly      = raw.ly;

    xpad_fold_stick(&pads[0], raw.lx, raw.ly, 40);

    xpad_commit(&state);
}
```

Providers should keep whatever `joyvec` or scancode injection they
already do running alongside, so existing software is unaffected.

## Publishing and withdrawing

`xpad_publish()` installs the `XPAD` cookie, or repoints it when one is
already there, and returns 1 on success. It appends at the jar's
terminator, whose second word is the number of free slots, so it returns
0 when the jar is full: enlarge the jar first. Call it from a TSR before
going resident, and keep the block itself resident, since the cookie
holds its address and consumers read it for the life of the session.

`xpad_unpublish()` removes the cookie, shuffles the entries after it
down, and returns the freed slot to the terminator's count. A provider
that installs and removes repeatedly therefore leaves the jar as it
found it.

Both use `Supexec` internally and are safe to call from user mode.

## Rumble and LEDs

The request area is the one part of the block a consumer writes. It is
optional: `XPAD.req` is NULL when the provider offers none. Reach it
through `xpad_req()` rather than through the field:

```c
XPAD_REQ *req = xpad_req(pads);     /* NULL when unavailable */

if (req && (pads->caps & XPAD_CAP_RUMBLE))
{
    req->rumble[0][0] = 200;        /* low frequency motor  */
    req->rumble[0][1] = 0;          /* high frequency motor */
    req->seq++;                     /* last, so the provider sees it */
}
```

`xpad_req()` returns NULL unless the provider's area is at least as
large as the consumer's header describes. Without that check a consumer
built against a later revision would write fields an older provider
never allocated: the request area is the only direction in this design
where a consumer can corrupt a provider, so it is the one place worth a
guard.

A non-NULL return means the memory is there, not that anything acts on
it. `caps` says what the provider honours. Bump `seq` after writing, and
last, so a provider that samples the area mid-update still sees a
consistent set of values on the next pass.

## Tearing

`pads` is double buffered. The provider fills the inactive buffer, then
bumps `seq` and writes `active`, which is one byte and therefore atomic
on 68000. A consumer reads `active`, copies, then rechecks `active` and
`seq`. Two buffers are enough because a consumer that takes longer than
a refresh interval to copy twelve bytes has larger problems; the recheck
exists to catch the pathological case rather than the normal one.

A provider must bump `seq` **before** flipping `active`. The consumer's
recheck relies on that order: flip first and a consumer can accept a
copy it took across a refresh. `xpad_commit()` does both in the right
order, so a provider that uses it gets this for free.

The two buffers sit `pad_count * pad_size` apart, not `XPAD_MAX_PADS`
apart, so a block reporting fewer pads than the maximum leaves the tail
of the array unused. `XPAD_PAD_AT()` and `xpad_back()` both derive that
stride, which is what keeps provider and consumer from disagreeing about
where buffer 1 begins.

## Conventions

Axes are signed `-127..127`, screen oriented: `+x` right, `+y` down.
Triggers are unsigned `0..255`.

Providers report the **physical position** of a face button, not its
printed legend, so a Nintendo-style pad still sets `XPAD_SOUTH` for its
lower face button.

Bit order and legend aliases follow `linux/input-event-codes.h`, so this
maps one to one onto the kernel's gamepad codes. That includes one trap
worth knowing: the kernel aliases `BTN_X` to **north** and `BTN_Y` to
**west**, which is the reverse of a physical Xbox pad and of the W3C
Gamepad API standard mapping, where index 2 is west and 3 is north.
`XPAD_X` and `XPAD_Y` reproduce the kernel behaviour faithfully.

Use the positional names in new code. Reach for `XPAD_X` or `XPAD_Y`
only when you are deliberately mirroring kernel-side definitions, and
expect to swap them when bridging to anything Gamepad API shaped.

Providers apply a deadzone and fold stick direction into the d-pad bits,
so a consumer that only wants digital directions can ignore the analogue
fields entirely. `xpad_fold_stick()` does this with a radial test.

## Forward compatibility

The header carries `hdr_size`, `pads_offset`, `pad_size` and
`pad_count`, and consumers locate pads through `XPAD_PAD_AT()` rather
than indexing the array. A later revision can therefore grow `XPAD_PAD`
or add header fields without breaking binaries already in the wild.
`xpad_read()` zeroes anything beyond the provider's `pad_size`, so a new
field reads as zero on an old provider rather than as garbage.

Button bit values are frozen. A later revision adds new buttons at bits
17 and up and never reorders, renumbers or reuses an existing bit, so an
unknown button reads as zero on an older provider the same way a new
field does.

The minor version may change freely. The major version changes only if
the layout rules above are broken, which is what `xpad_find()` guards
against.

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
port implement only step 1 and get the rest for nothing. Two such
drivers ship here, covering a plain joystick and the keyboard. Step 2,
the STE enhanced ports, is still to write, and so is step 3's trick of
borrowing joystick 1's fire bit as a second button: the joystick driver
publishes each port as its own pad rather than merging them.

## Example drivers

**The drivers in `src/drivers` are examples.** They exist to be read and
copied, not because they are the best possible drivers for the hardware
they touch. Each one shows the whole shape of a provider in as little
code as possible: publish a block, hook something, fill the back buffer,
commit. If you are writing a driver for a transport of your own, start
from whichever is closest and replace the middle.

They are also meant to genuinely work, because an example that does not
teaches the wrong things. But every one of them is limited, and the
limits are listed below rather than left for you to discover.

### joystick, via joyvec

Publishes both joystick ports as two pads, each with the d-pad and
`XPAD_SOUTH`. Works on any ST ever made, which makes it the floor the
ladder above rests on. Because MD/Sidepad synthesises `joyvec` packets
to inject a Bluetooth controller, this picks that up too, with no
change on its side.

It publishes two pads partly because the packet carries both bytes
anyway, and partly because a single pad provider never exercises the
buffer stride: the two buffers sit `pad_count` entries apart, so they
only overlap the declared array when `pad_count` is `XPAD_MAX_PADS`.
A two pad example is the smallest one that would notice if that ever
broke.

- **One button.** The IKBD reports a single fire bit per port. This is
  not a shortcut in the driver; there is no second bit to read.
- **No analogue.** `XPAD_CAP_ANALOG` is not claimed, and the sticks and
  triggers read zero.
- **No presence detection.** An ST cannot tell whether a joystick is
  plugged in, so both slots always report connected, plugged in or not.
- **Pad 0 usually sits idle.** Port 0 is normally the mouse, so unless
  something is driving it, only pad 1 will ever move.
- **`seq` advances on change**, not per frame, because that is when the
  IKBD reports. A still joystick is not a stalled provider.
- **It can be cut out.** A program that installs a `joyvec` handler and
  does not chain will stop this driver seeing packets. Nothing can be
  done about that from this side.

### keyboard, via kbdvec

Publishes the keyboard as one pad, using DOOM's default controls: arrows
to the d-pad, Ctrl to fire, Space to use, Alt to strafe, Shift to run,
comma and period to the shoulders, Tab to select and Esc to start.

- **Needs TOS 2.0 or later**, and refuses to install below it. Knowing
  which keys are *held* needs make and break codes, and only `kbdvec`,
  four bytes below `Kbdvbase()`, carries releases. Older TOS has no such
  vector, and reaching them through `ikbdsys` does not work: reading the
  ACIA data register destroys the status bits, so a driver cannot both
  look at a byte and let TOS have it. EmuTOS reports 2.06, so testing
  under an emulator will not warn you about this.
- **No analogue**, as above.
- **Five buttons are unmapped**: `XPAD_TL2`, `XPAD_TR2`, `XPAD_MODE`,
  `XPAD_THUMBL` and `XPAD_THUMBR`. DOOM has no key that means them, and
  inventing controls would make the example lie about the hardware.
- **The mapping is fixed.** There is no configuration. Change the table
  in `keymap.h` and rebuild.
- **A keyboard is not a pad.** Keyboard matrices limit which
  combinations of keys register together, so some multi-key holds a game
  would expect from a controller will not all arrive.

### What neither driver does

- **Rumble or LEDs.** Both pass NULL for `req`, so `xpad_req()` returns
  NULL and neither claims `XPAD_CAP_RUMBLE` or `XPAD_CAP_LED`.
- **More than two pads**, though the ABI carries four. The keyboard
  driver publishes one, the joystick driver two.
- **Coexist with another provider.** Xpad is single provider, so both
  refuse to install when an `XPAD` cookie is already present rather than
  displace something better.

### Where they have been tested

Both run self tests on an emulated ST under Hatari, and both are
additionally exercised end to end: installed resident from `AUTO`, then
read by a separate program through the cookie jar.

**Neither has been run on real hardware.** That is the distance between
"passes its tests" and "works", and it is worth closing before trusting
either one in anger.

## Writing your own

The shape is the same whatever the transport:

```c
static XPAD block;

xpad_init(&block, pad_count, caps, "My provider 1.0", req_or_null);
xpad_publish(&block);

/* whenever your input source has something new */
{
    XPAD_PAD *pads = xpad_back(&block);

    pads[0].type    = XPAD_TYPE_GAMEPAD;
    pads[0].buttons = translate(whatever);

    xpad_commit(&block);
}
```

What the examples are really demonstrating, and what a driver of your
own should copy:

- **Always chain to the handler you displaced.** One that swallows
  events breaks the desktop and every existing program. Both drivers do
  this, and both self tests check for it explicitly.
- **Keep the translation free of TOS headers**, in its own file, so it
  can be tested on a host without an emulator. That is where the logic
  worth getting wrong lives.
- **Do not claim capabilities you do not honour.** `caps` is a promise
  to consumers, not a description of ambition.
- **Reach system variables below `$800` through `Supexec`.** The ST bus
  errors on user mode access down there, which catches the cookie jar at
  `$5a0` and vector surgery alike.
- **Fill the whole pad, or set the unchanging fields once at startup.**
  The back buffer holds what you wrote two commits ago, not zeroes.

## The viewer

`src/tools/xpadview.c` shows live state for whichever provider is
installed: one pad at a time, keys 1 to 4 to select. It is also the
reference consumer, written exactly as this document describes, so the
advice above cannot drift away from something that compiles.

Run it with `-d` and it publishes a demo provider first, so it works
with no hardware and nothing else installed. `-1` prints one frame as
plain text and exits, which is what makes it testable without a person
watching.

Its limits: the live display has only been checked in an emulator at 80
columns, so the 40 column layout that low resolution gives you is
unverified, and buttons are labelled by position rather than by the
letters printed on a pad, deliberately. See the X/Y trap above.

## Integrating into a port

Copy `src/xpad.h` and `src/xpad.c` into the port. If you are writing a
provider rather than consuming one, take `src/xpad_provider.c` too:
`xpad.c` is the consumer half, and `xpad_provider.c` adds the helpers
for owning and publishing a block.

The split is there because there is no section garbage collection on
`m68k-atari-mint`, so the linker's unit is the object file. A game that
linked one file with both halves in it would carry the provider helpers
into every binary without ever calling them: 684 bytes against 1252,
measured, which is real money on a 512K machine.

At around 500 lines they are not worth a submodule, particularly inside
a Docker cross-compilation build. `XPAD_VERSION` makes drift visible if
it ever matters.

Because the licence is BSD-2-Clause, a port that ships a binary must
reproduce the copyright notice in its accompanying documentation. One
line in the port's own README or docs covers it:

```
Controller input via Xpad, Copyright (c) 2026 Neil Rackett,
BSD-2-Clause. See XPAD.TXT.
```

Easy to overlook, so handle it as part of the initial integration
rather than at release time.

## Naming

Deliberately not named after any one provider. A MIDI or parallel port
adapter, a mouse-port adapter or any other hardware can fill the
same block, and software that reads it gains support it never wrote.
`XPAD.provider` carries a display string if a game wants to report what
it found.

## Licence

BSD-2-Clause. Chosen so that any provider or consumer can adopt it
regardless of its own licence, which matters more for an interface than
for an implementation. In particular it stays compatible with GPL-2-only
codebases, which several Atari ST ports are.

Providers and tools built on Xpad are free to be copyleft. Only this
header and its reference implementation, which get compiled into other
people's programs, need to be permissive.
