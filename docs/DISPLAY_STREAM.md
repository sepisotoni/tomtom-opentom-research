# Experimental TomTom display stream

**Status:** receiver/protocol, BGRA-to-RGB565 conversion, and bounded
USB-network transport prototypes. The Windows Indirect Display Driver (IDD)
integration and physical-device test are not complete. The prototype receiver
is not installed or supervised.

## Wire protocol version 1

Transport is a persistent TCP connection from the direct USB host to the
TomTom receiver at `192.168.101.115:18745`. The receiver rejects peers outside
`192.168.101.0/24`. The service is plaintext and unauthenticated; never route
or bridge it to a LAN/Wi-Fi.

Each frame is sent as one 22-byte header followed by its exact fixed-size
payload. TCP may split or coalesce these bytes; implementations must read/write
the declared exact lengths rather than treating a socket call as a message.

| Offset | Size | Field | Required value / encoding |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `TTDP` |
| 4 | 1 | Version | `1` |
| 5 | 1 | Message type | `1` (frame) |
| 6 | 2 | Header length | `22`, unsigned little-endian |
| 8 | 2 | Width | `320`, unsigned little-endian |
| 10 | 2 | Height | `240`, unsigned little-endian |
| 12 | 2 | Pixel format | `1` (RGB565 little-endian) |
| 14 | 4 | Sequence | Unsigned little-endian frame sequence |
| 18 | 4 | Payload length | `153600`, unsigned little-endian |

The payload is tightly packed RGB565 little-endian pixels, row-major, with no
row padding. Each frame receives a 9-byte acknowledgement:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `TTA1` |
| 4 | 4 | Echoed sequence, unsigned little-endian |
| 8 | 1 | Status: `0` accepted, `1` reserved invalid, `2` rate-limited, `3` framebuffer failure |

The initial receiver accepts no other mode, header version, or payload size.
The native Studio tree now has a testable BGRA8-to-RGB565-LE converter which
scales arbitrary bounded source surfaces to 320x240 with nearest-neighbor
sampling. The IDD still needs to copy its acquired Direct3D surface into a CPU
readable buffer and submit the converted frame to the transport worker in
`face-studio-native/src/display/`. The worker keeps only the latest pending
frame, sends at most 10 frames per second, waits for the matching ACK, and
connects only to `192.168.101.115:18745`; it does not resolve hostnames or
accept a configurable LAN target. Connect and complete frame I/O have bounded
two-second deadlines. It closes the socket after five seconds without frames
and sleeps while idle. Stopping it wakes the worker and joins its thread;
shutdown can wait for the current bounded connect or frame-I/O operation.
Wait for the ACK before sending the next frame; the
receiver caps accepted frames at 10 fps and closes on malformed frames or
excess rate. Each exact receive operation has a two-second deadline to bound
slow clients.

## Device ownership and operational safety

Source:

- `watchface-research/project/src/opentom_skel/bin/tomtom-display-protocol.[ch]`
- `watchface-research/project/src/opentom_skel/bin/tomtom-display-receiver.c`
- `watchface-research/project/tests/test_display_protocol.c`
- `face-studio-native/src/display/frame_converter.[h,cpp]`
- `face-studio-native/src/display/frame_transport.[h,cpp]`

The receiver validates `/dev/fb0` is exactly 320x240 true-color RGB565 with a
compatible line stride before mapping it. It writes the physical framebuffer
directly. **It must not run concurrently with the Nano-X watchface renderer:**
both processes would overwrite the same pixels. It is not added to `start.sh`
or any supervisor. No physical TomTom test has been performed with this
receiver. Do not install or launch it until there is a coordinated stop/restore
procedure for the currently supervised renderer and an end-to-end test sender.

Host protocol, frame-conversion, and transport-lifecycle unit checks run as
the native CMake targets `display_protocol_tests`,
`display_frame_converter_tests`, and `display_frame_transport_tests`; build
and device-side compile commands are in
[`DEVELOPMENT.md`](DEVELOPMENT.md). These tests do not exercise framebuffer
mapping or a live PC-to-TomTom TCP connection.

## Remaining path to a true extended monitor

1. Verify the generated catalog signature and the DLL's membership in it with
   SignTool; PowerShell currently reports `UnknownError` for both files. The
   staged x64 INF passed `InfVerif /u`, but that does not establish signing
   trust or runtime behavior.
2. Only after review, validate the temporary software-device control app on a
   Windows test system and test protocol traffic over USB.
3. Build the TomTom receiver for the exact target headers and check its
   reported framebuffer mode before any deployment.
4. Stop the watchface supervisor in a reversible, documented manner for a
   short end-to-end test; verify receiver start/stop, content, reconnect, and
   restoration of the normal clock.
5. Only after successful tests, integrate an explicit display start/stop
   control flow. A generic Microsoft sample driver alone is not the product.
