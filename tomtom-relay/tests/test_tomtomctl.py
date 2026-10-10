import pathlib
import socket
import sys
import threading
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import tomtomctl


class TomTomCliTests(unittest.TestCase):
    def test_ping_uses_fixed_line_protocol(self):
        received = []

        def serve(listener):
            connection, _ = listener.accept()
            with connection:
                received.append(connection.recv(32))
                connection.sendall(b"OK TOMTOM_CONTROL 1\n")

        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        thread = threading.Thread(target=serve, args=(listener,))
        thread.start()
        try:
            self.assertEqual(
                tomtomctl.ping("127.0.0.1", listener.getsockname()[1]),
                "OK TOMTOM_CONTROL 1",
            )
        finally:
            listener.close()
            thread.join(timeout=2)
        self.assertEqual(received, [b"PING\n"])

    @mock.patch("tomtomctl._request", side_effect=["OK FACE 6"])
    def test_set_face_skips_sd_write_if_face_is_already_active(self, request):
        self.assertFalse(tomtomctl.set_face(6))
        request.assert_called_once_with(
            tomtomctl.DEVICE_HOST, tomtomctl.DEVICE_CONTROL_PORT, "STATUS"
        )

    @mock.patch("tomtomctl._request", side_effect=["OK FACE 6", "OK FACE 7"])
    def test_set_face_changes_only_after_status(self, request):
        self.assertTrue(tomtomctl.set_face(7))
        self.assertEqual(
            request.call_args_list,
            [
                mock.call(
                    tomtomctl.DEVICE_HOST, tomtomctl.DEVICE_CONTROL_PORT, "STATUS"
                ),
                mock.call(
                    tomtomctl.DEVICE_HOST,
                    tomtomctl.DEVICE_CONTROL_PORT,
                    "SET_FACE 7",
                ),
            ],
        )

    def test_rejects_non_usb_targets_and_arbitrary_commands(self):
        with self.assertRaises(ValueError):
            tomtomctl.validate_target("8.8.8.8", 18743)
        with self.assertRaises(ValueError):
            tomtomctl.validate_target("::1", 18743)
        with self.assertRaises(ValueError):
            tomtomctl._request("127.0.0.1", 28743, "SHELL rm -rf /")
        with self.assertRaises(ValueError):
            tomtomctl.set_face(9)

    @mock.patch("tomtomctl._request", return_value="OK FACE 06")
    def test_status_rejects_noncanonical_face_ids(self, request):
        with self.assertRaises(tomtomctl.TomTomError):
            tomtomctl.status()

    def test_notification_packet_and_validation(self):
        with mock.patch("tomtomctl.socket.socket") as socket_factory:
            udp = socket_factory.return_value.__enter__.return_value
            tomtomctl.notify("Hello TomTom", 12)
            udp.sendto.assert_called_once_with(
                b"OT1|N|12|Hello TomTom", ("192.168.101.115", 45872)
            )
        for message, ttl in (("", 10), ("x" * 33, 10), ("café", 10), ("ok", 0)):
            with self.subTest(message=message, ttl=ttl):
                with self.assertRaises(ValueError):
                    tomtomctl.notify(message, ttl)


if __name__ == "__main__":
    unittest.main()
