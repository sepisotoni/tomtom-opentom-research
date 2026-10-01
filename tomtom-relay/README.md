# TomTom Linux relay

`tomtom-relay` is a small, dependency-free Rust HTTP service for the Linux
computer connected to the TomTom. It exposes persistent asset storage and a
no-store proxy to the existing Render/Google weather service. It binds only to
the host's direct TomTom USB Ethernet address by default. It does not bridge,
forward, or expose the USB link to Wi-Fi or the internet.

## Current status and boundaries

- Asset storage is rooted at `~/TomTomStorage` and is separate from Git.
- The service serves a file listing, downloads, uploads, and deletes with
  bounded names and sizes. Uploads replace files atomically.
- Weather requests require explicit `location_sharing_enabled: true`, are
  forwarded to Render with the server-side token, and are not cached or logged.
- The service does not discover GPS coordinates. The TomTom GPS data path has
  not yet been validated; a GPS fix must be supplied to the weather endpoint
  by a trusted client after the user grants location consent.
- The device weather worker sends an explicitly consented GPS fix to
  `/v1/weather/display`; the relay returns a compact no-store summary. The
  Roboto watchface requests a validated Google PNG icon over USB only when its
  icon key changes. Icon responses are size/dimension bounded and not cached.
- The deployed Render weather API requires `WEATHER_SERVICE_TOKEN`. Until it
  is configured locally, the relay returns
  `503 weather_proxy_not_configured`; it never pretends that weather works.
- Router-hosting mode is configurable, but must not be enabled until the
  router's USB-Ethernet host support and network path are confirmed. Binding
  anywhere other than the direct USB host address requires a service bearer
  token.

## HTTP API

Default address: `192.168.101.114:18744` on the direct USB Ethernet link.

| Request | Behavior |
|---|---|
| `GET /healthz` | Reports service/storage health and whether weather is configured |
| `GET /v1/files` | Lists stored file names and byte sizes |
| `GET /v1/files/<name>` | Downloads a stored asset |
| `PUT /v1/files/<name>` | Atomically stores an asset (maximum 32 MiB) |
| `DELETE /v1/files/<name>` | Deletes an asset |
| `POST /v1/weather` | Forwards an explicitly consented coordinate request to Render |
| `POST /v1/weather/display` | Returns the bounded `TMW2` current/high/low/icon summary for the device |
| `GET /v1/weather/icon/<key>_dark.png` | Proxies a validated Google weather PNG without caching |

Asset names are single safe path components. The service does not accept
directories, traversal, symlinks, or unbounded uploads. If
`TOMTOM_SERVICE_TOKEN` is set, all API routes require
`Authorization: Bearer <token>`. The token is mandatory for non-USB bind
addresses. Keep any router deployment on a trusted LAN and never port-forward
these endpoints to the public internet.

Example weather request:

```json
{
  "latitude": 48.8566,
  "longitude": 2.3522,
  "location_sharing_enabled": true,
  "language_code": "en"
}
```

The relay calls `TOMTOM_WEATHER_URL` (defaulting to the deployed Render route),
does not retain the coordinates or forecast response, and preserves Google's
required attribution. The upstream Render service's response is capped at 4
KiB and returned with `Cache-Control: no-store`.

## Build and run

Requires Rust/Cargo and `curl` (used only for HTTPS to Render):

```sh
cargo test --manifest-path tomtom-relay/Cargo.toml
tomtom-relay/install-local.sh
```

The installer builds the optimized binary, installs a restricted user
systemd unit, enables autostart (the host already has user lingering enabled),
and uses `~/TomTomStorage` for persistent files. It requires the USB link to
have `192.168.101.114` assigned before the service can bind. To make that
address return automatically when the TomTom is plugged into the reviewed USB
port, inspect the interface with `udevadm info`, then run:

```sh
tomtom-relay/install-usb-link.sh enxYOUR_INTERFACE
```

This helper verifies the connected device and physical USB path before
installing its narrow systemd-networkd profile. It does not modify the
computer's wired/Wi-Fi router configuration.

To configure weather, put the Render service token in
`~/.config/tomtom-relay/environment`, never in the repository:

```text
WEATHER_SERVICE_TOKEN=<Render WEATHER_SERVICE_TOKEN>
```

The token must be the value configured on Render. File permissions should be
`0600`; restart the service after changing it. Do not print or commit the
credential. The Render token can be rotated independently without changing the
Google API key.

The optimized Rust binary has no Cargo dependencies. Weather forwarding uses
the host's `curl` executable for HTTPS. The service has systemd limits of 64
MiB RAM, 10% CPU, and 16 tasks; downloads stream from disk instead of loading
the full asset into memory.

The optional router-mode service address can be set in the user environment
file as `TOMTOM_RELAY_BIND=<trusted-host-LAN-IP>:18744`; include
`TOMTOM_SERVICE_TOKEN=<random-secret>` at the same time. The app/device client
must then be configured with that same service token. Changing this bind
address alone does not enable USB-to-LAN routing or make the router a USB host.

## Future router connection

The same service and `~/TomTomStorage` can be used on another trusted IP
network by setting `TOMTOM_RELAY_BIND` to the host's router-LAN address and
setting `TOMTOM_SERVICE_TOKEN`. The TomTom must first be confirmed to support
that router's USB-Ethernet host mode and be given a route to that address.
The USB network should never be bridged to public networks. No router-specific
assumptions are compiled into the service.
