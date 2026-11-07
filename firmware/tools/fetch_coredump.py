#!/usr/bin/env python3
"""Fetch a bulb's core dump over MQTT and decode it. Needs no cable.

Run from an activated ESP-IDF shell:

    python tools/fetch_coredump.py living-room            # print the crash report
    python tools/fetch_coredump.py living-room --gdb      # open it in GDB
    python tools/fetch_coredump.py living-room --erase    # afterwards, clear it from the bulb

Decoding needs the .elf of the firmware that crashed. tools/publish_firmware.py
keeps those in releases/, and this finds the right one from the hash the bulb
reports. Use --elf for a build that was flashed over USB.

The password is prompted for. A core dump holds the contents of the bulb's task
stacks and can include its passwords, so it's saved privately, and git ignores it.
"""
import argparse
import getpass
import hashlib
import json
import os
import socket
import ssl
import struct
import subprocess
import sys
import time
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
RELEASES = FIRMWARE / "releases"
TOPIC_PREFIX = "pico/bulb"
TIMEOUT_S = 30  # for each message to arrive


class Mqtt:
    """Just enough MQTT 3.1.1 over TLS for this job, so no broker tools need installing."""

    def __init__(self, host: str, port: int, user: str, password: str):
        raw = socket.create_connection((host, port), timeout=TIMEOUT_S)
        self.sock = ssl.create_default_context().wrap_socket(raw, server_hostname=host)
        client_id = f"fetch-coredump-{os.getpid()}"
        body = (self._text("MQTT") + bytes([4, 0xC2]) + struct.pack(">H", TIMEOUT_S * 2)
                + self._text(client_id) + self._text(user) + self._text(password))
        self._send(0x10, body)
        kind, reply = self._receive()
        if kind != 0x20 or reply[1] != 0:
            raise ConnectionError("the broker refused this login")

    @staticmethod
    def _text(value: str) -> bytes:
        data = value.encode()
        return struct.pack(">H", len(data)) + data

    def _send(self, first_byte: int, body: bytes) -> None:
        length = bytearray()
        remaining = len(body)
        while True:
            remaining, digit = divmod(remaining, 128)
            length.append(digit | (128 if remaining else 0))
            if not remaining:
                break
        self.sock.sendall(bytes([first_byte]) + bytes(length) + body)

    def _exactly(self, count: int) -> bytes:
        data = b""
        while len(data) < count:
            part = self.sock.recv(count - len(data))
            if not part:
                raise ConnectionError("the broker closed the connection")
            data += part
        return data

    def _receive(self) -> tuple[int, bytes]:
        first = self._exactly(1)[0]
        length, scale = 0, 1
        while True:
            digit = self._exactly(1)[0]
            length += (digit & 127) * scale
            scale *= 128
            if not digit & 128:
                return first, self._exactly(length)

    def subscribe(self, *topics: str) -> None:
        self._send(0x82, struct.pack(">H", 1) + b"".join(self._text(t) + b"\0" for t in topics))

    def publish(self, topic: str, payload: str) -> None:
        self._send(0x30, self._text(topic) + payload.encode())

    def next_message(self) -> tuple[str, bytes]:
        """The next published message. Raises TimeoutError if the bulb goes quiet."""
        while True:
            first, body = self._receive()
            if first >> 4 == 3:
                size = struct.unpack(">H", body[:2])[0]
                return body[2:2 + size].decode(), body[2 + size:]


def collect(mqtt: Mqtt, root: str) -> tuple[dict, bytes | None]:
    """Ask for the dump. Returns the bulb's info and the dump, or None if it has none."""
    mqtt.subscribe(f"{root}/info", f"{root}/coredump")
    info: dict = {}
    while not info:  # retained, so it arrives straight away
        topic, payload = mqtt.next_message()
        if topic.endswith("/info"):
            info = json.loads(payload)

    mqtt.publish(f"{root}/cmd", '{"cmd":"coredump"}')
    dump = bytearray()
    while True:
        topic, payload = mqtt.next_message()
        if not topic.endswith("/coredump"):
            continue
        offset, total = struct.unpack(">II", payload[:8])
        if total == 0:
            return info, None
        if offset != len(dump):
            raise ValueError(f"expected byte {len(dump)} but got {offset}: a piece was lost")
        dump += payload[8:]
        print(f"\r{len(dump)} of {total} bytes", end="", flush=True)
        if len(dump) >= total:
            print()
            return info, bytes(dump)


def find_elf(prefix: str) -> Path | None:
    """The released .elf whose hash starts with `prefix`."""
    for elf in RELEASES.glob("*/*.elf"):
        if hashlib.sha256(elf.read_bytes()).hexdigest().startswith(prefix):
            return elf
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("bulb", help="bulb id")
    parser.add_argument("--host", default="mqtt.apicov.xyz")
    parser.add_argument("--port", type=int, default=8883)
    parser.add_argument("--user", default="app", help="MQTT user: app, or the bulb's own (default: %(default)s)")
    parser.add_argument("--elf", type=Path, help="the crashed firmware's .elf (default: found in releases/)")
    parser.add_argument("--gdb", action="store_true", help="open a GDB session instead of printing a report")
    parser.add_argument("--erase", action="store_true", help="clear the dump from the bulb once it's saved")
    args = parser.parse_args()

    if "IDF_PATH" not in os.environ:
        print("Activate ESP-IDF first: IDF_PATH isn't set.", file=sys.stderr)
        return 1

    root = f"{TOPIC_PREFIX}/{args.bulb}"
    try:
        mqtt = Mqtt(args.host, args.port, args.user, getpass.getpass("MQTT password: "))
        info, dump = collect(mqtt, root)
    except (OSError, ValueError) as error:  # timeouts and lost connections are OSErrors
        print(f"\nCan't fetch the dump: {error}. Check the bulb is online, then try again.", file=sys.stderr)
        return 1
    if dump is None:
        print(f"{args.bulb} has no core dump saved.")
        return 0

    crash = info.get("crash") or {}
    print(f"Crash in task {crash.get('task', '?')} at {crash.get('pc', '?')}, return address {crash.get('ra', '?')}")

    RELEASES.mkdir(exist_ok=True)  # git ignores this folder
    saved = RELEASES / f"coredump-{args.bulb}-{time.strftime('%Y%m%d-%H%M%S')}.bin"
    saved.touch(mode=0o600)
    saved.write_bytes(dump)
    print(f"Saved {saved}. It can hold the bulb's passwords, so delete it when you're done.")

    if args.erase:
        mqtt.publish(f"{root}/cmd", '{"cmd":"coredump_erase"}')
        time.sleep(1)  # give the message time to leave
        print("Asked the bulb to clear it.")

    elf = args.elf or find_elf(crash.get("elf", "?"))
    if elf is None:
        print(f"No .elf in {RELEASES} has the hash {crash.get('elf', '?')}. "
              "Use --elf with the build that crashed to decode it.", file=sys.stderr)
        return 1
    action = "dbg_corefile" if args.gdb else "info_corefile"
    command = [sys.executable, "-m", "esp_coredump", "--chip", "esp32c6", action,
               "--core", str(saved), "--core-format", "raw", str(elf)]
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
