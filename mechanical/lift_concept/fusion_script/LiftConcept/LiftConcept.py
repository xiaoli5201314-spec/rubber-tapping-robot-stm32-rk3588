"""Run inside Autodesk Fusion. All public geometry dimensions are in mm."""

import json
import math
import traceback
from pathlib import Path

import adsk.core
import adsk.fusion


OUTPUT = Path(__file__).resolve().parents[2] / "output/v02"
MODEL_NAME = "割胶机器人_四柱升降概念_v02"


def point(values):
    return adsk.core.Point3D.create(*(v / 10.0 for v in values))


def vector(values):
    return adsk.core.Vector3D.create(*values)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def scale(a, factor):
    return tuple(x * factor for x in a)


class Builder:
    def __init__(self, design):
        self.design = design
        self.root = design.rootComponent
        self.temp = adsk.fusion.TemporaryBRepManager.get()
        self.components = []
        self.appearances = {}

    def component(self, name):
        occurrence = self.root.occurrences.addNewComponent(
            adsk.core.Matrix3D.create()
        )
        component = occurrence.component
        component.name = name
        self.components.append(component)
        return component

    def appearance(self, name, rgb):
        if name in self.appearances:
            return self.appearances[name]
        app = adsk.core.Application.get()
        source = None
        for library in app.materialLibraries:
            for candidate in library.appearances:
                if (candidate.name.startswith("ABS")
                        and candidate.appearanceProperties.itemById("opaque_albedo")):
                    source = candidate
                    break
            if source:
                break
        if not source:
            raise RuntimeError("No editable ABS appearance in material libraries")
        appearance = self.design.appearances.addByCopy(source, name)
        prop = adsk.core.ColorProperty.cast(
            appearance.appearanceProperties.itemById("opaque_albedo")
        )
        prop.value = adsk.core.Color.create(*rgb, 255)
        self.appearances[name] = appearance
        return appearance

    def save_body(self, component, temporary, name, appearance):
        if not temporary:
            raise RuntimeError("Geometry creation failed: " + name)
        body = component.bRepBodies.add(temporary)
        body.name = name
        body.appearance = appearance
        return body

    def raw_box(self, center, dimensions, length_dir=(1, 0, 0),
                width_dir=(0, 1, 0)):
        bounds = adsk.core.OrientedBoundingBox3D.create(
            point(center), vector(length_dir), vector(width_dir),
            *(d / 10.0 for d in dimensions)
        )
        return self.temp.createBox(bounds)

    def box(self, component, name, center, dimensions, appearance):
        return self.save_body(
            component, self.raw_box(center, dimensions), name, appearance
        )

    def cylinder(self, component, name, start, end, radius, appearance):
        temporary = self.temp.createCylinderOrCone(
            point(start), radius / 10.0, point(end), radius / 10.0
        )
        return self.save_body(component, temporary, name, appearance)

    def profile(self, component, name, start, end, appearance, section=40):
        delta = tuple(b - a for a, b in zip(start, end))
        length = math.sqrt(sum(v * v for v in delta))
        axis = scale(delta, 1.0 / length)
        direction = vector(axis)
        width = vector((1, 0, 0) if abs(axis[2]) > .9 else (0, 0, 1))
        height = direction.crossProduct(width)
        width_tuple = (width.x, width.y, width.z)
        height_tuple = (height.x, height.y, height.z)
        center = scale(add(start, end), .5)
        temporary = self.raw_box(
            center, (length, section, section), axis, width_tuple
        )
        # Bore and shallow slots only suggest a T-slot extrusion's appearance.
        # This is not a supplier's manufacturable extrusion cross-section.
        inner = self.raw_box(center, (length + 2, 12, 12), axis, width_tuple)
        self.temp.booleanOperation(
            temporary, inner, adsk.fusion.BooleanTypes.DifferenceBooleanType
        )
        for sign in (-1, 1):
            for across, dims in (
                (width_tuple, (length + 2, 4, 7)),
                (height_tuple, (length + 2, 7, 4)),
            ):
                cut_center = add(center, scale(across, sign * (section / 2 - 1)))
                cutter = self.raw_box(cut_center, dims, axis, width_tuple)
                self.temp.booleanOperation(
                    temporary, cutter,
                    adsk.fusion.BooleanTypes.DifferenceBooleanType
                )
        return self.save_body(component, temporary, name, appearance)

    def brace(self, component, name, start, end, appearance):
        delta = tuple(b - a for a, b in zip(start, end))
        length = math.sqrt(sum(v * v for v in delta))
        axis = scale(delta, 1 / length)
        normal = (1, 0, 0)
        return self.save_body(
            component,
            self.raw_box(scale(add(start, end), .5), (length, 8, 25), axis, normal),
            name, appearance,
        )


