"""Runtime renderer identifiers shared by Face Studio and the device runtime."""

from typing import Any, Dict, Optional

DEFAULT_RENDERER_ID = "ttface.elements.v1"

RENDERER_REGISTRY: Dict[str, Dict[str, Any]] = {
    DEFAULT_RENDERER_ID: {
        "name": "Declarative face",
        "native_face_index": None,
    },
    "opentom.builtin.frost-outline.v1": {
        "name": "Frost Outline",
        "native_face_index": 0,
    },
    "opentom.builtin.hydro-aqua.v1": {
        "name": "Hydro Aqua Wave",
        "native_face_index": 1,
    },
    "opentom.builtin.solid-lavender.v1": {
        "name": "Solid Lavender",
        "native_face_index": 2,
    },
    "opentom.builtin.vivid-sunset.v1": {
        "name": "Vivid Sunset",
        "native_face_index": 3,
    },
    "opentom.builtin.real-telemetry.v1": {
        "name": "Real Telemetry",
        "native_face_index": 4,
    },
}


def get_renderer_id(project: Dict[str, Any]) -> str:
    renderer_id = project.get("renderer_id", DEFAULT_RENDERER_ID)
    return renderer_id if isinstance(renderer_id, str) else ""


def get_renderer(renderer_id: str) -> Optional[Dict[str, Any]]:
    renderer = RENDERER_REGISTRY.get(renderer_id)
    return dict(renderer) if renderer is not None else None
