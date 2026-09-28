import json
import unittest
from datetime import datetime, timezone
from io import BytesIO
from urllib.parse import parse_qs, urlparse

from app import (
    GOOGLE_ATTRIBUTION,
    GoogleWeatherProvider,
    MAX_RESPONSE_BYTES,
    ServiceError,
    WeatherBackend,
    _timezone_offset_minutes,
)

AUTHORIZATION = "Be" + "arer service-secret"


class FakeGoogleResponse(BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class GoogleWeatherProviderTests(unittest.TestCase):
    def test_calls_google_endpoints_and_normalizes_forecast(self):
        responses = [
            {
                "timeZone": {"id": "Europe/Paris"},
                "temperature": {"degrees": 18.4, "unit": "CELSIUS"},
                "weatherCondition": {
                    "description": {"text": "Clear"},
                    "type": "CLEAR",
                },
            },
            {
                "forecastHours": [
                    {
                        "interval": {"startTime": "2026-09-28T12:00:00Z"},
                        "temperature": {"degrees": 19.7},
                        "weatherCondition": {
                            "description": {"text": "Light rain"},
                        },
                        "precipitation": {"probability": {"percent": 45}},
                    }
                ]
            },
            {
                "forecastDays": [
                    {
                        "displayDate": {"year": 2026, "month": 9, "day": 28},
                        "daytimeForecast": {
                            "weatherCondition": {
                                "description": {"text": "Cloudy"},
                            }
                        },
                        "nighttimeForecast": {
                            "weatherCondition": {
                                "description": {"text": "Clear"},
                            }
                        },
                        "maxTemperature": {"degrees": 21.2},
                        "minTemperature": {"degrees": 12.6},
                    }
                ]
            },
        ]
        requests = []

        def opener(request, timeout):
            requests.append((request, timeout))
            return FakeGoogleResponse(
                json.dumps(responses[len(requests) - 1]).encode("utf-8")
            )

        provider = GoogleWeatherProvider("server-only-key", opener=opener)
        result = provider.fetch(48.86, 2.35, "en")

        self.assertEqual(len(requests), 3)
        parsed = [
            (urlparse(request.full_url), parse_qs(urlparse(request.full_url).query))
            for request, _ in requests
        ]
        self.assertEqual(
            [item[0].path for item in parsed],
            [
                "/v1/currentConditions:lookup",
                "/v1/forecast/hours:lookup",
                "/v1/forecast/days:lookup",
            ],
        )
        self.assertEqual(parsed[1][1]["hours"], ["24"])
        self.assertEqual(parsed[2][1]["days"], ["7"])
        for _, query in parsed:
            self.assertEqual(query["key"], ["server-only-key"])
            self.assertEqual(query["location.latitude"], ["48.86"])
            self.assertEqual(query["location.longitude"], ["2.35"])
        self.assertEqual(result["current"]["temperature"], 18)
        self.assertEqual(result["hourly"][0]["temperature"], 20)
        self.assertEqual(result["hourly"][0]["precipitation_probability"], 45)
        self.assertEqual(result["daily"][0]["condition"], "Cloudy")
        self.assertEqual(
            result["timezone_offset_minutes"],
            _timezone_offset_minutes(
                "Europe/Paris",
                datetime.fromisoformat(
                    result["fetched_at_utc"].replace("Z", "+00:00")
                ),
            ),
        )
        self.assertEqual(result["attribution"], GOOGLE_ATTRIBUTION)
        self.assertEqual(result["cache_policy"], "no-store")

    def test_timezone_offset_tracks_daylight_saving(self):
        winter = datetime(2026, 1, 15, tzinfo=timezone.utc)
        summer = datetime(2026, 7, 15, tzinfo=timezone.utc)
        self.assertEqual(_timezone_offset_minutes("Europe/Paris", winter), 60)
        self.assertEqual(_timezone_offset_minutes("Europe/Paris", summer), 120)


class FakeProvider:
    def __init__(self):
        self.calls = []

    def fetch(self, latitude, longitude, language_code):
        self.calls.append((latitude, longitude, language_code))
        return {
            "version": 1,
            "provider": "google_maps_weather",
            "fetched_at_utc": "2026-09-28T11:00:00Z",
            "timezone": "Europe/Paris",
            "current": {"temperature": 18, "condition": "Clear"},
            "hourly": [
                {
                    "timestamp_utc": f"2026-09-28T{hour:02d}:00:00Z",
                    "temperature": 18,
                    "condition": "Partly cloudy",
                    "precipitation_probability": 5,
                }
                for hour in range(24)
            ],
            "daily": [
                {
                    "date": f"2026-09-{28 + day:02d}",
                    "condition": "x" * 32,
                }
                for day in range(7)
            ],
            "attribution": GOOGLE_ATTRIBUTION,
            "cache_policy": "no-store",
        }


class WeatherBackendTests(unittest.TestCase):
    def setUp(self):
        self.provider = FakeProvider()
        self.now = [10000.0]
        self.backend = WeatherBackend(
            "google-secret",
            "service-secret",
            provider=self.provider,
            clock=lambda: self.now[0],
        )
        self.payload = {
            "latitude": 48.856612,
            "longitude": 2.352221,
            "location_sharing_enabled": True,
            "language_code": "en",
        }

    def test_explicit_consent_rounds_coordinates_and_returns_bounded_no_store_data(self):
        result = self.backend.get_weather(
            self.payload, AUTHORIZATION
        )
        self.assertEqual(self.provider.calls, [(48.86, 2.35, "en")])
        self.assertEqual(len(result["hourly"]), 24)
        self.assertEqual(len(result["daily"]), 7)
        self.assertEqual(result["attribution"], GOOGLE_ATTRIBUTION)
        self.assertEqual(result["cache_policy"], "no-store")
        body = json.dumps(result, separators=(",", ":")).encode("utf-8")
        self.assertLessEqual(len(body), MAX_RESPONSE_BYTES)
        self.assertNotIn("latitude", result)
        self.assertNotIn("longitude", result)

    def test_maximum_forecast_payload_stays_under_four_kib(self):
        result = self.backend.get_weather(
            self.payload, AUTHORIZATION
        )
        result["current"]["condition"] = "x" * 32
        encoded = json.dumps(
            result, ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self.assertLessEqual(len(encoded), MAX_RESPONSE_BYTES)

    def test_missing_location_consent_is_rejected_before_provider_call(self):
        self.payload["location_sharing_enabled"] = False
        with self.assertRaises(ServiceError) as error:
            self.backend.get_weather(self.payload, AUTHORIZATION)
        self.assertEqual(error.exception.status, 403)
        self.assertEqual(self.provider.calls, [])

    def test_bad_coordinates_are_rejected(self):
        self.payload["latitude"] = 91
        with self.assertRaises(ServiceError) as error:
            self.backend.get_weather(self.payload, AUTHORIZATION)
        self.assertEqual(error.exception.code, "invalid_coordinates")

    def test_authentication_is_required(self):
        with self.assertRaises(ServiceError) as error:
            self.backend.get_weather(self.payload, "Bearer wrong")
        self.assertEqual(error.exception.status, 401)
        self.assertEqual(self.provider.calls, [])

    def test_location_refresh_is_limited_to_once_per_hour(self):
        self.backend.get_weather(self.payload, AUTHORIZATION)
        self.now[0] += 3599
        with self.assertRaises(ServiceError) as error:
            self.backend.get_weather(self.payload, AUTHORIZATION)
        self.assertEqual(error.exception.status, 429)
        self.now[0] += 1
        self.backend.get_weather(self.payload, AUTHORIZATION)
        self.assertEqual(len(self.provider.calls), 2)


if __name__ == "__main__":
    unittest.main()
