"""Exercise actual Keil ARM instructions with controlled ESP/UART fixtures.

This proves software behavior only. No physical board or Wi-Fi is simulated
as a successful hardware test. Run after rebuilding the standalone project.
"""
import json
import os
import re
import struct
import unittest
from functools import cache
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

ROOT = Path(__file__).resolve().parent.parent
TARGET = os.environ.get("F407_PROTOCOL_TARGET", "board_web_link_test_f407")
SUFFIX = {"board_rc_web_test_f407": "_RC_Web",
          "board_chassis_web_f407_30pct": "_Motor_Web_30pct",
          "board_chassis_web_f407_60pct": "_Motor_Web_60pct",
          "board_chassis_web_f407": "_Motor_Web"}.get(TARGET, "")
MOTOR_VERSIONS = {"board_chassis_web_f407_30pct": "mqttmotor30",
                  "board_chassis_web_f407_60pct": "mqttmotor60",
                  "board_chassis_web_f407": "mqttmotor100"}
MAP = (ROOT / f"MDK-ARM/Listings{SUFFIX}/{TARGET}.map").read_text("utf-8")
HEX = ROOT / f"MDK-ARM/Objects{SUFFIX}/{TARGET}.hex"
NONCE = b"0123456789abcdef0123456789abcdef"
TOPIC = b"f407/web_test/0123456789ABCDEF01234567/cmd"
CONNECTED = b'+MQTTCONNECTED:0,1,"192.168.137.1",1883,"",0'


@cache
def symbol(name):
    match = re.search(
        r"^\s*" + re.escape(name) + r"\s+(0x[0-9a-fA-F]+)\s+"
        r"(?:Thumb Code|Data)", MAP, re.M,
    )
    if not match:
        raise AssertionError(f"Missing compiled symbol: {name}")
    return int(match[1], 16)


