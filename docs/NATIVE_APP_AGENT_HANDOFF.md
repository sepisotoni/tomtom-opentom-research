# Native Face Studio - live features and agent handoff

Status of the Windows app's "Display" and "Live" tabs, the exact device-side contract each one relies on,
and what is still owed by the TomTom-side and driver agents. Written by the PC-app agent. Nothing here changes
firmware; every row below says whether it is already satisfied by code on `main` or still a request.

## What the PC app does today

| Feature (tab) | Host code | Device endpoint used | Device-side status |
| --- | --- | --- | --- |
| Face selection (Device) | `src/core/device_client.cpp` | TCP 18743 `PING` / `STATUS` / `SET_FACE <0-8>` | exists |
| Notification (Live) | `src/core/notify.cpp` | UDP 45872 `OT1|N|<1-60>|<=32 printable ASCII>` | exists (renderer placeholder receiver) |
| Webhook -> notification (Live) | `src/core/webhook.cpp` | same UDP packet | exists |
| Spotify track announce (Live) | `src/app/spotify.cpp` (window title) | same UDP packet, one per track change | exists, but see request 3 |
| Synced lyrics (Live) | `src/app/lrclib.cpp`, `src/core/lrc.cpp` | same UDP packet, one chunk of <=32 chars at a time | exists, but see request 3 |
| Screen / window mirror (Display) | `src/app/mirror.cpp` + `src/display/*` | TCP 18745 display frames (`docs/DISPLAY_STREAM.md`) | receiver exists; start/stop is manual (request 1) |
| True extended monitor | `windows-idd/` | same TCP 18745 | driver agent (request 4) |

The PC app only ever talks to the TomTom's USB address. It never adds a shell, never listens for the TomTom, and
the webhook listener is loopback-only unless "Allow private LAN" is ticked (token always required).

## Requests for the TomTom-side agent

1. **Display session control.** Add commands to the fixed-command service on TCP 18743, e.g. `DISPLAY_START` /
   `DISPLAY_STOP` / `DISPLAY_STATUS`, that atomically stop the Nano-X renderer and start
   `tomtom-display-receiver` (and the reverse). Today the user has to do this by hand because both write `/dev/fb0`.
   Requirements: reply format in the existing style (`OK DISPLAY <state>` / `ERR <CODE>`), USB-subnet only like the other
   commands, and a watchdog that restores the clock renderer if the receiver exits or the PC stops sending frames.
2. **Notification behaviour contract.** Document exactly what happens on repeated `OT1|N` packets: does a new packet
   replace the current text and restart the TTL? Is there a minimum interval? What is shown when the weather card is
   active? The PC app sends a lyric line every ~1-3 s.
3. **Media panel (optional, replaces notification abuse).** A dedicated "now playing" view would be nicer than
   notifications. Proposed packets (UDP 45872, same trust rules): `OT1|M|<artist<=24>|<title<=32>` and
   `OT1|L|<line<=32>` (lyric line, cleared by an empty line), plus `OT1|M|` to clear. Please say if you prefer something else.
4. **Filesystem inventory.** See the prompt below.

## Requests for the Windows display-driver agent (windows-idd)

* A stable control surface for the Studio: `TomTomDisplayControl.exe status` should print one machine-readable line
  (`present=<0|1> running=<0|1> frames=<n>`), with documented exit codes, so the Studio can show and toggle it.
* Mirror mode (Studio sends frames) and extend mode (the driver sends frames) both use TCP 18745 and the receiver accepts
  one client. The driver must expose a way to be paused/disabled so the Studio can own the connection, or define who wins.
* Confirm the REG/INF install path and uninstall story so the Studio can offer "Remove extended display".

## Known limits of the PC features

* Lyric sync is an estimate (time since the track title changed, paused when Spotify's title shows no track). Seeking
  drifts; the user can nudge +/-0.25 s or restart. Exact position needs Windows SMTC (C++/WinRT, MSVC only) or the
  Spotify Web API (user-registered client id + OAuth). Good follow-up for the MSVC-capable agent.
* Spotify lyrics themselves are not available from Spotify; synced lyrics come from lrclib.net (community database,
  opt-in, artist + title sent over HTTPS, nothing stored).
* Windows that opt out of capture (some DRM/banking windows) appear black in the mirror.
* Mirror and the extended-monitor driver are experimental until verified on hardware.
