# Power Button Investigation: 10-Second Continuous Hold & PIC Reset

## 1. GPIO Driver Architecture Analysis

Inspection of `project/kernel/drivers/barcelona/gpio/gpio.c` reveals two distinct, independent timer paths operating within the 5 Hz poll loop (`GPIO_POLL_DELAY = HZ / 5` = 200 ms per tick):

1. **Standard Shutdown Event Path**:
   ```c
   #define GPIO_SHUTDOWN_TIMEOUT (2) /* 2 ticks @ 5 Hz = 400 ms */
   ```
   When `IO_GetInput(ON_OFF)` is held, `power_button_timer` counts down from `GPIO_SHUTDOWN_TIMEOUT`.
   When it reaches 0 (after ~400–600 ms), the driver sets:
   ```c
   gpio_hw_status.u8InputStatus |= ONOFF_MASK;
   ```
   In user space, `applications/src/tools/power_button.c` detects `ONOFF_MASK` (`ID_BUTTON`) and spawns the configured job (e.g. `bin/suspend`).

2. **Hardware Pre-PIC Reset / Suicide Path**:
   ```c
   #define GPIO_PREPIC_TIMEOUT (10 * 5) /* 50 ticks @ 5 Hz = 10.0 seconds */
   ```
   If `IO_HavePicSecShutdown()` is true, a continuous hold decrements `power_button_picreset_timer`.
   When it reaches 0 (at 10 seconds), the driver invokes:
   ```c
   gpio_prepic_suicide();
   ```
   This routine disables interrupts and powers down the SD card / moviNAND bus controllers (`s3c_sdi_emergency_poweroff()`) to prevent filesystem corruption ~2 seconds before the physical PIC microcontroller performs an autonomous hard power cut.

---

## 2. Risk Analysis: 10-Second Continuous Press Requirement

The user desires that the device turn off **only** when the power button is held continuously for 10 seconds, while ignoring short presses.

### Critical Collision:
If `GPIO_SHUTDOWN_TIMEOUT` is set to exactly 50 (10 seconds):
- `power_button_timer` and `power_button_picreset_timer` expire at the exact same tick.
- `gpio_prepic_suicide()` shuts down memory card controllers immediately, preempting userland `power_button` and filesystem cache synchronization.
- If the physical PIC watchdog fires at 10.5 seconds, the TomTom abruptly reboots or cuts power rather than performing a clean shutdown.

### Proposed Safe Architecture:
1. **Decoupled Graceful Shutdown Threshold**:
   Set `GPIO_SHUTDOWN_TIMEOUT` to **45 ticks (~9.0 seconds)**.
   - 0.0s to 8.5s: Button release resets `power_button_timer`. No shutdown event is emitted.
   - At ~9.0s: Driver emits `ONOFF_MASK` to `power_button`. User space initiates clean unmounting, screen blanking, and sync.
   - At 10.0s: If system software is hung or unresponsive, `gpio_prepic_suicide()` executes as a fallback protection.
   - At ~10.5s–12.0s: Physical PIC hardware watchdog performs emergency reset.

2. **Low-Battery Safety (`ID_LOWBATT`)**:
   `low_dc_vcc_event_count` operates independently of `power_button_timer`.
   Low battery shutdown must remain immediate (at < 3.4V) to protect flash integrity and avoid battery deep discharge.

3. **Charger / Dock Interaction**:
   When external power (`ACPWR`) is active, ignition countdown timer (`ignition_timer`) ensures docking power cycles do not misinterpret power events as sustained button holds.

---

## 3. Physical Device Testing Plan

*Caution: Do not apply or test kernel modifications on the physical TomTom without an accessible serial console (JTAG or debug header) and full backup of the rootfs/SD card.*

1. **Step 1: Baseline Verification**:
   Verify userland `/dev/hwstatus` polling with `/mnt/sdcard/opentom/bin/power_button`.
2. **Step 2: Short-Press Rejection**:
   Press power button for 1 second, 3 seconds, and 5 seconds. Verify screen stays on and no shutdown script is triggered.
3. **Step 3: 9-Second Sustained Hold**:
   Hold power button continuously with a stopwatch. Verify `ONOFF_MASK` triggers clean suspend/halt at ~9.0 seconds before PIC hard reset.
4. **Step 4: Emergency 12-Second Hold on Freeze**:
   Intentionally freeze user space (`killall -STOP init`) and hold power button for 12 seconds to confirm PIC suicide hardware safety net remains fully intact.
