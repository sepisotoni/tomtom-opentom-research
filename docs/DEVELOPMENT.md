# Development, builds, and validation

Run commands from the repository root unless a command explicitly changes
directory. Build outputs should stay under `/tmp`, an ignored build directory,
or another local-only path; do not commit generated device executables.

## Desktop Face Studio

```sh
./run_studio.sh
python3 test_studio.py
python3 test_ttface_package.py
python3 test_gallery_package.py
python3 test_device_control.py
python3 test_weather_cache.py
```

Build a standalone executable with `python3 build_studio.py`. The result is
host-specific; build on Windows to produce a Windows `.exe`.

## Preview artwork and parser tests

```sh
python3 -m unittest watchface-research/project/tests/test_convert_icon.py
gcc -std=c89 -pedantic -Wall -Wextra -Werror -O2 \
  watchface-research/preview/info_anim/info_anim.c \
  watchface-research/preview/info_anim/test_info_anim.c \
  -o /tmp/test-info-anim
/tmp/test-info-anim
gcc -std=c89 -pedantic -Wall -Wextra -Werror -O2 \
  watchface-research/preview/notification_event_test.c \
  -o /tmp/test-notification-event
/tmp/test-notification-event
```

`info_anim/test_info_anim.c` and `notification_event_test.c` are native C tests;
the first links `info_anim.c`, while the notification test is header-only. The
animation module is C89 and should be tested at state boundaries as well as
visually on hardware.

Regenerate rounded/font atlases with the documented scripts in
`watchface-research/preview/README.md`. Keep original editable assets alongside
generated C headers or PGM atlases. Verify source license and attribution before
adding artwork.

## Exact ARM preview build

The target build needs the OpenTom ARM GCC 3.3.4 compiler and the matching
Nano-X/kernel headers and library. Paths differ between machines; configure
them locally rather than assuming a hard-coded home directory:

```sh
ARM_GCC=/path/to/OpenTom/gcc-3.3.4_glibc-2.3.2/bin/arm-linux-gcc
NANOX=/path/to/microwindows
KERNEL=/path/to/linux-s3c24xx

cd watchface-research/preview
"$ARM_GCC" -Wall -W -Werror -O2 -DWATCHFACE_DIRECT_FB \
  -I"$NANOX/src/include" -I"$KERNEL/include" \
  live_watchface.c info_anim/info_anim.c \
  -L"$NANOX/src/lib" -lnano-X -lm \
  -o /tmp/watchface-new
```

This exact build enables direct framebuffer presentation. Remove
`-DWATCHFACE_DIRECT_FB` only when intentionally building the Nano-X windowed
presentation. Verify the result is an ARM executable and do not claim this
build passed unless it was actually run with that compiler/sysroot.

The matching daemon sources are:

```sh
"$ARM_GCC" -Wall -W -Werror -O2 \
  ../project/src/opentom_skel/bin/tomtom-control.c \
  -o /tmp/tomtom-control
"$ARM_GCC" -Wall -W -Werror -O2 \
  ../project/src/opentom_skel/bin/weather-sync.c \
  -o /tmp/weather-sync
```

Compile from a working directory where the relative source paths above exist
(or use repository-root absolute paths).

## Host relay and weather service

```sh
cargo test --manifest-path tomtom-relay/Cargo.toml
python3 weather-service/test_app.py
```

The relay needs Rust/Cargo and host `curl` for HTTPS forwarding. The weather
service uses Python's standard library; its Render Blueprint lives at
`weather-service/render.yaml`. The expected Render start command is `python
app.py`, with `rootDir: weather-service`.

Never place `GOOGLE_WEATHER_API_KEY`, `WEATHER_SERVICE_TOKEN`, or
`TOMTOM_SERVICE_TOKEN` in the repository. Use local environment configuration
with restrictive permissions and redact secrets from logs/screenshots.

## Before opening or pushing a change

1. Run tests covering the changed subsystem.
2. Check `git diff --check`.
3. Review `git status --short`, staged paths, generated artifacts, and the
   actual diff. Do not stage unrelated user work.
4. For device changes, separately record the build, transfer, runtime status,
   and actual device capture. A unit test or successful transfer is not a
   visual/runtime verification.
