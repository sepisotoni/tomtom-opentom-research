"""Narrow USB-Ethernet control protocol for the TomTom face gallery."""

import socket


DEVICE_FACE_OPTIONS = (
    (0, "Blue Outline"),
    (1, "Aqua Wave"),
    (2, "Lavender"),
    (3, "Sunset"),
    (4, "Weather Preview"),
    (5, "Numerals Duo"),
    (6, "Roboto"),
    (7, "Ubuntu"),
    (8, "Nunito"),
)
DEVICE_CONTROL_PORT = 18743
DEVICE_CONTROL_TIMEOUT = 4.0


class DeviceControlError(Exception):
    """Raised when a bounded face-selection request cannot be completed."""


def _request(host, command, timeout):
    try:
        with socket.create_connection(
            (host, DEVICE_CONTROL_PORT), timeout=timeout
        ) as connection:
            connection.settimeout(timeout)
            connection.sendall(command.encode("ascii") + b"\n")
            response = bytearray()
            line_complete = False
            while len(response) <= 64:
                block = connection.recv(1)
                if not block:
                    break
                if block == b"\n":
                    line_complete = True
                    break
                response.extend(block)
    except OSError as error:
        raise DeviceControlError(
            "Could not reach the TomTom control service over USB Ethernet."
        ) from error

    if len(response) > 64:
        raise DeviceControlError("TomTom returned an oversized response.")
    if not line_complete:
        raise DeviceControlError("TomTom closed the connection without a response.")
    try:
        return response.decode("ascii").strip()
    except UnicodeDecodeError as error:
        raise DeviceControlError("TomTom returned an invalid response.") from error


def select_device_face(host, face_id, timeout=DEVICE_CONTROL_TIMEOUT):
    """Persist and apply one known face index on a directly connected TomTom."""
    if not isinstance(host, str) or not host.strip():
        raise ValueError("TomTom host must not be empty.")
    if isinstance(face_id, bool) or not isinstance(face_id, int):
        raise ValueError("Face ID must be an integer.")
    if face_id not in dict(DEVICE_FACE_OPTIONS):
        raise ValueError("Unsupported TomTom face ID.")

    response = _request(host.strip(), f"SET_FACE {face_id}", timeout)
    if response != f"OK FACE {face_id}":
        raise DeviceControlError(
            response.removeprefix("ERR ") or "TomTom rejected the face change."
        )


def read_device_face(host, timeout=DEVICE_CONTROL_TIMEOUT):
    """Return the face index currently persisted on the TomTom."""
    if not isinstance(host, str) or not host.strip():
        raise ValueError("TomTom host must not be empty.")
    response = _request(host.strip(), "STATUS", timeout)
    prefix = "OK FACE "
    if not response.startswith(prefix):
        raise DeviceControlError(
            response.removeprefix("ERR ") or "TomTom returned an invalid status."
        )
    value = response[len(prefix):]
    if not value.isdigit() or int(value) not in dict(DEVICE_FACE_OPTIONS):
        raise DeviceControlError("TomTom returned an invalid face index.")
    return int(value)
