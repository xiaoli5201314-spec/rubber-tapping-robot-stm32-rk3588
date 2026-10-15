"""Exercise the *compiled ARM firmware* parser functions using Unicorn.

This validates stream framing and MQTT parsing only, not a physical Wi-Fi link.
Needs the Keil HEX/map plus: python -m pip install unicorn
"""
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
MAP = (ROOT / "MDK-ARM/Listings/board_wifi_test_f407.map").read_text(
    encoding="utf-8"
)
HEX = ROOT / "MDK-ARM/Objects/board_wifi_test_f407.hex"
machine = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
machine.mem_map(0x08000000, 0x100000)
machine.mem_map(0x08100000, 0x1000)
machine.mem_map(0x20000000, 0x20000)
machine.mem_write(0x08100000, b"\x70\x47" * 32)  # callback stubs: bx lr
machine.reg_write(UC_ARM_REG_SP, 0x2001FFF8)
base = 0
for record in HEX.read_text(encoding="ascii").splitlines():
    data = bytes.fromhex(record[1:])
    assert sum(data) % 256 == 0, "Bad HEX checksum"
    size = data[0]
    address = int.from_bytes(data[1:3], "big")
    kind = data[3]
    payload = data[4:4 + size]
    if kind == 4:
        base = int.from_bytes(payload, "big") << 16
    elif kind == 0:
        machine.mem_write(base + address, payload)


@cache
def symbol(name):
    match = re.search(
        r"^\s*" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+)\s+"
        r"(?:Thumb Code|Data)", MAP, re.M
    )
    assert match, f"Symbol missing: {name}"
    return int(match[1], 16)


def call(name, *args):
    for register, value in zip(
        (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args
    ):
        machine.reg_write(register, value)
    machine.reg_write(UC_ARM_REG_LR, 0x08100031)
    machine.emu_start(symbol(name) | 1, 0x08100030, count=100000)
    assert machine.reg_read(UC_ARM_REG_PC) == 0x08100030
    return machine.reg_read(UC_ARM_REG_R0)


def cstring(address):
    return bytes(machine.mem_read(address, 1024)).split(b"\0", 1)[0]


lines, payload, prompts = [], bytearray(), []


def callback(uc, address, size, userdata):
    if address == 0x08100000:
        lines.append(cstring(uc.reg_read(UC_ARM_REG_R0)))
    elif address == 0x08100010:
        payload.append(uc.reg_read(UC_ARM_REG_R0) & 255)
    elif address == 0x08100020:
        prompts.append(True)
    elif address in (symbol("Board_Debug") & ~1, symbol("Board_DebugLine") & ~1):
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))


machine.hook_add(UC_HOOK_CODE, callback)
call("AtParser_Init", 0x08100001, 0x08100011, 0x08100021)


def feed(data):
    for byte in data:
        call("AtParser_Feed", byte)


checks = []
feed(b"\r\nOK\r\nERROR\r\n> ")
assert lines == [b"OK", b"ERROR"] and len(prompts) == 1
checks.append("AT lines and bare send prompt")

lines.clear()
binary = b"\0ERROR\r\n> +MQTTSUBRECV:0,\"x\",2,ab"
feed(f"+IPD,{len(binary)}:".encode() + binary + b"\r\nSEND OK\r\n")
assert payload == binary and lines == [b"SEND OK"] and len(prompts) == 1
checks.append("Binary IPD payload cannot create fake AT events")

payload.clear()
lines.clear()
feed(b"+IPD,0:+IPD,3:abc+IPD,2:de\r\nOK\r\n")
assert payload == b"abcde" and lines == [b"OK"]
checks.append("Zero length and consecutive fragmented IPD frames")

lines.clear()
feed(b"x" * 900 + b"\r\nOK\r\n")
assert call("AtParser_Errors") == 1 and lines == [b"OK"]
feed(b"+IPD,99999999:\r\n")
assert call("AtParser_Errors") == 2
checks.append("Oversized lines and invalid IPD lengths are rejected")

lines.clear()
feed(b"+IPD,100:partial")
call("AtParser_Reset")
feed(b"OK\r\n")
assert lines == [b"OK"]
checks.append("Parser reset recovers from a partial frame")

topic = b"f407/wifi_test/0123456789ABCDEF01234567/cmd"
machine.mem_write(symbol("s_cmd_topic"), topic + b"\0")
nonce = b"0123456789abcdef0123456789abcdef"


def mqtt_line(topic_name, body, length=None):
    data = (
        b'+MQTTSUBRECV:0,"' + topic_name + b'",'
        + str(len(body) if length is None else length).encode()
        + b"," + body
    )
    machine.mem_write(0x20018000, data + b"\0")
    call("on_mqtt", 0x20018000)


mqtt_line(topic, b"CHALLENGE:" + nonce)
assert machine.mem_read(symbol("s_echo_pending"), 1) == b"\1"
assert cstring(symbol("s_echo_nonce")) == nonce
checks.append("Compiled MQTT parser accepts the exact challenge and topic")

machine.mem_write(symbol("s_echo_pending"), b"\0")
mqtt_line(topic + b"x", b"CHALLENGE:" + nonce)
mqtt_line(topic, b"CHALLENGE:" + nonce, 41)
mqtt_line(topic, b"CHALLENGE:" + b"z" * 32)
assert machine.mem_read(symbol("s_echo_pending"), 1) == b"\0"
checks.append("Wrong topic, length and nonce rejected")

machine.mem_write(symbol("s_last_echo_nonce"), nonce + b"\0")
flag = symbol("g_wifi_test_status") + 32
mqtt_line(topic, b"VERIFIED:" + b"0" * 32)
assert struct.unpack("<I", machine.mem_read(flag, 4))[0] == 0
mqtt_line(topic, b"VERIFIED:" + nonce)
assert struct.unpack("<I", machine.mem_read(flag, 4))[0] == 1
checks.append("Host confirmation must match the echoed nonce")

ip_line = b'+CIFSR:STAIP,"192.0.2.50"'
machine.mem_write(0x20018000, ip_line + b"\0")
call("on_line", 0x20018000)
assert cstring(symbol("s_ip")) == b"192.0.2.50"
checks.append("Module IP address parsed without dropping a character")


def http_validate(response, overflow=0):
    machine.mem_write(symbol("s_http"), response + b"\0")
    machine.mem_write(symbol("s_http_overflow"), bytes([overflow]))
    return call("validate_http")


response = b"HTTP/1.1 200 OK\r\nContent-Length: 46\r\n\r\nF407_HTTP_OK:" + nonce + b"\n"
assert http_validate(response) == 1
assert cstring(symbol("s_http_token")) == nonce
assert http_validate(response.replace(b"200 OK", b"404 Error")) == 0
assert http_validate(response[:-10]) == 0
assert http_validate(response, overflow=1) == 0
checks.append("Compiled HTTP verifier rejects errors, partial tokens and overflow")

report = {"state": "COMPILED_PROTOCOL_TEST_PASS", "checks": checks,
          "physical_board_verified": False}
(ROOT / "Host/protocol_test.json").write_text(
    json.dumps(report, indent=2), encoding="utf-8"
)
print(json.dumps(report, indent=2))
