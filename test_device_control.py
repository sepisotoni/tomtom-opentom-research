import socket
import threading
import unittest
from unittest import mock

from studio.core.device_control import (
    DeviceControlError,
    read_device_face,
    select_device_face,
)


class DeviceControlTests(unittest.TestCase):
    def test_rejects_invalid_face_ids_before_connecting(self):
        for face_id in (-1, 9, True, "7"):
            with self.subTest(face_id=face_id):
                with self.assertRaises(ValueError):
                    select_device_face("127.0.0.1", face_id)

    def test_writes_allowlisted_face_id_and_waits_for_ack(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]
        received_commands = []

        def fake_device():
            connection, _ = listener.accept()
            with connection:
                command = connection.recv(64)
                received_commands.append(command.decode("ascii").strip())
                connection.sendall(b"OK FACE 7\n")
            listener.close()

        server = threading.Thread(target=fake_device, daemon=True)
        server.start()
        with mock.patch(
            "studio.core.device_control.DEVICE_CONTROL_PORT", port
        ):
            select_device_face("127.0.0.1", 7)
        server.join(timeout=2)

        self.assertFalse(server.is_alive())
        self.assertEqual(
            received_commands,
            ["SET_FACE 7"],
        )

    def test_reads_persisted_face_status(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]

        def fake_device():
            connection, _ = listener.accept()
            with connection:
                self.assertEqual(connection.recv(64), b"STATUS\n")
                connection.sendall(b"OK FACE 7\n")
            listener.close()

        server = threading.Thread(target=fake_device, daemon=True)
        server.start()
        with mock.patch(
            "studio.core.device_control.DEVICE_CONTROL_PORT", port
        ):
            self.assertEqual(read_device_face("127.0.0.1"), 7)
        server.join(timeout=2)
        self.assertFalse(server.is_alive())

    def test_rejects_response_without_line_terminator(self):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        port = listener.getsockname()[1]

        def fake_device():
            connection, _ = listener.accept()
            with connection:
                connection.recv(64)
                connection.sendall(b"OK FACE 7")
            listener.close()

        server = threading.Thread(target=fake_device, daemon=True)
        server.start()
        with mock.patch(
            "studio.core.device_control.DEVICE_CONTROL_PORT", port
        ):
            with self.assertRaises(DeviceControlError):
                read_device_face("127.0.0.1")
        server.join(timeout=2)
        self.assertFalse(server.is_alive())

    def test_reports_unreachable_device(self):
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            port = listener.getsockname()[1]
        with mock.patch(
            "studio.core.device_control.DEVICE_CONTROL_PORT", port
        ):
            with self.assertRaises(DeviceControlError):
                select_device_face("127.0.0.1", 7, timeout=0.1)


if __name__ == "__main__":
    unittest.main()
