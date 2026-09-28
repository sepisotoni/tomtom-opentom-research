"""
TomTom Face Studio - Bounded Weather Cache & Service Contract
============================================================
Manages bounded weather caching (<= 4 KiB limit), hourly forecast (24h) and
daily summaries (7d), stale data marking, location-sharing privacy controls,
network reachability checks, and deterministic provider interfaces.
"""

import json
import os
import socket
import time
from datetime import datetime, timezone, timedelta
from typing import Dict, Any, List, Tuple, Optional


MAX_CACHE_BYTES = 4096  # Strict 4 KiB limit
REFRESH_INTERVAL_SECONDS = 3600  # 1 hour minimum refresh interval
MAX_HOURLY_RECORDS = 24  # 24 hours max forecast
MAX_DAILY_RECORDS = 7    # 7 days max forecast
STALE_PURGE_SECONDS = 86400  # Purge stale data if older than 24 hours


class WeatherProvider:
    """Abstract interface for weather data providers."""
    def fetch_weather(self, location_id: str, timezone_str: str) -> Dict[str, Any]:
        raise NotImplementedError


class FakeWeatherProvider(WeatherProvider):
    """Deterministic fake weather provider for testing (makes NO live API calls)."""

    def __init__(
        self,
        current_temp: int = 18,
        current_cond: str = "Clear",
        hourly_temps: Optional[List[int]] = None,
        hourly_conds: Optional[List[str]] = None,
        daily_conds: Optional[List[str]] = None,
        is_malformed: bool = False,
        trigger_timeout: bool = False
    ):
        self.current_temp = current_temp
        self.current_cond = current_cond
        self.hourly_temps = hourly_temps
        self.hourly_conds = hourly_conds
        self.daily_conds = daily_conds
        self.is_malformed = is_malformed
        self.trigger_timeout = trigger_timeout
        self.call_count = 0

    def fetch_weather(self, location_id: str, timezone_str: str) -> Dict[str, Any]:
        self.call_count += 1
        if self.trigger_timeout:
            raise TimeoutError("Weather provider network request timed out.")

        if self.is_malformed:
            return {"garbage_field": "invalid_data_schema"}

        # Build valid 24-hour hourly forecast
        now_dt = datetime.now(timezone.utc)
        hourly_list = []
        temps = self.hourly_temps or [self.current_temp + (i % 5 - 2) for i in range(24)]
        conds = self.hourly_conds or ["Clear" if i % 2 == 0 else "Partly Cloudy" for i in range(24)]

        for i in range(min(24, len(temps))):
            h_dt = now_dt + timedelta(hours=i)
            hourly_list.append({
                "timestamp_utc": h_dt.strftime("%Y-%m-%d%H:00:00Z"),
                "temperature": temps[i],
                "condition": conds[i % len(conds)],
                "precipitation_probability": (i * 5) % 100
            })

        # Build valid 7-day daily forecast
        daily_list = []
        d_conds = self.daily_conds or ["Clear", "Sunny", "Cloudy", "Rain", "Clear", "Partly Cloudy", "Sunny"]
        for i in range(min(7, len(d_conds))):
            d_dt = now_dt + timedelta(days=i)
            daily_list.append({
                "date": d_dt.strftime("%Y-%m-%d"),
                "condition": d_conds[i]
            })

        return {
            "version": 1,
            "location_id": location_id or "coarse-region-7521",
            "timezone": timezone_str or "Europe/Paris",
            "current": {
                "temperature": self.current_temp,
                "condition": self.current_cond
            },
            "hourly": hourly_list,
            "daily": daily_list
        }


