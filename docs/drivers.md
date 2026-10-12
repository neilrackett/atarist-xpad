<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# Example drivers

[← Back to the README](../README.md)

**The drivers in `src/drivers` are examples.** They exist to be read and
copied, not because they are the best possible drivers for the hardware
they touch. Each one shows the whole shape of a provider in as little
code as possible: publish a block, hook something, fill the back buffer,
commit. If you are writing a driver for a transport of your own, start
from whichever is closest and replace the middle.

They are also meant to genuinely work, because an example that does not
teaches the wrong things. But every one of them is limited, and the
limits are listed below rather than left for you to discover.

| Name | Description | Device |
| ---- | ----------- | ------ |
| [STEpad](../src/drivers/stepad) | Both enhanced joystick ports as two pads, reading a real Jaguar style gamepad. | STE, Falcon |
| [joymouse](../src/drivers/joymouse) | Joystick 1 as one pad, with the left mouse button as button 2. | ST, Mega ST, STE, Mega STE, TT, Falcon |
| [joystick](../src/drivers/joystick) | Both joystick ports as two pads, each with a d-pad and one button. | ST, Mega ST, STE, Mega STE, TT, Falcon |
| [keyboard](../src/drivers/keyboard) | The keyboard as one pad, using DOOM's default controls. | All, with TOS 2.0 or later: Mega STE, TT and Falcon ship with it, and an earlier ST or STE can be upgraded to it |
| [DevOps](../src/drivers/devops) | Up to four pads driven from the workstation, for unattended tests and remote play. | Any, with a SidecarTridge Multi-device running md-devops with its rom4 area |

## STEpad, via the STE's enhanced ports

Publishes both of an STE's 15 pin enhanced joystick ports as two pads,
each with the d-pad, three fire buttons and two menu buttons. This is
the only driver here that reaches a real gamepad on stock hardware: an
Atari Jaguar style pad plugs straight in, with no adapter, cartridge or
Bluetooth anywhere.

It is also the only one that polls. Nothing calls a driver when a
joypad button changes, because the ports are registers rather than an
interrupt source, so this one hooks `etv_timer` at `$400` rather than
the VBL, for the reason [AGENTS.md](../AGENTS.md) gives: a game is far
more likely to have taken the VBL for itself.

The ports are a scanned matrix. A row is selected by pulling one bit of
`$FF9202` low and both pads answer at once, pad 0 in the low nibble and
pad 1 in the high one, with `$FF9200` carrying a fire and a pause line
per pad. Four rows cover the whole pad. Everything is active low.

- **STE and Falcon only.** A plain ST bus errors on `$FF9200`, so `_MCH`
  is checked before anything is read and the driver refuses rather than
  taking the machine down. A TT has no enhanced ports either. Note that
  `$FF9200` must be read as a **word**: a byte read of it bus errors on
  STE and Mega STE.
- **A Mega STE is refused, and the reason is the interesting one.** It
  is an STE board in a Mega case, and the case has nowhere to plug a
  joypad in, but the registers are still there: `$FF9200` on a Mega STE
  reads the motherboard DIP switches in its upper byte. So a driver that
  gated on the machine *family* would find no bus error, install
  happily, and publish two pads that could never move. Since Xpad is
  single provider, that silently idle provider would be sitting where a
  working one could have been. The gate uses the whole `_MCH` value,
  because the low word is the sub model: `00010000` is an STE and
  `00010010` is a Mega STE.
- **The 12 key keypad is not mapped.** It is sampled, because it shares
  its rows with the fire line, and then ignored. Xpad v1 has no home for
  a keypad, new buttons would have to start at bit 17, and deciding to
  spend twelve of those is not an example's call to make. `decode.h`
  documents which key sits in which row and column for anyone who wants
  them.
