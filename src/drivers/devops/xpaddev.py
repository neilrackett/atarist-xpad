#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

"""Drive Xpad pads on an Atari ST from the workstation, through md-devops.

md-devops, a SidecarTridge Multi-device microfirmware, keeps 1 KB of the
cartridge window that the workstation writes over HTTP and any ST program
reads: its rom4 area. This writes an XPAD block at the start of the area,
then commits pad state into it the way xpad_commit() does on an ST. With
XPADDEV.PRG in the ST's AUTO folder the XPAD cookie points at the block,
so every Xpad consumer on the ST reads these pads.

    xpaddev.py init                      write the block, pad 0 idle
    xpaddev.py set [-p N] [BUTTON...] [--lx N ...]
                                         hold exactly this, nothing else
    xpaddev.py tap [-p N] [--ms MS] BUTTON...
                                         press, wait, let go
    xpaddev.py off [-p N]                disconnect a pad
    xpaddev.py show                      the pads as the ST reads them
    xpaddev.py run [FILE]                commands from a file, or stdin

Run init once after the SidecarTridge starts, then reset the ST: the area
survives an ST reset, and XPADDEV refuses an area with no block in it.

Buttons are named by position, as in XPADEMU.CFG: up down left right
south east north west tl tr tl2 tr2 select start mode thumbl thumbr, with
a and b accepted for south and east. There is deliberately no x or y, for
the reason xpad.h gives. Separate them with spaces or +.

Sticks run -127..127 with +y down, triggers 0..255. The left stick is
folded into the d-pad with xpad_fold_stick()'s radial deadzone of 40, as
a provider should; the right stick is not, since it is usually the aim.

run takes one command per line, written as on the command line without
the program name, plus `wait MS`. # starts a comment. It keeps the block's
state itself, so each step is a single HTTP request:

    set start
    wait 100
    set
    tap --ms 200 south

The host is --host, else $SIDECART_HOST, else sidecart.local, as for
md-devops' own sidecart.py. --file IMAGE works on a local copy of the
area instead of a SidecarTridge, which is how the tests drive this.

Run one at a time: each process assumes it is the only writer.
"""

import argparse
import json
import os
import shlex
import struct
import sys
import time
import urllib.error
import urllib.request
from dataclasses import astuple, dataclass, replace

# ------------------------------------------------------------------ #
# From src/xpad.h. test/devops.py checks every one against the header.
# ------------------------------------------------------------------ #

XPAD_MAGIC = 0x58504144
XPAD_VERSION = 0x0100
XPAD_MAX_PADS = 4
XPAD_PAD_SIZE_V1 = 12
XPAD_HDR_FIXED = 18

XPAD_CAP_ANALOG = 0x0001
XPAD_CAP_HOTPLUG = 0x0008

XPAD_TYPE_NONE = 0
XPAD_TYPE_GAMEPAD = 2
XPAD_PAD_ANALOG = 0x01

BUTTONS = {
    "up": 0x00000001,
    "down": 0x00000002,
    "left": 0x00000004,
    "right": 0x00000008,
    "south": 0x00000010,
    "east": 0x00000020,
    "north": 0x00000040,
    "west": 0x00000080,
    "tl": 0x00000100,
    "tr": 0x00000200,
    "tl2": 0x00000400,
    "tr2": 0x00000800,
    "select": 0x00001000,
    "start": 0x00002000,
    "mode": 0x00004000,
    "thumbl": 0x00008000,
    "thumbr": 0x00010000,
}

ALIASES = {"a": "south", "b": "east"}

# ------------------------------------------------------------------ #
# md-devops' rom4 area
# ------------------------------------------------------------------ #

ROM4_MAGIC = 0x52344131  # 'R4A1', XPAD_DEVOPS_MAGIC in locate.h
ROM4_ADDRESS = 0xFA8B00  # where md-devops puts it, and what --file assumes
AREA_SIZE = 1024

# ------------------------------------------------------------------ #
# The block, as an m68k lays it out
# ------------------------------------------------------------------ #

# The fixed header is the same on every architecture. After it come two
# pointers, four bytes each and two-aligned on an m68k, so the offsets
# from here on are the ST's and not what offsetof() gives on a PC.
OFF_MAGIC = 0
OFF_SEQ = 14
OFF_ACTIVE = 17
OFF_PROVIDER = XPAD_HDR_FIXED
OFF_REQ = OFF_PROVIDER + 4
OFF_PADS = OFF_REQ + 4

