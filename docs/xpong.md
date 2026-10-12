<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# Xpong: the example game

[← Back to the README](../README.md)

[`examples/xpong`](../examples/xpong) is Xpong: Pong for one player
against the computer, or two against each other, on a pad each or
sharing one. It is a port of MD/Sidepad's SIDEPONG, which hooked the
IKBD's joystick and mouse vectors; Xpong reads nothing but Xpad, so it
plays the same on any provider. Two joysticks through `XPADJOY` give
two players, the keyboard gives one against the computer, and COMpad
gives up to four pads with rumble.

## Playing

| Control        | Does                                                              |
| -------------- | ----------------------------------------------------------------- |
| up, down       | move your paddle; an analogue pad uses its left stick, as far as it is pushed |
| right stick    | sharing a pad, the right player's paddle; north and south without analogue |
| south, start   | choose on the menus; start pauses a match                         |
| select         | back: from a match to the menu, from the menu to the desktop      |
| Escape         | the same as select, which is the way out with a joystick          |

The menu offers three matches: one player against the computer, two
players with a pad each, and two players sharing one pad, left stick
against right. Whoever chooses plays on the left. For two players with
a pad each, the second is whichever other pad presses fire first, so it
makes no difference which slots the pads landed in. A match pauses
itself if a player's pad goes away, and stays paused until it is back
and start is pressed. Every serve starts in the middle of the court.

Sharing a pad without analogue sticks, the left player has the d-pad
and the right player the north and south face buttons. South is then
somebody's down button, so only start plays again after a match.

Rumble, where the provider offers it: a tap on the high frequency motor
when you return the ball, a thump on the low frequency one when you
lose a point, and both when you lose the match. Each player feels only
their own events, on their own pad. Sharing a pad, each feels them only
in their own grip: the left player on the low frequency motor, which is
the heavy one in the left grip, and the right player on the high
frequency one in the right.

## What it shows

It is written to be copied, and it shows the parts of a consumer the
viewer does not:

- **Edge detection.** Xpad deliberately has no helper for it.
  `read_pads()` keeps last frame's buttons and works out what went
  down, in a few lines.
- **Analogue, with a digital fallback.** `pong_pad_speed()` checks
  `XPAD_PAD_ANALOG` rather than assuming a stick. COMpad reports a plain
  gamepad until the adapter has described the pad, and the folded d-pad
  bits keep its stick working until then.
- **Rumble that always stops.** The request area carries a strength and
  no duration, so every pulse is a start and a stop. `rumble.h` keeps
  track of both, for each motor of any number of pads at once, and
  stops everything on pause and on exit.
- **A pad going away.** A provider marks it `XPAD_TYPE_NONE`, which is
  how the game knows to pause.

## How it is tested

The rules, in `pong.h`, and the rumble bookkeeping, in `rumble.h`, are
free of TOS and tested on the host, and so is the decision of who feels
what. That includes the computer opponent, which is tuned there by
playing it against itself and against simulated players. A perfect
player who never misses never concedes a point, so it has no shot that
cannot be returned, and one who plays the angles usually wins. One
modelled on a person, who sees the ball a fifth of a second late and
aims roughly, wins about one match in five.

`XPONG -d` publishes a demo provider and plays a scripted session: the
menus, a two player match, a pause, a pad going away and coming back, a
match against the computer, and a match on one shared pad. It then
checks that each pad, and each stick of the shared one, moved its
paddle, that each player was rumbled on their own pad or in their own
grip, that every motor stopped, and that the game kept up with the
display. `make hatari-xpong` runs it. It refuses when a
provider is already installed, because publishing over one and then
withdrawing would leave that provider unreachable until a reset.

## Drawing

It redraws only what changed, for the same reason the viewer does:
clearing and redrawing the whole screen every frame ran at about half
the display's rate on an emulated ST. The court and its text are drawn
again only when they change, and the paddles and the ball are drawn
with exclusive OR, so they come off again without disturbing what was
under them.

## Limits

Its limits: low resolution only, so it needs a colour monitor. The
computer is tuned against simulated players, not people. The self test
runs its provider in the same process, and nothing automated can play
the real game against a driver resident from `AUTO`, since that needs
somebody on the pad.
