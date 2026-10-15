"""Reopen the exported v02 and verify geometry and saved view orientation."""
import json
import traceback
from pathlib import Path

import adsk.core
import adsk.fusion

OUTPUT = Path(r".\mechanical\lift_concept\output\v02")
FILE = OUTPUT / "割胶机器人_四柱升降概念_v02.f3d"
app = adsk.core.Application.get()


def coordinates(value):
    return [round(value.x, 6), round(value.y, 6), round(value.z, 6)]


try:
    options = app.importManager.createFusionArchiveImportOptions(str(FILE))
    document = app.importManager.importToNewDocument(options)
    if not document:
        raise RuntimeError("Unable to reopen exported v02 F3D")
    document.activate()
    design = adsk.fusion.Design.cast(app.activeProduct)
    root = design.rootComponent
    bodies = [
        body for occurrence in root.occurrences
        for body in occurrence.component.bRepBodies
    ]
    names = [body.name for body in bodies]
    front_eye = coordinates(app.activeViewport.frontEyeDirection)
    front_up = coordinates(app.activeViewport.frontUpDirection)
    ground_normal = coordinates(root.xYConstructionPlane.geometry.normal)
    assert len(bodies) == 193, (len(bodies), "Expected two removed bodies")
    assert all(body.isSolid for body in bodies), "Non-solid body found"
    assert not any("控制盒占位" in name or "电池占位" in name for name in names)
    assert any("空托盘底板" in name for name in names), "Tray missing"
    assert front_eye == [-1.0, 0.0, 0.0], front_eye
    assert front_up == [0.0, 0.0, 1.0], front_up
    assert ground_normal == [0.0, 0.0, 1.0], ground_normal
    assert abs(root.boundingBox.minPoint.z * 10) < .1
    views = [view.name for view in design.namedViews]
    assert len(views) == 4, views
    assert app.activeViewport.goHome(False)
    adsk.doEvents()
    home_up = coordinates(app.activeViewport.camera.upVector)
    assert home_up[2] > 0.8, ("Home view is not upright", home_up)
    app.userInterface.activeSelections.clear()
    app.activeViewport.refresh()
    report = {
        "status": "PASS",
        "source": str(FILE),
        "reopened_export": True,
        "solid_bodies": len(bodies),
        "tray_present": True,
        "placeholders_absent": True,
        "xy_plane_normal": ground_normal,
        "front_eye_direction": front_eye,
        "front_up_direction": front_up,
        "home_up_vector": home_up,
        "named_views": views,
    }
    (OUTPUT / "reopen_verification.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    app.log("v02 reopen verification PASS")
except Exception:
    (OUTPUT / "reopen_verification_error.txt").write_text(
        traceback.format_exc(), encoding="utf-8"
    )
    raise