def make_tracks(b, rubber, steel, dark):
    for side, y in (("左", 250), ("右", -250)):
        c = b.component(side + "履带_80宽_简化外形")
        # Capsule belt: 500 mm straight length plus two 100 mm radii.
        belt = b.raw_box((0, y, 100), (500, 80, 200))
        for x in (-250, 250):
            cap = b.temp.createCylinderOrCone(
                point((x, y - 40, 100)), 10,
                point((x, y + 40, 100)), 10
            )
            b.temp.booleanOperation(
                belt, cap, adsk.fusion.BooleanTypes.UnionBooleanType
            )
        inner = b.raw_box((0, y, 100), (500, 84, 152))
        for x in (-250, 250):
            cap = b.temp.createCylinderOrCone(
                point((x, y - 42, 100)), 7.6,
                point((x, y + 42, 100)), 7.6
            )
            b.temp.booleanOperation(
                inner, cap, adsk.fusion.BooleanTypes.UnionBooleanType
            )
        b.temp.booleanOperation(
            belt, inner, adsk.fusion.BooleanTypes.DifferenceBooleanType
        )
        b.save_body(c, belt, side + "橡胶履带环", rubber)
        for i in range(18):
            x = -245 + i * 29
            b.box(c, f"上履带纹{i + 1:02}", (x, y, 203), (13, 78, 6), dark)
            b.box(c, f"下履带纹{i + 1:02}", (x, y, 3), (13, 78, 6), dark)
        for x in (-250, -125, 0, 125, 250):
            radius = 71 if abs(x) == 250 else 52
            b.cylinder(c, f"轮体{x}", (x, y - 31, 100),
                       (x, y + 31, 100), radius, dark)
            for sign in (-1, 1):
                outer_y = y + sign * 33
                b.cylinder(c, f"轮毂{x}_{sign}", (x, outer_y, 100),
                           (x, outer_y + sign * 3, 100), radius * .45, steel)


def make_chassis(b, silver, dark):
    c = b.component("01_实物底盘参考_720x580x220")
    for y in (-190, 190):
        b.box(c, "纵向框架梁", (0, y, 200), (720, 40, 40), silver)
    for x in (-340, 340):
        b.box(c, "端部框架梁", (x, 0, 200), (40, 340, 40), silver)
    b.box(c, "中央横梁", (0, 0, 200), (35, 340, 40), silver)
    b.box(c, "空托盘底板_概念尺寸", (-10, 0, 130), (340, 290, 6), silver)
    for y in (-147, 147):
        b.box(c, "空托盘侧边", (-10, y, 158), (340, 5, 50), silver)
    for y in (-135, 135):
        b.cylinder(c, "24V有刷电机占位", (-275, y - 42, 105),
                   (-275, y + 42, 105), 45, dark)


