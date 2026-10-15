"""Re-export the active concept after display preparation; no geometry edits."""
import json
import runpy
from pathlib import Path
import adsk.core
import adsk.fusion

builder_module = runpy.run_path(
    r".\mechanical\lift_concept\fusion_script\LiftConcept\LiftConcept.py"
)
OUTPUT = builder_module["OUTPUT"]
MODEL_NAME = builder_module["MODEL_NAME"]
app = adsk.core.Application.get()
design = adsk.fusion.Design.cast(app.activeProduct)
if not app.activeDocument.name.startswith(MODEL_NAME):
    raise RuntimeError("Active document is not the generated concept")
ui = app.userInterface
ui.activeSelections.clear()
grid = ui.commandDefinitions.itemById("ViewLayoutGridOnCommand")
checked = adsk.core.CheckBoxControlDefinition.cast(grid.controlDefinition)
if checked and checked.isChecked:
    grid.execute()
environment = ui.commandDefinitions.itemById("ViewEnvCommand")
environment_list = adsk.core.ListControlDefinition.cast(environment.controlDefinition)
if environment_list:
    (OUTPUT / "environments.json").write_text(
        json.dumps([
            {"name": item.name, "selected": item.isSelected}
            for item in environment_list.listItems
        ], ensure_ascii=False, indent=2), encoding="utf-8"
    )
    for item in environment_list.listItems:
        if item.name.strip() == "照相亭":
            item.isSelected = True
            environment.execute()
            break
adsk.doEvents()

camera = app.activeViewport.camera
camera.isSmoothTransition = False
camera.cameraType = adsk.core.CameraTypes.OrthographicCameraType
camera.eye = adsk.core.Point3D.create(155, -155, 130)
camera.target = adsk.core.Point3D.create(6, 0, 48)
camera.upVector = adsk.core.Vector3D.create(0, 0, 1)
camera.isFitView = True
app.activeViewport.camera = camera
app.activeViewport.refresh()
adsk.doEvents()
app.activeViewport.saveAsImageFile(str(OUTPUT / "isometric.png"), 1800, 1500)
options = adsk.core.SaveImageFileOptions.create(str(OUTPUT / "isometric_transparent.png"))
options.width = 1800
options.height = 1500
options.isBackgroundTransparent = True
app.activeViewport.saveAsImageFileWithOptions(options)
save_view = builder_module["save_view"]
save_view(app, "front.png", (1900, 0, 480))
save_view(app, "side.png", (0, -1900, 480))
save_view(app, "top.png", (60, 0, 2500), up=(1, 0, 0))
save_view(app, "isometric.png", (1550, -1550, 1300))
app.activeViewport.saveAsImageFileWithOptions(options)
app.activeViewport.setCurrentAsHome(True)
manager = design.exportManager
if not manager.execute(manager.createFusionArchiveExportOptions(
        str(OUTPUT / (MODEL_NAME + ".f3d")))):
    raise RuntimeError("Final F3D export failed")
commands = [
    {"id": command.id, "name": command.name}
    for command in ui.commandDefinitions
    if ("view" in command.id.lower()
        or any(t in command.name for t in ("环境", "浅", "暗", "照片", "光")))
]
(OUTPUT / "view_commands.json").write_text(
    json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8"
)