def serialize_weather_cache(data: Dict[str, Any]) -> bytes:
    """
    Serialize weather cache object to compact UTF-8 JSON bytes.
    Enforces strict 4 KiB (4096 bytes) size limit.
    """
    if not isinstance(data, dict):
        raise ValueError("Weather cache data must be a dictionary.")

    # Bound hourly and daily arrays before serialization
    if "hourly" in data and isinstance(data["hourly"], list):
        data["hourly"] = data["hourly"][:MAX_HOURLY_RECORDS]
    if "daily" in data and isinstance(data["daily"], list):
        data["daily"] = data["daily"][:MAX_DAILY_RECORDS]

    serialized = json.dumps(data, separators=(',', ':'), sort_keys=True).encode("utf-8")

    # If exceeding 4 KiB limit, attempt to truncate description strings or excess records
    if len(serialized) > MAX_CACHE_BYTES:
        if "hourly" in data and isinstance(data["hourly"], list):
            while len(data["hourly"]) > 1 and len(serialized) > MAX_CACHE_BYTES:
                data["hourly"].pop()
                serialized = json.dumps(data, separators=(',', ':'), sort_keys=True).encode("utf-8")

    if len(serialized) > MAX_CACHE_BYTES:
        raise ValueError(f"Weather cache size ({len(serialized)} bytes) exceeds strict 4 KiB ({MAX_CACHE_BYTES} bytes) limit.")

    return serialized


def parse_weather_cache(raw_bytes: bytes) -> Tuple[Optional[Dict[str, Any]], List[str]]:
    """
    Parse raw cache bytes into dictionary.
    Enforces <= 4 KiB size limit during parse.
    """
    if not raw_bytes:
        return None, ["Cache is empty."]
    if len(raw_bytes) > MAX_CACHE_BYTES:
        return None, [f"Validation Error: Cache size ({len(raw_bytes)} bytes) exceeds 4 KiB limit."]

    try:
        data = json.loads(raw_bytes.decode("utf-8"))
        if not isinstance(data, dict):
            return None, ["Validation Error: Cache root must be a JSON object."]

        # Verify bounded limits
        hourly = data.get("hourly", [])
        if isinstance(hourly, list) and len(hourly) > MAX_HOURLY_RECORDS:
            return None, [f"Validation Error: Hourly forecast contains {len(hourly)} items (max 24)."]

        daily = data.get("daily", [])
        if isinstance(daily, list) and len(daily) > MAX_DAILY_RECORDS:
            return None, [f"Validation Error: Daily forecast contains {len(daily)} items (max 7)."]

        return data, []
    except Exception as e:
        return None, [f"Validation Error: Malformed JSON in cache: {str(e)}"]


def is_network_connected(host: str = "1.1.1.1", port: int = 53, timeout: float = 1.0) -> bool:
    """
    Check if a real routed network connection is available.
    Uses short non-blocking socket connect test.
    """
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        sock.connect((host, port))
        sock.close()
        return True
    except Exception:
        return False


