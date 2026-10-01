# TomTom Face Studio feature draft

**Status:** draft for review. This separates features already in the native
Windows app from requested work that still needs product and technical
decisions. The current local executable is a preview build, not the final
release.

## In the native app now

`face-studio-native/` is a C++17/Win32 implementation of the desktop Face
Studio. Its current feature set includes:

- A 320x240 pixel-art canvas with pencil, eraser, fill, eyedropper, brush
  sizing, color selection, zoom, grid, and undo/redo.
- PNG/JPEG/BMP background import with contain, cover, stretch, and custom
  placement.
- Editable time, date, text, and status-icon elements with constrained
  declarative visibility rules.
- A simulated device state and clock preview.
- `.ttproj` editing, `.ttface` validation/export/inspection, and `.ttgallery`
  creation/inspection/change planning.
- Basic TomTom face read/apply/PING over the existing USB-only control
  protocol.
- A small, standalone Windows x64 executable with no Python or Qt runtime.

The Python/PyQt app in `studio/` remains a separate reference implementation.
The native app's known omissions and parity differences are documented in
[`face-studio-native/README.md`](face-studio-native/README.md).

## Requested TomTom-facing features

These reflect the recent project requests; they are **not all implemented in
the native app**.

| Feature | Expected behavior | Current state |
|---|---|---|
| PC notification test sender | Send a short test message over the USB link; show it in place of the weather content for about 15 seconds, then fade and restore weather. | The embedded preview has a bounded placeholder notification receiver; the native Windows app has no sender UI yet. |
| Live weather card | Keep the existing TomTom weather card, local layout, battery, icons, and required provider attribution. Never present sample values as live data. | Implemented in the separate TomTom preview/weather services; not controlled by the native Studio app. |
| TomTom as a PC display | Show PC-provided content on the 320x240 TomTom screen while it is USB-connected. | Selected as true Windows extended monitor. An experimental TomTom frame receiver exists; Windows IDD/frame transport remains unimplemented. |

## Display scope that needs a decision

A **true extended monitor** lets Windows move an app window onto a new monitor.
It generally requires a Windows Indirect Display Driver (IDD), driver
installation/signing and permissions, plus a device-side frame receiver and
pixel transport. A normal Face Studio `.exe` alone cannot make the TomTom
appear in Windows Display Settings.

### Toolchain milestone

The owner confirmed Visual Studio 2022, Windows SDK/WDK build 10.0.26100, and
the `IddCx` headers are installed. Microsoft's `IddSampleDriver.sln` compiled
the sample UMDF driver DLL and sample app and generated a catalog. The final
staged x64 INF passed `InfVerif /u` with exit code 0. An earlier automatic INF
verification attempt failed because MSBuild could not load its x86 verifier
DLL; manual verification of the staged x64 INF succeeded. This confirms the
basic IDD toolchain can build/sample-validate, not that the sample is a usable
TomTom display driver.

A **streamed preview/mirror** captures a PC window or region and sends scaled
frames to the TomTom. It can be prototyped as an app feature without a Windows
display driver, but it is not a true second monitor and cannot accept arbitrary
windows dragged onto the device.

Both options need a device rendering/transport path and performance checks over
the USB link. The existing TCP face-control service is not a pixel-stream
protocol; do not overload it or expose it beyond the isolated USB connection.
Keep any frame transport bounded, USB-only, and separate from the control
commands. Use screenshots/sample content before enabling continuous desktop
capture.

**Owner decision:** choose true extended monitor, streamed preview/mirror, or
defer the display feature. **Selected: true Windows extended monitor.** The
owner confirmed that windows should be movable onto the TomTom display; a
mirrored preview or Face Studio dashboard alone does not meet this requirement.

### Proposed true-monitor architecture

1. A Windows Indirect Display Driver (IDD) registers a virtual 320x240 display
   target with Windows, so it appears in Display Settings and can be enabled in
   Extend mode.
2. A Windows user-mode transport component receives bounded frame updates from
   the IDD and sends them over the isolated USB Ethernet link.
3. A separate TomTom-side receiver validates frames and presents RGB565 output
   to the physical framebuffer. It must not share the face-control service's
   command port.
4. The native Studio app provides user-facing start/stop/status and diagnostics;
   it is not itself the display driver.

An experimental standalone C89 TomTom frame protocol and framebuffer receiver
now exist under `watchface-research/project/src/opentom_skel/bin/`. They accept
only fixed 320x240 RGB565 little-endian frames from the USB subnet, validate
the framebuffer mode, and cap delivery at 10 fps. They are not installed or
wired into device startup, and the Windows IDD-to-USB frame source is still
unimplemented.

The fixed 320x240 panel is the initial mode. Uncompressed RGB565 is 153,600
bytes per frame (about 1.47 MiB); at 10 frames/second that is about 12.3
Mbit/s before protocol overhead. Begin with a bounded low frame rate and
measure end-to-end latency, USB throughput, and TomTom CPU use before adding
compression or higher refresh rates.

Driver packaging, installation privileges, test-signing, disconnect behavior,
sleep/resume, and the interaction with the running Nano-X renderer all need
explicit design and Windows/TomTom hardware validation. MinGW can build the
ordinary Studio `.exe`, but it is not a substitute for the Windows Driver Kit
(WDK) or a test-signed IDD package.

## Suggested implementation/review order

1. Review this draft and confirm display mode and notification scope.
2. Keep the native app's existing authoring/package/device-face workflows
   stable; compare behavior with the Python reference and test the Windows
   executable on actual Windows.
3. Add a separate PC notification sender only after verifying the receiver
   packet contract and target TomTom renderer on the same branch.
4. Implement and test the IDD and its user-mode frame transport to the
   experimental receiver; cover bounded frame sizes/rates, disconnect
   recovery, display-mode changes, and an explicit stop control.
5. Build and label a final release only after Claude's review and physical
   Windows/TomTom verification. Keep preview builds separate from that final.

## Current local preview build

The Linux host cross-builds the Windows x64 executable to
`build/native-face-studio-windows/TomTomFaceStudio.exe`. The binary is ignored
by Git. A successful cross-build and portable-core test do not verify the GUI
on Windows or install anything on the TomTom.
