# TomTom storage and rollback policy

The TomTom SD card is limited storage. Keep runtime dependencies, not the
history of every build, on the device. Treat this policy as the default for
future renderer deployments and cleanup work.

## Runtime directories

The OpenTom distribution root is `/mnt/sdcard/opentom`. Files that are needed
at boot/runtime include, as configured by the deployed `start.sh`:

- `start.sh` and `bin/` startup executables/scripts (including
  `watchface-main`, `tomtom-control`, and `weather-sync` when enabled);
- `lib/`, `etc/`, Nano-X assets/configuration, logs, and support files actually
  referenced by the boot script;
- `preview-gallery/modern-20260928/watchface.new`;
- the gallery's configured PGM atlases and any runtime-loaded assets;
- `preview-gallery/watchface.cfg` and `preview-gallery/current_face`.

This is an observed layout, not an exhaustive universal manifest. Before
removing anything, inspect the *deployed* startup script, `watchface.cfg`,
open files, and runtime file accesses. Do not remove shared libraries,
atlases, configs, current-face state, logs still needed for diagnosis, or
unknown files just because they look old.

## Deployment retention rules

- Keep only **one** known-good rollback renderer on the device.
- Keep rollback executables and dated experiment history on the host, outside
  the repository unless explicitly versioned source artifacts are intended.
- Do not keep one backup per iteration on the SD card.
- Back up the current binary before an upgrade; never delete the last known
  working version until the replacement has started and has been verified on
  the physical display.
- Archive a backup to host storage and verify its byte count and SHA-256 before
  deleting older device copies. If host archival is not possible, keep one
  conservative on-device rollback and postpone bulk cleanup.
- Do not archive API keys, weather responses, GPS/location history, or
  unredacted credentials as part of device backups.
- Clean temporary build transfers and capture staging files after use.

## Safe cleanup sequence

1. Verify the TomTom is reachable on its directly attached USB subnet and read
   the live deployment directory. If it is unreachable, do not claim or
   attempt remote cleanup.
2. Classify each file as runtime-required, current executable, the one selected
   rollback, diagnosable log, or obsolete build/backup.
3. Copy obsolete executables to a host-only archive outside the Git worktree.
   Verify size and SHA-256 against the source.
4. List the exact obsolete paths and check free space before deletion. Never
   use broad wildcards, recursive directory deletion, or `rm -rf` against a
   runtime tree.
5. Delete only the enumerated obsolete backup files, one explicit path at a
   time. Keep the sole rollback and all verified runtime dependencies.
6. Check free space again, verify renderer/control/weather service health and
   face selection, then inspect a fresh display capture.
7. Record what was archived/deleted, resulting free space, and any files kept
   because their role was uncertain. Keep the host archive outside Git.

Use available BusyBox applets only after checking their actual support/version;
the target image is old and omits common commands such as `tail` or `grep -E`.
Prefer explicit `ls -l`, `df`, `sha256sum` (if present), and small bounded
commands. Do not rely on GNU options on the device.

## Last observed state (2026-10-01)

- Active gallery path:
  `/mnt/sdcard/opentom/preview-gallery/modern-20260928/watchface.new`.
- Face selection was reported as `OK FACE 8`.
- The renderer opened `/dev/fb` and logged
  `DIRECT_FB=320x240 RGB565 stride=640 offset=0,0`.
- A 62,735-byte compact-attribution build was installed and launched; a fresh
  capture was saved locally as
  `~/Downloads/TomTom-weather-compact-map-logo-20261001T0349.png`.
- A single pre-install renderer rollback was added as
  `watchface.new.before-compact-maps-logo-20261001T034838`.
- Earlier iterations had many dated `watchface.new.before-*` binaries in the
  same gallery directory. They were not archived or deleted in that session.
- A later connection attempt found no route to the device, so those older
  backups were not cleaned up. Reconnect and inspect/verify them before cleanup;
  do not assume this historical file list is still current.

No device cleanup is complete until the safe cleanup sequence is carried out
while the SD card is accessible.
