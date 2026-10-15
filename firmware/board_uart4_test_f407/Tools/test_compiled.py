"""Exercise the compiled UART4 HEX with simulated registers and host output."""
import json
import re
import struct
from functools import cache
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parent.parent
MAP = (ROOT / "MDK-ARM/Listings/board_uart4_test_f407.map").read_text("utf-8")
machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
for start, size in [(0x08000000, 0x100000), (0x08100000, 0x1000),
                    (0x20000000, 0x20000), (0x40000000, 0x30000),
                    (0xE000E000, 0x1000)]:
    machine.mem_map(start, size)
machine.reg_write(UC_ARM_REG_SP, 0x2001FFF8)
machine.mem_write(0x08100000, b"\x70\x47" * 32)
base = 0
for record in (ROOT / "MDK-ARM/Objects/board_uart4_test_f407.hex").read_text("ascii").splitlines():
    data = bytes.fromhex(record[1:])
    assert sum(data) % 256 == 0
    length, kind = data[0], data[3]
    address = int.from_bytes(data[1:3], "big")
    if kind == 4:
        base = int.from_bytes(data[4:4 + length], "big") << 16
    elif kind == 0:
        machine.mem_write(base + address, data[4:4 + length])


@cache
def symbol(name):
    match = re.search(r"^\s*" + re.escape(name) +
                      r"\s+(0x[\da-fA-F]+)\s+(?:Thumb Code|Data)", MAP, re.M)
    assert match, name
    return int(match[1], 16)


def u32(address):
    return struct.unpack("<I", machine.mem_read(address, 4))[0]


def set32(address, value):
    machine.mem_write(address, struct.pack("<I", value))


def call(name, *values):
    for reg, value in zip(
        (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), values
    ):
        machine.reg_write(reg, value)
    machine.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
    machine.reg_write(UC_ARM_REG_LR, 0x08100031)
    machine.emu_start(symbol(name) | 1, 0x08100030, count=500000)
    assert machine.reg_read(UC_ARM_REG_PC) == 0x08100030, name
    return machine.reg_read(UC_ARM_REG_R0)


ticks = 0
output = bytearray()
capture_enabled = False


def hook_output(uc, address, size, userdata):
    if not capture_enabled:
        return
    text = uc.reg_read(UC_ARM_REG_R0)
    for i in range(4096):
        byte = uc.mem_read(text + i, 1)[0]
        if byte == 0:
            break
        output.append(byte)
    else:
        raise AssertionError("Unterminated output string")
    uc.reg_write(UC_ARM_REG_R0, 1)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


address = symbol("Uart4_Write") & ~1
machine.hook_add(UC_HOOK_CODE, hook_output, begin=address, end=address)
set32(symbol("SystemCoreClock"), 16000000)
call("Uart4_Init")
gpio_c, uart4 = 0x40020800, 0x40004C00
assert (u32(gpio_c) >> 20) & 15 == 10
assert (u32(gpio_c + 0x24) >> 8) & 255 == 0x88
assert u32(uart4 + 8) == 139
assert u32(uart4 + 12) == 0x202C
assert u32(uart4 + 16) == u32(uart4 + 20) == 0
assert u32(0xE000E104) & (1 << 20)  # UART4 IRQ52
assert u32(0xE000E014) == 15999  # 1ms SysTick
assert u32(0x40023800 + 0x40) & (1 << 19)
checks = ["Compiled GPIO AF8, UART4 115200/8N1/no-flow, IRQ and millisecond tick setup"]

assert call("Uart4_Read") == 0xFFFFFFFF
for byte in b"abc":
    set32(uart4, 0x20)
    set32(uart4 + 4, byte)
    call("UART4_IRQHandler")
assert bytes(call("Uart4_Read") for _ in range(3)) == b"abc"
assert call("Uart4_Read") == 0xFFFFFFFF
set32(uart4, 0x28)  # RXNE + ORE
set32(uart4 + 4, 0x58)
call("UART4_IRQHandler")
assert call("Uart4_Read") == 0xFFFFFFFF
call("Uart4_GetStats", 0x20018000)
assert u32(0x20018000 + 8) == 1
for byte in range(1025):
    set32(uart4, 0x20)
    set32(uart4 + 4, byte & 255)
    call("UART4_IRQHandler")
assert bytes(call("Uart4_Read") for _ in range(1023)) == bytes(i & 255 for i in range(1023))
assert call("Uart4_Read") == 0xFFFFFFFF
call("Uart4_GetStats", 0x20018000)
assert u32(0x20018000 + 12) == 2
checks.append("Compiled interrupt buffers bytes in order and counts hardware/buffer loss")

set32(uart4, 0x80)  # TXE
machine.mem_write(0x20018000, b"UART4 test\0")
assert call("Uart4_Write", 0x20018000) == 1
assert u32(uart4 + 4) == ord("t")
call("Uart4_GetStats", 0x20018100)
assert u32(0x20018100 + 4) == 10
checks.append("Compiled transmitter writes bytes to UART4, with sent-byte accounting")

capture_enabled = True
call("Uart4Test_Init")
assert b"115200 baud, 8N1" in output
output.clear()


def feed(data, now=0):
    for byte in data:
        call("Uart4Test_Feed", byte, now)


feed(b"PING\r\n")
assert output == b"PONG\r\n"
output.clear()
feed(b"hello UART4\n")
assert output == b"ECHO: hello UART4\r\n"
output.clear()
feed(b"without newline", 10)
call("Uart4Test_Service", 89)
assert not output
call("Uart4Test_Service", 90)
assert output == b"ECHO: without newline\r\n"
checks.append("Compiled PING, exact text echo, CR/LF and 80ms idle framing")

output.clear()
feed(b"A" * 191 + b"\r")
assert output == b"ECHO: " + b"A" * 191 + b"\r\n"
output.clear()
feed(b"A" * 192 + b"\nPING\n")
assert output.endswith(b"PONG\r\n") and output.startswith(b"ERROR:")
output.clear()
feed(b"bad\0text\rPING\r")
assert output.startswith(b"ERROR:") and output.endswith(b"PONG\r\n")
assert u32(symbol("g_uart4_test_status") + 28) == 2
checks.append("Maximum message works; overlong/NUL frames fail explicitly and recover")

output.clear()
feed(b"STATUS\r")
assert output.startswith(b"STATUS tick=") and b"lineerr=2" in output
output.clear()
call("Uart4Test_Service", 1000)
assert output.startswith(b"UART4 tick=1")
output.clear()
call("Uart4Test_Service", 1999)
assert not output
call("Uart4Test_Service", 2000)
assert output.startswith(b"UART4 tick=2")
checks.append("Status counters and one-second heartbeat use compiled firmware state")
report = {"state": "COMPILED_UART4_SOFTWARE_PASS",
          "checks": checks, "physical_serial_verified": False}
(ROOT / "Tools/software_test.json").write_text(json.dumps(report, indent=2), "utf-8")
print(json.dumps(report, indent=2))
