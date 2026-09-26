# Collected hardware and source facts

These facts come from the LeddaZ/OpenTom checkout and prior read-only checks of
the attached TomTom ONE v6. They are evidence to guide investigation, not
proof that the current device image has every source-tree feature enabled.

## Device-reported facts

Observed in `/proc/barcelona` on the device:

- `modelid`: `19`
- `modelname`: `TomTom ONE`
- `usbname`: `ONE (v6)`
- `familyname`: `TomTom ONE`
- `cputype`: `4`
- `gpsdev`: `ttySAC1`
- `gpstype`: `128`
- `bluetooth`: `0`
- `btchip`: `0`
- `btdev`: empty

Earlier, `/dev/bt` was absent. Bluetooth protocol modules existed in the kernel,
but the TomTom reported no Bluetooth chip/device.

## RTC source/config evidence

The copied `project/kernel/.config` contains:

```text
CONFIG_RTC=m
CONFIG_S3C2410_RTC=y
CONFIG_S3C2410_RTC_SETTIMEOFDAY=y
# CONFIG_S3C2410_RTC_GETTIMEOFDAY is not set
```

The kernel source includes `project/kernel/drivers/char/s3c2410-rtc.c`. The
SoC RTC driver supports register access, but the GETTIMEOFDAY option that
reads RTC time into system time is disabled in this config; SETTIMEOFDAY
enables setting the RTC from system time. Determine whether the running image
matches this exact config and whether the firmware/init process separately
reads the RTC. Prior interactive checks did not confirm `/dev/rtc` or
`/proc/driver/rtc`; treat physical RTC availability and retention as
unverified until tested.

A device battery may keep the RTC backup domain alive, but merely having an
internal battery or RTC hardware does not establish correct time retention.
Use a controlled test on the physical unit: set known correct time, fully
shut down (not just suspend), wait, start, compare RTC and system time, then
repeat after a longer interval. Protect user data and do not perform this test
without owner approval.

## Power-button source facts

From `project/kernel/drivers/barcelona/gpio/gpio.c`:

```c
#define GPIO_POLL_DELAY (HZ / 5)     /* 5 polls / sec */
#define GPIO_PREPIC_TIMEOUT (10 * 5) /* 10 seconds */
#define GPIO_SHUTDOWN_TIMEOUT (2)    /* 400 ms. Actual event is sent between 400-600 ms*/
```

The driver independently handles a normal ON/OFF status event after
`GPIO_SHUTDOWN_TIMEOUT` and a 10-second pre-PIC reset/suicide path when the
platform indicates that path is supported. `applications/src/tools/power_button.c`
polls `/dev/hwstatus` and executes the configured button/low-battery commands;
the OpenTom startup template currently supplies `bin/suspend` for both.

Therefore current source does **not** mean ordinary power-off waits ten
seconds. Making a short press change faces and reserving power-off for a
10-second hold would require carefully changing the GPIO event/shutdown
behavior and validating interactions with suspend, low battery, charger, and
PIC reset. Do not implement based on the timeout constant alone.

## GPS/time facts

The model reports a GPS UART at `ttySAC1` with `gpstype=128`. This does not
prove a GPS receiver currently has a fix or that user space can read a valid
UTC timestamp. Inspect existing GPS startup/listener utilities and NMEA
permissions/protocol before proposing GPS as a clock source. GPS time must be
validated for fix quality and UTC/date handling.

## Network facts

USB gadget Ethernet at `192.168.101.115` was observed while tethered to a
Linux host. A host assigns/routs networking in that arrangement. A generic
router USB connector does not necessarily implement USB Ethernet host mode,
DHCP, forwarding, DNS, internet access, or NTP. No always-on router connection
or automatic update channel has been validated.

## Timezone

The startup script previously used `TZ=CEST-2`. This is a fixed summer offset,
not a year-round daylight-saving rule. Prefer a valid Europe/Paris timezone
database entry if the root filesystem contains zoneinfo; otherwise document
and test an appropriate compact daylight-saving-aware alternative.
