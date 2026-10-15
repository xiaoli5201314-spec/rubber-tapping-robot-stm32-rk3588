#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成STM32中文字库 - 16x16点阵
"""

from PIL import Image, ImageDraw, ImageFont
import sys
from pathlib import Path

# 需要的中文字符（按使用频率和功能分组）
CHARS = [
    # 状态相关
    '底', '盘', '监', '控', '系', '统',
    '状', '态', '锁', '定', '等', '待', '就', '绪', '失',
    
    # 通道和方向
    '通', '道', '左', '右', '目', '标', '输', '入', '出',
    '前', '进', '后', '退', '制', '动', '停',
    
    # 数值和单位
    '速', '度', '值', '微', '秒',
    
    # 故障相关
    '故', '障', '无', '信', '号', '效', '常', '正',

    # Dashboard labels
    '电', '机', '息',

    # Receiver-only bench dashboard
    '遥', '联', '调', '模', '拟', '禁', '用',
]

FONT_SPECS = {
    16: 13,
    24: 20,
    48: 40,
}


def find_font_path():
    font_paths = [
        'C:/Windows/Fonts/msyhbd.ttc',
        'C:/Windows/Fonts/simhei.ttf',
        'C:/Windows/Fonts/msyh.ttc',
    ]

    for path in font_paths:
        try:
            ImageFont.truetype(path, 16)
            return path
        except OSError:
            continue

    raise RuntimeError("Cannot find a Chinese font")


def render_glyph(char, cell_size, font):
    image = Image.new('1', (cell_size, cell_size), color=0)
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), char, font=font)
    width = right - left
    height = bottom - top
    x = ((cell_size - width) // 2) - left
    y = ((cell_size - height) // 2) - top
    draw.text((x, y), char, font=font, fill=1)
    return image


def pack_bitmap(image):
    width, height = image.size
    bytes_per_row = (width + 7) // 8
    pixels = list(image.get_flattened_data())
    packed = []

    for row in range(height):
        for byte_column in range(bytes_per_row):
            value = 0
            for bit in range(8):
                column = byte_column * 8 + bit
                if column < width and pixels[row * width + column]:
                    value |= 0x80 >> bit
            packed.append(value)

    return packed


def generate_font_data():
    """Generate centered Chinese bitmaps in all dashboard font sizes."""
    font_path = find_font_path()
    print(f"Using font: {font_path}")
    fonts = {}

    for cell_size, point_size in FONT_SPECS.items():
        font = ImageFont.truetype(font_path, point_size)
        fonts[cell_size] = {
            char: pack_bitmap(render_glyph(char, cell_size, font))
            for char in CHARS
        }

    return fonts

def generate_header(font_data):
    """生成C头文件"""
    
    lines = []
    lines.append("// Auto-generated Chinese font data - 16/24/48 pixel bitmaps")
    lines.append("// 中文字库 - 多字号点阵，字形已居中并保留边距")
    lines.append("")
    lines.append("#ifndef LCD_FONT_CHINESE_H")
    lines.append("#define LCD_FONT_CHINESE_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append("// 字符数量")
    lines.append(f"#define CHINESE_CHAR_COUNT {len(CHARS)}")
    lines.append("")
    lines.append("// 字符Unicode编码表")
    lines.append("static const uint16_t chinese_char_codes[CHINESE_CHAR_COUNT] = {")
    for char in CHARS:
        lines.append(f"    0x{ord(char):04X},  // {char}")
    lines.append("};")
    lines.append("")
    
    for cell_size in FONT_SPECS:
        bytes_per_row = (cell_size + 7) // 8
        bytes_per_char = cell_size * bytes_per_row
        lines.append(f"#define CHINESE_FONT_{cell_size}_SIZE {cell_size}")
        lines.append(f"#define CHINESE_FONT_{cell_size}_BYTES {bytes_per_char}")
        lines.append(
            f"static const uint8_t chinese_font_{cell_size}_data"
            f"[CHINESE_CHAR_COUNT][CHINESE_FONT_{cell_size}_BYTES] = {{"
        )

        for char in CHARS:
            data = font_data[cell_size][char]
            lines.append(f"    // {char} (U+{ord(char):04X})")
            lines.append("    {")
            for row in range(cell_size):
                start = row * bytes_per_row
                row_data = data[start:start + bytes_per_row]
                byte_str = ", ".join(f"0x{value:02X}" for value in row_data)
                lines.append(f"        {byte_str},")
            lines.append("    },")

        lines.append("};")
        lines.append("")
    lines.append("#endif // LCD_FONT_CHINESE_H")
    
    return '\n'.join(lines)

def main():
    print("Generating Chinese font data...")
    font_data = generate_font_data()
    
    header = generate_header(font_data)
    
    output_path = Path(__file__).resolve().parents[1] / "App/lcd_font_chinese.h"
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(header + '\n')
    
    print(f"Generated: {output_path}")

if __name__ == '__main__':
    main()
