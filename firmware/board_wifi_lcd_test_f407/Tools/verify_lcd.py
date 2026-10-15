"""Run the compiled ARM display code; render its calls with its own bitmap fonts.

Peripheral memory is simulated. PNGs and PASS mean software validation only.
"""
import json
import re
import struct
from functools import cache
from pathlib import Path

from PIL import Image, ImageDraw
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parent.parent
MAP = (ROOT / "MDK-ARM/Listings/board_wifi_lcd_test_f407.map").read_text("utf-8")
machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for address, size in [
    (0x08000000, 0x100000), (0x08100000, 0x1000), (0x20000000, 0x20000),
    (0x40000000, 0x30000), (0x6C000000, 0x1000), (0xA0000000, 0x1000),
]:
    machine.mem_map(address, size)
machine.mem_write(0x08100000, b"\x70\x47" * 32)
machine.reg_write(UC_ARM_REG_SP, 0x2001FFF8)
base = 0
for record in (ROOT / "MDK-ARM/Objects/board_wifi_lcd_test_f407.hex").read_text("ascii").splitlines():
    data = bytes.fromhex(record[1:])
    assert sum(data) % 256 == 0
    size, kind = data[0], data[3]
    address = int.from_bytes(data[1:3], "big")
    payload = data[4:4 + size]
    if kind == 4:
        base = int.from_bytes(payload, "big") << 16
    elif kind == 0:
        machine.mem_write(base + address, payload)


@cache
def symbol(name):
    match = re.search(
        r"^\s*" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+)\s+(?:Thumb Code|Data)",
        MAP, re.M,
    )
    assert match, f"Missing compiled symbol {name}"
    return int(match[1], 16)


def u32(address):
    return struct.unpack("<I", machine.mem_read(address, 4))[0]


def set32(address, value):
    machine.mem_write(address, struct.pack("<I", value))


def call(name, *args):
    regs = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)
    for register, value in zip(regs, args):
        machine.reg_write(register, value)
    stack = 0x2001FFF0
    machine.reg_write(UC_ARM_REG_SP, stack)
    for i, value in enumerate(args[4:]):
        set32(stack + 4 * i, value)
    machine.reg_write(UC_ARM_REG_LR, 0x08100031)
    machine.emu_start(symbol(name) | 1, 0x08100030, count=2000000)
    assert machine.reg_read(UC_ARM_REG_PC) == 0x08100030, name
    return machine.reg_read(UC_ARM_REG_R0)


