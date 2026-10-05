#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: 2026 Neil Rackett

"""Tests for xpaddev.py, the DevOps driver's workstation half.

The tool writes an XPAD block in m68k layout and byte order from a
workstation, so everything it knows about the ABI is transcribed rather
than compiled against xpad.h. This holds the transcription to the
header, checks the block against the rules xpad_valid() applies, and
checks the order commits land in, which is what makes a consumer's
recheck mean anything. It also drives the HTTP path against a stand-in
for md-devops, so the request bodies are tested and not just the bytes.

Whether the ST's own consumer code reads the result is the driver's
self test, `make hatari-devops`, which loads an image this tool made.
"""

import http.server
import importlib.util
import json
import os
import re
import struct
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEVOPS = os.path.join(ROOT, "src", "drivers", "devops")

spec = importlib.util.spec_from_file_location(
    "xpaddev", os.path.join(DEVOPS, "xpaddev.py"))
xd = importlib.util.module_from_spec(spec)
spec.loader.exec_module(xd)

failures = 0


def check(ok, what):
    global failures
    print("%-44s %s" % (what, "ok" if ok else "FAIL"))
    if not ok:
        failures += 1


def defines(path):
    """The plain numeric #defines in a C header."""
    found = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|\d+)U?L?\b",
                         line)
            if m:
                found[m.group(1)] = int(m.group(2), 0)
    return found


class Recorder:
    """An area that keeps every write, in order, and the bytes."""

    def __init__(self, address=xd.ROM4_ADDRESS):
        self.at = address
        self.data = bytearray(xd.AREA_SIZE)
        self.writes = []

    def address(self):
        return self.at

    def read(self):
        return bytes(self.data)

    def write(self, frames):
        self.writes.append(list(frames))
        for off, data in frames:
            self.data[off:off + len(data)] = data


def run(argv, area_file):
    """The tool as a user runs it, on a local image."""
    return xd.main(["--file", area_file] + argv)


def pad_at(data, buf, i):
    off = xd.OFF_PADS + (buf * xd.PAD_COUNT + i) * xd.XPAD_PAD_SIZE_V1
    return xd.Pad(*xd.PAD.unpack_from(data, off))


def front(data):
    active = data[xd.OFF_ACTIVE]
    return [pad_at(data, active, i) for i in range(xd.PAD_COUNT)]


# ------------------------------------------------------------------ #
# The transcription
# ------------------------------------------------------------------ #


def test_constants():
    h = defines(os.path.join(ROOT, "src", "xpad.h"))
    locate = defines(os.path.join(DEVOPS, "locate.h"))

    for name in ("XPAD_MAGIC", "XPAD_MAX_PADS", "XPAD_PAD_SIZE_V1",
                 "XPAD_HDR_FIXED", "XPAD_CAP_ANALOG", "XPAD_CAP_HOTPLUG",
                 "XPAD_TYPE_NONE", "XPAD_TYPE_GAMEPAD", "XPAD_PAD_ANALOG"):
        check(getattr(xd, name) == h[name], "%s matches xpad.h" % name)

    check(xd.XPAD_VERSION ==
          (h["XPAD_VER_MAJOR"] << 8 | h["XPAD_VER_MINOR"]),
          "XPAD_VERSION matches xpad.h")

    check(all(xd.BUTTONS[n] == h["XPAD_" + n.upper()] for n in xd.BUTTONS),
          "every button bit matches xpad.h")
    check(len(xd.BUTTONS) == 17, "every button xpad.h defines is named")

    check(xd.PROVIDER_OFFSET == locate["XPAD_DEVOPS_PROVIDER_OFFSET"] and
          xd.PROVIDER_SIZE == locate["XPAD_DEVOPS_PROVIDER_SIZE"],
          "the provider's place matches locate.h")
    check(xd.SPAN == xd.PROVIDER_OFFSET + xd.PROVIDER_SIZE,
          "the span is the block's room plus the name")
    with open(os.path.join(DEVOPS, "locate.h")) as f:
        name = re.search(r'#define XPAD_DEVOPS_PROVIDER "([^"]*)"', f.read())
    check(name is not None and name.group(1).encode() == xd.PROVIDER,
          "the provider name matches locate.h")
    check(defines(os.path.join(DEVOPS, "devops.c"))["IMAGE_ADDRESS"] ==
          xd.ROM4_ADDRESS, "the self test's image address is the tool's")
    check(xd.ROM4_MAGIC == locate["XPAD_DEVOPS_MAGIC"],
          "the rom4 magic matches locate.h")


# ------------------------------------------------------------------ #
# The block
# ------------------------------------------------------------------ #


