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
port implement only step 1 and get the rest for nothing.

Two of these exist:

- `src/drivers/joystick` hooks `joyvec` and publishes joystick 1 as one
  pad: four directions and one button, which is everything the IKBD
  reports for a port. It works on any ST ever made, so it is the floor
  the ladder above rests on.
- `src/drivers/keyboard` hooks `kbdvec` and publishes the keyboard as
  one pad, using DOOM's default controls. It needs TOS 2.0 or later,
  because older TOS cannot report key releases.

Both chain to the handler they displace, so an existing joystick and the
keyboard carry on working normally. Step 2 of the ladder, the STE
enhanced ports, is still to write.

`src/tools/xpadview.c` shows live state for whichever provider is
installed, and doubles as the reference consumer: it is written exactly
as this document describes, so it cannot drift from the advice. Run it
with `-d` to publish a demo provider and watch the viewer work with no
hardware at all.

## Integrating into a port

Copy `src/xpad.h` and `src/xpad.c` into the port. At around 500 lines between
them they are not worth a submodule, particularly inside a Docker
cross-compilation build. `XPAD_VERSION` makes drift visible if it ever matters.

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
