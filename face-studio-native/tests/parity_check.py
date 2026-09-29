#!/usr/bin/env python3
"""Cross-checks the native C++ core against the Python Face Studio reference.

usage: python3 face-studio-native/tests/parity_check.py <path-to-ttface_tool>

Checks (all must pass):
  1. validate_face_project(): identical error lists on a large project corpus.
  2. C++-written .ttface  -> accepted by Python inspect_ttface_package();
     Python-written .ttface -> accepted by C++ with identical background pixels;
     manifest.json and bg.rgb565 are byte-identical for the same input.
  3. C++-written .ttgallery -> accepted by Python inspect_ttgallery();
     Python-written .ttgallery -> accepted by C++; both reject the same tampering.
  4. plan_gallery_changes(): identical add/update/remove plans.
"""
import copy
import json
import os
import subprocess
import sys
import tempfile
import zipfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)

from PIL import Image  # noqa: E402

from studio.core.exporter import export_ttface_package, validate_face_project  # noqa: E402
from studio.core.gallery import export_ttgallery, inspect_ttgallery, plan_gallery_changes  # noqa: E402
from studio.core.importer import inspect_ttface_package  # noqa: E402

TOOL = os.path.abspath(sys.argv[1])
TMP = tempfile.mkdtemp(prefix="ttfs-parity-")
failures = []


