"""Local F407 web link host. Current messages are evidence, never cached online state."""
from __future__ import annotations

import argparse
import copy
import ipaddress
import json
import mimetypes
import os
import re
import socket
import threading
import time
import urllib.parse
import uuid
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEB_ROOT = ROOT.parent / "web_prototype"
PREFIX = "f407/web_test/"
UID = re.compile(r"[0-9A-F]{24}")
HEX32 = re.compile(r"[0-9a-fA-F]{32}")
UINT32 = re.compile(r"(0|[1-9][0-9]{0,9})")
FIELDS = {"fw", "mqtt", "token", "ip", "server", "uptime", "seq", "runled", "cmdcount", "rxerr", "parseerr"}
RC_FIELDS = {"ch1", "ch2", "ch5", "p1", "p2", "p5", "v1", "v2", "v5", "state", "left", "right", "motor"}
SIGNED_OUTPUT = re.compile(r"-?(0|[1-9][0-9]{0,3})")
MQTT_VERSIONS = {"mqttweb1", "mqttrc1", "mqttmotor30", "mqttmotor60", "mqttmotor100"}
RC_OUTPUT_LIMITS = {"mqttrc1": 300, "mqttmotor30": 300, "mqttmotor60": 600, "mqttmotor100": 1000}
RC_STATES = {"LOCKED": "锁定", "WAIT": "等待解锁", "ARMED": "就绪", "FAILSAFE": "失效"}
STATIC_FILES = {"index.html", "app.js", "status.js", "styles.css"}


def parse_heartbeat(payload):
    if len(payload) > 512:
        return None
    try:
        parts = payload.decode("ascii").split(";")
        fields = {}
        for part in parts:
            key, value = part.split("=", 1)
            if key in fields:
                return None
            fields[key] = value
        version = fields.get("fw")
        required = FIELDS if version == "mqttweb1" else FIELDS | RC_FIELDS
        if version not in MQTT_VERSIONS or set(fields) != required:
            return None
        if fields["mqtt"] not in ("0", "1") or fields["runled"] not in ("0", "1"):
            return None
        if not HEX32.fullmatch(fields["token"]):
            return None
        for key in ("ip", "server"):
            ipaddress.IPv4Address(fields[key])
        for key in ("uptime", "seq", "cmdcount", "rxerr", "parseerr"):
            if not UINT32.fullmatch(fields[key]) or int(fields[key]) > 0xFFFFFFFF:
                return None
            fields[key] = int(fields[key])
        fields["runled"] = fields["runled"] == "1"
        if version in RC_OUTPUT_LIMITS:
            expected_motor = "0" if version == "mqttrc1" else "1"
            if fields["motor"] != expected_motor or fields["state"] not in RC_STATES:
                return None
            for channel in ("1", "2", "5"):
                if fields["v" + channel] not in ("0", "1"):
                    return None
                for key in ("ch" + channel, "p" + channel):
                    if not UINT32.fullmatch(fields[key]):
                        return None
                    fields[key] = int(fields[key])
                pulse, period = fields["ch" + channel], fields["p" + channel]
                if fields["v" + channel] == "1":
                    if not 800 <= pulse <= 2200 or not 15000 <= period <= 25000:
                        return None
                elif pulse != 0 or period != 0:
                    return None
            for key in ("left", "right"):
                if not SIGNED_OUTPUT.fullmatch(fields[key]):
                    return None
                fields[key] = int(fields[key])
                limit = RC_OUTPUT_LIMITS[version]
                if not -limit <= fields[key] <= limit:
                    return None
            if fields["state"] == "ARMED":
                if any(fields["v" + c] != "1" for c in ("1", "2", "5")) or fields["ch5"] < 1700:
                    return None
            elif fields["left"] != 0 or fields["right"] != 0:
                return None
        return fields
    except (UnicodeDecodeError, ValueError):
        return None


def rc_telemetry(fields):
    """Expose input captures and PWM commands with their firmware's limit."""
    motor_enabled = fields["fw"] != "mqttrc1"
    data = {"simulation": not motor_enabled, "motorEnabled": motor_enabled,
            "outputLimit": RC_OUTPUT_LIMITS[fields["fw"]] / 10,
            "safetyState": fields["state"], "locked": fields["state"] != "ARMED",
            "left": fields["left"] / 10, "right": fields["right"] / 10}
    missing = []
    for channel in ("1", "2", "5"):
        key = "ch" + channel
        valid = fields["v" + channel] == "1"
        data[key + "Valid"] = valid
        data[key] = fields[key] if valid else None
        data[key + "Period"] = fields["p" + channel] / 1000 if valid else None
        if not valid:
            missing.append("CH" + channel)
    data["rcValid"] = not missing
    data["fault"] = (" / ".join(missing) + " 无有效 PWM") if missing else "无故障"
    left, right = fields["left"], fields["right"]
    # Opposite wheel directions indicate turning even when their magnitudes differ.
    data["direction"] = ("右转" if left > 0 > right else "左转" if left < 0 < right
                         else "前进" if left + right > 0 else "后退" if left + right < 0
                         else "右转" if left > right else "左转" if left < right else "静止")
    return data