def make_frame(b, silver, dark, steel):
    c = b.component("02_四柱固定框架_4040_高700概念")
    for x in (-300, 300):
        for y in (-170, 170):
            label = ("前" if x > 0 else "后") + ("左" if y > 0 else "右")
            b.box(c, label + "底部连接板", (x, y, 224), (90, 70, 8), steel)
            b.profile(c, label + "固定立柱", (x, y, 228), (x, y, 928), silver)
            for dx in (-30, 30):
                b.cylinder(c, label + "固定螺栓占位", (x + dx, y, 228),
                           (x + dx, y, 232), 5, dark)
            b.box(c, label + "角码竖边", (x - 24, y, 255), (8, 30, 46), steel)
            b.box(c, label + "角码横边", (x - 40, y, 234), (40, 30, 8), steel)
    for y in (-170, 170):
        b.profile(c, "顶部纵梁", (-320, y, 948), (320, y, 948), silver)
    for x in (-300, 300):
        b.profile(c, "顶部横梁", (x, -150, 948), (x, 150, 948), silver)
    for z in (270, 880):
        b.profile(c, "前侧升降固定横梁", (300, -150, z),
                  (300, 150, z), silver)
    b.brace(c, "后侧斜撑A", (-324, -150, 280), (-324, 150, 900), steel)
    b.brace(c, "后侧斜撑B", (-335, 150, 280), (-335, -150, 900), steel)


def make_lift(b, steel, blue, orange, dark):
    fixed = b.component("03_Z轴固定导轨_中央丝杆及电机")
    for y in (-170, 170):
        b.box(fixed, "竖直导轨安装条", (326, y, 577), (12, 32, 582), blue)
        b.box(fixed, "竖直直线导轨", (337, y, 577), (10, 18, 580), steel)
        for z in range(320, 850, 100):
            b.cylinder(fixed, "导轨安装螺钉占位", (343, y, z),
                       (345, y, z), 3, dark)
    b.cylinder(fixed, "中央升降丝杆_螺纹简化", (338, 0, 288),
               (338, 0, 864), 8, steel)
    for z in (283, 866):
        b.box(fixed, "升降丝杆支承座", (335, 0, z), (42, 44, 22), blue)
    b.box(fixed, "升降电机安装板", (335, 0, 920), (82, 76, 8), steel)
    b.box(fixed, "升降电机占位", (335, 0, 965), (57, 57, 80), dark)
    b.cylinder(fixed, "联轴器占位", (338, 0, 880),
               (338, 0, 914), 15, steel)
    for z in (300, 850):
        b.box(fixed, "上下限位开关占位", (350, 143, z), (20, 18, 14), orange)

    moving = b.component("04_Z轴移动横梁_当前示意高度580")
    for y in (-170, 170):
        for z in (535, 625):
            b.box(moving, "竖直导轨滑块", (352, y, z), (24, 36, 45), blue)
        b.box(moving, "移动横梁端板", (371, y, 580), (14, 65, 150), orange)
    b.box(moving, "丝杆螺母占位", (338, 0, 580), (27, 34, 36), orange)
    b.box(moving, "螺母连接板", (366, 0, 580), (35, 45, 8), steel)
    b.profile(moving, "随Z轴升降的横梁", (390, -210, 580),
              (390, 210, 580), steel, section=40)
    return moving


def make_horizontal(b, steel, blue, orange, dark):
    c = b.component("05_横移机构_刀具沿横梁左右移动")
    b.box(c, "横移背板", (417, 0, 580), (12, 410, 95), blue)
    for z in (555, 605):
        b.box(c, "横移导轨", (430, 0, z), (10, 370, 12), steel)
    b.cylinder(c, "横移丝杆_螺纹简化", (440, -186, 580),
               (440, 186, 580), 6, steel)
    for y in (-192, 192):
        b.box(c, "横移丝杆支承", (436, y, 580), (30, 16, 30), blue)
    b.box(c, "横移电机占位", (432, -234, 580), (42, 65, 42), dark)
    b.cylinder(c, "横移联轴器", (440, -207, 580),
               (440, -195, 580), 10, steel)
    slider = b.component("06_横移滑座_刀具占位_未定最终刀头")
    for z in (555, 605):
        b.box(slider, "横移滑块", (445, 55, z), (20, 40, 25), blue)
    b.box(slider, "横移滑座", (463, 55, 580), (16, 90, 100), orange)
    b.box(slider, "刀具安装支架", (515, 55, 570), (88, 45, 40), steel)
    b.box(slider, "刀头夹座占位", (571, 55, 565), (40, 54, 58), dark)
    # Blunt visual placeholder, not an actual cutting edge or depth design.
    b.box(slider, "刀具占位_尚未设计", (604, 55, 545), (26, 14, 28), orange)


