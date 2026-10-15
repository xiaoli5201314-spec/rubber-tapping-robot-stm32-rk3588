"""Generate local LCD glyph data from this target's declared Chinese labels."""
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
generator_path = ROOT.parent / "chassis_f407/tools/gen_chinese_font.py"
spec = importlib.util.spec_from_file_location("existing_font_generator", generator_path)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
source = (ROOT / "Src/lcd_display.c").read_text(encoding="utf-8")
generator.CHARS = sorted({
    chr(int(code, 16))
    for body in re.findall(r"ZH\(\w+,\s*(.*?)\);", source, re.S)
    for code in re.findall(r"0x([0-9A-Fa-f]{4})", body)
})
fonts = generator.generate_font_data()
for size, glyphs in fonts.items():
    for char, bitmap in glyphs.items():
        assert any(bitmap), f"Empty glyph: {char}"
        stride = (size + 7) // 8
        assert not any(bitmap[:stride]) and not any(bitmap[-stride:])
        for row in range(size):
            assert not bitmap[row * stride] & 0x80
            assert not bitmap[(row + 1) * stride - 1] & 1
(ROOT / "Inc/lcd_font_chinese.h").write_text(
    generator.generate_header(fonts) + "\n", encoding="utf-8"
)
print(f"Generated {len(generator.CHARS)} centered Chinese glyphs in 3 sizes.")
