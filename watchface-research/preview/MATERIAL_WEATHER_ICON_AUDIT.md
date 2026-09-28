# Material Design weather-icon audit

Checked `google/material-design-icons` under `src/` on 2026-09-28. The
weather-related subset contains 17 symbols and more than 60 SVG files across
the classic icon styles. The classic variants are Material Icons, Outlined,
Round, Sharp, and Two Tone; newer `home` condition symbols generally have only
the default `materialicons` style. Files are mostly 24px SVGs; `cloudy_snowing`
also has a 20px file.

| Source category | Symbols |
|---|---|
| `image` | `grain`, `thermostat_auto`, `wb_cloudy`, `wb_sunny` |
| `device` | `air`, `device_thermostat`, `storm`, `thermostat`, `water` |
| `home` | `cloudy_snowing`, `foggy`, `snowing`, `sunny`, `sunny_snowing`, `wind_power` |
| `places` | `ac_unit`, `water_damage` |

The most useful condition glyphs are `wb_sunny` / `sunny`, `wb_cloudy`,
`cloudy_snowing`, `foggy`, `snowing`, `sunny_snowing`, and `storm`.
`air`, `thermostat`, `device_thermostat`, `thermostat_auto`, `water`, `grain`,
`ac_unit`, `wind_power`, and `water_damage` are supporting measurement,
environment, or hazard symbols rather than direct forecast conditions.

No dedicated plain-rain icon appeared in the audited categories. Do not label
`cloudy_snowing` or `storm` as ordinary rain; obtain a separately licensed
rain glyph if the forecast UI needs that condition.

Upstream source: <https://github.com/google/material-design-icons>. The
repository's `LICENSE` is Apache-2.0. Selected 24px SVGs are stored in
`assets/material-weather/` with the upstream Apache license. Their upstream
paths and pinned source revision are recorded in `assets/material-weather/README.md`.

This is an asset inventory only. The preview still has no real weather feed,
forecast mapping, location permission flow, or verified weather telemetry.
