"""Remove only the lower orange limit-switch placeholder marked by the user.

Run inside Fusion's Python console with runpy.run_path. Export the current
document first, edit a new imported copy, and reopen the final F3D to verify it.
"""
import json
import runpy
import traceback
from collections import Counter
from pathlib import Path

import adsk.core
import adsk.fusion

OUTPUT = Path(__file__).resolve().parents[1] / "output/v03"
MODEL_NAME = "割胶机器人_四柱升降概念_v03"
OUTPUT.mkdir(parents=True, exist_ok=True)
app = adsk.core.Application.get()


def all_bodies(design):
    return [
        body
        for component in design.allComponents
        for body in component.bRepBodies
    ]


def xyz(point):
    return [round(point.x * 10, 6), round(point.y * 10, 6),
            round(point.z * 10, 6)]


def signature(body):
    bounds = body.boundingBox
    return json.dumps({
        "component": body.parentComponent.name,
        "name": body.name,
        "min_mm": xyz(bounds.minPoint),
        "max_mm": xyz(bounds.maxPoint),
        "volume_cm3": round(body.volume, 6),
        "solid": body.isSolid,
    }, ensure_ascii=False, sort_keys=True)


def is_marked(body):
    bounds = body.boundingBox
    center = [
        (lower + upper) / 2
        for lower, upper in zip(xyz(bounds.minPoint), xyz(bounds.maxPoint))
    ]
    return (
        body.name == "上下限位开关占位"
        and body.parentComponent.name.startswith("03_Z轴固定导轨")
        and all(abs(actual - expected) < 0.01
                for actual, expected in zip(center, (350, 143, 300)))
    )


def export_archive(design, target):
    manager = design.exportManager
    if not manager.execute(manager.createFusionArchiveExportOptions(str(target))):
        raise RuntimeError("Fusion archive export failed: " + str(target))


def reopen(target):
    options = app.importManager.createFusionArchiveImportOptions(str(target))
    document = app.importManager.importToNewDocument(options)
    if not document:
        raise RuntimeError("Fusion archive import failed: " + str(target))
    document.activate()
    return document, adsk.fusion.Design.cast(app.activeProduct)


try:
    interrupted_document = app.activeDocument
    source_document = app.activeDocument
    if source_document and source_document.name == MODEL_NAME:
        source_document = next(
            (document for document in app.documents if document.name.startswith(
                "割胶机器人_四柱升降概念_v02")), None)
        if source_document:
            source_document.activate()
    source_design = adsk.fusion.Design.cast(app.activeProduct)
    if not source_document or not source_document.name.startswith(
            "割胶机器人_四柱升降概念_v02"):
        raise RuntimeError("Expected the user's open v02 concept")
    original = all_bodies(source_design)
    candidates = [body for body in original if is_marked(body)]
    if len(candidates) != 1:
        raise RuntimeError("Expected exactly one marked lower block")
    removed_signature = signature(candidates[0])
    expected = Counter(signature(body) for body in original)
    expected.subtract([removed_signature])
    expected = +expected
    backup = OUTPUT / "割胶机器人_删除黄块前备份.f3d"
    final = OUTPUT / (MODEL_NAME + ".f3d")
    step = OUTPUT / (MODEL_NAME + ".step")
    if final.exists():
        raise RuntimeError("v03 already exists; refusing to overwrite model files")
    app.userInterface.activeSelections.clear()
    if (backup.exists() and interrupted_document
            and interrupted_document.name == MODEL_NAME):
        interrupted_document.activate()
        document = interrupted_document
        design = adsk.fusion.Design.cast(app.activeProduct)
        assert Counter(signature(body) for body in all_bodies(design)) == expected
    else:
        if not backup.exists():
            export_archive(source_design, backup)
        document, design = reopen(backup)
        assert Counter(signature(body) for body in all_bodies(design)) == Counter(
            signature(body) for body in original), "Backup differs from current v02"
        candidates = [body for body in all_bodies(design) if is_marked(body)]
        if len(candidates) != 1 or not candidates[0].deleteMe():
            raise RuntimeError("Unable to remove the exact marked body")
    document.name = MODEL_NAME
    actual = Counter(signature(body) for body in all_bodies(design))
    assert actual == expected, "Another body's name or geometry changed"
    assert len(all_bodies(design)) == len(original) - 1
    assert all(body.isSolid for body in all_bodies(design))
    assert not any(is_marked(body) for body in all_bodies(design))
    remaining_limits = sum("限位开关占位" in body.name
                           for body in all_bodies(design))
    assert remaining_limits == sum("限位开关占位" in body.name
                                   for body in original) - 1
    builder = runpy.run_path(
        r".\mechanical\lift_concept"
        r"\fusion_script\LiftConcept\LiftConcept.py")
    # Reuse the already verified v02 camera routines without rebuilding geometry.
    builder["save_view"].__globals__["OUTPUT"] = OUTPUT
    save_view = builder["save_view"]
    save_view(app, "front.png", (1900, 0, 480))
    save_view(app, "side.png", (0, -1900, 480))
    save_view(app, "top.png", (60, 0, 2500), up=(1, 0, 0))
    save_view(app, "isometric.png", (1550, -1550, 1300))
    app.activeViewport.setCurrentAsHome(True)
    export_archive(design, final)
    if not design.exportManager.execute(
            design.exportManager.createSTEPExportOptions(str(step))):
        raise RuntimeError("STEP export failed")
    document, reopened = reopen(final)
    reopened_bodies = all_bodies(reopened)
    assert Counter(signature(body) for body in reopened_bodies) == expected
    assert not any(is_marked(body) for body in reopened_bodies)
    assert len(reopened_bodies) == len(original) - 1
    assert all(body.isSolid for body in reopened_bodies)
    assert abs(reopened.rootComponent.boundingBox.minPoint.z * 10) < 0.1
    assert xyz(app.activeViewport.frontEyeDirection) == [-10.0, 0.0, 0.0]
    assert xyz(app.activeViewport.frontUpDirection) == [0.0, 0.0, 10.0]
    assert app.activeViewport.goHome(False)
    adsk.doEvents()
    app.userInterface.activeSelections.clear()
    app.activeViewport.refresh()
    report = {
        "status": "PASS",
        "reopened_export": True,
        "source_document": source_document.name,
        "backup": str(backup),
        "f3d": str(final),
        "step": str(step),
        "bodies_before": len(original),
        "bodies_after": len(reopened_bodies),
        "removed": json.loads(removed_signature),
        "all_other_bodies_unchanged": True,
        "other_limit_placeholders_preserved": True,
        "remaining_limit_placeholders": remaining_limits,
        "ground_xy_preserved": True,
        "named_views": [view.name for view in reopened.namedViews],
    }
    (OUTPUT / "reopen_verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    app.log("v03 PASS: only the user-marked lower orange block was removed.")
except Exception:
    (OUTPUT / "edit_error.txt").write_text(
        traceback.format_exc(), encoding="utf-8")
    raise