class LinkState:
    def __init__(self, clock=time.monotonic, now=time.time):
        self.clock, self.now = clock, now
        self.lock = threading.RLock()
        self.instance_id = uuid.uuid4().hex
        self.broker_connected = False
        self.boards = {}
        self.tokens = {}
        self.sessions = {}
        self.retired_tokens = {}
        self.selected = None
        self.events = deque(maxlen=100)
        self.event_id = 0
        self.self_test = {"status": "idle", "requestId": "", "message": "", "sentAt": None, "completedAt": None}
        self.pending_device = None
        self.pending_clock = None
        self._event("system", "电脑通信服务已启动，等待真实 STM32 心跳")

    def _ms(self):
        return int(self.now() * 1000)

    def _event(self, kind, message):
        self.event_id += 1
        self.events.append({"id": self.event_id, "time": self._ms(), "type": kind, "message": message})

    def _finish_test(self, status, message):
        self.self_test.update(status=status, message=message, completedAt=self._ms())
        self.pending_clock = None
        self._event("status", message)

    def _expire(self):
        now = self.clock()
        if self.self_test["status"] == "pending" and now - self.pending_clock > 5:
            self._finish_test("timeout", "LED 自检超时：5 秒内未收到本次匹配应答")
        for device, board in self.boards.items():
            if board["active"] and now - board["receivedClock"] > 5:
                board["active"] = False
                self._event("status", f"STM32 {device} 通信超时：超过 5 秒无新心跳")

    def set_broker(self, connected):
        with self.lock:
            connected = bool(connected)
            if connected == self.broker_connected:
                return
            self.broker_connected = connected
            self._event("system", "MQTT broker 已连接，等待新心跳" if connected else "MQTT broker 断开，清除当前在线证据")
            if not connected:
                for board in self.boards.values():
                    board["active"] = False
                if self.self_test["status"] == "pending":
                    self._finish_test("error", "LED 自检中止：MQTT broker 已断开")

    def _mqtt_session(self, device, fields, publish):
        """A stable challenge travels through MQTT, including after either end restarts."""
        token = fields["token"]
        pending = self.sessions.get(device)
        if fields["mqtt"] != "1":
            return False
        if token in self.retired_tokens.get(device, set()):
            return False
        if pending and token == pending["token"]:
            old = self.tokens.get(device)
            if old and old != token:
                self.retired_tokens.setdefault(device, set()).add(old)
            self.tokens[device] = token
            del self.sessions[device]
            return True
        if not pending and token != "0" * 32 and token == self.tokens.get(device):
            return True
        if pending is None:
            pending = {"token": uuid.uuid4().hex, "sentClock": None}
            self.sessions[device] = pending
        if publish and (pending["sentClock"] is None or self.clock() - pending["sentClock"] >= 1):
            try:
                sent = publish(PREFIX + device + "/cmd", "SESSION:" + pending["token"])
            except Exception:
                sent = False
            if sent:
                pending["sentClock"] = self.clock()
        return False

    def receive(self, topic, payload, retained=False, publish=None):
        match = re.fullmatch(re.escape(PREFIX) + r"([0-9A-F]{24})/(status|echo)", topic)
        if retained or not match:
            return False
        device, kind = match.groups()
        with self.lock:
            self._expire()
            if not self.broker_connected:
                return False
            if kind == "echo":
                if (self.self_test["status"] != "pending" or device != self.pending_device
                        or payload != f"LEDACK:{self.self_test['requestId']}:OK".encode("ascii")):
                    return False
                self._finish_test("success", "收到本次 LED 自检匹配应答：板子报告闪灯完成")
                return True
            fields = parse_heartbeat(payload)
            if fields is None:
                return False
            if not self._mqtt_session(device, fields, publish):
                return False
            previous = self.boards.get(device)
            reboot = False
            if previous is not None:
                old = previous["telemetry"]
                # A slow first connection can produce a high uptime with only
                # one or two heartbeats. After reset the new sequence may be
                # equal or higher; a fresh MQTT nonce establishes its epoch.
                reboot = fields["uptime"] < old["uptime"]
                current_token = self.tokens.get(device)
                if reboot and not (fields["token"] == current_token and fields["token"] != previous["token"]):
                    return False
                if not reboot and fields["token"] not in (previous["token"], current_token):
                    return False
                if not reboot:
                    delta = (fields["seq"] - old["seq"]) & 0xFFFFFFFF
                    if fields["uptime"] < old["uptime"] or delta == 0 or delta >= 0x80000000:
                        return False
                if reboot:
                    self._event("status", f"STM32 {device} 重启：运行时间和心跳序号已重置")
                    if self.self_test["status"] == "pending" and self.pending_device == device:
                        self._finish_test("error", "LED 自检中止：STM32 已重启")
            verified = fields["mqtt"] == "1" and fields["token"] == self.tokens.get(device)
            telemetry = {key: fields[key] for key in ("uptime", "seq", "runled", "cmdcount", "rxerr", "parseerr", "server")}
            telemetry.update(deviceId=device, firmware=fields["fw"])
            telemetry["mqttVerified"] = verified
            if fields["fw"] in RC_OUTPUT_LIMITS:
                telemetry.update(rc_telemetry(fields))
                old = previous["telemetry"] if previous else {}
                if old.get("rcValid") != telemetry["rcValid"]:
                    self._event("status", "遥控 PWM 三路有效" if telemetry["rcValid"] else telemetry["fault"])
                if old.get("safetyState") != telemetry["safetyState"]:
                    mode_note = "电机输出已启用" if telemetry["motorEnabled"] else "电机禁用"
                    self._event("status", "遥控状态：" + RC_STATES[telemetry["safetyState"]] + f"（{mode_note}）")
            self.boards[device] = {
                "ip": fields["ip"], "lastSeen": self._ms(), "receivedClock": self.clock(),
                "active": True, "telemetry": telemetry,
                "token": fields["token"],
            }
            self.selected = device
            if previous is None:
                self._event("status", f"STM32 {device} 已收到首次完整心跳")
            elif not previous["active"]:
                self._event("status", f"STM32 {device} 新心跳到达，恢复在线")
            if verified and (previous is None or not previous["telemetry"].get("mqttVerified")):
                self._event("status", f"STM32 {device} 已完成 MQTT 双向握手")
            return True

    def request_self_test(self, publish):
        with self.lock:
            self._expire()
            board = self.boards.get(self.selected)
            if not self.broker_connected or board is None or not board["active"]:
                return 409, {"status": "error", "message": "STM32 当前离线，不能发送 LED 自检"}
            if self.self_test["status"] == "pending":
                return 409, {"status": "pending", "message": "已有 LED 自检正在等待应答"}
            nonce = uuid.uuid4().hex
            self.self_test = {"status": "pending", "requestId": nonce, "message": "已发送 LED 自检，等待本次应答",
                              "sentAt": self._ms(), "completedAt": None}
            self.pending_device, self.pending_clock = self.selected, self.clock()
            self._event("status", f"已向 STM32 {self.selected} 发送 LED 自检")
            try:
                sent = publish(PREFIX + self.selected + "/cmd", "LEDTEST:" + nonce)
            except Exception:
                sent = False
            if not sent:
                self._finish_test("error", "LED 自检发送失败，未自动重发")
                return 503, copy.deepcopy(self.self_test)
            return 202, copy.deepcopy(self.self_test)

    def snapshot(self, service_ip=""):
        with self.lock:
            self._expire()
            now = self._ms()
            stm32 = {"link": False, "ip": "", "lastSeen": None, "telemetry": {}}
            if self.selected in self.boards:
                board = self.boards[self.selected]
                stm32 = {"link": self.broker_connected and board["active"], "ip": board["ip"],
                         "lastSeen": board["lastSeen"], "telemetry": copy.deepcopy(board["telemetry"])}
            return {
                "mode": "live", "instanceId": self.instance_id, "nowMs": now, "brokerConnected": self.broker_connected,
                "devices": {
                    "service": {"link": True, "ip": service_ip, "lastSeen": now, "telemetry": {"brokerConnected": self.broker_connected}},
                    "stm32": stm32,
                    "rk3588": {"link": False, "ip": "", "lastSeen": None, "telemetry": {}},
                },
                "selfTest": copy.deepcopy(self.self_test), "events": copy.deepcopy(list(self.events)),
            }