def save_view(app, name, eye, target=(60, 0, 480), up=(0, 0, 1),
              named_view=None):
    viewport = app.activeViewport
    camera = viewport.camera
    camera.isSmoothTransition = False
    camera.cameraType = adsk.core.CameraTypes.OrthographicCameraType
    camera.eye = point(eye)
    camera.target = point(target)
    camera.upVector = vector(up)
    camera.isFitView = True
    viewport.camera = camera
    adsk.doEvents()
    viewport.refresh()
    if named_view:
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.namedViews.add(viewport.camera, named_view)
    if not viewport.saveAsImageFile(str(OUTPUT / name), 1800, 1500):
        raise RuntimeError("Failed viewport export: " + name)


def prepare_views(app, design):
    """Use chassis ground XY, forward +X, left +Y and lifting +Z."""
    viewport = app.activeViewport
    camera = viewport.camera
    camera.isSmoothTransition = False
    camera.cameraType = adsk.core.CameraTypes.OrthographicCameraType
    camera.eye = point((1900, 0, 480))
    camera.target = point((60, 0, 480))
    camera.upVector = vector((0, 0, 1))
    camera.isFitView = True
    viewport.camera = camera
    adsk.doEvents()
    if not viewport.setCurrentAsFront():
        raise RuntimeError("Failed to align ViewCube front with chassis +X")
    ui = app.userInterface
    environment = ui.commandDefinitions.itemById("ViewEnvCommand")
    if environment:
        choices = adsk.core.ListControlDefinition.cast(
            environment.controlDefinition
        )
        if choices:
            for item in choices.listItems:
                if item.name.strip() == "照相亭":
                    item.isSelected = True
                    environment.execute()
                    break
    save_view(app, "front.png", (1900, 0, 480),
              named_view="前视_车头正X_Z向上")
    save_view(app, "side.png", (0, -1900, 480),
              named_view="右侧视_Z向上")
    save_view(app, "top.png", (60, 0, 2500), up=(1, 0, 0),
              named_view="俯视_XY水平_车头朝上")
    save_view(app, "isometric.png", (1550, -1550, 1300),
              named_view="整体_车底XY_升降Z")
    if not viewport.setCurrentAsHome(True):
        raise RuntimeError("Failed to set upright chassis home view")
    options = adsk.core.SaveImageFileOptions.create(
        str(OUTPUT / "isometric_transparent.png")
    )
    options.width = 1800
    options.height = 1500
    options.isBackgroundTransparent = True
    if not viewport.saveAsImageFileWithOptions(options):
        raise RuntimeError("Failed transparent viewport export")


