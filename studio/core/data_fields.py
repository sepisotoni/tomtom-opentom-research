"""
TomTom Face Studio - Data Field Add-ons Registry & Requirements Manager
========================================================================
Shared registry for face-declared data fields and provider capabilities.
"""

from typing import Dict, Any, List, Tuple

SUPPORTED_DATA_FIELDS: Dict[str, Dict[str, Any]] = {
    "battery.percent": {
        "id": "battery.percent",
        "description": "Battery percentage level (0-100%)",
        "category": "battery",
        "type": "integer",
        "capability": "device-telemetry-v1"
    },
    "gps.fix": {
        "id": "gps.fix",
        "description": "GPS fix status boolean",
        "category": "gps",
        "type": "boolean",
        "capability": "device-gps-v1"
    },
    "weather.current.temperature": {
        "id": "weather.current.temperature",
        "description": "Current outdoor temperature in Celsius",
        "category": "weather",
        "type": "integer",
        "capability": "weather-provider-v1"
    },
    "weather.current.condition": {
        "id": "weather.current.condition",
        "description": "Current weather condition summary string",
        "category": "weather",
        "type": "string",
        "capability": "weather-provider-v1"
    },
    "weather.hourly.temperature": {
        "id": "weather.hourly.temperature",
        "description": "24-hour forecast hourly temperatures",
        "category": "weather",
        "type": "array[integer]",
        "capability": "weather-provider-v1"
    },
    "weather.hourly.condition": {
        "id": "weather.hourly.condition",
        "description": "24-hour forecast hourly conditions",
        "category": "weather",
        "type": "array[string]",
        "capability": "weather-provider-v1"
    },
    "weather.hourly.precipitation_probability": {
        "id": "weather.hourly.precipitation_probability",
        "description": "24-hour forecast precipitation probability (0-100%)",
        "category": "weather",
        "type": "array[integer]",
        "capability": "weather-provider-v1"
    },
    "weather.daily.condition": {
        "id": "weather.daily.condition",
        "description": "7-day forecast summary conditions",
        "category": "weather",
        "type": "array[string]",
        "capability": "weather-provider-v1"
    }
}


def validate_data_requirements(reqs: Any) -> Tuple[bool, List[str]]:
    """
    Validate data requirements against SUPPORTED_DATA_FIELDS.
    Returns (is_valid, list_of_error_strings).
    """
    errors = []
    if reqs is None:
        return True, []

    if not isinstance(reqs, list):
        return False, ["Validation Error: data_requirements must be a list of string field IDs."]

    seen = set()
    for idx, field_id in enumerate(reqs):
        if not isinstance(field_id, str):
            errors.append(f"Validation Error: data_requirements[{idx}] must be a string.")
            continue
        if field_id not in SUPPORTED_DATA_FIELDS:
            errors.append(f"Validation Error: Unknown data field requirement '{field_id}'. Supported: {sorted(list(SUPPORTED_DATA_FIELDS.keys()))}")
        if field_id in seen:
            errors.append(f"Validation Error: Duplicate data field requirement '{field_id}'.")
        seen.add(field_id)

    return len(errors) == 0, errors


def has_weather_requirements(reqs: Any) -> bool:
    """Return True if any requested field is a weather field."""
    if not isinstance(reqs, list):
        return False
    return any(isinstance(f, str) and f.startswith("weather.") for f in reqs)


def get_provider_capabilities(reqs: Any) -> List[Dict[str, str]]:
    """Map requested fields to runtime/provider capabilities."""
    if not isinstance(reqs, list):
        return []

    capabilities_set = set()
    for f in reqs:
        if isinstance(f, str) and f in SUPPORTED_DATA_FIELDS:
            capabilities_set.add(SUPPORTED_DATA_FIELDS[f]["capability"])

    catalog = []
    for cap_id in sorted(list(capabilities_set)):
        catalog.append({
            "capability": cap_id,
            "version": "1.0.0"
        })
    return catalog


def get_union_data_requirements(all_reqs_list: List[List[str]]) -> List[str]:
    """Compute the sorted unique union of data requirements across multiple faces."""
    union_set = set()
    for reqs in all_reqs_list:
        if isinstance(reqs, list):
            for f in reqs:
                if isinstance(f, str) and f in SUPPORTED_DATA_FIELDS:
                    union_set.add(f)
    return sorted(list(union_set))