def test_init():
    area = Recorder()
    xd.Block(area).init()
    data = area.read()

    # Written out by hand from the m68k layout, so this does not lean on
    # the tool's own struct formats to check themselves.
    want = bytes.fromhex(
        "58504144"  # magic 'XPAD'
        "0100"      # version 1.0
        "007a"      # hdr_size: 26 + 2 buffers * 4 pads * 12
        "001a"      # pads_offset: past the fixed 18 and two pointers
        "000c"      # pad_size
        "0009"      # caps: analogue, hotplug
        "0000"      # seq
        "04"        # pad_count
        "00"        # active
        "00fa8b80"  # provider: the area plus 0x80
        "00000000"  # req: none
    )
    check(data[:26] == want, "the header is the m68k layout")

    magic, version, hdr_size, pads_offset, pad_size, _, _, count, active = \
        struct.unpack_from(">IHHHHHHBB", data)

    # What xpad_valid() asks of a block, asked again here.
    check(magic == 0x58504144 and version >> 8 == 1, "magic and version")
    check(1 <= count <= 4 and pad_size >= 12, "pad count and size")
    check(18 <= pads_offset <= hdr_size and
          count * 2 * pad_size <= hdr_size - pads_offset,
          "the pads lie inside the block")
    check(xd.ROM4_ADDRESS % 2 == 0, "the block is at an even address")
    check(hdr_size <= xd.PROVIDER_OFFSET and xd.SPAN <= xd.AREA_SIZE,
          "the provider follows the block, in the area")

    check(data[xd.PROVIDER_OFFSET:xd.PROVIDER_OFFSET + 19] ==
          b"DevOps workstation\0", "the provider string")
    check(active == 0, "buffer 0 is in front")

    pads = front(data)
    check(pads[0] == xd.Pad(type=2, flags=1), "pad 0 is connected and idle")
    check(all(p == xd.Pad() for p in pads[1:]),
          "pads 1 to 3 are not connected")
    check(all(pad_at(data, 1, i) == pads[i] for i in range(4)),
          "both buffers start the same")

    last = area.writes[0][-1]
    check(last == (0, b"XPAD"), "the magic goes in last")


def test_commit():
    area = Recorder()
    block = xd.Block(area)
    block.init()
    area.writes.clear()

    block.pads[2] = xd.Pad(buttons=0x10, type=2, flags=1)
    block.commit()

    frames = area.writes[0]
    check(len(area.writes) == 1, "a commit is one request")
    check([off for off, _ in frames] ==
          [xd.OFF_PADS + 48, xd.OFF_SEQ, xd.OFF_ACTIVE],
          "back buffer, then seq, then active")
    check(frames[2][1] == b"\x01", "active is written as 1")
    check(pad_at(area.data, 1, 2).buttons == 0x10, "the pad is in buffer 1")
    check(pad_at(area.data, 0, 2) == xd.Pad(),
          "and the old front buffer is left alone")

    block.commit()
    check(area.writes[1][0][0] == xd.OFF_PADS and
          area.writes[1][2][1] == b"\x00", "the next commit fills buffer 0")
    check(struct.unpack_from(">H", area.data, xd.OFF_SEQ)[0] == 2,
          "seq counts commits")

    block.seq = 0xFFFF
    block.commit()
    check(struct.unpack_from(">H", area.data, xd.OFF_SEQ)[0] == 0,
          "seq wraps at 16 bits")


def test_fold():
    b = xd.BUTTONS
    cases = [
        ((0, 0), 0, "a centred stick is no direction"),
        ((39, 0), 0, "inside the deadzone is no direction"),
        ((40, 0), b["right"], "the deadzone's edge counts"),
        ((-40, 0), b["left"], "left"),
        ((0, -40), b["up"], "up is negative y"),
        ((0, 127), b["down"], "down is positive y"),
        # The case test/abi.c pins: a box test would miss it.
        ((30, 30), b["right"] | b["down"], "a diagonal past the radius"),
        ((100, -20), b["right"], "mostly right is only right"),
        ((-127, -127), b["left"] | b["up"], "the corner"),
    ]
    for (x, y), want, what in cases:
        check(xd.fold_stick(x, y) == want, what)


def test_buttons():
    b = xd.BUTTONS
    check(xd.parse_buttons(["south+east", "start"]) ==
          b["south"] | b["east"] | b["start"], "+ and spaces both combine")
    check(xd.parse_buttons(["a", "B"]) == b["south"] | b["east"],
          "a and b are south and east")
    check(xd.parse_buttons([]) == 0, "no buttons is none held")

    for name in ("x", "y", "jump"):
        try:
            xd.parse_buttons([name])
            refused = False
        except xd.Failure:
            refused = True
        check(refused, "%s is refused" % name)


# ------------------------------------------------------------------ #
# The commands
# ------------------------------------------------------------------ #