def make_handler(state, publish):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def respond(self, code, body, content_type="application/json; charset=utf-8"):
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store, max-age=0")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass
            self.close_connection = True

        def json_response(self, code, content):
            self.respond(code, json.dumps(content, ensure_ascii=False).encode("utf-8"))

        def do_GET(self):
            query = urllib.parse.urlsplit(self.path)
            if query.path == "/api/status":
                self.json_response(200, state.snapshot(self.connection.getsockname()[0]))
                return
            if query.path == "/health":
                self.json_response(200, {"application": "f407-web-link-host", "pid": os.getpid(), "instanceId": state.instance_id})
                return
            name = "index.html" if query.path == "/" else urllib.parse.unquote(query.path).lstrip("/")
            if name not in STATIC_FILES:
                self.json_response(404, {"message": "Unknown endpoint"})
                return
            path = WEB_ROOT / name
            try:
                body = path.read_bytes()
            except OSError:
                self.json_response(404, {"message": "Static file missing"})
                return
            content_type = mimetypes.guess_type(name)[0] or "application/octet-stream"
            self.respond(200, body, content_type + "; charset=utf-8")

        def do_POST(self):
            if self.path != "/api/self-test":
                self.json_response(404, {"message": "Unknown endpoint"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if length < 1 or length > 256 or self.headers.get_content_type() != "application/json":
                    raise ValueError()
                body = json.loads(self.rfile.read(length))
                if not isinstance(body, dict) or body:
                    raise ValueError()
            except (ValueError, UnicodeDecodeError):
                self.json_response(400, {"message": "请求体必须为 application/json 空对象 {}"})
                return
            code, response = state.request_self_test(publish)
            self.json_response(code, response)

        def log_message(self, *_):
            pass

    return Handler


def mqtt_client(name):
    import paho.mqtt.client as mqtt
    return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=name, protocol=mqtt.MQTTv311, clean_session=True)


class MqttLink:
    def __init__(self, state, address, port):
        self.state = state
        self.client = mqtt_client("f407_web_host_" + uuid.uuid4().hex[:12])
        self.client.on_connect = self.on_connect
        self.client.on_subscribe = self.on_subscribe
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message = self.on_message
        self.client.reconnect_delay_set(min_delay=1, max_delay=5)
        self.client.connect_async(address, port, 20)

    def on_connect(self, client, userdata, flags, reason, properties):
        if reason.is_failure:
            self.state.set_broker(False)
            return
        client.subscribe([(PREFIX + "+/status", 1), (PREFIX + "+/echo", 1)])

    def on_subscribe(self, client, userdata, mid, reasons, properties):
        self.state.set_broker(bool(reasons) and all(not code.is_failure for code in reasons))

    def on_disconnect(self, client, userdata, flags, reason, properties):
        self.state.set_broker(False)

    def on_message(self, client, userdata, message):
        self.state.receive(message.topic, message.payload, message.retain, self.publish)

    def publish(self, topic, payload):
        # QoS 0 and clean sessions avoid queued LED commands after reconnection.
        return self.client.publish(topic, payload, qos=0, retain=False).rc == 0


def check_broker(address, port):
    ready = threading.Event()
    accepted = []
    client = mqtt_client("f407_web_probe_" + uuid.uuid4().hex[:12])

    def connected(client, userdata, flags, reason, properties):
        if reason.is_failure:
            ready.set()
        else:
            client.subscribe(PREFIX + "+/status", 1)

    def subscribed(client, userdata, mid, reasons, properties):
        accepted.append(bool(reasons) and all(not code.is_failure for code in reasons))
        ready.set()

    client.on_connect, client.on_subscribe = connected, subscribed
    try:
        client.connect(address, port, 10)
        client.loop_start()
        return ready.wait(4) and accepted == [True]
    except OSError:
        return False
    finally:
        client.disconnect()
        client.loop_stop()


def ipv4_addresses():
    addresses = {"127.0.0.1"}
    try:
        addresses.update(item[4][0] for item in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET))
    except OSError:
        pass
    return sorted(addresses)


def main():
    parser = argparse.ArgumentParser(description="F407 web link host; waits for fresh board heartbeats")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=18080)
    parser.add_argument("--mqtt-host", default="127.0.0.1")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--check-broker", action="store_true")
    args = parser.parse_args()
    if args.check_broker:
        compatible = check_broker(args.mqtt_host, args.mqtt_port)
        print("BROKER_COMPATIBLE" if compatible else "BROKER_INCOMPATIBLE", flush=True)
        return 0 if compatible else 1
    state = LinkState()
    link = MqttLink(state, args.mqtt_host, args.mqtt_port)
    server = ThreadingHTTPServer((args.bind, args.http_port), make_handler(state, link.publish))
    server.daemon_threads = True
    link.client.loop_start()
    print(f"HTTP listening on {args.bind}:{server.server_port}; MQTT {args.mqtt_host}:{args.mqtt_port}", flush=True)
    for address in ipv4_addresses():
        print(f"Web: http://{address}:{server.server_port}/", flush=True)
    print("Waiting for physical board; software simulation does not verify physical hardware.", flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        link.client.disconnect()
        link.client.loop_stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
