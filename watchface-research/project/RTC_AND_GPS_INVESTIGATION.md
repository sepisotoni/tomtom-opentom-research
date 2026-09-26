# Hardware Timekeeping Audit: S3C2412 RTC & GPS on TomTom ONE v6

## 1. RTC Driver & Kernel Configuration Audit

The repository contains the kernel configuration excerpt and driver source (`project/kernel/drivers/char/s3c2410-rtc.c`):

```text
CONFIG_RTC=m
CONFIG_S3C2410_RTC=y
CONFIG_S3C2410_RTC_SETTIMEOFDAY=y
# CONFIG_S3C2410_RTC_GETTIMEOFDAY is not set
```

### Key Technical Findings:

1. **Inverted Driver Naming**:
   In the Simtec S3C2410 RTC driver:
   - `CONFIG_S3C2410_RTC_SETTIMEOFDAY` implements:
     ```c
     s3c2410_rtc_gettime(&tm);
     rtc_tm_to_time(&tm, &time.tv_sec);
     do_settimeofday(&time);
     ```
     During `s3c2410_rtc_probe()`, it reads the hardware BCD registers (`RTCMIN`, `RTCHOUR`, `RTCDATE`, `RTCMON`, `RTCYEAR`, `RTCSEC`) and initializes the kernel's system clock (`xtime`) from the RTC.
   - `CONFIG_S3C2410_RTC_GETTIMEOFDAY` implements:
     ```c
     do_gettimeofday(&time);
     rtc_time_to_tm(time.tv_sec, &tm);
     s3c2410_rtc_settime(&tm);
     ```
     This routine reads the Linux kernel system time and writes it to the hardware RTC registers.
   
2. **Missing RTC Writeback on Shutdown**:
   Because `CONFIG_S3C2410_RTC_GETTIMEOFDAY` is **not set** in `.config`:
   - `s3c2410_rtc_shutdown` is defined as `NULL`.
   - On shutdown or suspend, the kernel **never writes system time back into the RTC**.
   - If the system time was updated by user space, network NTP, or GPS, that time is lost on power down unless user space manually writes it using `hwclock -w` via `/dev/rtc`.

3. **User Space Device Node `/dev/rtc`**:
   `CONFIG_RTC=m` means the generic Linux RTC subsystem (`drivers/char/rtc.c`) is built as a loadable module (`rtc.ko`).
   If `rtc.ko` is not explicitly inserted (`insmod /lib/modules/.../rtc.ko`), the character device `/dev/rtc` (major 10, minor 135) does not exist, causing userland tools (`hwclock`) to fail.

4. **Hardware Backup Domain Constraints**:
   The Samsung S3C2412 SoC contains an internal RTC powered via `VDDi_RTC` and `VDD_RTCext`.
   On the TomTom ONE v6, there is no dedicated lithium coin cell (e.g. CR2032). The RTC power rail is backed either by a small rechargeable supercap/coin cell trickle-charged from the main battery, or directly tapped from the main single-cell Lithium-Ion battery (3.7V).
   - If the main battery discharges below ~3.0V or is disconnected, the RTC registers lose power and reset to their uninitialized default (typically year 2000 or 1970).
   - Supercaps on older GPS units (15+ years old) often experience high ESR or dielectric breakdown, failing to retain time for more than a few minutes after power disconnection.

---

## 2. GPS on `ttySAC1` (`gpstype=128`)

Observed device metadata reports: `gpsdev=ttySAC1`, `gpstype=128`.

### Key Technical Findings:

1. **Hardware Transceiver**:
   `gpstype=128` designates a SiRF Star III GPS receiver wired to UART channel 1 (`/dev/ttySAC1`).
2. **Power Gating**:
   The GPS receiver is power-gated through a GPIO pin (`GPS_POWER` / `GPS_ON`). At boot, the GPS module may remain powered down until initialized by software.
3. **Fix Requirement**:
   GPS time cannot be used as an immediate clock source on boot:
   - A cold fix requires 35–45 seconds under open sky.
   - Indoors or without an external patch antenna view, the receiver will never obtain a 3D satellite lock.
   - NMEA `$GPRMC` sentences output status `'V'` (Navigation receiver warning / invalid) during search mode. Only when status transitions to `'A'` (Valid) is the UTC timestamp valid.
   - An unconditional blocking read at boot would stall system startup indefinitely if satellite signal is unavailable.

---

## 3. Recommended Physical Validation Test Plans

### A. RTC Retention & Read Verification Test:
*Note: Do not run automatically; perform under supervised physical testing.*
1. Power on device with external power connected.
2. Check if `/dev/rtc` exists:
   ```sh
   ls -l /dev/rtc
   [ ! -e /dev/rtc ] && insmod /lib/modules/$(uname -r)/kernel/drivers/char/rtc.ko
   ```
3. Set a specific known timestamp:
   ```sh
   date -s "2026-09-26 12:00:00"
   hwclock -w -u
   hwclock -r -u
   ```
4. Perform clean shutdown:
   ```sh
   poweroff
   ```
5. Disconnect USB power cord.
6. Test intervals:
   - Test 1: Wait 5 minutes disconnected, power on, and inspect `date` and `hwclock -r`.
   - Test 2: Wait 2 hours disconnected, power on, and inspect `date` and `hwclock -r`.
7. **Evaluation**:
   - If date persists accurately: battery backup domain is operational.
   - If date resets to 2000-01-01: RTC backup battery is absent or degraded.

### B. GPS `ttySAC1` Stream Validation Test:
1. Verify baud rate and serial read permissions:
   ```sh
   stty -F /dev/ttySAC1 4800 cs8 -cstopb -parenb
   cat /dev/ttySAC1 | grep -m 5 "^\$GPRMC"
   ```
2. Inspect field 2 of `$GPRMC`:
   `$GPRMC,123456.000,A,...` -> 'A' = valid UTC lock; 'V' = search mode.
3. Background time-sync daemon pattern:
   Only trigger `date -u -s` and `hwclock -w` after verifying fix status `'A'`.