# Room for every pad the ABI allows, so a second pad needs no new block:
# pads_offset, pad_size and pad_count may never change under a consumer.
PAD_COUNT = XPAD_MAX_PADS
HDR_SIZE = OFF_PADS + 2 * PAD_COUNT * XPAD_PAD_SIZE_V1

PROVIDER = b"DevOps workstation"
PROVIDER_OFFSET = 0x80
PROVIDER_SIZE = 32
SPAN = PROVIDER_OFFSET + PROVIDER_SIZE  # XPAD_DEVOPS_SPAN in locate.h

# Analogue because the sticks and triggers are set here; hotplug because
# off disconnects a pad. No rumble or LED: req is NULL, since a consumer
# cannot write a request into a cartridge.
CAPS = XPAD_CAP_ANALOG | XPAD_CAP_HOTPLUG
DEADZONE = 40

PAD = struct.Struct(">IbbbbBBBB")


class Failure(Exception):
    """Something the user needs to hear about, in one line."""


@dataclass
class Pad:
    buttons: int = 0
    lx: int = 0
    ly: int = 0
    rx: int = 0
    ry: int = 0
    lt: int = 0
    rt: int = 0
    type: int = XPAD_TYPE_NONE
    flags: int = 0

    def pack(self):
        return PAD.pack(*astuple(self))


CONNECTED = {"type": XPAD_TYPE_GAMEPAD, "flags": XPAD_PAD_ANALOG}


def fold_stick(x, y, threshold=DEADZONE):
    """xpad_fold_stick(): the d-pad bits a stick at (x, y) holds."""
    ax, ay = abs(x), abs(y)
    bits = 0

    # Radial, so a diagonal past the deadzone counts even when neither
    # axis on its own would.
    if ax * ax + ay * ay < threshold * threshold:
        return 0

    if ax > ay >> 1:
        bits |= BUTTONS["left"] if x < 0 else BUTTONS["right"]

    if ay > ax >> 1:
        bits |= BUTTONS["up"] if y < 0 else BUTTONS["down"]

    return bits


def parse_buttons(words):
    mask = 0

    for word in words:
        for name in word.lower().split("+"):
            name = ALIASES.get(name, name)
            if name not in BUTTONS:
                raise Failure("no button called %r; try %s"
                              % (name, " ".join(BUTTONS)))
            mask |= BUTTONS[name]

    return mask


def button_names(mask):
    return "+".join(n for n, bit in BUTTONS.items() if mask & bit) or "-"


# ------------------------------------------------------------------ #
# Where the bytes go
# ------------------------------------------------------------------ #


def encode_frames(frames):
    """md-devops' PUT body: [u16 offset][u16 length][bytes], big-endian.

    md-devops writes the frames in order, each from its lowest address
    up, and every 16-bit word in one store, so the order here is the
    order the ST sees.
    """
    return b"".join(struct.pack(">HH", off, len(data)) + data
                    for off, data in frames)


def base_url(host):
    if "://" in host:
        return host.rstrip("/")
    return "http://%s" % host


