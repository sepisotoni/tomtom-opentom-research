"""
Unit & Integration Test Suite for TomTom Face Studio Data Field Add-ons & Weather Cache Contract
=============================================================================================
"""

import os
import tempfile
import unittest
from datetime import datetime, timezone, timedelta

from studio.core.data_fields import (
    SUPPORTED_DATA_FIELDS,
    validate_data_requirements,
    has_weather_requirements,
    get_provider_capabilities,
    get_union_data_requirements,
)
from studio.core.weather import (
    MAX_CACHE_BYTES,
    FakeWeatherProvider,
    SharedWeatherService,
    serialize_weather_cache,
    parse_weather_cache,
)
from studio.core.exporter import validate_face_project, export_ttface_package
from studio.core.importer import inspect_ttface_package
from studio.core.gallery import export_ttgallery, inspect_ttgallery


class TestDataFieldsRegistry(unittest.TestCase):

    def test_validate_valid_data_requirements(self):
        reqs = ["battery.percent", "gps.fix", "weather.current.temperature"]
        is_valid, errors = validate_data_requirements(reqs)
        self.assertTrue(is_valid)
        self.assertEqual(len(errors), 0)

    def test_validate_empty_or_none_data_requirements(self):
        is_valid, errors = validate_data_requirements([])
        self.assertTrue(is_valid)
        self.assertEqual(len(errors), 0)

        is_valid, errors = validate_data_requirements(None)
        self.assertTrue(is_valid)
        self.assertEqual(len(errors), 0)

    def test_validate_unknown_field_rejection(self):
        reqs = ["battery.percent", "invalid.custom_field"]
        is_valid, errors = validate_data_requirements(reqs)
        self.assertFalse(is_valid)
        self.assertTrue(any("Unknown data field requirement" in e for e in errors))

    def test_validate_duplicate_field_rejection(self):
        reqs = ["battery.percent", "battery.percent"]
        is_valid, errors = validate_data_requirements(reqs)
        self.assertFalse(is_valid)
        self.assertTrue(any("Duplicate data field requirement" in e for e in errors))

    def test_validate_malformed_type_rejection(self):
        is_valid, errors = validate_data_requirements("not a list")
        self.assertFalse(is_valid)
        self.assertTrue(any("must be a list" in e for e in errors))

        is_valid, errors = validate_data_requirements([123, True])
        self.assertFalse(is_valid)
        self.assertTrue(any("must be a string" in e for e in errors))

    def test_has_weather_requirements(self):
        self.assertTrue(has_weather_requirements(["battery.percent", "weather.current.temperature"]))
        self.assertFalse(has_weather_requirements(["battery.percent", "gps.fix"]))
        self.assertFalse(has_weather_requirements([]))

    def test_get_provider_capabilities(self):
        reqs = ["battery.percent", "gps.fix", "weather.current.temperature", "weather.daily.condition"]
        caps = get_provider_capabilities(reqs)
        cap_names = [c["capability"] for c in caps]
        self.assertIn("device-telemetry-v1", cap_names)
        self.assertIn("device-gps-v1", cap_names)
        self.assertIn("weather-provider-v1", cap_names)

    def test_get_union_data_requirements(self):
        face1 = ["battery.percent", "gps.fix"]
        face2 = ["gps.fix", "weather.current.temperature"]
        face3 = ["weather.daily.condition"]

        union = get_union_data_requirements([face1, face2, face3])
        self.assertEqual(
            union,
            ["battery.percent", "gps.fix", "weather.current.temperature", "weather.daily.condition"]
        )


