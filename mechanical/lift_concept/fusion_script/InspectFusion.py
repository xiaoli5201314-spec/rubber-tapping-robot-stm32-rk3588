"""One-off inspection of locally available Fusion appearance properties."""
import json
from pathlib import Path
import adsk.core

app = adsk.core.Application.get()
result = []
for library in app.materialLibraries:
    appearances = []
    for i, appearance in enumerate(library.appearances):
        if i >= 18:
            break
        appearances.append({
            "name": appearance.name,
            "id": appearance.id,
            "properties": [
                {"id": prop.id, "name": prop.name, "type": prop.objectType}
                for prop in appearance.appearanceProperties
            ],
        })
    result.append({
        "library": library.name,
        "appearance_count": library.appearances.count,
        "appearances": appearances,
    })
Path(r".\mechanical\lift_concept\output\materials.json").write_text(
    json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8"
)
commands = [
    {"id": command.id, "name": command.name}
    for command in app.userInterface.commandDefinitions
    if any(term in command.id.lower() for term in
           ("grid", "environment", "ground", "photo", "background"))
]
Path(r".\mechanical\lift_concept\output\display_commands.json").write_text(
    json.dumps(commands, ensure_ascii=False, indent=2), encoding="utf-8"
)
