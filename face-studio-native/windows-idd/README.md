# Experimental TomTom IDD (extended display) and its control surface

A UMDF/IddCx indirect display driver that shows up in Windows as a 320x240 monitor and streams it to the
TomTom ONE (model 19) over the USB-only display receiver (`192.168.101.115:18745`, RGB565-LE frames, see
[`docs/DISPLAY_STREAM.md`](../../docs/DISPLAY_STREAM.md)). **Experimental: nothing here has run on real
hardware, and no driver has ever been installed.** Read [What has and has not been verified](#what-has-and-has-not-been-verified)
before installing anything.

> **Do not install this on a daily-use PC.** The package is signed only with a local WDK test certificate
> (`WDKTestCert Sepiso Toni`) whose root is untrusted, so installing it needs *test-signing mode* (Secure Boot
> off, a "Test Mode" watermark, weaker driver-signature enforcement for the whole machine). Use a spare/test
> Windows machine or VM.

The link to the TomTom is plain text and unauthenticated. It is USB-only: never bridge or route it to
Wi-Fi/LAN/internet, and there is deliberately no way to configure another target.

## Control tool: `TomTomDisplayControl.exe`

| Command | Elevation | What it does |
| --- | --- | --- |
| `status` | no | Prints exactly one line, `present=<0\|1> running=<0\|1> frames=<n>`, and nothing else |
| `pause` / `resume` | no | Driver stops / restarts streaming to the TomTom (releases the TCP connection while paused) |
| `enable [--interactive]` | **yes** | Creates the virtual display device and **holds it** until `disable`/Ctrl+C/exit |
| `disable` | **yes** | Asks the `enable` host to remove the device (idempotent: success if nothing is enabled) |
| `install <path\to\IddPackage.inf>` | **yes** | Adds the driver package to the driver store (`pnputil /add-driver … /install`) |
| `uninstall` | **yes** | `disable`, then deletes our driver package(s) from the driver store |
| *(no arguments)* | **yes** | Same as `enable --interactive` (the original development behaviour: press **X** to remove) |

`pause`, `resume`, `disable`, `install`, `uninstall` print nothing on success (`install`/`uninstall` print one
human-readable hint line). Errors go to stderr as `TomTomDisplayControl: <message>`.

### Exit codes (`tt::idd_ipc::exit_code` in `src/display/idd_ipc.h`)

| Code | Name | Meaning |
| ---: | --- | --- |
| 0 | ok | Success (for `status`: the line was printed, whatever it says) |
| 1 | internal_error | Unexpected failure; details on stderr |
| 2 | not_installed | Driver package / virtual display device not found (`pause`/`resume` with no device; `enable` when no driver matches; `uninstall` with no package) |
| 3 | access_denied | Needs an elevated (Administrator) process — re-run with `runas` |
| 4 | device_faulted | The device node exists but is not started (problem code on stderr) |
| 5 | usage | Unknown command / bad arguments / `install` pointed at something that is not a built package folder |
| 6 | driver_unresponsive | Device is present but the driver's control pipe did not answer (driver host starting or restarting) |
| 7 | reboot_required | Operation completed; Windows needs a restart (`pnputil` returned 3010) |
| 8 | already_running | Another `enable` host is running |

### `status` contract (for the Studio)

```
present=1 running=1 frames=4821
```

* One line, LF-terminated, ASCII, three keys **in this order**. Exit code 0, empty stderr. A parser should
  require the three keys and ignore any further trailing `key=value` tokens (future versions may append).
  `tt::idd_ipc::parse_status_line()` in `src/display/idd_ipc.h` does exactly that.
* `present=1` — a device node with hardware ID `Root\TomTomIndirectDisplay` exists, is started, and has no
  problem code. A node that exists but is faulted reports `present=0` (`pause`/`resume` then exit 4;
  `disable` then `enable` recreates it).
* `running=1` — the driver is armed to stream: Windows has assigned the monitor's swap chain **and** the driver
  is neither paused nor yielding to the Studio (see below). `present=1 running=0` therefore means any of:
  paused, Studio mirror active, monitor not extended/active in Windows, or driver still starting.
* `frames` — frames the receiver has acknowledged (ACK status 0) since the driver adapter loaded. It only
  grows while streaming and **resets to 0 when the driver host restarts**; treat a decrease as a restart.
* `status` is cheap but it spawns a process: poll no faster than about once per second, with a ~3 s timeout.
* `status` exits 0 for "nothing installed" (`present=0 running=0 frames=0`). Non-zero means the query itself
  failed (1 internal, 3 access denied by the device tree).

### How "toggle the extended display" maps to commands

| Studio UI | Command | Cost |
| --- | --- | --- |
| Extended display **on** | `enable` (elevated, long-running) | One UAC prompt; the process stays alive as the device's owner |
| Extended display **off** / "Remove extended display" | `disable` (elevated) | One UAC prompt |
| Stream to the TomTom **on/off** without removing the monitor | `resume` / `pause` | No elevation |

Launching `enable` from the Studio: `ShellExecuteExW` with verb `runas`, `SW_HIDE`, `SEE_MASK_NOCLOSEPROCESS`;
then poll `status` for `present=1` (allow up to ~20 s) while also watching the process handle — if it exits
early, its exit code is the reason (table above). `enable` itself prints nothing the Studio needs to parse.

**Why `enable` stays running:** the software device lives exactly as long as the creating process holds its
`HSWDEVICE`. That gives clean crash behaviour (host dies → Windows removes the device and the monitor; nothing
is orphaned) and avoids relying on persistent software-device lifetime, which is unverified. The price is that
the device does not survive logoff/reboot and there is no auto-enable at logon (possible follow-up once the
driver is proven on hardware).

## Sharing TCP 18745 between the driver and the Studio's mirror mode

The receiver (`tomtom-display-receiver`) serves **one client at a time** (`listen(…, 1)`, synchronous
`handle_client`) and drops a client that sends nothing for 2 s. If the driver and the Studio's mirror both
stream, they alternately steal the connection and the panel flickers between the two sources. Rule:

> **The Studio's mirror always wins. While the Studio holds its lease, the driver holds no connection to the
> receiver. When the lease disappears for any reason, the driver takes over again within ~0.5 s.**
> The driver also has an explicit `pause`/`resume` for the user.

The driver streams only if `streaming_allowed(paused, mirror_lease) == !paused && !mirror_lease`.

### The lease (what the Studio must implement)

* Object: a **named event** `Global\TomTomFaceStudio.MirrorLease` (`tt::idd_ipc::kMirrorLeaseEventName`).
  Existence is the signal; its state is irrelevant. Created with `CreateEventW` by the Studio and kept open.
  Ordinary users may create `Global\` events (no special privilege needed). A mutex under the same name also
  counts as a lease, but use the event.
* **Ready-made helper:** `tt::display::MirrorLease` in `src/display/mirror_lease.h` (header-only, Windows only).
  `acquire()` returns false if somebody else already holds the lease (e.g. a second Studio instance is
  mirroring; `last_error() == ERROR_ALREADY_EXISTS`) — do not start mirroring then.
* **Mirror start:** `lease.acquire()` → (optional) wait ~300 ms → `FrameTransport::start()`.
  The driver polls the lease every 200 ms, then drops its TCP connection (immediately if idle; after the frame in
  flight, ≤ 2 s worst case). `FrameTransport` already retries a refused/stalled connection every 500 ms, so a
  short overlap heals itself. Waiting for `status` to show `running=0` (≤ 1.5 s) is optional and not required.
* **Mirror stop:** `FrameTransport::stop()` **first**, then `lease.release()` — otherwise the driver could
  grab the connection while the Studio's socket is still open. On release the driver reconnects at the next
  frame and also re-sends its last frame at once, so a static desktop does not leave the Studio's last mirror
  frame on the TomTom.
* The lease is independent of whether the driver is even installed; acquiring it is always harmless.

### Pause/resume (explicit user control)

`TomTomDisplayControl.exe pause` / `resume` talk to the driver over the control pipe. Pause is **volatile
driver state**: it is lost if the driver host restarts, the device is removed, or the PC reboots (the driver
then streams again by default). A pause never overrides or releases a Studio lease and the lease never clears a
pause: both must be clear for the driver to stream. While paused or yielding, the Windows extended monitor
still exists (windows can still be dragged onto it); the TomTom keeps showing whatever it last received.

### Behaviour on crash / failure

| Event | Result |
| --- | --- |
| Studio crashes or is killed while mirroring | Kernel closes the lease handle → driver resumes by itself within ~0.5 s. The receiver drops the dead client (RST, or its 2 s timeout) |
| Second Studio instance starts mirroring | `acquire()` fails (`ERROR_ALREADY_EXISTS`); the first instance is undisturbed |
| Driver host crashes / restarts | Pause is forgotten (streams again) but a live Studio lease is honoured from the first frame; `status` may briefly give `running=0 frames=0`; `frames` restarts at 0; `pause`/`resume` exit 6 until it is back |
| `enable` host killed or crashes | Windows removes the device (handle closed) → monitor disappears, `status` → `present=0`; no orphan |
| Receiver restarts / TomTom unplugged | Existing `FrameTransport` behaviour: reconnect every 500 ms; the driver never needs the Studio to be told |
| Control pipe unavailable (name already taken) | Driver logs it and still honours the lease; only `status`/`pause`/`resume` stop working (`running=0`, exit 6) |

### Control pipe protocol (driver ↔ `TomTomDisplayControl.exe`; the Studio does not need it)

`\\.\pipe\TomTomFaceStudio.IddControl`, byte mode, one request line per connection, one reply line, then the
server disconnects: `STATUS` → `OK running=<0|1> frames=<n>`; `PAUSE` / `RESUME` → `OK`; anything else →
`ERR UNKNOWN_COMMAND`. Requests are ≤ 32 bytes, case-sensitive, each I/O bounded to 500 ms so a stalled client
cannot block the driver. Clients must request the explicit access mask `tt::idd_ipc::kControlPipeClientAccess`
(0x00100083) — not `GENERIC_READ|GENERIC_WRITE`, which includes
`FILE_CREATE_PIPE_INSTANCE` and is not granted. `tt::idd::control_pipe_request()` in `ControlPipeClient.h` does
this correctly.

### Security notes

* The pipe is created with `PIPE_REJECT_REMOTE_CLIENTS` (never reachable over SMB), `FILE_FLAG_FIRST_PIPE_INSTANCE`
  (a squatted name is refused, not trusted) and a DACL of SYSTEM/Administrators full + Authenticated Users
  read/write data only. Clients connect with anonymous impersonation. Any local authenticated user can therefore
  query, pause and resume the stream; none of this carries display content or secrets.
* Any local user can create the lease name and thereby silence the driver, or pre-create the `enable` host
  mutex name and make `enable` fail with exit 8. Both are local denial-of-service of a cosmetic feature only.
* The host stop event (`Global\TomTomFaceStudio.IddDeviceHostStop`) is Administrators/SYSTEM only, so `disable`
  needs elevation.
* None of this touches TCP 18745's trust model: the receiver stays plain-text and USB-subnet-only.

## Install / uninstall / "Remove extended display"

### What needs test-signing and what you must do by hand

The tool never changes machine-wide signing policy or certificate stores — that is deliberate. On a **spare
test machine**, by hand, from an elevated prompt:

1. **Test-signing mode** (required while the package is signed with the WDK test certificate):
   `bcdedit /enum {current}` (look for `testsigning`), then `bcdedit /set testsigning on`, then **reboot**.
   If Secure Boot is on, `bcdedit` refuses ("protected by Secure Boot policy"): turn Secure Boot off in
   firmware first. A "Test Mode" watermark appears after the reboot.
2. **Trust the test certificate** (public part only — never export or copy the private key):
   ```powershell
   $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -like '*WDKTestCert Sepiso Toni*' } | Select-Object -First 1
   Export-Certificate -Cert $cert -FilePath .\TomTomIdd-test.cer     # on the build PC
   certutil -addstore Root TomTomIdd-test.cer                         # on the test PC, elevated
   certutil -addstore TrustedPublisher TomTomIdd-test.cer
   ```
3. Build (below) and copy the **package folder** — the folder under `out\x64\Release\` that contains
   `TomTomIdd.dll`, `TomTomIdd.cat` and the INF — to the test PC.
4. `TomTomDisplayControl.exe install <package folder>\IddPackage.inf`  (the INF file name is whatever the WDK
   build staged next to `TomTomIdd.cat`; `install` refuses a folder that lacks the `.dll`/`.cat`).
5. `TomTomDisplayControl.exe enable` (elevated; leave it running) → the 320x240 monitor appears.

Without test-signing and the trusted certificate, `pnputil` is expected to reject the catalog (signature/publisher
errors; not yet observed); `install` then exits 1 with a hint that says so. A production signature (EV certificate and/or Microsoft Hardware
Dev Center attestation) would remove steps 1–2; none exists yet.

**Reboots:** only the test-signing toggle is known to need one. `install` reports exit 7 if `pnputil` says a
restart is needed (expected to be rare for a UMDF package; untested). The device never survives a reboot:
`enable` has to be run again.

### Verify

* `bcdedit /enum {current}` → `testsigning Yes` (before installing).
* `pnputil /enum-drivers` → a package with provider `TomTom Face Studio`; `pnputil /enum-devices /class Display` or
  Device Manager → *Display adapters* → **TomTom 320x240 Indirect Display** without a warning icon.
* `TomTomDisplayControl.exe status` → `present=1`. Windows *Settings → System → Display* lists an extra 320x240
  display (use *Extend these displays* if it is duplicated/disabled).
* With the TomTom receiver running (renderer stopped, see `docs/DISPLAY_STREAM.md`): `running=1` and `frames`
  increasing between two `status` calls while something on the extended monitor changes.

### Remove

| Goal | Command (elevated) | Result |
| --- | --- | --- |
| **Remove extended display** (Studio button) | `TomTomDisplayControl.exe disable` | Device removed, monitor gone, driver stays installed; `enable` brings it back. Exit 0 even if it was not enabled |
| Remove the driver package too | `TomTomDisplayControl.exe uninstall` | `disable`, then `pnputil /delete-driver` for every `%WINDIR%\INF\oem*.inf` that names `Root\TomTomIndirectDisplay` **and** `TomTomIdd.dll`. Exit 2 if no package is installed. Never deletes a package whose device is still present |
| Without the tool | Device Manager → uninstall the device (tick *Attempt to remove the driver*) or `pnputil /delete-driver oemNN.inf /uninstall` | Same effect |
| Undo the test setup (always manual) | `bcdedit /set testsigning off` + reboot; `certutil -delstore Root <thumbprint>`; `certutil -delstore TrustedPublisher <thumbprint>` | Machine is back to normal signing policy |

## How the pieces fit

```
Windows desktop ──swap chain──> TomTomIdd.dll (UMDF, WUDFHost) ──FrameTransport──> TCP 18745 ──> receiver
                                    │  ControlChannel: gate + control pipe + frame counter
   Studio mirror ── MirrorLease ────┤  (lease event polled every 200 ms)
   TomTomDisplayControl status/pause/resume ── \\.\pipe\TomTomFaceStudio.IddControl
   TomTomDisplayControl enable/disable ── software device (SwDeviceCreate) + host mutex/stop event