class Firmware:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        for address, size in [
            (0x08000000, 0x100000), (0x08100000, 0x1000),
            (0x20000000, 0x20000), (0x40000000, 0x30000),
            (0xA0000000, 0x1000), (0x6C000000, 0x1000),
            (0xE000E000, 0x2000),
        ]:
            self.uc.mem_map(address, size)
        self.uc.mem_write(0x08100000, b"\x70\x47" * 32)
        base = 0
        for record in HEX.read_text("ascii").splitlines():
            data = bytes.fromhex(record[1:])
            assert sum(data) % 256 == 0, "Bad HEX checksum"
            size, kind = data[0], data[3]
            address = int.from_bytes(data[1:3], "big")
            if kind == 4:
                base = int.from_bytes(data[4:4 + size], "big") << 16
            elif kind == 0:
                self.uc.mem_write(base + address, data[4:4 + size])
        self.ticks = 1000
        # Busy AT waits need a progressing clock; precise control-boundary
        # fixtures can set this to zero and advance wall time explicitly.
        self.clock_step = 1
        self.rx = bytearray()
        self.scheduled_rx = []
        self.tx = []
        self.published = []
        self.leds = []
        self.lines = []
        self.data = bytearray()
        self.prompts = 0
        self.raw_expected = None
        self.mqtt_conn_pending = False
        self.mqtt_conn_reply = b"\r\nOK\r\n"
        self.mqtt_connected_reply = b"\r\n" + CONNECTED + b"\r\n"
        self.mqtt_connected_delay = 0
        self.auto_esp = False
        self.drop_next_response = False
        self.debug = []
        try:
            self.lcd_service = symbol("LCD_Display_Service") & ~1
        except AssertionError:
            self.lcd_service = None
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def text(self, address):
        data = bytearray()
        for index in range(2048):
            value = self.uc.mem_read(address + index, 1)[0]
            if not value:
                return bytes(data)
            data.append(value)
        raise AssertionError("Unterminated string")

    def write_text(self, name, value):
        self.uc.mem_write(symbol(name), value + b"\0")

    def word(self, name, offset=0, value=None):
        address = symbol(name) + offset
        if value is not None:
            self.uc.mem_write(address, struct.pack("<I", value))
        return struct.unpack("<I", self.uc.mem_read(address, 4))[0]

    def call(self, name, *args):
        self.uc.reg_write(UC_ARM_REG_SP, 0x2001FFF0)
        for register, value in zip(
            (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3), args
        ):
            self.uc.reg_write(register, value)
        self.uc.reg_write(UC_ARM_REG_LR, 0x08100031)
        self.uc.emu_start(symbol(name) | 1, 0x08100030, count=2000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == 0x08100030, name
        return self.uc.reg_read(UC_ARM_REG_R0)

    def hook(self, uc, address, size, unused):
        r0 = uc.reg_read(UC_ARM_REG_R0)
        r1 = uc.reg_read(UC_ARM_REG_R1)
        result = None
        if address == 0x08100000:
            self.lines.append(self.text(r0))
        elif address == 0x08100010:
            self.data.append(r0 & 255)
        elif address == 0x08100020:
            self.prompts += 1
        elif address == (symbol("Board_Millis") & ~1):
            self.ticks += self.clock_step
            result = self.ticks
        elif address == (symbol("Board_WifiRead") & ~1):
            while self.scheduled_rx and self.scheduled_rx[0][0] <= self.ticks:
                unused_tick, data = self.scheduled_rx.pop(0)
                self.rx.extend(data)
            result = self.rx.pop(0) if self.rx else 0xFFFFFFFF
        elif address == (symbol("Board_WifiRxErrors") & ~1):
            result = 0
        elif address == (symbol("Board_Leds") & ~1):
            self.leds.append((self.ticks, r0, r1))
        elif address in (symbol("Board_Debug") & ~1, symbol("Board_DebugLine") & ~1):
            self.debug.append(self.text(r0))
        elif address == self.lcd_service:
            pass
        elif address == (symbol("Board_WifiWrite") & ~1):
            text = self.text(r0)
            self.tx.append(text)
            if self.auto_esp:
                if text.startswith(b"AT+MQTTPUBRAW="):
                    match = re.fullmatch(
                        rb'AT\+MQTTPUBRAW=0,"([^"]+)",(\d+),[01],0', text
                    )
                    assert match, text
                    self.raw_expected = (match[1], int(match[2]))
                elif text.startswith(b"AT+MQTTCONN="):
                    self.mqtt_conn_pending = True
                elif text == b"\r\n":
                    if self.drop_next_response:
                        self.drop_next_response = False
                    elif self.raw_expected:
                        self.rx.extend(b"\r\nOK\r\n> ")
                    elif self.mqtt_conn_pending:
                        self.mqtt_conn_pending = False
                        self.rx.extend(self.mqtt_conn_reply)
                        self.schedule_rx(
                            self.mqtt_connected_delay, self.mqtt_connected_reply
                        )
                    else:
                        self.rx.extend(b"\r\nOK\r\n")
                elif self.raw_expected:
                    topic, length = self.raw_expected
                    assert len(text) == length, (text, length)
                    self.published.append((topic, text, self.ticks))
                    self.raw_expected = None
                    self.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        else:
            return
        if result is not None:
            uc.reg_write(UC_ARM_REG_R0, result)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def feed(self, data):
        for byte in data:
            self.call("AtParser_Feed", byte)

    def schedule_rx(self, milliseconds, data):
        if milliseconds == 0:
            self.rx.extend(data)
        elif data:
            self.scheduled_rx.append((self.ticks + milliseconds, data))
            self.scheduled_rx.sort(key=lambda response: response[0])

    def line(self, text):
        self.uc.mem_write(0x20018000, text + b"\0")
        self.call("on_line", 0x20018000)

    def mqtt(self, payload, topic=TOPIC, length=None):
        self.line(
            b'+MQTTSUBRECV:0,"' + topic + b'",'
            + str(len(payload) if length is None else length).encode()
            + b"," + payload
        )

    def publish_status(self):
        self.call("publish_status")
        self.call("service")  # prompt -> exact RAW payload
        self.call("service")  # +MQTTPUB:OK -> completed transaction

    def ready(self):
        self.call("AtParser_Init", symbol("on_line") | 1,
                  symbol("on_data") | 1, symbol("on_prompt") | 1)
        self.write_text("s_cmd_topic", TOPIC)
        self.write_text("s_echo_topic", TOPIC[:-3] + b"echo")
        self.write_text("s_status_topic", TOPIC[:-3] + b"status")
        self.write_text("s_ip", b"192.168.137.2")
        self.line(b'+CIPSTA:gateway:"192.168.137.1"')
        self.write_text("s_session_token", NONCE)
        self.word("g_wifi_test_status", 12, 5)  # stage
        self.word("g_wifi_test_status", 20, 1)  # wifi_ok
        self.word("g_wifi_test_status", 24, 1)  # session_ok
        self.word("g_wifi_test_status", 28, 1)  # mqtt_ok
        self.auto_esp = True


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()
        if TARGET in MOTOR_VERSIONS:
            # Real main initializes capture/control before any network reporting;
            # unlike the simulation, a snapshot never initializes control itself.
            self.f.call("Board_Init")
            self.f.call("RcWeb_Init")

    def test_stream_framing_does_not_create_fake_at_events(self):
        f = self.f
        f.call("AtParser_Init", 0x08100001, 0x08100011, 0x08100021)
        binary = b"\0ERROR\r\n> +MQTTSUBRECV:0,\"x\",2,ab"
        f.feed(b"\r\nOK\r\n> " + f"+IPD,{len(binary)}:".encode()
               + binary + b"\r\nSEND OK\r\n")
        self.assertEqual(f.lines, [b"OK", b"SEND OK"])
        self.assertEqual(f.data, binary)
        self.assertEqual(f.prompts, 1)
        f.feed(b"x" * 900 + b"\r\nOK\r\n+IPD,99999999:\r\n")
        self.assertEqual(f.call("AtParser_Errors"), 2)
        f.feed(b"+IPD,100:partial")
        f.call("AtParser_Reset")
        f.feed(b"OK\r\n")
        self.assertEqual(f.lines[-1], b"OK")

    def test_gateway_and_ip_query_are_used_for_hotspot_address(self):
        f = self.f
        f.line(b'+CIPSTA:ip:"192.168.137.2"')
        self.assertEqual(f.text(symbol("s_ip")), b"192.168.137.2")
        f.line(b'+CIPSTA:gateway:"192.168.137.1"')
        self.assertEqual(f.text(symbol("s_server")), b"192.168.137.1")
        f.line(b'+CIPSTA:gateway:"999.168.137.1"')
        self.assertEqual(f.text(symbol("s_server")), b"192.168.137.1")
        f.line(b'+CIPSTA:gateway:"0.0.0.0"')
        self.assertEqual(f.text(symbol("s_server")), b"192.168.137.1")
        f.line(b"WIFI DISCONNECT")
        self.assertEqual(f.text(symbol("s_server")), b"")
        f.line(b'+CIPSTA_CUR:gateway:"192.168.50.1"')
        self.assertEqual(f.text(symbol("s_server")), b"192.168.50.1")

    def test_session_requires_correct_topic_exact_length_and_nonce(self):
        f = self.f
        f.ready()
        f.publish_status()
        f.call("clear_session")
        for payload, topic, length in [
            (b"SESSION:" + NONCE, TOPIC + b"x", None),
            (b"SESSION:" + NONCE, TOPIC, 39),
            (b"SESSION:" + b"z" * 32, TOPIC, None),
            (b"SESSION:" + NONCE + b"x", TOPIC, None),
        ]:
            f.mqtt(payload, topic, length)
            self.assertEqual(f.word("g_wifi_test_status", 24), 0)
        f.mqtt(b"SESSION:" + NONCE)
        self.assertEqual(f.word("g_wifi_test_status", 24), 1)
        self.assertEqual(f.text(symbol("s_session_token")), NONCE)

    def test_status_uses_new_topic_complete_fields_and_raw_length(self):
        f = self.f
        f.ready()
        f.publish_status()
        self.assertEqual(len(f.published), 1)
        topic, body, unused = f.published[0]
        self.assertEqual(topic, TOPIC[:-3] + b"status")
        motor = TARGET in MOTOR_VERSIONS
        version = MOTOR_VERSIONS[TARGET].encode() if motor else b"mqttrc1" if SUFFIX else b"mqttweb1"
        extra = (rb";ch1=0;ch2=0;ch5=0;p1=0;p2=0;p5=0;v1=0;v2=0;v5=0;"
                 rb"state=LOCKED;left=0;right=0;motor=" + (b"1" if motor else b"0")) if SUFFIX else b""
        self.assertRegex(body, rb"^fw=" + version + rb";mqtt=1;token=" + NONCE
                         + rb";ip=192\.168\.137\.2;server=192\.168\.137\.1;"
                         rb"uptime=\d+;seq=1;runled=0;cmdcount=0;rxerr=0;parseerr=0"
                         + extra + rb"$")
        self.assertTrue(all(len(part) < 256 for part in f.tx if part.startswith(b"AT+")))
        self.assertLess(len(body), 512)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)

    def test_led_ack_occurs_only_after_two_flashes_and_is_deduplicated(self):
        f = self.f
        f.ready()
        f.publish_status()
        f.published.clear()
        f.mqtt(b"LEDTEST:" + NONCE)
        start = f.ticks
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)
        f.call("send_echo")
        self.assertEqual(f.published, [])
        f.ticks = start + 100
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.ticks = start + 180
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)
        f.ticks = start + 260
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.call("send_echo")
        f.call("service")
        f.call("service")
        self.assertEqual(f.published[-1][1], b"LEDACK:" + NONCE + b":OK")
        self.assertGreaterEqual(f.published[-1][2] - start, 240)
        f.published.clear()
        f.mqtt(b"LEDTEST:" + NONCE)
        f.ticks += 500
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.published, [])

    def test_invalid_queued_and_disconnected_commands_do_not_flash(self):
        f = self.f
        f.ready()
        # SUB can deliver retained/queued data before first successful heartbeat.
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.publish_status()
        f.published.clear()
        for body, topic, length in [
            (b"LEDTEST:" + NONCE, TOPIC + b"x", None),
            (b"LEDTEST:" + NONCE, TOPIC, 39),
            (b"LEDTEST:" + b"z" * 32, TOPIC, None),
            (b"LEDTEST:" + NONCE + b"x", TOPIC, None),
            (b"CHALLENGE:" + NONCE, TOPIC, None),
        ]:
            f.mqtt(body, topic, length)
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.published, [])
        f.mqtt(b"LEDTEST:" + NONCE)
        f.line(b"+MQTTDISCONNECTED:0")
        f.ticks += 500
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.published, [])
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)
        f.line(b"WIFI DISCONNECT")
        self.assertEqual(f.text(symbol("s_ip")), b"")
        self.assertEqual(f.word("g_wifi_test_status", 24), 0)
        self.assertEqual(f.text(symbol("s_session_token")), b"0" * 32)

    def test_first_session_requires_confirmed_raw_status_before_led_flash(self):
        f = self.f
        f.ready()
        f.call("clear_session")
        f.auto_esp = False
        f.mqtt(b"SESSION:" + NONCE)
        self.assertEqual(f.word("g_wifi_test_status", 24), 1)
        self.assertEqual(f.text(symbol("s_session_token")), NONCE)
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        f.call("send_echo")
        self.assertEqual(f.tx, [])

        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n> ")
        f.call("service")
        raw = [part for part in f.tx if part.startswith(b"fw=")]
        self.assertEqual(len(raw), 1)
        self.assertIn(b";token=" + NONCE + b";", raw[0])
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)

        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        f.call("service")
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)
        for expected in (0, 1, 0):
            f.ticks += 100
            f.call("service")
            self.assertEqual(f.leds[-1][2], expected)
        self.assertEqual(f.word("g_wifi_test_status", 64), 1)
        f.auto_esp = True
        f.call("send_echo")
        f.call("service")
        f.call("service")
        self.assertEqual([body for unused, body, tick in f.published],
                         [b"LEDACK:" + NONCE + b":OK"])

    def test_early_led_during_matching_status_waits_then_flashes_without_retry(self):
        f = self.f
        f.ready()
        self.assertEqual(f.call("setup_mqtt"), 1)
        f.mqtt(b"SESSION:" + NONCE)
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n> ")
        f.call("service")
        raw = [part for part in f.tx if part.startswith(b"fw=")]
        self.assertEqual(len(raw), 1)
        self.assertIn(b";token=" + NONCE + b";", raw[0])

        request = b"11112222333344445555666677778888"
        received = f.ticks
        f.mqtt(b"LEDTEST:" + request)
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        self.assertFalse(any(part.startswith(b"LEDACK:") for part in f.tx))
        f.mqtt(b"LEDTEST:" + b"fedcba9876543210fedcba9876543210")
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.ticks += 2000
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)

        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)
        for expected in (0, 1, 0):
            f.ticks += 100
            f.call("service")
            self.assertEqual(f.leds[-1][2], expected)
        f.auto_esp = True
        f.call("send_echo")
        f.call("service")
        f.call("service")
        self.assertEqual([body for unused, body, tick in f.published],
                         [b"LEDACK:" + request + b":OK"])
        self.assertLess(f.published[-1][2] - received, 5000)
        self.assertEqual(f.word("g_wifi_test_status", 64), 1)
        self.assertEqual(f.word("g_wifi_test_status", 36), 1)

    def test_status_started_before_fresh_session_cannot_enable_led_commands(self):
        f = self.f
        f.ready()
        f.call("clear_session")
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n> ")
        f.call("service")
        raw = [part for part in f.tx if part.startswith(b"fw=")]
        self.assertEqual(len(raw), 1)
        self.assertIn(b";token=" + b"0" * 32 + b";", raw[0])

        f.mqtt(b"SESSION:" + NONCE)
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        f.call("service")
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)

        f.auto_esp = True
        f.publish_status()
        self.assertIn(b";token=" + NONCE + b";", f.published[-1][1])
        self.assertEqual(f.leds[-1][2], 0)
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)

    def test_old_led_ack_cannot_confirm_a_fresh_session(self):
        f = self.f
        f.ready()
        f.publish_status()
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        for unused in range(3):
            f.ticks += 100
            f.call("service")
        self.assertEqual(f.word("g_wifi_test_status", 64), 1)
        f.auto_esp = False
        f.call("send_echo")
        f.rx.extend(b"\r\nOK\r\n> ")
        f.call("service")
        self.assertIn(b"LEDACK:" + NONCE + b":OK", f.tx)

        fresh = b"fedcba9876543210fedcba9876543210"
        f.mqtt(b"SESSION:" + fresh)
        f.mqtt(b"LEDTEST:" + fresh)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        f.call("service")
        f.mqtt(b"LEDTEST:" + fresh)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 36), 0)
        f.auto_esp = True
        f.publish_status()
        self.assertIn(b";token=" + fresh + b";", f.published[-1][1])
        self.assertEqual(f.leds[-1][2], 0)
        f.mqtt(b"LEDTEST:" + fresh)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 1)

    def test_pending_led_request_expires_without_late_ack(self):
        f = self.f
        f.ready()
        f.publish_status()
        f.published.clear()
        f.mqtt(b"LEDTEST:" + NONCE)
        f.ticks += 5100
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.published, [])
        self.assertEqual(f.leds[-1][2], 0)

    def test_led_needs_physical_phase_progress_before_ack(self):
        f = self.f
        f.ready()
        f.publish_status()
        f.published.clear()
        f.mqtt(b"LEDTEST:" + NONCE)
        # No service has applied PF10 while time advances: not a completed flash.
        f.ticks += 300
        f.call("service")
        f.call("send_echo")
        self.assertEqual(f.published, [])
        self.assertEqual(f.leds[-1][2], 1)

    def test_setup_and_status_never_open_http_socket(self):
        f = self.f
        f.ready()
        self.assertEqual(f.call("setup_mqtt"), 1)
        f.publish_status()
        self.assertFalse(any(p.startswith(b"AT+CIP") or b"GET /" in p for p in f.tx))
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)

    def test_setup_accepts_connected_notification_in_same_rx_batch_as_ok(self):
        f = self.f
        f.ready()
        f.word("g_wifi_test_status", 28, 0)
        self.assertEqual(f.call("setup_mqtt"), 1)
        self.assertIn(b'AT+MQTTSUB=0,"' + TOPIC + b'",0', f.tx)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)
        self.assertEqual(f.word("g_wifi_test_status", 24), 0)

    def test_setup_waits_for_connected_notification_after_ok(self):
        f = self.f
        f.ready()
        f.word("g_wifi_test_status", 28, 0)
        f.mqtt_connected_delay = 350
        start = f.ticks
        self.assertEqual(f.call("setup_mqtt"), 1)
        self.assertGreaterEqual(f.ticks - start, 350)
        self.assertIn(b'AT+MQTTSUB=0,"' + TOPIC + b'",0', f.tx)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)

    def test_setup_cannot_use_bare_ok_or_a_stale_connection_event(self):
        f = self.f
        f.ready()
        f.line(CONNECTED)
        f.mqtt_connected_reply = b""
        self.assertEqual(f.call("setup_mqtt"), 0)
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)
        self.assertFalse(any(part.startswith(b"AT+MQTTSUB=") for part in f.tx))

    def test_mqtt_disconnect_does_not_claim_wifi_disconnect(self):
        f = self.f
        f.ready()
        f.line(b"+MQTTDISCONNECTED:0")
        self.assertEqual(f.word("g_wifi_test_status", 20), 1)
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)
        self.assertEqual(f.word("g_wifi_test_status", 24), 0)
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("service")
        self.assertEqual(f.leds[-1][2], 0)

    def test_safe_motor_outputs_are_low_and_do_not_conflict_lcd(self):
        f = self.f
        f.call("Board_Init")
        for gpio, pins in [
            (0x40020400, (6, 7)), (0x40020C00, (6, 7)),
            (0x40021000, (5, 6)),
        ]:
            mode = struct.unpack("<I", f.uc.mem_read(gpio, 4))[0]
            bsrr = struct.unpack("<I", f.uc.mem_read(gpio + 0x18, 4))[0]
            for pin in pins:
                self.assertEqual((mode >> (2 * pin)) & 3, 1)
                self.assertTrue(bsrr & (1 << (pin + 16)))


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(ProtocolTests)
    )
    report = {
        "state": "COMPILED_PROTOCOL_TEST_PASS" if result.wasSuccessful() else "FAIL",
        "tests_run": result.testsRun,
        "failures": len(result.failures),
        "errors": len(result.errors),
        "physical_board_verified": False,
    }
    variant = {"board_chassis_web_f407_30pct": "_30pct",
               "board_chassis_web_f407_60pct": "_60pct"}.get(TARGET, "")
    (ROOT / (f"Tools/motor_web{variant}_protocol_test.json" if TARGET in MOTOR_VERSIONS else
             "Tools/rc_web_protocol_test.json" if SUFFIX else
             "Tools/protocol_test.json")).write_text(
        json.dumps(report, indent=2), encoding="utf-8"
    )
    print(json.dumps(report, indent=2))
    raise SystemExit(not result.wasSuccessful())
