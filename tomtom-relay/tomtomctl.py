#!/usr/bin/env python3
"""Fixed-command CLI for a TomTom on the directly connected USB network."""

import argparse
import ipaddress
import re
import socket
import sys


DEVICE_HOST = "192.168.101.115"
DEVICE_CONTROL_PORT = 18743
DEVICE_NOTIFICATION_PORT = 45872
CONTROL_TIMEOUT_SECONDS = 3.0
FACE_NAMES = (
    "Blue Outline",
    "Aqua Wave",
    "Lavender",
    "Sunset",
    "Weather Preview",
    "Numerals Duo",
    "Roboto",
    "Ubuntu",
    "Nunito",
)
ERROR_REPLY = re.compile(r"ERR [A-Z0-9_]{1,32}\Z")


class TomTomError(Exception):
    """A bounded TomTom request failed or returned an invalid reply."""


def validate_target(host, port):
    try:
        address = ipaddress.ip_address(host)
    except ValueError as error:
        raise ValueError("host must be a literal IPv4 address") from error
    if not isinstance(address, ipaddress.IPv4Address):
        raise ValueError("host must be a literal IPv4 address")
    allowed_usb = ipaddress.ip_network("192.168.101.0/24")
    if not (address in allowed_usb or address.is_loopback):
        raise ValueError("host must be on the TomTom USB subnet or loopback")
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError("port must be between 1 and 65535")
    return str(address)


def _request(host, port, command, timeout=CONTROL_TIMEOUT_SECONDS):
    target = validate_target(host, port)
    if command not in ("PING", "STATUS") and not re.fullmatch(
        r"SET_FACE [0-8]", command
    ):
        raise ValueError("unsupported fixed TomTom command")
    if not 0 < timeout <= CONTROL_TIMEOUT_SECONDS:
        raise ValueError("timeout must be greater than 0 and at most 3 seconds")

    try:
        with socket.create_connection((target, port), timeout=timeout) as connection:
            connection.settimeout(timeout)
            connection.sendall(command.encode("ascii") + b"\n")
            response = bytearray()
            while len(response) <= 64:
                chunk = connection.recv(1)
                if not chunk:
                    break
                if chunk == b"\n":
                    break
                response.extend(chunk)
            else:
                raise TomTomError("TomTom returned an oversized response")
    except OSError as error:
        raise TomTomError(f"cannot reach TomTom at {target}:{port}: {error}") from error

    if not chunk or chunk != b"\n":
        raise TomTomError("TomTom closed the connection without a complete response")
    try:
        line = response.decode("ascii")
    except UnicodeDecodeError as error:
        raise TomTomError("TomTom returned non-ASCII data") from error
    if not line.isprintable():
        raise TomTomError("TomTom returned control characters")
    return line


def ping(host=DEVICE_HOST, port=DEVICE_CONTROL_PORT):
    reply = _request(host, port, "PING")
    if reply != "OK TOMTOM_CONTROL 1":
        raise TomTomError(f"unexpected PING response: {reply}")
    return reply


def status(host=DEVICE_HOST, port=DEVICE_CONTROL_PORT):
    reply = _request(host, port, "STATUS")
    match = re.fullmatch(r"OK FACE ([0-8])", reply)
    if match is None:
        raise TomTomError(f"unexpected STATUS response: {reply}")
    return int(match.group(1))


def set_face(face_id, host=DEVICE_HOST, port=DEVICE_CONTROL_PORT):
    if isinstance(face_id, bool) or not isinstance(face_id, int):
        raise ValueError("face ID must be an integer")
    if not 0 <= face_id < len(FACE_NAMES):
        raise ValueError("face ID must be between 0 and 8")
    current = status(host, port)
    if current == face_id:
        return False
    reply = _request(host, port, f"SET_FACE {face_id}")
    if reply != f"OK FACE {face_id}":
        raise TomTomError(f"unexpected SET_FACE response: {reply}")
    return True


def notify(message, ttl, host=DEVICE_HOST):
    if isinstance(ttl, bool) or not isinstance(ttl, int) or not 1 <= ttl <= 60:
        raise ValueError("notification TTL must be between 1 and 60 seconds")
    try:
        payload = message.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("notification text must be printable ASCII") from error
    if not 1 <= len(payload) <= 32 or any(byte < 32 or byte > 126 for byte in payload):
        raise ValueError("notification text must be 1-32 printable ASCII bytes")
    target = validate_target(host, DEVICE_NOTIFICATION_PORT)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as connection:
        connection.sendto(
            b"OT1|N|" + str(ttl).encode("ascii") + b"|" + payload,
            (target, DEVICE_NOTIFICATION_PORT),
        )


def build_parser():
    parser = argparse.ArgumentParser(
        description="Run only the built-in, fixed TomTom USB commands."
    )
    parser.add_argument("--host", default=DEVICE_HOST, help="TomTom USB IPv4 address")
    parser.add_argument(
        "--port",
        type=int,
        default=DEVICE_CONTROL_PORT,
        help="fixed-command TCP port (default: 18743; useful with a localhost SSH tunnel)",
    )
    commands = parser.add_subparsers(dest="action", required=True)
    commands.add_parser("ping", help="check the control service")
    commands.add_parser("status", help="show the current face")
    commands.add_parser("faces", help="list built-in face IDs")
    face_parser = commands.add_parser("face", help="select a face (persisted to SD)")
    face_parser.add_argument("face_id", type=int, choices=range(len(FACE_NAMES)))
    notify_parser = commands.add_parser(
        "notify", help="send one short notification over UDP"
    )
    notify_parser.add_argument("text")
    notify_parser.add_argument("--ttl", type=int, default=10, choices=range(1, 61))
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.action == "ping":
            validate_target(args.host, args.port)
            print(ping(args.host, args.port))
        elif args.action == "status":
            validate_target(args.host, args.port)
            face_id = status(args.host, args.port)
            print(f"OK FACE {face_id} - {FACE_NAMES[face_id]}")
        elif args.action == "faces":
            for face_id, name in enumerate(FACE_NAMES):
                print(f"{face_id}: {name}")
        elif args.action == "face":
            validate_target(args.host, args.port)
            changed = set_face(args.face_id, args.host, args.port)
            state = "applied (one SD-card write)" if changed else "already active; no SD write"
            print(f"OK FACE {args.face_id} - {FACE_NAMES[args.face_id]} ({state})")
        elif args.action == "notify":
            validate_target(args.host, DEVICE_NOTIFICATION_PORT)
            notify(args.text, args.ttl, args.host)
            print("notification datagram sent (device does not acknowledge UDP)")
        else:
            parser.error("unsupported command")
    except (TomTomError, ValueError, OSError) as error:
        print(f"tomtomctl: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