class HttpArea:
    """The rom4 area on a SidecarTridge, through md-devops' HTTP API."""

    def __init__(self, host):
        self.base = base_url(host)

    def _request(self, method, path, body=None):
        headers = {"User-Agent": "xpaddev"}
        if body is not None:
            headers["Content-Type"] = "application/octet-stream"
        req = urllib.request.Request(self.base + path, data=body,
                                     headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                return resp.read()
        except urllib.error.HTTPError as exc:
            if exc.code == 404:
                raise Failure("%s has no rom4 area: it needs md-devops "
                              "with rom4" % self.base)
            detail = exc.read().decode("utf-8", "replace").strip()
            raise Failure("%s %s answered %d %s"
                          % (method, path, exc.code, detail))
        except OSError as exc:  # URLError is one
            raise Failure("cannot reach %s: %s"
                          % (self.base, getattr(exc, "reason", exc)))

    def address(self):
        info = json.loads(self._request("GET", "/api/v1/rom4"))
        if info.get("magic") != ROM4_MAGIC or info.get("size", 0) < SPAN:
            raise Failure("%s's rom4 area is not one this understands"
                          % self.base)
        return info["address"]

    def read(self):
        return self._request("GET", "/api/v1/rom4/data")

    def write(self, frames):
        self._request("PUT", "/api/v1/rom4", encode_frames(frames))


class FileArea:
    """A local copy of the area, byte for byte what the ST reads."""

    def __init__(self, path, address):
        self.path = path
        self.at = address
        try:
            with open(path, "rb") as f:
                data = f.read(AREA_SIZE)
        except FileNotFoundError:
            data = b""
        self.data = bytearray(data.ljust(AREA_SIZE, b"\0"))

    def address(self):
        return self.at

    def read(self):
        return bytes(self.data)

    def write(self, frames):
        for off, data in frames:
            self.data[off:off + len(data)] = data
        with open(self.path, "wb") as f:
            f.write(self.data)


# ------------------------------------------------------------------ #
# The block
# ------------------------------------------------------------------ #


class Block:
    """What the block holds: seq, active and the buffer consumers read."""

    def __init__(self, area):
        self.area = area
        self.seq = 0
        self.active = 0
        self.pads = []

    def init(self):
        address = self.area.address()

        self.seq = 0
        self.active = 0
        self.pads = [Pad(**CONNECTED)]
        self.pads += [Pad() for _ in range(PAD_COUNT - 1)]

        pads = b"".join(p.pack() for p in self.pads)
        header = struct.pack(">HHHHHHBBII", XPAD_VERSION, HDR_SIZE,
                             OFF_PADS, XPAD_PAD_SIZE_V1, CAPS, 0,
                             PAD_COUNT, 0, address + PROVIDER_OFFSET, 0)

        # The magic last. A consumer that looks while this is going in
        # finds no block rather than a magic in front of a layout that
        # is not there yet.
        self.area.write([
            (OFF_MAGIC + 4, header + pads + pads),
            (PROVIDER_OFFSET, PROVIDER.ljust(PROVIDER_SIZE, b"\0")),
            (OFF_MAGIC, struct.pack(">I", XPAD_MAGIC)),
        ])

        return address

    def load(self):
        if self.pads:  # init or an earlier load already has it
            return

        data = self.area.read()
        (magic, version, hdr_size, pads_offset, pad_size, _caps, seq,
         pad_count, active) = struct.unpack_from(">IHHHHHHBB", data)

        if (magic != XPAD_MAGIC or version >> 8 != XPAD_VERSION >> 8
                or hdr_size != HDR_SIZE or pads_offset != OFF_PADS
                or pad_size != XPAD_PAD_SIZE_V1 or pad_count != PAD_COUNT
                or active > 1):
            raise Failure("no xpaddev block in the rom4 area: run init")

        front = OFF_PADS + active * PAD_COUNT * XPAD_PAD_SIZE_V1
        self.seq = seq
        self.active = active
        self.pads = [Pad(*PAD.unpack_from(data, front + i * PAD.size))
                     for i in range(PAD_COUNT)]

    def commit(self):
        """xpad_commit(): the back buffer, then seq, then active.

        All in one request, which md-devops writes in that order, so a
        consumer's recheck of seq and active means what it says.
        """
        back = self.active ^ 1
        seq = (self.seq + 1) & 0xFFFF

        self.area.write([
            (OFF_PADS + back * PAD_COUNT * XPAD_PAD_SIZE_V1,
             b"".join(p.pack() for p in self.pads)),
            (OFF_SEQ, struct.pack(">H", seq)),
            (OFF_ACTIVE, bytes([back])),
        ])

        self.seq = seq
        self.active = back


# ------------------------------------------------------------------ #
# Commands
# ------------------------------------------------------------------ #


def cmd_init(block, args):
    address = block.init()
    print("Xpad block at $%06X: pad 0 connected, %d pads"
          % (address, PAD_COUNT))


def cmd_set(block, args):
    block.load()
    block.pads[args.pad] = Pad(
        parse_buttons(args.buttons) | fold_stick(args.lx, args.ly),
        args.lx, args.ly, args.rx, args.ry, args.lt, args.rt, **CONNECTED)
    block.commit()


def cmd_tap(block, args):
    block.load()
    held = block.pads[args.pad]
    pressed = replace(held, buttons=held.buttons | parse_buttons(args.buttons),
                      **CONNECTED)

    block.pads[args.pad] = pressed
    block.commit()
    try:
        time.sleep(args.ms / 1000.0)
    finally:
        # Let go even when interrupted, or the buttons stay held.
        block.pads[args.pad] = replace(pressed, buttons=held.buttons)
        block.commit()


def cmd_off(block, args):
    block.load()
    block.pads[args.pad] = Pad()
    block.commit()


def cmd_show(block, args):
    block.load()
    print("seq %d, buffer %d" % (block.seq, block.active))
    for i, p in enumerate(block.pads):
        if p.type == XPAD_TYPE_NONE:
            print("pad %d  not connected" % i)
        else:
            print("pad %d  %s  sticks %d,%d %d,%d  triggers %d %d"
                  % (i, button_names(p.buttons), p.lx, p.ly, p.rx, p.ry,
                     p.lt, p.rt))


def cmd_wait(block, args):
    time.sleep(args.ms / 1000.0)


def cmd_run(block, args):
    try:
        script = open(args.script) if args.script else sys.stdin
    except OSError as exc:
        raise Failure("cannot read %s: %s" % (args.script, exc.strerror))
    parser = build_parser(script_line=True)

    with script:
        for number, line in enumerate(script, 1):
            words = shlex.split(line, comments=True)
            if not words:
                continue
            try:
                step = parser.parse_args(words)
            except SystemExit:
                raise Failure("line %d: %s" % (number, line.strip()))
            step.func(block, step)


# ------------------------------------------------------------------ #
# Command line
# ------------------------------------------------------------------ #


def number(lo, hi):
    def parse(text):
        value = int(text, 0)
        if not lo <= value <= hi:
            raise argparse.ArgumentTypeError("%d is outside %d..%d"
                                             % (value, lo, hi))
        return value
    return parse


def add_pad(p):
    p.add_argument("-p", "--pad", type=number(0, PAD_COUNT - 1), default=0,
                   help="which pad, 0 to %d (default 0)" % (PAD_COUNT - 1))


def build_parser(script_line=False):
    top = argparse.ArgumentParser(
        prog="xpaddev.py" if not script_line else "run",
        description="Drive Xpad pads on an ST through md-devops' rom4 area.",
        epilog="See the top of this file, or docs/drivers.md.")

    if not script_line:
        top.add_argument("--host", help="SidecarTridge host[:port]")
        top.add_argument("--file", metavar="IMAGE",
                         help="work on a local image of the area instead")
        top.add_argument("--address", type=lambda s: int(s, 0),
                         default=ROM4_ADDRESS,
                         help="with --file, the ST address it stands for")

    sub = top.add_subparsers(dest="command", metavar="COMMAND")
    sub.required = True

    p = sub.add_parser("init", help="write the block, pad 0 idle")
    p.set_defaults(func=cmd_init)

    p = sub.add_parser("set", help="hold exactly these, on one pad")
    add_pad(p)
    p.add_argument("buttons", nargs="*", metavar="BUTTON")
    for axis in ("lx", "ly", "rx", "ry"):
        p.add_argument("--" + axis, type=number(-127, 127), default=0)
    for trigger in ("lt", "rt"):
        p.add_argument("--" + trigger, type=number(0, 255), default=0)
    p.set_defaults(func=cmd_set)

    p = sub.add_parser("tap", help="press, wait, let go")
    add_pad(p)
    p.add_argument("buttons", nargs="+", metavar="BUTTON")
    p.add_argument("--ms", type=number(0, 60000), default=100,
                   help="how long to hold them (default 100)")
    p.set_defaults(func=cmd_tap)

    p = sub.add_parser("off", help="disconnect a pad")
    add_pad(p)
    p.set_defaults(func=cmd_off)

    p = sub.add_parser("show", help="the pads as the ST reads them")
    p.set_defaults(func=cmd_show)

    if script_line:
        p = sub.add_parser("wait", help="pause")
        p.add_argument("ms", type=number(0, 3600000))
        p.set_defaults(func=cmd_wait)
    else:
        p = sub.add_parser("run", help="commands from a file, or stdin")
        p.add_argument("script", nargs="?", metavar="FILE")
        p.set_defaults(func=cmd_run)

    return top


def main(argv=None):
    args = build_parser().parse_args(argv)

    if args.file:
        area = FileArea(args.file, args.address)
    else:
        host = (args.host or os.environ.get("SIDECART_HOST", "").strip()
                or "sidecart.local")
        area = HttpArea(host)

    try:
        args.func(Block(area), args)
    except Failure as exc:
        print("xpaddev: %s" % exc, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130

    return 0


if __name__ == "__main__":
    sys.exit(main())