```

* `Driver.cpp` — IddCx callbacks; swap-chain thread converts at most one frame per 100 ms (nearest-neighbour,
  BGRA8 → RGB565-LE), keeps the latest converted frame even while gated, and re-sends it when the gate reopens.
* `ControlChannel.*` — the gate (`!paused && !lease`), the control pipe server, the cumulative frame counter.
  Drives the active `FrameTransport` via `set_paused()` (closes the TCP connection, drops pending frames).
* `src/display/idd_ipc.h` — names, exit codes, `status`-line and pipe-protocol formatting/parsing, shared by
  the driver, the tool and the Studio. `src/display/mirror_lease.h` — Studio-side lease helper.
* `OverlappedIo.h`, `ControlPipeClient.h`, `InfScan.h` — small shared helpers.

## Build on Windows

Use a VS 2022 Developer PowerShell with the WDK and the x64 Spectre-mitigated libraries installed:

```powershell
msbuild .\face-studio-native\windows-idd\TomTomIdd.sln /m /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Output is under `face-studio-native\windows-idd\out\x64\Release`. This only builds the package and the helper;
it does not install anything. `TomTomDisplayControl.exe` is also built by the main CMake build (CI) and links
`cfgmgr32` and `swdevice`; `advapi32` is a default library on MSVC.

