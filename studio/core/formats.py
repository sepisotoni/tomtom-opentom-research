"""Display formats implemented by the shared Face Studio renderer."""

from typing import Any, Dict, List, Tuple

FORMAT_REGISTRY = {
    "horizontal": {
        "name": "Side by side",
        "renderer": "digital-time-horizontal-v1",
    },
    "stacked": {
        "name": "Stacked",
        "renderer": "digital-time-stacked-v1",
    },
}


def get_project_formats(project: Dict[str, Any]) -> Tuple[List[str], str]:
    display = project.get("display", {})
    if not isinstance(display, dict):
        return [], ""

    formats = display.get("formats")
    default_format = display.get("default_format")
    if formats is None and "layout" in display:
        formats = [display["layout"]]
        default_format = display["layout"]
    elif formats is None:
        formats = [default_format] if default_format else ["horizontal"]
        if not default_format:
            default_format = "horizontal"

    if not isinstance(formats, list):
        return [], default_format if isinstance(default_format, str) else ""
    return formats, default_format if isinstance(default_format, str) else ""


def make_format_catalog(format_ids: List[str]) -> List[Dict[str, str]]:
    return [
        {
            "id": format_id,
            "name": FORMAT_REGISTRY[format_id]["name"],
            "renderer": FORMAT_REGISTRY[format_id]["renderer"],
        }
        for format_id in sorted(set(format_ids))
    ]
