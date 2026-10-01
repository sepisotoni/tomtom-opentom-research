# TomTom device operations

This guide describes the tested device workflows. The device may be unplugged,
sleeping, or on a different interface/IP at any later time. Re-discover and
verify the link before every operation; do not assume a stale address is live.

## Connection and safety

Observed USB Ethernet addresses have included TomTom `192.168.101.115` and
Linux host `192.168.101.114`; another host address/interface was observed
later. The device startup script configures `usb0` as `192.168.101.115/24`.
Discover the current Linux route and local address before connecting:

```sh
ip -br addr
ip route get 192.168.101.115
```

The device is only expected to be reachable when physically connected over
the direct USB Ethernet link. Do not bridge that link or expose its services
through Wi-Fi/router routing. The application protocols are plaintext and
unauthenticated. Telnet is an unencrypted root-shell transport; use it only
for an explicitly authorized, isolated USB session. Do not put login details
in source control or automate destructive commands against an unknown host.

If the host route shows an unrelated interface, there is no neighbor/reply, or
the device control service is unavailable, stop. Do not try alternate LANs,
guess credentials, flash firmware, or infer that device cleanup succeeded.

## Read-only status over TCP 18743

`tomtom-control` accepts one line per connection. It is the preferred way to
read face status and change face selection; it does not provide shell or file
transfer operations.

```python
import socket

with socket.create_connection(("192.168.101.115", 18743), timeout=3) as sock:
    sock.sendall(b"STATUS\n")
    print(sock.recv(128).decode("ascii").strip())
```

Other fixed commands include `PING`, `WEATHER_STATUS`, and `CLEAR_WEATHER`.
`SET_FACE 8` is a persistent change; only issue it when requested. Weather
state is transient. See the protocol table in
[`ARCHITECTURE.md`](ARCHITECTURE.md).

## Building and transferring one renderer

Build using the exact compiler and headers in
[`DEVELOPMENT.md`](DEVELOPMENT.md). Keep the host build outside Git. Before
replacing a live executable:

1. Confirm the device route and status service.
2. Record the deployed binary path, size, current face, and relevant service
   state.
3. Make one rollback copy of the currently working binary on the device and,
   when possible, archive it to host storage and verify byte count/hash.
4. Serve only the intended staged binary over the direct USB host address.
   A temporary one-file HTTP server can be bound to the host's USB IP (example
   port `18745`); do not use a public bind address or serve the repository root.
5. Fetch to a temporary path on the SD card with the device's BusyBox `wget`,
   verify the size, set executable permissions, then rename into place on the
   same filesystem. Never overwrite the only working binary in-place.
6. Let the existing startup supervisor restart `watchface.new` or use the
   project's documented restart method. Do not start a duplicate renderer.
7. Verify the process, direct framebuffer log line (if requested), control
   status, and a real device capture before reporting success.
8. Stop the temporary host server and remove its device staging file.

The gallery entrypoint in the deployed startup tree is
`/mnt/sdcard/opentom/bin/watchface-main`; it changes to
`/mnt/sdcard/opentom/preview-gallery/modern-20260928` and executes
`watchface.new`. The source template is
`watchface-research/project/src/opentom_skel/bin/watchface-main`.
`start.sh` supervises the renderer. Confirm these paths on-device before
relying on them; packaging may change.

Do not transfer using shell echo/base64 or interactive terminal copy/paste.
Binary-safe HTTP over the isolated USB link avoids terminal encoding and line
discipline corruption. Serve only files intended for the device.

Example host-side staging (use the currently assigned USB host IP, not
necessarily `.114`):

```sh
mkdir -m 700 /tmp/tomtom-transfer
cp /tmp/watchface-new /tmp/tomtom-transfer/watchface.new
cd /tmp/tomtom-transfer
python3 -m http.server 18745 --bind "$HOST_USB_IP" --directory .
```

In a separate trusted Telnet session on the direct USB cable, the matching
device-side download has this shape:

```sh
/bin/busybox wget -O /tmp/watchface.stage \
  "http://$HOST_USB_IP:18745/watchface.new"
ls -l /tmp/watchface.stage
```

After size/hash verification and after preserving the rollback, copy the
staged file to a temporary name beside the live executable, `chmod 755` that
temporary file, and rename it atomically to `watchface.new`. These are
illustrative commands: verify the actual host USB IP, BusyBox applets,
deployment path, and running process first. Stop the HTTP server after the
one transfer and remove the staging file.

## Screenshots and runtime verification

For visual tasks, inspect a true 320x240 device capture, not only a desktop
mockup. A raw framebuffer capture observed on this device used 320x240
RGB565 (153,600 bytes); conversion must match the actual channel/byte order
reported by the live framebuffer. Protect captures as local artifacts and do
not commit them unless explicitly requested.

Use the actual service response and device log to report:

- current face ID (`STATUS`);
- `DIRECT_FB=320x240 RGB565` only if present in the fresh renderer log;
- current weather status as transient data (do not share coordinates/tokens);
- renderer PID and the exact binary path;
- a fresh device screenshot path and any visible issue.

The last recorded test installation included a direct-framebuffer renderer and
a compact official Google Maps attribution logo. That historical success does
not establish the current device state.