Tests (no WDK needed):

* `tests/test_idd_ipc.cpp` — portable: status line, pipe protocol, single-owner rule, INF ownership matching,
  `FrameTransport` pause/counter. Needs `src/display/frame_transport.cpp` and the protocol `.c` file.
* `tests/test_control_channel.cpp` — Windows only: the real `ControlChannel` over a real named pipe and a real
  `Global\` lease event, including a silent client, a squatted pipe name, a wrong-typed lease object, and the
  `MirrorLease` helper. Needs `ControlChannel.cpp`, `src/display/frame_transport.cpp`, the protocol `.c` file and
  `ws2_32`.

## What has and has not been verified

Verified (in a Linux sandbox, not on the owner's Windows machine):

* 58 portable checks (g++ 13, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`) plus the project's four
  existing CTest suites on the modified `FrameTransport`.
* `ControlChannel.cpp`, `ControlPipeClient.h`, `OverlappedIo.h`, `mirror_lease.h`, `TomTomDisplayControl.cpp`
  cross-compiled with mingw-w64 13 under the same strict flags; the 47-check control-channel test passed
  15/15 runs **under Wine 9.0** (Wine is not Windows). A mutation check (breaking lease detection) was caught.
* The CLI's usage handling, exit codes and `status` line under Wine (device absent).