- **A row is not a diamond.** A, B and C sit in a row on the pad and
  Xpad's face buttons are a diamond, so there is no mapping that
  preserves position the way the
  [X/Y rule](specification.md#conventions) asks for. A goes to
  `XPAD_SOUTH` because it is the primary button, then B to `XPAD_EAST`
  and C to `XPAD_WEST`. That is a convention, not a measurement.
- **No analogue.** `XPAD_CAP_ANALOG` is not claimed, and the sticks
  and triggers read zero. The same ports carry paddles at `$FF9211`
  and up, which would map onto the axes nicely, but a paddle is a
  different peripheral and belongs in a different example.
- **No presence detection.** The ports cannot say whether anything is
  plugged in, so both slots always report connected and an empty port
  reads as a pad with nothing pressed.
- **`seq` advances every tick**, not on change, because a poll cannot
  know a button moved without looking. Unlike the joystick driver, a
  still pad here still commits.

## joymouse, via joyvec and mousevec

Publishes joystick 1 as one pad, with its fire as `XPAD_SOUTH` and the
left mouse button as `XPAD_EAST`. Port 0's fire line is the left mouse
button, so button 2 is either a mouse in port 0 or a two button
joystick wired so its second button reaches pin 6 of port 0, such as a
modified Cheetah 125+. It is the joystick driver plus one thing, a pad
assembled from two vectors, and it links that driver's `joyvec.s` and
`translate.h` as they are.

The IKBD reports that wire in one of two ways, depending on the mode a
program has put it in: as the left button of a relative mouse packet,
or as joystick 0's fire in joystick mode. Both write the same variable
and the last report wins, so a game that switches modes while the
button is held cannot leave it stuck down.

- **Two buttons.** The right mouse button shares a line with joystick
  1's fire, so it could only repeat button 1, and is not read.
- **No analogue**, as above. Moving the mouse does nothing.
- **Absolute mouse mode is not read**, nor mouse buttons reported as
  keys. A game that sets either loses button 2 until it switches back.
- **No presence detection.** The pad always reports connected.
- **`seq` advances on every joystick packet**, but on a mouse packet
  only when button 2 changes, because a moving mouse reports
  constantly.
- **Not with XPADEMU.** It adds nothing, since the stick and the button
  already reach a game directly, and its injected joystick packets come
  back through this driver: button 2 becomes fire as well, and can stay
  down after it is released.
- **It can be cut out** by a program that takes `joyvec` or `mousevec`
  without chaining, as with the joystick driver.

## joystick, via joyvec

Publishes both joystick ports as two pads, each with the d-pad and
`XPAD_SOUTH`. Works on any ST ever made, which makes it the floor the
[fallback ladder](integrating.md#falling-back) rests on. Because
MD/Sidepad synthesises `joyvec` packets to inject a Bluetooth
controller, this picks that up too, with no change on its side.

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

## keyboard, via kbdvec

Publishes the keyboard as one pad, using DOOM's default controls: arrows
to the d-pad, Ctrl to fire, Space to use, Alt to strafe, Shift to run,
comma and period to the shoulders, Tab to select and Esc to start.

- **Needs TOS 2.0 or later**, and refuses to install below it. Knowing
  which keys are _held_ needs make and break codes, and only `kbdvec`,
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

## DevOps, from the workstation through md-devops

Publishes up to four pads whose state comes from the workstation, so a
script can play a game unattended or a person can drive one from across
the room. It needs a SidecarTridge Multi-device running md-devops with
its rom4 area: 1 KB of the cartridge window that the workstation writes
over HTTP and any ST program reads.

It is the only driver here that never reads a controller, and the only
one that does not stay resident, because the block lives in the
cartridge. `xpaddev.py`, beside the driver, writes a whole XPAD block
into the rom4 area and then commits pad state into it the way
`xpad_commit()` does: back buffer, then `seq`, then `active`, in one
HTTP request that md-devops applies in that order. The ST cannot write
the cartridge, so `XPADDEV.PRG` only checks that the block is there and
points the cookie at it.

```sh
python3 src/drivers/devops/xpaddev.py init        # once the SidecarTridge is up
python3 src/drivers/devops/xpaddev.py set south start --lx 100
python3 src/drivers/devops/xpaddev.py tap --ms 200 east
python3 src/drivers/devops/xpaddev.py show
python3 src/drivers/devops/xpaddev.py run steps.txt
```

`set` holds exactly what it is given and releases everything else on
that pad; `tap` presses, waits and lets go; `off` disconnects a pad;
`run` takes the same commands one per line, plus `wait MS`, from a file
or stdin. Buttons are named by position as in `XPADEMU.CFG`, sticks run
-127 to 127 and triggers 0 to 255, and the left stick is folded into
the d-pad with the usual radial deadzone. `--host` or `$SIDECART_HOST`
names the SidecarTridge, as for md-devops' own CLI. The top of
`xpaddev.py` has the details.

- **Needs md-devops with the rom4 area**, which is proposed to
  SidecarTridge but not yet in a release. Without it the driver
  refuses. Until it is, `XPADDEV.PRG` is not on Xpad's release either:
  build it with `STCMD_NO_TTY=1 stcmd make st`.
- **Run `init` before the ST boots.** The driver refuses an area with no
  block in it rather than publish a cookie that points at nothing,
  because a consumer looks once, at startup, and XPADEMU refuses to
  install without a provider. The area survives an ST reset, so after a
  SidecarTridge restart it is `init`, then reset the ST.
- **No rumble or LEDs.** `req` is NULL: a consumer cannot write a
  request into a cartridge.
- **A SidecarTridge restart empties the area**, and a consumer still
  holding the block then reads four disconnected pads, which is what a
  provider going away is meant to look like.
- **Each change is an HTTP request**, so input arrives at network pace,
  not frame by frame. `run` keeps the block's state itself and spends
  one request per step.
- **One writer at a time.** Each `xpaddev.py` assumes it is the only
  one.
- **The first 160 bytes of the area are the block's.** Anything else
  using rom4 has to stay clear of them.
- **The Mega STE's cache is unknown territory.** md-drives-emulator
  found that with the cache on a Mega STE answers cartridge reads from
  the cache, so a consumer polling this block could see a stale pad.
  Nobody has checked yet. If it does, the fix is a copy in RAM
  refreshed with the cache off, and the workstation side would not
  change.
- **`seq` advances once per change**, not per frame. A pad left alone
  is not a stalled provider.

## What none of the drivers does

- **Rumble or LEDs.** All five pass NULL for `req`, so `xpad_req()`
  returns NULL and none claims `XPAD_CAP_RUMBLE` or `XPAD_CAP_LED`.
- **Coexist with another provider.** Xpad is single provider, so all
  five refuse to install when an `XPAD` cookie is already present
  rather than displace something better.

The keyboard and joymouse drivers publish one pad each, the joystick
and STE drivers two each, and the DevOps driver four, the most the ABI
carries.

## Where they have been tested

All five run self tests under Hatari, the STE driver on an emulated
STE and Mega STE. The joystick, joymouse and keyboard drivers are
additionally exercised end to end: installed resident from `AUTO`, then
read by a separate program through the cookie jar.

The DevOps driver's self test reads an image of the area that
`xpaddev.py` wrote, so the ST's own consumer code checks the
workstation's bytes, and `test/devops.py` checks the tool on the host,
over HTTP against a stand-in for md-devops. What the harness cannot do
is attach a cartridge: Hatari turns its cartridge off for the GEMDOS
drive the harness uses, and its `--auto` replaces it. So the path
through a real cartridge, from `AUTO` to a separate consumer, has been
run by hand once, from a floppy with a cartridge image, and is not
part of `verify.sh`.

**None has been run on real hardware.** That is the distance between
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
  events breaks the desktop and every existing program. Every driver
  here that takes a vector does this, and its self test checks for it
  explicitly.
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

The rules every provider follows, from publishing to the order of a
commit, are in [the specification](specification.md), and adding Xpad
to your own build is in [Integrating into a port](integrating.md).
