<!-- SPDX-License-Identifier: BSD-2-Clause -->
<!-- SPDX-FileCopyrightText: 2026 Neil Rackett -->

# XPADVIEW: the viewer

[← Back to the README](../README.md)

[`src/tools/xpadview.c`](../src/tools/xpadview.c) shows live state for
whichever provider is installed: one pad at a time. It is also the
reference consumer, written exactly as
[the specification](specification.md) describes, so its advice cannot
drift away from something that compiles.

| Key     | Does                                                      |
| ------- | --------------------------------------------------------- |
| `1`-`4` | select which pad is shown                                 |
| `(`     | rumble the selected pad's left motor for half a second    |
| `)`     | the same, right motor                                     |
| `*`     | the same, both                                            |
| `Q`     | quit, and so does Escape                                  |

The three rumble keys are the keypad's top row, laid out the way the
motors are. Driving them apart is the point: a pad with one dead motor,
or a provider that has the pair the wrong way round, feels almost right
when both go at once.

They are the only thing the viewer writes, and the worked example of
the request area: set the magnitudes, bump `seq`, then write zeroes
back half a second later, because the area carries strength and no
duration. Quitting mid-pulse writes them back too, since a provider
holds a rumble until something replaces it. Providers that offer no
request area, which is all of the example drivers, say so on screen
rather than doing nothing.

Keys are matched on the ASCII byte and on the scancode, so the key with
`Q` printed on it quits whatever national keyboard table the machine's
TOS carries.

Run it with `-d` and it publishes a demo provider first, so it works
with no hardware and nothing else installed. `-1` prints one frame as
plain text and exits, which is what makes it testable without a person
watching.

It is idle when nothing changes: each frame compares what the provider
published with what is on screen and draws nothing if they match.
Buttons redraw the moment they change. Sticks and triggers redraw at
most ten times a second, and only the numbers that moved, because a
real stick at rest jitters by a count or two and the TOS console is
slow enough for that to matter. Timing comes from the 200 Hz system
timer, so a rumble pulse is half a second however busy the screen is.

Its limits: the live display has been checked at 80 columns, in an
emulator and on a Mega STE, but the 40 column layout that low
resolution gives you is unverified, and buttons are labelled by position
rather than by the letters printed on a pad, deliberately. See the X/Y
trap in [the specification's conventions](specification.md#conventions).