**Not** verified — assume any of these may need a fix on first contact:

* `Driver.cpp`/`Driver.h`/`ControlChannel.cpp` have **not been compiled by the WDK/MSVC** (`/W4 /WX`). Driver.cpp
  changes were reviewed by hand only.
* The WDK's Universal-driver API validation may object to the named-pipe / `sddl.h` APIs in a
  `DriverTargetPlatform=Universal` UMDF driver. If so, the fallback is `DriverTargetPlatform=Desktop` for this
  project (IddCx supports it) — do not weaken the pipe security to work around it.
* Whether the UMDF host (WUDFHost, LocalService) may create the pipe and open `Global\` names. The driver probes
  this once at start (how a missing object looks) and only treats `ERROR_ACCESS_DENIED` as "lease exists" if a
  missing name reports `ERROR_FILE_NOT_FOUND`, so the worst case is "lease ignored", not "driver silenced forever".
* `SwDeviceCreate`/`SwDeviceClose` usage and the creation-failure classification (problem code 28/1 → exit 2,
  other → exit 4) are unchanged-in-spirit from the earlier prototype but untested; they were type-checked only
  against a stub header (mingw has no `swdevice.h`).
* Whether SwDevice creation needs elevation (assumed yes), device removal timing, and every USB/throughput,
  sleep/resume, display-mode and receiver-handoff behaviour listed in `docs/DISPLAY_STREAM.md`.

## Known prototype limitations

- Nearest-neighbour scaling from a CPU-readable staging texture; at most one frame per 100 ms.
- The software device does not outlive its `enable` process (see above); no auto-enable at logon.
- The TomTom receiver must be started separately with the normal Nano-X renderer stopped; never run both writers
  to `/dev/fb0`. A coordinated start/stop is a device-side request (`DISPLAY_START`/`DISPLAY_STOP`).
- The receiver is unauthenticated and must stay isolated to USB.

The Microsoft Windows driver samples are licensed under MS-PL. This implementation is independently written
based on the documented IddCx sample architecture and does not copy its source files.
