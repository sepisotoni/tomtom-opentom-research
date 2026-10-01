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

1. **Baseline**: Confirm the saved `ttsystem`, startup script, and power-button
   executable rollback copies are present before rebooting.
2. **Quick action**: Press briefly and verify the user-space action log
   reports a measured duration at or below 250 ms and the info panel
   toggles. Allow for the GPIO sampler's approximately 20 ms resolution.
3. **Unmapped interval**: A press reported from 251 through 399 ms should log
   `action=none` and should not suspend.
4. **Suspend action**: A press reported at 400 ms or longer runs the configured
   suspend command on release. Test only when a wake/recovery path is known.
5. **Hardware fallback**: The independent PIC cutoff remains at 10 seconds.
   Do not deliberately test the emergency cutoff without a separate recovery
   plan; it can remove power before user-space sync completes.

## 4. Generic Press-Duration Event Interface

The duration-aware implementation keeps gesture policy in user space:

- The GPIO driver samples only the power-button input at 50 Hz (about 20 ms
  resolution). Other GPIO status and the PIC fallback timer remain on their
  original 5 Hz cadence.
- On release, the driver latches a monotonically increasing event sequence and
  measured duration in milliseconds. `IOR_BUTTON_EVENT` reads that record;
  `ONOFF_MASK` remains a generic notification that a button press completed.
- The kernel does not classify short, long, or double presses and does not
  choose an action. `power_button` reads `etc/power-button.cfg`, reports the
  measured duration in its log, and selects the configured user-space action.
- The initial configuration assigns presses through 250 ms to the watchface
  info-panel toggle, 251–399 ms to no action, and presses of 400 ms or longer
  to the existing suspend action. Low-battery handling remains independent.
- The hardware PIC reset path remains at 10 seconds. The regular GPIO status
  scan, ignition timing, and dock power-cycle timing remain at their original
  5 Hz cadence.

The measured time is quantized by the 20 ms sample interval and can vary with
switch bounce. Validate the short-press range on the physical unit before
relying on it for other gestures. The OS-side thresholds and action paths can
be changed in `/mnt/sdcard/opentom/etc/power-button.cfg` without rebuilding the
kernel; send `SIGHUP` to `power_button` to reload the file without restarting
the daemon.