class TestWeatherCacheContract(unittest.TestCase):

    def setUp(self):
        self.provider = FakeWeatherProvider()
        self.service = SharedWeatherService()
        self.service.provider = self.provider

    def test_fake_provider_deterministic_generation(self):
        res = self.provider.fetch_weather("amsterdam-nl", "Europe/Amsterdam")
        self.assertIn("current", res)
        self.assertIn("hourly", res)
        self.assertIn("daily", res)
        self.assertEqual(len(res["hourly"]), 24)
        self.assertEqual(len(res["daily"]), 7)

    def test_cache_serialization_size_bound(self):
        res = self.provider.fetch_weather("amsterdam-nl", "Europe/Amsterdam")
        now = datetime.now(timezone.utc)
        cache_obj = {
            "version": 1,
            "location_id": "amsterdam-nl",
            "timezone": "Europe/Amsterdam",
            "fetched_at_utc": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "valid_until_utc": (now + timedelta(hours=1)).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "is_stale": False,
            "current": res["current"],
            "hourly": res["hourly"],
            "daily": res["daily"]
        }

        serialized = serialize_weather_cache(cache_obj)
        self.assertLessEqual(len(serialized), MAX_CACHE_BYTES)

        # Roundtrip deserialization via parse_weather_cache
        parsed, errors = parse_weather_cache(serialized)
        self.assertEqual(len(errors), 0)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed.get("location_id"), "amsterdam-nl")
        self.assertFalse(parsed.get("is_stale"))

    def test_cache_rejection_over_4k(self):
        oversized_data = {
            "current": {"temperature": 20},
            "huge_blob": "X" * 5000
        }
        with self.assertRaises(ValueError):
            serialize_weather_cache(oversized_data)

    def test_privacy_location_sharing_disabled_by_default(self):
        self.assertFalse(self.service.location_sharing_enabled)
        cache, msg = self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )
        self.assertIn("disabled", msg.lower())
        self.assertIsNone(cache)

    def test_fetch_weather_with_location_sharing_enabled(self):
        self.service.set_location_sharing(True)
        cache, msg = self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )
        self.assertIsNotNone(cache)
        self.assertFalse(cache.get("is_stale", False))

    def test_no_weather_reqs_causes_no_fetch(self):
        self.service.set_location_sharing(True)
        cache, msg = self.service.refresh_weather_if_needed(
            requested_fields=["battery.percent", "gps.fix"],
            gps_fix=True,
            force_network_check=True
        )
        self.assertIn("no weather", msg.lower())

    def test_missing_gps_fix_returns_stale_cache(self):
        self.service.set_location_sharing(True)
        # First fetch to populate cache
        self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )

        # Second fetch without GPS fix
        now_future = datetime.now(timezone.utc) + timedelta(hours=2)
        cache, msg = self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=False,
            now_utc=now_future
        )
        self.assertIn("gps fix is unavailable", msg.lower())
        self.assertIsNotNone(cache)
        self.assertTrue(cache.get("is_stale"))

    def test_rate_limiting_one_hour(self):
        self.service.set_location_sharing(True)
        initial_calls = self.provider.call_count

        # First fetch
        self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )
        self.assertEqual(self.provider.call_count, initial_calls + 1)

        # Immediate second fetch (within 1 hour)
        self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )
        # Provider count should NOT increase due to rate limit cache hit
        self.assertEqual(self.provider.call_count, initial_calls + 1)

    def test_no_raw_gps_coordinates_in_cache(self):
        self.service.set_location_sharing(True)
        cache, _ = self.service.refresh_weather_if_needed(
            requested_fields=["weather.current.temperature"],
            gps_fix=True,
            force_network_check=True
        )
        serialized = serialize_weather_cache(cache).decode("utf-8")
        self.assertNotIn("lat", serialized.lower())
        self.assertNotIn("lon", serialized.lower())
        self.assertNotIn("latitude", serialized.lower())
        self.assertNotIn("longitude", serialized.lower())


class TestPackageAndGalleryDataIntegration(unittest.TestCase):

    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_ttface_export_with_data_requirements(self):
        project = {
            "metadata": {"name": "Weather Watch", "version": "1.0.0", "author": "Tester"},
            "background_color": "#000000",
            "canvas": {"width": 320, "height": 240},
            "display": {"formats": ["horizontal"], "default_format": "horizontal"},
            "data_requirements": ["battery.percent", "weather.current.temperature"],
            "elements": [
                {"id": "t1", "type": "digital_time", "x": 10, "y": 10, "width": 100, "height": 30, "font_size": 24}
            ],
            "rules": []
        }

        package_path = os.path.join(self.temp_dir.name, "weather_watch.ttface")
        success, errors = export_ttface_package(project, None, package_path)
        self.assertTrue(success, f"Export failed: {errors}")

        manifest, bg, val_errors = inspect_ttface_package(package_path)
        self.assertIsNotNone(manifest)
        self.assertEqual(len(val_errors), 0)
        self.assertEqual(manifest.get("data_requirements"), ["battery.percent", "weather.current.temperature"])

    def test_ttgallery_union_data_requirements(self):
        proj1 = {
            "metadata": {"name": "Face One", "version": "1.0.0"},
            "canvas": {"width": 320, "height": 240},
            "data_requirements": ["battery.percent", "gps.fix"],
            "elements": [], "rules": []
        }
        proj2 = {
            "metadata": {"name": "Face Two", "version": "1.0.0"},
            "canvas": {"width": 320, "height": 240},
            "data_requirements": ["gps.fix", "weather.current.temperature"],
            "elements": [], "rules": []
        }

        p1_path = os.path.join(self.temp_dir.name, "face1.ttface")
        p2_path = os.path.join(self.temp_dir.name, "face2.ttface")
        export_ttface_package(proj1, None, p1_path)
        export_ttface_package(proj2, None, p2_path)

        gallery_path = os.path.join(self.temp_dir.name, "test_collection.ttgallery")
        success, errors = export_ttgallery([p1_path, p2_path], gallery_path, "Test Gallery")
        self.assertTrue(success, f"Gallery export failed: {errors}")

        manifest, val_errors = inspect_ttgallery(gallery_path)
        self.assertIsNotNone(manifest)
        self.assertEqual(len(val_errors), 0)
        self.assertEqual(
            manifest.get("data_requirements"),
            ["battery.percent", "gps.fix", "weather.current.temperature"]
        )
        caps = [c["capability"] for c in manifest.get("provider_capabilities", [])]
        self.assertIn("device-telemetry-v1", caps)
        self.assertIn("device-gps-v1", caps)
        self.assertIn("weather-provider-v1", caps)


if __name__ == "__main__":
    unittest.main()