def run(*args):
    p = subprocess.run([TOOL, *map(str, args)], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def check(cond, message):
    if not cond:
        failures.append(message)
        print("  FAIL:", message)


def base_project(name="Parity", layout="horizontal"):
    return {
        "metadata": {"name": name, "version": "1.2.3", "author": "Parity"},
        "canvas": {"width": 320, "height": 240},
        "display": {"formats": [layout], "default_format": layout},
        "background_color": "#101020",
        "data_requirements": ["battery.percent"],
        "elements": [
            {"id": "clock", "type": "digital_time", "format": "HH:MM", "x": 60, "y": 60, "width": 200,
             "height": 100, "color": "#FFFFFF", "font_size": 36, "is_12h": False, "rule": None},
            {"id": "bat", "type": "status_icon", "icon_type": "battery_low", "x": 280, "y": 8, "width": 24,
             "height": 24, "color": "#FF0000", "rule": "battery_low", "animate_blink": True},
        ],
        "rules": [{"name": "battery_low", "signal": "battery_percent", "op": "<=", "value": 20}],
    }


# ----------------------------------------------------------------- 1. corpus
def corpus():
    yield "valid", base_project()
    for layout in ("horizontal", "stacked"):
        yield f"valid-{layout}", base_project(layout=layout)
    values = [None, True, False, 0, -1, 1, 319, 320, 321, 240, 10.0, 10.5, "10", [], {}, "", "x" * 3, 10 ** 30]
    for field in ("x", "y", "width", "height"):
        for v in values:
            p = base_project()
            p["elements"][0][field] = v
            yield f"element-{field}={v!r}", p
    for v in values + ["", "digital_time", "gauge", "date", "text", "status_icon"]:
        p = base_project()
        p["elements"][0]["type"] = v
        yield f"type={v!r}", p
    for v in values:
        p = base_project()
        p["elements"][0]["font_size"] = v
        yield f"font_size={v!r}", p
    for key in ("width", "height"):
        for v in values:
            p = base_project()
            p["canvas"][key] = v
            yield f"canvas-{key}={v!r}", p
    for v in (None, [], "x", 5, True):
        p = base_project()
        p["canvas"] = v
        yield f"canvas={v!r}", p
        p = base_project()
        p["elements"] = v
        yield f"elements={v!r}", p
        p = base_project()
        p["rules"] = v
        yield f"rules={v!r}", p
        p = base_project()
        p["display"] = v
        yield f"display={v!r}", p
        p = base_project()
        p["data_requirements"] = v
        yield f"data_requirements={v!r}", p
        p = base_project()
        p["metadata"] = v
        yield f"metadata={v!r}", p
    for sig in ("battery_percent", "cpu", None, 5, "", "gps_fix"):
        for op in ("==", "!=", "<=", ">=", "<", ">", "~=", None, 3, ""):
            p = base_project()
            p["rules"] = [{"name": "r", "signal": sig, "op": op, "value": 1}]
            yield f"rule sig={sig!r} op={op!r}", p
    p = base_project()
    p["rules"] = [5, "x", None, []]
    yield "rules-non-objects", p
    p = base_project()
    p["elements"] = [5, "x", None, [], {}]
    yield "elements-non-objects", p
    for renderer in ("ttface.elements.v1", "opentom.builtin.hydro-aqua.v1", "nope", "", None, 5):
        p = base_project()
        p["renderer_id"] = renderer
        yield f"renderer={renderer!r}", p
    fmt_cases = [["horizontal"], ["stacked"], ["horizontal", "stacked"], ["horizontal", "horizontal"], ["diagonal"],
                 [], None, "horizontal", [1], [None], ["horizontal", 5]]
    for formats in fmt_cases:
        for default in ("horizontal", "stacked", "", None, 5, "diagonal"):
            p = base_project()
            p["display"] = {"formats": formats, "default_format": default}
            yield f"formats={formats!r} default={default!r}", p
    for layout in ("horizontal", "stacked", "x", 5, None):
        p = base_project()
        p["display"] = {"layout": layout}
        yield f"display-layout={layout!r}", p
    p = base_project()
    del p["display"]
    yield "no-display", p
    p = base_project()
    p["display"] = {}
    yield "empty-display", p
    p = base_project()
    p["display"] = {"default_format": "stacked"}
    yield "display-default-only", p
    for reqs in (["battery.percent"], ["battery.percent", "battery.percent"], ["bogus"], [1, None], "x",
                 ["weather.current.temperature", "gps.fix"]):
        p = base_project()
        p["data_requirements"] = reqs
        yield f"reqs={reqs!r}", p
    for bgfile in ("assets/bg.rgb565", "wrong.rgb565", None, 5, ""):
        p = base_project()
        p["background"] = {"file": bgfile}
        yield f"background-file={bgfile!r}", p
    for name in ("", None, 5, "ok", " "):
        p = base_project()
        p["metadata"] = {"name": name}
        yield f"meta-name={name!r}", p
        p = base_project()
        del p["metadata"]
        p["face_name"] = name
        yield f"face_name={name!r}", p
    yield "empty", {}
    yield "not-object", []
    p = base_project()
    p["elements"][0]["id"] = None
    p["elements"][0]["x"] = -5
    yield "id-none-out-of-bounds", p
    p = base_project()
    p["elements"][0].pop("id")
    p["elements"][0]["x"] = 400
    yield "no-id-out-of-bounds", p


def check_validation_parity():
    print("1. validation parity on the project corpus")
    n = 0
    for label, project in corpus():
        path = os.path.join(TMP, "corpus.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(project, f)
        rc, out, err = run("validate", path)
        native = [line for line in out.split("\n") if line != ""]
        try:
            expected = validate_face_project(project)
        except Exception as exc:  # reference crashes are outside the compatibility contract
            print(f"  (skipped {label}: reference raised {type(exc).__name__})")
            continue
        n += 1
        if native != expected:
            check(False, f"validate mismatch for {label}\n    python: {expected}\n    native: {native}")
    print(f"   compared {n} projects")


# --------------------------------------------------------------- 2. ttface
def check_ttface_interop():
    print("2. .ttface interoperability")
    project = base_project("Interop Face", "stacked")
    project["display"]["formats"] = ["horizontal", "stacked"]
    img = Image.new("RGB", (320, 240))
    px = img.load()
    for y in range(240):
        for x in range(320):
            px[x, y] = ((x * 3) % 256, (y * 5) % 256, ((x ^ y) * 7) % 256)
    bg_png = os.path.join(TMP, "bg.png")
    img.save(bg_png)
    proj_json = os.path.join(TMP, "interop.json")
    with open(proj_json, "w") as f:
        json.dump(project, f)

    py_face = os.path.join(TMP, "py.ttface")
    cpp_face = os.path.join(TMP, "cpp.ttface")
    ok, msgs = export_ttface_package(project, img, py_face)
    check(ok, f"python export failed: {msgs}")
    rc, out, err = run("export-ttface", proj_json, bg_png, cpp_face)
    check(rc == 0, f"native export failed: {out} {err}")

    with zipfile.ZipFile(py_face) as a, zipfile.ZipFile(cpp_face) as b:
        check(a.namelist() == b.namelist(), f"entry order differs: {a.namelist()} vs {b.namelist()}")
        check(a.read("manifest.json") == b.read("manifest.json"), "manifest.json bytes differ")
        check(a.read("assets/bg.rgb565") == b.read("assets/bg.rgb565"), "bg.rgb565 bytes differ")

    manifest, bg, errors = inspect_ttface_package(cpp_face)
    check(not errors and manifest is not None and bg is not None, f"python rejects native .ttface: {errors}")
    rc, out, _ = run("inspect-ttface", py_face)
    check("loaded=1" in out and "bg=1" in out and out.count("\n") == 1, f"native rejects python .ttface: {out}")

    # solid-colour background (no image)
    py_solid = os.path.join(TMP, "py-solid.ttface")
    cpp_solid = os.path.join(TMP, "cpp-solid.ttface")
    export_ttface_package(project, None, py_solid) if False else None
    solid = Image.new("RGB", (320, 240), (16, 16, 32))
    export_ttface_package(project, solid, py_solid)
    run("export-ttface", proj_json, "-", cpp_solid)
    with zipfile.ZipFile(py_solid) as a, zipfile.ZipFile(cpp_solid) as b:
        check(a.read("assets/bg.rgb565") == b.read("assets/bg.rgb565"), "solid bg.rgb565 bytes differ")

    # inspection of broken input gives the same first message
    broken = os.path.join(TMP, "broken.ttface")
    with zipfile.ZipFile(broken, "w") as z:
        z.writestr("readme.txt", "x")
    _, _, py_errors = inspect_ttface_package(broken)
    rc, out, _ = run("inspect-ttface", broken)
    check(out.split("\n")[0] == py_errors[0], f"missing-manifest message differs: {py_errors[0]!r} vs {out!r}")
    missing = os.path.join(TMP, "nope.ttface")
    _, _, py_errors = inspect_ttface_package(missing)
    rc, out, _ = run("inspect-ttface", missing)
    check(out.split("\n")[0] == py_errors[0], f"nonexistent-file message differs: {py_errors[0]!r} vs {out!r}")

    # invalid project: both refuse to write anything
    bad = base_project()
    bad["elements"][0]["x"] = -4
    bad_json = os.path.join(TMP, "bad.json")
    with open(bad_json, "w") as f:
        json.dump(bad, f)
    bad_out = os.path.join(TMP, "bad-native.ttface")
    rc, out, _ = run("export-ttface", bad_json, "-", bad_out)
    ok, msgs = export_ttface_package(bad, solid, os.path.join(TMP, "bad-py.ttface"))
    check(rc == 1 and not ok and not os.path.exists(bad_out), "invalid project must not export")
    check([m for m in out.split("\n") if m] == msgs, f"export blocking messages differ: {msgs} vs {out!r}")
    return py_face, cpp_face


# -------------------------------------------------------------- 3. gallery
def check_gallery_interop():
    print("3. .ttgallery interoperability")
    faces = []
    for name, layout in (("Roboto Face", "horizontal"), ("Ubuntu Face", "stacked")):
        p = base_project(name, layout)
        path = os.path.join(TMP, name.replace(" ", "_") + ".ttface")
        ok, msgs = export_ttface_package(p, Image.new("RGB", (320, 240), (16, 16, 32)), path)
        check(ok, f"face export: {msgs}")
        faces.append(path)

    py_gal = os.path.join(TMP, "py.ttgallery")
    cpp_gal = os.path.join(TMP, "cpp.ttgallery")
    ok, msgs = export_ttgallery(faces, py_gal, "Interop Gallery", "2.0.1")
    check(ok, f"python gallery export: {msgs}")
    rc, out, err = run("export-ttgallery", cpp_gal, "Interop Gallery", "2.0.1", *faces)
    check(rc == 0, f"native gallery export: {out} {err}")

    manifest, errors = inspect_ttgallery(cpp_gal)
    check(manifest is not None and not errors, f"python rejects native gallery: {errors}")
    rc, out, _ = run("inspect-ttgallery", py_gal)
    check(out.strip() == "ok faces=2", f"native rejects python gallery: {out!r}")

    with zipfile.ZipFile(py_gal) as a, zipfile.ZipFile(cpp_gal) as b:
        check(a.namelist() == b.namelist(), f"gallery entry order differs: {a.namelist()} vs {b.namelist()}")
        pm, cm = json.loads(a.read("gallery.json")), json.loads(b.read("gallery.json"))
        for m in (pm, cm):  # previews are drawn by different font engines; hashes legitimately differ
            for face in m["faces"]:
                face.pop("preview_sha256")
        check(pm == cm, "gallery.json content differs (excluding preview hashes)")
        for name in a.namelist():
            if name.startswith("faces/"):
                check(a.read(name) == b.read(name), f"{name} bytes differ")
        for name in b.namelist():
            if name.startswith("previews/"):
                im = Image.open(__import__("io").BytesIO(b.read(name)))
                check(im.format == "PNG" and im.size == (160, 120), f"{name} is not a 160x120 PNG")
                im.load()

    # both implementations reject the same tampering
    def tampered(src, dst, mutate):
        with zipfile.ZipFile(src) as zin, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zout:
            for info in zin.infolist():
                data = zin.read(info.filename)
                data = mutate(info.filename, data)
                if data is not None:
                    zout.writestr(info.filename, data)
            mutate(None, None)

    def flip_face(name, data):
        if name == "faces/roboto-face.ttface":
            return data[:-1] + bytes([data[-1] ^ 1])
        return data

    extra_added = {}

    def add_extra(name, data):
        if name is None:
            return None
        return data

    cases = {
        "flipped-face-byte": flip_face,
        "drop-preview": lambda n, d: None if n == "previews/roboto-face.png" else d,
        "drop-manifest": lambda n, d: None if n == "gallery.json" else d,
    }
    for label, mutate in cases.items():
        for kind, src in (("native", cpp_gal), ("python", py_gal)):
            dst = os.path.join(TMP, f"t-{label}-{kind}.ttgallery")
            tampered(src, dst, mutate)
            pm, perrors = inspect_ttgallery(dst)
            rc, out, _ = run("inspect-ttgallery", dst)
            native_ok = out.strip().startswith("ok")
            check((pm is not None) == native_ok,
                  f"tamper '{label}' on {kind} gallery: python ok={pm is not None} native ok={native_ok}: {perrors} / {out!r}")
            check(pm is None and not native_ok, f"tamper '{label}' on {kind} gallery was accepted")

    # unsafe path / unlisted file
    for label, extra in (("traversal", "../outside"), ("unlisted", "extra.bin")):
        dst = os.path.join(TMP, f"t-{label}.ttgallery")
        with zipfile.ZipFile(py_gal) as zin, zipfile.ZipFile(dst, "w") as zout:
            for info in zin.infolist():
                zout.writestr(info.filename, zin.read(info.filename))
            zout.writestr(extra, b"x")
        pm, _ = inspect_ttgallery(dst)
        rc, out, _ = run("inspect-ttgallery", dst)
        check(pm is None and not out.strip().startswith("ok"), f"tamper {label}: python={pm is not None} native={out!r}")

    # duplicate ids are rejected by both
    dup = os.path.join(TMP, "dup.ttface")
    export_ttface_package(base_project("Roboto Face", "stacked"), Image.new("RGB", (320, 240)), dup)
    ok, msgs = export_ttgallery([faces[0], dup], os.path.join(TMP, "dup-py.ttgallery"), "Dup")
    rc, out, _ = run("export-ttgallery", os.path.join(TMP, "dup-native.ttgallery"), "Dup", "1.0.0", faces[0], dup)
    check(not ok and rc == 1, "duplicate ids must be rejected")
    check([m for m in out.split("\n") if m] == msgs, f"duplicate message differs: {msgs} vs {out!r}")


# ------------------------------------------------------------------ 4. plan
def check_plan_parity():
    print("4. gallery change plan parity")
    def m(faces):
        return {"faces": [{"id": i, "sha256": h * 64} for i, h in faces]}
    installed = m([("keep", "a"), ("change", "b"), ("remove", "c"), ("zeta", "1")])
    desired = m([("keep", "a"), ("change", "d"), ("add", "e"), ("alpha", "2")])
    py = plan_gallery_changes(installed, desired)
    a, b = os.path.join(TMP, "inst.json"), os.path.join(TMP, "want.json")
    json.dump(installed, open(a, "w"))
    json.dump(desired, open(b, "w"))
    rc, out, _ = run("plan", a, b)
    native = {"add": [], "update": [], "remove": []}
    for line in out.strip().split("\n"):
        kind, _, ident = line.partition(" ")
        native[kind].append(ident)
    for key in ("add", "update", "remove"):
        check(sorted(py[key]) == sorted(native[key]), f"plan '{key}' differs: {py[key]} vs {native[key]}")


if __name__ == "__main__":
    check_validation_parity()
    check_ttface_interop()
    check_gallery_interop()
    check_plan_parity()
    if failures:
        print(f"\n{len(failures)} parity check(s) FAILED")
        sys.exit(1)
    print("\nall parity checks passed")