class SharedWeatherService:
    """
    Shared weather cache and refresh service.
    Serves the union of requested data fields across all faces.
    """

    def __init__(self, cache_file_path: Optional[str] = None):
        self.cache_file_path = cache_file_path
        self.location_sharing_enabled = False  # Disabled by default for privacy
        self.provider_name = "fake_provider"
        self.provider: WeatherProvider = FakeWeatherProvider()
        self.memory_cache: Optional[Dict[str, Any]] = None

    def set_location_sharing(self, enabled: bool):
        """Enable or disable telemetry location sharing for weather fetching."""
        self.location_sharing_enabled = bool(enabled)

    def load_cache(self) -> Optional[Dict[str, Any]]:
        """Load cache from memory or disk file if present."""
        if self.memory_cache is not None:
            return self.memory_cache
        if self.cache_file_path and os.path.exists(self.cache_file_path):
            try:
                with open(self.cache_file_path, "rb") as f:
                    raw = f.read(MAX_CACHE_BYTES + 1024)
                data, errs = parse_weather_cache(raw)
                if data:
                    self.memory_cache = data
                    return data
            except Exception:
                pass
        return None

    def save_cache(self, data: Dict[str, Any]):
        """Save cache data enforcing <= 4 KiB limit."""
        serialized = serialize_weather_cache(data)
        self.memory_cache = data
        if self.cache_file_path:
            with open(self.cache_file_path, "wb") as f:
                f.write(serialized)

    def prune_expired_and_mark_stale(self, now_utc: datetime) -> Optional[Dict[str, Any]]:
        """Drop expired hourly records and mark stale if past valid_until."""
        cache = self.load_cache()
        if not cache:
            return None

        now_str = now_utc.strftime("%Y-%m-%dT%H:%M:%SZ")
        valid_until = cache.get("valid_until_utc")

        # Drop expired hourly forecast entries
        if "hourly" in cache and isinstance(cache["hourly"], list):
            valid_hourly = []
            for item in cache["hourly"]:
                ts = item.get("timestamp_utc")
                if ts and ts >= now_str:
                    valid_hourly.append(item)
            cache["hourly"] = valid_hourly

        # Mark as stale if current time exceeds valid_until
        if valid_until and now_str > valid_until:
            cache["is_stale"] = True

        # Purge if stale data is older than 24 hours
        fetched_at = cache.get("fetched_at_utc")
        if fetched_at:
            try:
                fetched_dt = datetime.strptime(fetched_at, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
                if (now_utc - fetched_dt).total_seconds() > STALE_PURGE_SECONDS:
                    self.memory_cache = None
                    if self.cache_file_path and os.path.exists(self.cache_file_path):
                        os.remove(self.cache_file_path)
                    return None
            except Exception:
                pass

        self.save_cache(cache)
        return cache

    def refresh_weather_if_needed(
        self,
        requested_fields: List[str],
        gps_fix: bool = False,
        coarse_location_id: str = "coarse-region-7521",
        timezone_str: str = "Europe/Paris",
        now_utc: Optional[datetime] = None,
        force_network_check: Optional[bool] = None
    ) -> Tuple[Optional[Dict[str, Any]], str]:
        """
        Refresh weather cache according to contract:
        - If requested_fields contains NO weather fields: Return cached/None with message (NO network call).
        - If location_sharing_enabled == False: Return cached/None with message (NO network call).
        - If gps_fix == False: Return cached/None with message (NO network call).
        - If refresh interval < 1 hour: Return cached data.
        - If network unreachable: Return cached stale data.
        """
        if now_utc is None:
            now_utc = datetime.now(timezone.utc)

        # 1. Check if any weather fields requested
        has_weather = any(isinstance(f, str) and f.startswith("weather.") for f in requested_fields)
        if not has_weather:
            return self.load_cache(), "No weather fields requested. Refresh skipped."

        # 2. Check location sharing privacy switch
        if not self.location_sharing_enabled:
            return self.load_cache(), "Location sharing is disabled in settings. Weather refresh skipped."

        # 3. Check GPS fix availability
        if not gps_fix:
            stale_cache = self.prune_expired_and_mark_stale(now_utc)
            return stale_cache, "GPS fix is unavailable. Weather refresh skipped."

        # 4. Check refresh rate limit (<= 1 per hour)
        existing = self.load_cache()
        if existing and not existing.get("is_stale", False):
            valid_until = existing.get("valid_until_utc")
            if valid_until:
                now_str = now_utc.strftime("%Y-%m-%dT%H:%M:%SZ")
                if now_str < valid_until:
                    return existing, "Weather cache is current (< 1 hour old). Refresh skipped."

        # 5. Check real network connectivity
        connected = force_network_check if force_network_check is not None else is_network_connected()
        if not connected:
            stale_cache = self.prune_expired_and_mark_stale(now_utc)
            return stale_cache, "No routed network connectivity. Weather refresh skipped."

        # 6. Perform fetch using provider (deterministic fake provider or configured provider)
        try:
            raw_data = self.provider.fetch_weather(coarse_location_id, timezone_str)
            if not isinstance(raw_data, dict) or "current" not in raw_data:
                stale_cache = self.prune_expired_and_mark_stale(now_utc)
                return stale_cache, "Malformed response from weather provider."

            # Build valid cache object
            fetched_str = now_utc.strftime("%Y-%m-%dT%H:%M:%SZ")
            valid_str = (now_utc + timedelta(seconds=REFRESH_INTERVAL_SECONDS)).strftime("%Y-%m-%dT%H:%M:%SZ")

            cache_obj = {
                "version": 1,
                "location_id": coarse_location_id,
                "timezone": timezone_str,
                "fetched_at_utc": fetched_str,
                "valid_until_utc": valid_str,
                "is_stale": False,
                "current": raw_data.get("current", {}),
                "hourly": raw_data.get("hourly", [])[:MAX_HOURLY_RECORDS],
                "daily": raw_data.get("daily", [])[:MAX_DAILY_RECORDS]
            }

            self.save_cache(cache_obj)
            return cache_obj, "Weather refreshed successfully."
        except Exception as e:
            stale_cache = self.prune_expired_and_mark_stale(now_utc)
            return stale_cache, f"Weather refresh failed with error: {str(e)}"
