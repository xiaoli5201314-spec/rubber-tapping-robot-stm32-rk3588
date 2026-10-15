import re
import os
from pathlib import Path

vendor_lcd_dir = os.environ.get("ALIENTEK_LCD_DIR")
if not vendor_lcd_dir:
    raise SystemExit("Set ALIENTEK_LCD_DIR to your separately obtained BSP/LCD directory.")
ref_lcd_ex = os.path.join(vendor_lcd_dir, "lcd_ex.c")
ref_font = os.path.join(vendor_lcd_dir, "lcdfont.h")
out_dir = str(Path(__file__).resolve().parents[1] / "App")

with open(ref_lcd_ex, "r", encoding="gbk", errors="replace") as f:
    src = f.read()

m = re.search(
    r"void lcd_ex_nt35510_reginit\(void\)\s*\{(.*?)\n\s*/\*\*\n\s*\* @brief\s+SSD1963",
    src,
    re.S,
)
assert m, "nt35510 init not found"
body = m.group(1)

pairs = re.findall(r"lcd_write_reg\((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)\s*;", body)
cmds = re.findall(r"lcd_wr_regno\((0x[0-9A-Fa-f]+)\)\s*;", body)

lines = ["#ifndef LCD_NT35510_INIT_H", "#define LCD_NT35510_INIT_H", "",
         "#include <stdint.h>", "",
         "static const uint16_t LCD_NT35510_INIT[][2] = {"]
for reg, val in pairs:
    lines.append(f"    {{0x{int(reg, 16):04X}, 0x{int(val, 16):04X}}},")
lines.append("};")
lines.append("")
lines.append("#define LCD_NT35510_INIT_COUNT \\")
lines.append("    (sizeof(LCD_NT35510_INIT) / sizeof(LCD_NT35510_INIT[0]))")
lines.append("")
lines.append("static const uint16_t LCD_NT35510_CMD_ONLY[] = {")
for cmd in cmds:
    lines.append(f"    0x{int(cmd, 16):04X},")
lines.append("};")
lines.append("")
lines.append("#define LCD_NT35510_CMD_ONLY_COUNT \\")
lines.append("    (sizeof(LCD_NT35510_CMD_ONLY) / sizeof(LCD_NT35510_CMD_ONLY[0]))")
lines.append("")
lines.append("#endif")

with open(os.path.join(out_dir, "lcd_nt35510_init.h"), "w", encoding="ascii", newline="\n") as f:
    f.write("\n".join(lines) + "\n")

with open(ref_font, "r", encoding="gbk", errors="replace") as f:
    font_src = f.read()

fm = re.search(r"const unsigned char asc2_1608\[95\]\[16\]\s*=\s*\{(.*?)\n\s*\};", font_src, re.S)
assert fm, "asc2_1608 not found"
font_body = fm.group(1)
hexes = re.findall(r"0x([0-9A-Fa-f]{2})", font_body)
assert len(hexes) == 95 * 16, f"font byte count {len(hexes)}"

rows = []
for i in range(95):
    source = [int(value, 16) for value in hexes[i * 16 : (i + 1) * 16]]
    glyph = []
    for y in range(16):
        row = 0
        for x in range(8):
            source_byte = source[x * 2 + (y // 8)]
            if source_byte & (0x80 >> (y % 8)):
                row |= 0x80 >> x
        glyph.append(row)
    rows.append("    {" + ",".join(f"0x{value:02X}" for value in glyph) + "},")

flines = ["#ifndef LCD_FONT_8X16_H", "#define LCD_FONT_8X16_H", "",
          "#include <stdint.h>", "",
          "static const uint8_t LCD_FONT_8X16[95][16] = {"]
flines.extend(rows)
flines.append("};")
flines.append("")
flines.append("#endif")

with open(os.path.join(out_dir, "lcd_font_8x16.h"), "w", encoding="ascii", newline="\n") as f:
    f.write("\n".join(flines) + "\n")

print("pairs", len(pairs), "cmds", cmds)
print("wrote", os.path.join(out_dir, "lcd_nt35510_init.h"))
print("wrote", os.path.join(out_dir, "lcd_font_8x16.h"))
