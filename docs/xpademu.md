<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# XPADEMU: your pad as a joystick and mouse

[← Back to the README](../README.md)

[`src/tools/xpademu.c`](../src/tools/xpademu.c) is the other direction
from the drivers, and the one most people will actually run. It
publishes nothing and owns nothing: it reads whatever `XPAD` block is
installed and injects real IKBD packets, so an unmodified game from 1987
sees a joystick on port 1 and GEM sees a mouse. Whatever put the block
in the jar, a Bluetooth pad through COMpad, a Mega Drive pad through
MD/Sidepad, an STE joypad, the keyboard shim, comes out the far end as
input that needs no port at all.

It installs from `AUTO` after a provider, reads `XPADEMU.CFG` from the
root of the boot drive if there is one, and stays resident on
`etv_timer`. The real vectors are left in place, so a physical joystick
on port 1 keeps working and whichever one you move drives the game.

## Settings

| Key | Default | Takes |
| --- | --- | --- |
| `pad` | `0` | which pad drives joystick 1 |
| `fire` | *everything spare* | buttons that count as fire |
| `autofire` | `west` | buttons held for repeating fire |
| `autorate` | `8` | shots per second, 1 to 25 |
| `jump` | `none` | a button that reads as up |
| `mouse` | `on` | `off` disables mouse emulation |
| `mousepad` | `0` | whose right stick drives the mouse |
| `left` | `thumbr` | buttons for left click |
| `right` | *none* | and for right click |
| `deadzone` | `40` | below this the stick is at rest |
| `speed` | `6` | pointer movement per unit of deflection |

**Fire is whatever is left over.** Only autofire, jump and the mouse
buttons have a named job by default, and every other button becomes
fire, so the pad has no dead ones: whichever you press, it shoots. A
joystick has one button and a pad has thirteen, so allocating two and
leaving eleven inert would be the wrong way round. Set `fire` yourself
and it is taken literally instead. Directions are never handed out as
fire however it is set, or walking left would shoot.

Buttons are named by position, combined with `+`: `south` `east`
`north` `west` `tl` `tr` `select` `start` `thumbl` `thumbr` `none`,
with `a` and `b` accepted for south and east. There is deliberately no
`x` or `y`, because `XPAD_X` is north and `XPAD_Y` is west and a config
file has nowhere to print the warning `xpad.h` does. The
[specification's conventions](specification.md#conventions) explain why.

The file is read once, at install. The tick runs in interrupt context
where GEMDOS is not reentrant, so nothing here can be re-read while
resident.

## How it works

The mechanism is ported from the assembly injector in
[MD/Sidepad](https://github.com/neilrackett/md-sidepad), which worked
it out on real hardware. Three parts of it are not obvious:

- **The vectors are read live**, out of `KBDVECS`, on every injection.
  A game installs its own `joyvec` *after* a resident program, so a
  pointer saved at install time points at whatever the game replaced
  and the game hears nothing.
- **The IKBD interrupt is masked** across the call. Driving `joyvec`
  leaves the ACIA enabled, so a real packet arriving mid-call would
  re-enter the handler on top of itself.
- **The `0xFF` header sits at an odd address**, because consumers read
  a word from header+1 to get both joystick bytes aligned, and a 68000
  address errors otherwise.

## Limits

Its limit is worth stating plainly: a game that drives the IKBD ACIA at
`$FFFC00` itself, rather than letting TOS deliver its input, never sees
any of this. You cannot write to a receive register, so there is no
software fix for that tier.

On real hardware, driven by a Bluetooth pad through COMpad, the mouse
and every MD/Sidepad demo work as intended. Most original games tried
from floppy do not, which is that limit showing: they read the IKBD
themselves.

It is not for use with the
[joymouse driver](drivers.md#joymouse-via-joyvec-and-mousevec). It adds
nothing there, since that driver's joystick and mouse button already
reach a game directly, and the joystick packets it injects come back
through that driver: button 2 becomes fire as well, and can stay down
after it is released.
