# Experimental TomTom IDD

This is the first WDK-specific implementation slice for the 320x240 TomTom
display. It is not yet installable or ready for hardware. The INF/package,
software-device lifecycle, Windows build, and physical frame-transfer path all
need validation before installation is attempted.

The UMDF/IddCx driver advertises one EDID-less 320x240 @ 30 Hz monitor mode.
When Windows assigns an active swap chain, it creates the matching D3D device
and a dedicated processing thread. The thread continues servicing the IddCx
swap chain, but maps and converts at most one frame every 100 ms. The transport
worker keeps a single latest-frame buffer, sends at most 10 fps over the
hard-coded TomTom USB address, and disconnects after five seconds without
frames. Unassigning the swap chain stops the worker and processing thread.

`TomTomDisplayControl.exe` is a temporary console utility for creating and
removing the software device. It is for development only, not an installer or
the finished Studio integration.

The source follows the documented IddCx callback pattern and does not include
Microsoft sample source. The Windows WDK 10.0.26100 build compiled and linked
the driver DLL, control utility, and generated a driver catalog. The build's
automatic `InfVerif` invocation reported that it could not load
`x86\InfVerif.dll`; MSBuild nevertheless completed signability/catalog work
with no listed signing errors. The staged INF still needs separate manual
x64 `InfVerif` validation. No driver has been installed.

## Build on Windows

Use a VS 2022 Developer PowerShell with the WDK and the x64 Spectre-mitigated
libraries installed:

```powershell
msbuild .\face-studio-native\windows-idd\TomTomIdd.sln /m /p:Configuration=Release /p:Platform=x64 /v:minimal
```

Build output is under `face-studio-native\windows-idd\out\x64\Release`.
This only builds the package and helper; it does not install or enable a
driver. Do not use `pnputil`, enable test-signing, or reboot based on this
prototype until the Windows build has been reviewed and the test plan is ready.

## Known prototype limitations

- The control utility's virtual software-device lifecycle has not been
  validated against an installed driver.
- There is no signed catalog, installer, rollback/uninstaller, or Studio UI.
- Frame conversion uses a CPU-readable D3D staging texture and nearest-neighbor
  scaling. USB throughput, GPU/CPU cost, latency, display mode negotiation,
  disconnect/reconnect, sleep/resume, and framebuffer handoff remain untested.
- The TomTom receiver must be separately built and started while the normal
  Nano-X renderer is safely stopped. Never run both writers to `/dev/fb0`.
- The receiver is unauthenticated and must remain isolated to USB; do not
  bridge its TCP port to LAN or Wi-Fi.

The Microsoft Windows driver samples are licensed under MS-PL. This
implementation is independently written based on the documented IddCx sample
architecture and does not copy its source files.
