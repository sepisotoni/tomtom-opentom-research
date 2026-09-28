# TomTom Google Weather Service

This folder is a self-contained Render web service. Render should use
`weather-service/render.yaml` as its Blueprint file; its `rootDir` is set to
`weather-service`. The service uses Python's standard library only.

## Google Cloud setup

1. Create or select a Google Cloud project and attach a billing account.
2. Enable **Weather API** (`weather.googleapis.com`) for that project.
3. Create a server-side API key under **Google Maps Platform → Credentials**.
4. Restrict the key to the Weather API. Add server-IP restrictions only if the
   outbound IPs used by the chosen Render plan are stable and configured.
5. Review the Weather API SKU pricing and configure API quotas and billing
   alerts. One request to this service makes three Google API calls.

Official references:

- [Weather API overview](https://developers.google.com/maps/documentation/weather/overview)
- [Current conditions](https://developers.google.com/maps/documentation/weather/current-conditions)
- [Hourly forecast](https://developers.google.com/maps/documentation/weather/hourly-forecast)
- [Daily forecast](https://developers.google.com/maps/documentation/weather/daily-forecast)
- [Usage and billing](https://developers.google.com/maps/documentation/weather/usage-and-billing)
- [Weather API policies and attribution](https://developers.google.com/maps/documentation/weather/policies)
- [Google Maps Platform API security best practices](https://developers.google.com/maps/api-security-best-practices)
- [Google Maps Platform getting started](https://developers.google.com/maps/get-started)

## Render deployment

1. In Render, create a Blueprint and select this repository's
   `weather-service/render.yaml`.
2. Set `GOOGLE_WEATHER_API_KEY` to the restricted Google key.
3. Set `WEATHER_SERVICE_TOKEN` to a new, high-entropy secret (for example,
   generate one with `openssl rand -hex 32`). Do not commit either secret.
4. Deploy and check the service's `/healthz` endpoint.

`/healthz` never reports secret configuration. Requests to the weather endpoint
must include the standard bearer-token authorization header.

## Request and response

Send an explicit location-sharing consent flag with a GPS fix:

```sh
AUTH_SCHEME="Be""arer"
curl --fail-with-body \
  -H "Authorization: ${AUTH_SCHEME} ${WEATHER_SERVICE_TOKEN}" \
  -H "Content-Type: application/json" \
  -d '{"latitude":48.8566,"longitude":2.3522,"location_sharing_enabled":true,"language_code":"en"}' \
  "https://YOUR-SERVICE.onrender.com/v1/weather"
```

Coordinates are rounded to two decimal places before leaving this service for
Google (roughly kilometre-scale, depending on latitude). Request bodies,
coordinates, authorization headers, and upstream URLs are never logged. The
response includes current conditions, up to 24 hourly periods, and up to 7 daily
condition summaries, and stays within 4 KiB. No coordinates or Google API key
are returned.
The rounded coordinates are still sent to Google, so the explicit consent flag
is required for each request.
The response also includes the current location's `timezone` IANA ID and
`timezone_offset_minutes`. The TomTom companion can write that signed offset
to `/mnt/sdcard/opentom/etc/weather_timezone_offset_minutes`; the watchface
applies it without requiring a timezone database on the device.

This service intentionally does **not** store or cache Google weather content:
Google's Weather API policies do not grant an exception to the Maps Platform
caching restrictions for forecast data. Responses use `Cache-Control: no-store`.
Only a one-hour cooldown keyed by a keyed digest of the rounded coordinates is
retained in memory to avoid duplicate billable refreshes. The digest is not
logged or returned. The 4 KiB bound applies to the response payload, not
permission to persist it. Check Google's current terms before changing this
behavior.

Display the exact attribution **“Source: Includes weather data from Google”**
on or next to weather content. The caller must obtain consent before sending a
device location; a false or absent consent flag is rejected.

### Error codes

The service returns a JSON `error` code for invalid requests, missing consent,
authentication failures, a per-location refresh inside one hour, missing
secrets, and Google/provider failures. Provider error bodies and credentials
are never returned to callers.