def args(count):
    result = [machine.reg_read(r) for r in
              (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]
    stack = machine.reg_read(UC_ARM_REG_SP)
    return (result + [u32(stack + i * 4) for i in range(max(0, count - 4))])[:count]


def cstring(address):
    data = bytearray()
    for i in range(128):
        byte = machine.mem_read(address + i, 1)[0]
        if byte == 0:
            return data.decode("ascii")
        data.append(byte)
    raise AssertionError("Unterminated ASCII text")


def chinese_string(address):
    result = []
    for i in range(48):
        code = struct.unpack("<H", machine.mem_read(address + 2 * i, 2))[0]
        if not code:
            return "".join(result)
        result.append(chr(code))
    raise AssertionError("Unterminated Chinese text")


def color(value):
    return ((value >> 11) * 255 // 31,
            ((value >> 5) & 63) * 255 // 63,
            (value & 31) * 255 // 31)


ticks = 0
render_enabled = False
canvas = Image.new("RGB", (480, 800))
draw = ImageDraw.Draw(canvas)
text_calls = []
fills = []
register_writes = []
pixel_writes = []
last_command = 0
dynamic = False
last_value_rect = None


def within(x, y, w, h):
    assert 0 <= x < 480 and 0 <= y < 800 and x + w <= 480 and y + h <= 800, \
        f"Clipped drawing: {(x, y, w, h)}"
    if dynamic:
        rx, ry, rw, rh = last_value_rect
        assert rx <= x and ry <= y and x + w <= rx + rw and y + h <= ry + rh, \
            f"Value text crosses its erase rectangle: {(x, y, w, h)}"


def glyph(data, x, y, w, h, fg, bg, scale=1):
    image = Image.new("RGB", (w, h), color(bg))
    pixels = image.load()
    stride = (w + 7) // 8
    for row in range(h):
        for col in range(w):
            if data[row * stride + col // 8] & (0x80 >> (col % 8)):
                pixels[col, row] = color(fg)
    if scale != 1:
        image = image.resize((w * scale, h * scale), Image.Resampling.NEAREST)
    canvas.paste(image, (x, y))


font_header = (ROOT / "Inc/lcd_font_chinese.h").read_text("utf-8")
char_count = int(re.search(r"#define CHINESE_CHAR_COUNT (\d+)", font_header)[1])
codes = struct.unpack("<" + "H" * char_count,
                      machine.mem_read(symbol("chinese_char_codes"), 2 * char_count))
chinese_indices = {chr(code): i for i, code in enumerate(codes)}


def hook_code(uc, address, size, name):
    global ticks, last_value_rect
    if name == "Board_Delay":
        ticks += args(1)[0]
    elif name == "Board_Millis":
        uc.reg_write(UC_ARM_REG_R0, ticks)
    elif not render_enabled:
        return
    elif name == "LCD_Clear":
        draw.rectangle((0, 0, 479, 799), fill=color(args(1)[0]))
    elif name == "LCD_FillRect":
        x, y, w, h, fg = args(5)
        # Field erasure sets the rectangle before the next dynamic text call.
        if dynamic:
            last_value_rect = (x, y, w, h)
        assert x + w <= 480 and y + h <= 800
        draw.rectangle((x, y, x + w - 1, y + h - 1), fill=color(fg))
        fills.append((x, y, w, h))
    elif name in ("LCD_DrawString", "LCD_DrawStringScaled"):
        if name == "LCD_DrawString":
            x, y, address, fg, bg = args(5)
            scale = 1
        else:
            x, y, address, scale, fg, bg = args(6)
        text = cstring(address)
        within(x, y, len(text) * 8 * scale, 16 * scale)
        for i, char in enumerate(text):
            offset = (ord(char) - 32) * 16
            bitmap = machine.mem_read(symbol("LCD_FONT_8X16") + offset, 16)
            glyph(bitmap, x + i * 8 * scale, y, 8, 16, fg, bg, scale)
        text_calls.append((x, y, text, fg))
    elif name == "LCD_DrawChineseStringSized":
        x, y, address, cell, fg, bg = args(6)
        text = chinese_string(address)
        within(x, y, len(text) * cell, cell)
        for i, char in enumerate(text):
            assert char in chinese_indices, f"Missing glyph: {char}"
            stride = cell * ((cell + 7) // 8)
            bitmap = machine.mem_read(symbol(f"chinese_font_{cell}_data") +
                                      chinese_indices[char] * stride, stride)
            assert any(bitmap), f"Blank glyph: {char}"
            glyph(bitmap, x + i * cell, y, cell, cell, fg, bg)
        text_calls.append((x, y, text, fg))
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


for name in ("Board_Delay", "Board_Millis", "LCD_Clear", "LCD_FillRect",
             "LCD_DrawString", "LCD_DrawStringScaled", "LCD_DrawChineseStringSized"):
    address = symbol(name) & ~1
    machine.hook_add(UC_HOOK_CODE, hook_code, user_data=name, begin=address, end=address)


def hook_bus(uc, access, address, size, value, userdata):
    global last_command
    assert size == 2, "LCD bus must use halfword writes"
    if address == 0x6C00007E:
        last_command = value
    elif address == 0x6C000080:
        if last_command == 0x2C00:
            pixel_writes.append(value)
        else:
            register_writes.append((last_command, value))
    else:
        raise AssertionError(f"Unexpected LCD address {address:#x}")


machine.hook_add(UC_HOOK_MEM_WRITE, hook_bus, begin=0x6C000000, end=0x6C000FFF)
checks = []
# Seed existing LED/UART GPIO to catch accidental changes in LCD setup.
gpio_a, gpio_f = 0x40020000, 0x40021400
set32(gpio_a, 0x002800A0)
set32(gpio_a + 0x20, 0x00007700)
set32(gpio_a + 0x24, 0x00000770)
set32(gpio_f, (1 << 18) | (1 << 20))
saved_uart = bytes(machine.mem_read(gpio_a, 0x28))
call("LCD_Init")
assert bytes(machine.mem_read(gpio_a, 0x28)) == saved_uart
assert u32(gpio_f) & ((3 << 18) | (3 << 20)) == (1 << 18) | (1 << 20)
for gpio, pins in [(0x40020C00, (0, 1, 4, 5, 8, 9, 10, 14, 15)),
                   (0x40021000, range(7, 16)), (gpio_f, (12,)),
                   (0x40021800, (12,))]:
    for pin in pins:
        assert (u32(gpio) >> (2 * pin)) & 3 == 2
        assert (u32(gpio + 0x20 + (pin // 8) * 4) >> (pin % 8 * 4)) & 15 == 12
assert u32(0xA0000018) == 0x5011
assert u32(0xA000011C) == 0x0202
assert register_writes[-1] == (0x3600, 0)
checks.append("Compiled LCD initialization sets AF12/NE4/A6 and preserves LED/UART pins")

pixel_writes.clear()
call("LCD_DrawCharScaled", 10, 10, ord("A"), 2, 0xFFFF, 0)
assert len(pixel_writes) == 16 * 32
pixel_writes.clear()
call("LCD_DrawChineseSized", 20, 20, ord("联"), 48, 0xFFFF, 0)
assert len(pixel_writes) == 48 * 48
pixel_writes.clear()
call("LCD_FillRect", 470, 790, 40, 40, 0xF800)
assert pixel_writes == [0xF800] * 100
pixel_writes.clear()
call("LCD_DrawCharScaled", 470, 790, ord("A"), 2, 0xFFFF, 0)
assert not pixel_writes
checks.append("Compiled pixel writes, Chinese glyphs and edge clipping use the correct 16-bit bus")

render_enabled = True
call("LCD_Display_Init")
assert u32(0x40020400 + 0x18) == 1 << 15  # PB15 backlight on
canvas.save(ROOT / "Tools/preview_boot.png")
checks.append("Boot layout has all required glyphs and fits the 480x800 portrait screen")
dynamic = True


def snapshot(*, stage=5, at=1, wifi=1, http=1, mqtt=1, verified=1,
             echoes=1, rx=0, parse=0, ip="192.0.2.27", failure=0,
             http_busy=0, mqtt_busy=0, attempted=1):
    fields = (115200, rx, parse, stage, at, wifi, http, mqtt, verified, echoes,
              http_busy, mqtt_busy, attempted, attempted, failure)
    return struct.pack("<17I", *fields, rx, parse) + ip.encode().ljust(16, b"\0")


def update(data):
    global ticks
    machine.mem_write(0x20018000, data)
    start = len(text_calls)
    for _ in range(18):
        ticks += 25
        before = len(fills)
        call("LCD_Display_Service", 0x20018000, ticks)
        assert len(fills) - before <= 1, "More than one value redrawn per service"
    return text_calls[start:]


ticks = 65000
update(snapshot())
canvas.save(ROOT / "Tools/preview_connected.png")
assert any(t[2] == "通过" for t in text_calls)
checks.append("Connected view displays the actual snapshot and computer-confirmed pass")
changed = update(snapshot(verified=0))
assert any(t[2] == "待验证" for t in changed)
assert any(t[2] == "等待" for t in changed)
changed = update(snapshot(verified=1, rx=1))
assert not any(t[2] == "已通过" for t in changed)
assert any(t[2] == "异常" for t in changed)
checks.append("Unconfirmed MQTT or UART errors cannot display overall PASS")

update(snapshot(http=0, mqtt=0, verified=0))
canvas.save(ROOT / "Tools/preview_server_wait.png")
changed = update(snapshot(stage=99, wifi=0, http=0, mqtt=0, verified=0,
                          failure=2, ip=""))
assert any(t[2] == "--" for t in changed)
assert not any(t[2] == "已通过" for t in changed)
checks.append("Lost connection removes the old IP and clears the successful display")
update(snapshot(ip="255.255.255.255", echoes=0xFFFFFFFF,
                rx=0xFFFFFFFF, parse=0xFFFFFFFF))
update(snapshot(stage=1, at=0, wifi=0, http=0, mqtt=0, verified=0, attempted=0))
update(snapshot(stage=2, at=1, wifi=0, http=0, mqtt=0, verified=0, attempted=0))
update(snapshot(stage=3, mqtt=0, http=0, verified=0, http_busy=1))
update(snapshot(stage=4, mqtt=0, verified=0, mqtt_busy=1))
checks.append("Retry, in-progress, maximum IPv4/counter values fit their erase rectangles")
report = {
    "state": "COMPILED_LCD_SOFTWARE_TEST_PASS",
    "checks": checks,
    "physical_lcd_verified": False,
    "previews_use_example_data": True,
}
(ROOT / "Tools/lcd_test.json").write_text(json.dumps(report, indent=2), "utf-8")
print(json.dumps(report, indent=2))