def test_commands(tmp):
    image = os.path.join(tmp, "area.img")

    check(run(["set", "south"], image) == 1,
          "set refuses an area with no block")

    check(run(["init"], image) == 0, "init")
    check(run(["set", "--pad", "1", "south", "start", "--lx", "100",
               "--ly", "-20", "--lt", "255"], image) == 0, "set")

    with open(image, "rb") as f:
        data = f.read()
    pads = front(data)
    b = xd.BUTTONS

    check(pads[1] == xd.Pad(b["south"] | b["start"] | b["right"], 100, -20,
                            0, 0, 255, 0, 2, 1),
          "set holds exactly that, stick folded in")
    check(pads[0] == xd.Pad(type=2, flags=1), "and leaves pad 0 alone")

    check(run(["tap", "--pad", "1", "--ms", "0", "north"], image) == 0, "tap")
    with open(image, "rb") as f:
        data = f.read()
    check(front(data)[1] == pads[1], "tap lets go of what it pressed")
    check(pad_at(data, 0, 1).buttons ==
          b["south"] | b["start"] | b["right"] | b["north"],
          "after holding it in between")
    check(struct.unpack_from(">H", data, xd.OFF_SEQ)[0] == 3,
          "tap is two commits")

    check(run(["off", "--pad", "1"], image) == 0, "off")
    with open(image, "rb") as f:
        check(front(f.read())[1] == xd.Pad(), "off disconnects the pad")

    script = os.path.join(tmp, "steps.txt")
    with open(script, "w") as f:
        f.write("# a comment\n\n"
                "init\n"
                "set start   # hold start\n"
                "wait 0\n"
                "tap --ms 0 -p 2 a\n"
                "set -p 3 west --rx -5 --rt 9\n")
    check(run(["run", script], image) == 0, "run")
    with open(image, "rb") as f:
        pads = front(f.read())
    check(pads[0].buttons == b["start"], "run's steps all land")
    check(pads[2] == xd.Pad(type=2, flags=1),
          "a tap connects a pad and lets go")
    check(pads[3] == xd.Pad(b["west"], 0, 0, -5, 0, 0, 9, 2, 1),
          "the right stick is not folded")

    with open(script, "w") as f:
        f.write("init\nset nothing\n")
    check(run(["run", script], image) == 1, "run stops at a bad line")


# ------------------------------------------------------------------ #
# Over HTTP, against a stand-in for md-devops
# ------------------------------------------------------------------ #


class FakeDevops(http.server.BaseHTTPRequestHandler):
    """md-devops' rom4 endpoints, as docs/api.md describes them."""

    area = bytearray(1024)

    def log_message(self, *args):
        pass

    def reply(self, code, body, kind="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/v1/rom4":
            self.reply(200, json.dumps({
                "ok": True, "address": 0xFA8B00, "size": 1024,
                "magic": 0x52344131}).encode())
        elif self.path == "/api/v1/rom4/data":
            self.reply(200, bytes(self.area), "application/octet-stream")
        else:
            self.reply(404, b'{"ok":false}')

    def do_PUT(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        frames, pos = [], 0
        while pos < len(body):
            off, n = struct.unpack_from(">HH", body, pos)
            if pos + 4 + n > len(body) or off + n > 1024:
                return self.reply(400, b'{"ok":false}')
            frames.append((off, body[pos + 4:pos + 4 + n]))
            pos += 4 + n
        for off, data in frames:
            self.area[off:off + len(data)] = data
        self.reply(200, b'{"ok":true}')


def test_http(tmp):
    server = http.server.HTTPServer(("127.0.0.1", 0), FakeDevops)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    host = "127.0.0.1:%d" % server.server_port
    image = os.path.join(tmp, "same.img")

    try:
        steps = [["init"], ["set", "start", "--ly", "90"],
                 ["tap", "--ms", "0", "-p", "1", "east"], ["off", "-p", "1"]]
        ok = True
        for step in steps:
            ok &= xd.main(["--host", host] + step) == 0
            ok &= run(step, image) == 0
        check(ok, "every step works over HTTP")

        with open(image, "rb") as f:
            check(bytes(FakeDevops.area) == f.read(),
                  "and leaves the same bytes as the file")
    finally:
        server.shutdown()
        server.server_close()

    check(xd.main(["--host", host, "show"]) == 1,
          "an unreachable host is one error")


def main():
    print("xpad DevOps workstation tool\n")
    test_constants()
    test_init()
    test_commit()
    test_fold()
    test_buttons()
    with tempfile.TemporaryDirectory() as tmp:
        test_commands(tmp)
        test_http(tmp)
    print("\n%s" % ("FAILED" if failures else "all checks passed"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
