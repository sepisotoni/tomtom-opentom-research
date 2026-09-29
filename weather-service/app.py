#!/usr/bin/env python3
"""Small authenticated, no-store proxy for Google Maps Platform Weather API."""

import hashlib
import hmac
import json
import math
import os
import re
import threading
import time
from collections import OrderedDict
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit
from urllib.request import Request, urlopen
from zoneinfo import ZoneInfo


GOOGLE_WEATHER_BASE = "https://weather.googleapis.com/v1"
GOOGLE_ATTRIBUTION = "Source: Includes weather data from Google"
MAX_REQUEST_BYTES = 1024
MAX_RESPONSE_BYTES = 4096
LOCATION_PRECISION = 2
COOLDOWN_SECONDS = 3600
MAX_LOCATION_KEYS = 128
UPSTREAM_TIMEOUT_SECONDS = 12
CONDITION_MAX_CHARS = 32


class ServiceError(Exception):
    def __init__(self, status, code):
        super().__init__(code)
        self.status = status
        self.code = code


def _utc_now():
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _timezone_offset_minutes(timezone_id, at_utc):
    local_time = at_utc.astimezone(ZoneInfo(timezone_id))
    offset = local_time.utcoffset()
    if offset is None:
        raise ValueError("Timezone offset is unavailable.")
    return int(offset.total_seconds() // 60)


def _temperature(value):
    if not isinstance(value, dict):
        raise ValueError("Missing temperature.")
    degrees = value.get("degrees")
    if isinstance(degrees, bool) or not isinstance(degrees, (int, float)):
        raise ValueError("Invalid temperature.")
    if not math.isfinite(degrees) or not -100 <= degrees <= 100:
        raise ValueError("Temperature is outside supported bounds.")
    return int(round(degrees))


def _condition(value):
    if not isinstance(value, dict):
        raise ValueError("Missing condition.")
    description = value.get("description")
    text = description.get("text") if isinstance(description, dict) else None
    if not isinstance(text, str) or not text.strip():
        text = value.get("type")
    if not isinstance(text, str) or not text.strip():
        raise ValueError("Invalid condition.")
    return text.strip()[:CONDITION_MAX_CHARS]


def _icon_key(value):
    if not isinstance(value, dict):
        raise ValueError("Missing weather icon.")
    uri = value.get("iconBaseUri")
    if not isinstance(uri, str):
        raise ValueError("Invalid weather icon.")
    parsed = urlsplit(uri)
    prefix = "/weather/v1/"
    key = parsed.path[len(prefix):] if parsed.path.startswith(prefix) else ""
    if (
        parsed.scheme != "https"
        or parsed.netloc != "maps.gstatic.com"
        or parsed.path != f"/weather/v1/{key}"
        or not re.fullmatch(r"[a-z0-9_]{1,40}", key)
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("Invalid weather icon.")
    return key


def _precipitation_probability(value):
    precipitation = value.get("precipitation")
    probability = (
        precipitation.get("probability")
        if isinstance(precipitation, dict)
        else None
    )
    percent = probability.get("percent") if isinstance(probability, dict) else 0
    if isinstance(percent, bool) or not isinstance(percent, (int, float)):
        percent = 0
    return max(0, min(100, int(round(percent))))


def _parse_utc_timestamp(value):
    if not isinstance(value, str):
        raise ValueError("Invalid forecast timestamp.")
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("Forecast timestamp has no timezone.")
    return parsed.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class GoogleWeatherProvider:
    """Fetch and normalize the three Google Weather API resources."""

    def __init__(self, api_key, opener=urlopen):
        self.api_key = api_key
        self.opener = opener

    def _get_json(self, resource, latitude, longitude, language_code, **params):
        query = {
            "key": self.api_key,
            "location.latitude": f"{latitude:.2f}",
            "location.longitude": f"{longitude:.2f}",
            "unitsSystem": "METRIC",
            "languageCode": language_code,
        }
        query.update(params)
        request = Request(
            f"{GOOGLE_WEATHER_BASE}/{resource}?{urlencode(query)}",
            headers={"Accept": "application/json", "User-Agent": "TomTomWeather/1.0"},
        )
        try:
            with self.opener(request, timeout=UPSTREAM_TIMEOUT_SECONDS) as response:
                raw = response.read(256 * 1024 + 1)
        except (HTTPError, URLError, TimeoutError, OSError) as error:
            raise ServiceError(502, "weather_provider_unavailable") from error
        if len(raw) > 256 * 1024:
            raise ServiceError(502, "weather_provider_response_too_large")
        try:
            result = json.loads(raw.decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as error:
            raise ServiceError(502, "invalid_weather_provider_response") from error
        if not isinstance(result, dict):
            raise ServiceError(502, "invalid_weather_provider_response")
        return result

    def fetch(self, latitude, longitude, language_code):
        current = self._get_json(
            "currentConditions:lookup", latitude, longitude, language_code
        )
        hourly = self._get_json(
            "forecast/hours:lookup",
            latitude,
            longitude,
            language_code,
            hours=24,
            pageSize=24,
        )
        daily = self._get_json(
            "forecast/days:lookup",
            latitude,
            longitude,
            language_code,
            days=7,
            pageSize=7,
        )
        try:
            hourly_records = hourly["forecastHours"]
            daily_records = daily["forecastDays"]
            if not isinstance(hourly_records, list) or not hourly_records:
                raise ValueError("Missing hourly forecast.")
            if not isinstance(daily_records, list) or not daily_records:
                raise ValueError("Missing daily forecast.")

            current_weather = {
                "temperature": _temperature(current["temperature"]),
                "condition": _condition(current["weatherCondition"]),
                "icon_key": _icon_key(current["weatherCondition"]),
            }
            hourly_weather = []
            for item in hourly_records[:24]:
                interval = item.get("interval")
                start_time = (
                    interval.get("startTime") if isinstance(interval, dict) else None
                )
                hourly_weather.append({
                    "timestamp_utc": _parse_utc_timestamp(start_time),
                    "temperature": _temperature(item["temperature"]),
                    "condition": _condition(item["weatherCondition"]),
                    "precipitation_probability": _precipitation_probability(item),
                })

            daily_weather = []
            for item in daily_records[:7]:
                date_value = item.get("displayDate")
                if isinstance(date_value, dict):
                    date_text = "{:04d}-{:02d}-{:02d}".format(
                        date_value["year"], date_value["month"], date_value["day"]
                    )
                else:
                    interval = item.get("interval")
                    start_time = (
                        interval.get("startTime")
                        if isinstance(interval, dict)
                        else None
                    )
                    date_text = _parse_utc_timestamp(start_time)[:10]
                daytime = item.get("daytimeForecast")
                nighttime = item.get("nighttimeForecast")
                daytime_condition = _condition(
                    daytime.get("weatherCondition")
                    if isinstance(daytime, dict)
                    else None
                )
                daily_weather.append({
                    "date": date_text,
                    "condition": daytime_condition,
                    "high": _temperature(item["maxTemperature"]),
                    "low": _temperature(item["minTemperature"]),
                })

            fetched_at = datetime.now(timezone.utc)
            time_zone = current.get("timeZone")
            timezone_id = time_zone.get("id") if isinstance(time_zone, dict) else ""
            if not isinstance(timezone_id, str) or not timezone_id:
                raise ValueError("Missing timezone ID.")
            return {
                "version": 1,
                "provider": "google_maps_weather",
                "fetched_at_utc": fetched_at.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "timezone": timezone_id[:64],
                "timezone_offset_minutes": _timezone_offset_minutes(
                    timezone_id, fetched_at
                ),
                "current": current_weather,
                "hourly": hourly_weather,
                "daily": daily_weather,
                "attribution": GOOGLE_ATTRIBUTION,
                "cache_policy": "no-store",
            }
        except (KeyError, TypeError, ValueError, OverflowError) as error:
            raise ServiceError(502, "invalid_weather_provider_response") from error


class WeatherBackend:
    def __init__(self, api_key, service_token, provider=None, clock=time.monotonic):
        self.api_key = api_key
        self.service_token = service_token
        self.provider = provider or GoogleWeatherProvider(api_key)
        self.clock = clock
        self._lock = threading.Lock()
        self._last_request_by_location = OrderedDict()

    @staticmethod
    def validate_request(payload):
        if not isinstance(payload, dict):
            raise ServiceError(400, "invalid_request")
        if payload.get("location_sharing_enabled") is not True:
            raise ServiceError(403, "location_sharing_consent_required")
        latitude = payload.get("latitude")
        longitude = payload.get("longitude")
        if (
            isinstance(latitude, bool)
            or not isinstance(latitude, (int, float))
            or isinstance(longitude, bool)
            or not isinstance(longitude, (int, float))
            or not math.isfinite(latitude)
            or not math.isfinite(longitude)
            or not -90 <= latitude <= 90
            or not -180 <= longitude <= 180
        ):
            raise ServiceError(400, "invalid_coordinates")
        language_code = payload.get("language_code", "en")
        if (
            not isinstance(language_code, str)
            or len(language_code) > 35
            or not re.fullmatch(r"[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*", language_code)
        ):
            raise ServiceError(400, "invalid_language_code")
        return (
            round(float(latitude), LOCATION_PRECISION),
            round(float(longitude), LOCATION_PRECISION),
            language_code,
        )

    def get_weather(self, payload, authorization):
        if not self.api_key or not self.service_token:
            raise ServiceError(503, "weather_service_not_configured")
        expected = "Be" + "arer " + self.service_token
        if (
            not isinstance(authorization, str)
            or not hmac.compare_digest(authorization, expected)
        ):
            raise ServiceError(401, "unauthorized")
        latitude, longitude, language_code = self.validate_request(payload)
        location_hash = hmac.new(
            self.service_token.encode("utf-8"),
            f"{latitude:.2f},{longitude:.2f}".encode("ascii"),
            hashlib.sha256,
        ).hexdigest()
        now = self.clock()
        with self._lock:
            previous = self._last_request_by_location.get(location_hash)
            if previous is not None and now - previous < COOLDOWN_SECONDS:
                raise ServiceError(429, "location_refresh_limited")
            self._last_request_by_location[location_hash] = now
            self._last_request_by_location.move_to_end(location_hash)
            while len(self._last_request_by_location) > MAX_LOCATION_KEYS:
                self._last_request_by_location.popitem(last=False)
        return self.provider.fetch(latitude, longitude, language_code)


class WeatherRequestHandler(BaseHTTPRequestHandler):
    backend = None
    server_version = "TomTomWeather"
    sys_version = ""

    def log_message(self, format_string, *args):
        # Do not log request bodies, coordinates, authorization headers, or API keys.
        return

    def _send_json(self, status, payload):
        body = json.dumps(
            payload, ensure_ascii=False, separators=(",", ":"), allow_nan=False
        ).encode("utf-8")
        if len(body) > MAX_RESPONSE_BYTES:
            status = 502
            body = b'{"error":"weather_response_exceeds_4k_limit"}'
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/healthz":
            self._send_json(200, {"status": "ok"})
        else:
            self._send_json(404, {"error": "not_found"})

    def do_POST(self):
        if self.path != "/v1/weather":
            self._send_json(404, {"error": "not_found"})
            return
        content_length = self.headers.get("Content-Length")
        try:
            size = int(content_length)
        except (TypeError, ValueError):
            self._send_json(400, {"error": "invalid_content_length"})
            return
        if size <= 0 or size > MAX_REQUEST_BYTES:
            self._send_json(413, {"error": "request_body_size_invalid"})
            return
        try:
            payload = json.loads(self.rfile.read(size).decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError):
            self._send_json(400, {"error": "invalid_json"})
            return
        try:
            result = self.backend.get_weather(
                payload, self.headers.get("Authorization")
            )
        except ServiceError as error:
            self._send_json(error.status, {"error": error.code})
            return
        self._send_json(200, result)


def main():
    api_key = os.environ.get("GOOGLE_WEATHER_API_KEY", "")
    service_token = os.environ.get("WEATHER_SERVICE_TOKEN", "")
    port = int(os.environ.get("PORT", "10000"))
    WeatherRequestHandler.backend = WeatherBackend(api_key, service_token)
    server = ThreadingHTTPServer(("0.0.0.0", port), WeatherRequestHandler)
    server.daemon_threads = True
    print(f"Weather service listening on port {port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