def run(context):
    OUTPUT.mkdir(parents=True, exist_ok=True)
    app = adsk.core.Application.get()
    try:
        # A new document preserves any user edits in the already open v01.
        preferences = app.preferences.generalPreferences
        previous_up = preferences.defaultModelingOrientation
        try:
            preferences.defaultModelingOrientation = \
                adsk.core.DefaultModelingOrientations.ZUpModelingOrientation
            document = app.documents.add(
                adsk.core.DocumentTypes.FusionDesignDocumentType
            )
        finally:
            preferences.defaultModelingOrientation = previous_up
        document.name = MODEL_NAME
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.DirectDesignType
        design.fusionUnitsManager.distanceDisplayUnits = \
            adsk.fusion.DistanceUnits.MillimeterDistanceUnits
        b = Builder(design)
        silver = b.appearance("框架_铝银", (186, 196, 202))
        steel = b.appearance("导轨及连接件_钢灰", (122, 140, 153))
        blue = b.appearance("固定安装件_蓝", (34, 107, 147))
        orange = b.appearance("运动件和刀头占位_橙", (232, 137, 35))
        dark = b.appearance("电机_深灰", (38, 43, 47))
        rubber = b.appearance("履带_橡胶黑", (27, 30, 32))

        make_chassis(b, silver, dark)
        make_tracks(b, rubber, steel, dark)
        make_frame(b, silver, dark, steel)
        make_lift(b, steel, blue, orange, dark)
        make_horizontal(b, steel, blue, orange, dark)
        app.activeViewport.visualStyle = \
            adsk.core.VisualStyles.ShadedWithVisibleEdgesOnlyVisualStyle
        design.rootComponent.isOriginFolderLightBulbOn = False
        adsk.doEvents()
        prepare_views(app, design)

        exports = design.exportManager
        f3d = OUTPUT / (MODEL_NAME + ".f3d")
        step = OUTPUT / (MODEL_NAME + ".step")
        if not exports.execute(exports.createFusionArchiveExportOptions(str(f3d))):
            raise RuntimeError("F3D export failed")
        if not exports.execute(exports.createSTEPExportOptions(str(step))):
            raise RuntimeError("STEP export failed")
        bounds = b.root.boundingBox
        solid_count = sum(
            int(body.isSolid)
            for component in b.components for body in component.bRepBodies
        )
        report = {
            "status": "complete",
            "document": document.name,
            "units": "mm",
            "components": [
                {"name": c.name, "bodies": c.bRepBodies.count}
                for c in b.components
            ],
            "solid_bodies": solid_count,
            "total_bounding_box_mm": {
                "min": [round(v * 10, 2) for v in
                        (bounds.minPoint.x, bounds.minPoint.y, bounds.minPoint.z)],
                "max": [round(v * 10, 2) for v in
                        (bounds.maxPoint.x, bounds.maxPoint.y, bounds.maxPoint.z)],
            },
            "reference_chassis_mm": [720, 580, 220],
            "coordinates": {
                "ground_plane": "XY; z=0 at track contact level",
                "forward": "+X",
                "left": "+Y",
                "lift_up": "+Z",
                "front_eye_direction": [
                    round(v, 6) for v in
                    (app.activeViewport.frontEyeDirection.x,
                     app.activeViewport.frontEyeDirection.y,
                     app.activeViewport.frontEyeDirection.z)
                ],
                "front_up_direction": [
                    round(v, 6) for v in
                    (app.activeViewport.frontUpDirection.x,
                     app.activeViewport.frontUpDirection.y,
                     app.activeViewport.frontUpDirection.z)
                ],
            },
            "removed_placeholders": ["控制盒占位", "电池占位"],
            "named_views": [view.name for view in design.namedViews],
            "concept_post_height_mm": 700,
            "f3d": str(f3d),
            "step": str(step),
            "limitations": [
                "布局概念模型；不是加工图",
                "型材截面与导轨、丝杆、电机尺寸仅作占位",
                "刀头、仿形、限深、断电保持和碰撞校核尚未设计",
                "不包含运动关节或动画",
            ],
        }
        (OUTPUT / "build_report.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        error_path = OUTPUT / "build_error.txt"
        if error_path.exists():
            error_path.unlink()
        app.log("Lift concept exported to " + str(OUTPUT))
    except Exception:
        error = traceback.format_exc()
        (OUTPUT / "build_error.txt").write_text(error, encoding="utf-8")
        app.log(error)
        raise


if __name__ == "__main__":
    run(None)
