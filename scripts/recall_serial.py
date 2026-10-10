#!/usr/bin/env python3
"""Drive a RECALL board over USB serial (standard library only).

  recall_serial.py send "status"
  recall_serial.py send "press Start" --wait "Saved .*T0_roomtone" --timeout 20
  recall_serial.py get /RECALL/RECALL-01-0001/RECALL-01-0001_log.json out/log.json
"""
import argparse
import base64
import glob
import os
import re
import select
import sys
import termios
import time
import tty
import zlib


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        attrs[2] |= termios.CLOCAL | termios.CREAD
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.buffer = b""

    def send(self, line):
        os.write(self.fd, (line + "\n").encode())

    def lines(self, timeout):
        end = time.time() + timeout
        while True:
            while b"\n" in self.buffer:
                line, self.buffer = self.buffer.split(b"\n", 1)
                yield line.decode(errors="replace").rstrip("\r")
            remaining = end - time.time()
            if remaining <= 0:
                return
            ready, _, _ = select.select([self.fd], [], [], min(remaining, 0.2))
            if ready:
                try:
                    self.buffer += os.read(self.fd, 65536)
                except BlockingIOError:
                    pass


def default_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("No /dev/cu.usbmodem* port found")
    return ports[0]


def cmd_send(port, args):
    pattern = re.compile(args.wait) if args.wait else None
    for command in args.commands:
        port.send(command)
        time.sleep(0.3)
    for line in port.lines(args.timeout):
        print(line, flush=True)
        if pattern and pattern.search(line):
            return 0
    return 1 if pattern else 0


def cmd_get(port, args):
    port.send("get " + args.remote)
    chunks, size, started = [], None, False
    for line in port.lines(args.timeout):
        if not started:
            if line.startswith("BEGIN "):
                size, started = int(line.split()[1]), True
            elif line.startswith("ERROR"):
                sys.exit(line)
            continue
        if line.startswith("END "):
            data = b"".join(base64.b64decode(c) for c in chunks)
            crc = int(line.split()[1], 16)
            if len(data) != size or (zlib.crc32(data) & 0xFFFFFFFF) != crc:
                sys.exit(f"Transfer check failed: {len(data)}/{size} bytes")
            os.makedirs(os.path.dirname(os.path.abspath(args.local)), exist_ok=True)
            with open(args.local, "wb") as f:
                f.write(data)
            print(f"{args.remote} -> {args.local} ({size} bytes, crc ok)")
            return 0
        chunks.append(line)
    sys.exit("Timed out")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default=None)
    sub = parser.add_subparsers(dest="action", required=True)
    send = sub.add_parser("send")
    send.add_argument("commands", nargs="+")
    send.add_argument("--wait")
    send.add_argument("--timeout", type=float, default=3)
    get = sub.add_parser("get")
    get.add_argument("remote")
    get.add_argument("local")
    get.add_argument("--timeout", type=float, default=300)
    args = parser.parse_args()
    port = Port(args.port or default_port())
    return cmd_send(port, args) if args.action == "send" else cmd_get(port, args)


if __name__ == "__main__":
    sys.exit(main())
