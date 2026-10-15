"""Real HTTP/Mosquitto verifier for the F407 board, with a separate host self-test.

Normal mode waits for physical board traffic. --self-test uses a simulated client
on loopback and writes self_test.json, never a physical-board PASS.
"""
from __future__ import annotations

import argparse
import json
import re
import socket
import subprocess
import threading
import time
import urllib.parse
import urllib.request
import uuid
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import paho.mqtt.client as mqtt

ROOT = Path(__file__).resolve().parent
PREFIX = "f407/wifi_test/"
DEVICE = re.compile(r"[0-9A-F]{24}")


def timestamp():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def mqtt_client(name):
    return mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=name,
        protocol=mqtt.MQTTv311,
    )


class Verification:
    def __init__(self, args):
        self.args = args
        self.lock = threading.RLock()
        self.http_tokens = {}
        self.boards = {}
        self.connected = threading.Event()
        self.subscribed = threading.Event()
        self.client = mqtt_client("f407_verifier_" + uuid.uuid4().hex[:12])
        self.client.on_connect = self.on_connect
        self.client.on_subscribe = self.on_subscribe
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect
        self.started = timestamp()
        self.report_path = ROOT / (
            "self_test.json" if args.self_test else "result.json"
        )
        self.save()

    def valid_device(self, device):
        return DEVICE.fullmatch(device) is not None or (
            self.args.self_test and device == "SIMULATED_HOST_TEST"
        )

    def board(self, device):
        return self.boards.setdefault(device, {
            "http_verified": False,
            "mqtt_status_received": False,
            "mqtt_round_trip_verified": False,
            "physical_board_verified": False,
            "nonce": "",
            "last_challenge": 0.0,
            "serial_errors": None,
        })

    def save(self):
        with self.lock:
            passed = any(b["physical_board_verified"] for b in self.boards.values())
            if self.args.self_test:
                passed = any(
                    b["http_verified"] and b["mqtt_round_trip_verified"]
                    and b["serial_errors"] == 0
                    for b in self.boards.values()
                )
                state = "HOST_SELF_TEST_PASS" if passed else "HOST_SELF_TEST_RUNNING"
            else:
                state = "PASS" if passed else (
                    "BOARD_DETECTED_TESTING" if self.boards else "WAITING_FOR_BOARD"
                )
            report = {
                "state": state,
                "physical_board_verified": passed and not self.args.self_test,
                "started": self.started,
                "updated": timestamp(),
                "server_ip": self.args.server_ip,
                "http_port": self.args.http_port,
                "mqtt_port": self.args.mqtt_port,
                "broker_connected": self.connected.is_set(),
                "mqtt_subscription_ready": self.subscribed.is_set(),
                "boards": self.boards,
            }
            temp = self.report_path.with_suffix(".tmp")
            temp.write_text(
                json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
            )
            temp.replace(self.report_path)

    def on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print("MQTT connect failed:", reason_code, flush=True)
            return
        self.connected.set()
        client.subscribe([(PREFIX + "+/status", 1), (PREFIX + "+/echo", 1)])
        self.save()

    def on_subscribe(self, client, userdata, mid, reason_codes, properties):
        if all(not code.is_failure for code in reason_codes):
            self.subscribed.set()
        self.save()

    def on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.connected.clear()
        self.subscribed.clear()
        self.save()

    def http_probe(self, device, peer):
        with self.lock:
            token = uuid.uuid4().hex
            self.http_tokens[device] = token
            board = self.board(device)
            board["http_peer"] = peer
            board["http_request_seen"] = timestamp()
            board["http_token_issued"] = token
            board["http_verified"] = False
            board["physical_board_verified"] = False
            self.save()
            print(f"HTTP request received: {device} from {peer}", flush=True)
            return ("F407_HTTP_OK:" + token + "\n").encode("ascii")

    def finalize(self, device, board):
        passed = (
            board["http_verified"] and board["mqtt_round_trip_verified"]
            and board["serial_errors"] == 0 and bool(board["nonce"])
        )
        previously = board["physical_board_verified"]
        board["physical_board_verified"] = passed and not self.args.self_test
        if passed:
            board["verified_at"] = timestamp()
            self.client.publish(
                PREFIX + device + "/cmd",
                "VERIFIED:" + board["nonce"],
                qos=1,
                retain=False,
            )
            if not previously:
                kind = "HOST SELF-TEST" if self.args.self_test else "PHYSICAL BOARD"
                print(f"{kind} PASS: HTTP token + MQTT bidirectional echo: {device}",
                      flush=True)
        self.save()

    def on_message(self, client, userdata, message):
        try:
            topic = message.topic[len(PREFIX):].split("/")
            if not message.topic.startswith(PREFIX) or len(topic) != 2:
                return
            device, kind = topic
            if not self.valid_device(device) or message.retain:
                return
            payload = message.payload.decode("ascii", errors="strict")
            with self.lock:
                board = self.board(device)
                board["last_mqtt_message"] = timestamp()
                if kind == "status":
                    fields = dict(part.split("=", 1) for part in payload.split(";"))
                    board["mqtt_status_received"] = True
                    board["reported_ip"] = fields.get("ip", "")
                    board["serial_errors"] = int(fields["rxerr"]) + int(fields["parseerr"])
                    board["http_verified"] = (
                        fields.get("http") == "1"
                        and bool(self.http_tokens.get(device))
                        and fields.get("token") == self.http_tokens.get(device)
                    )
                    if not board["physical_board_verified"] and (
                        not board["nonce"] or
                        (not board["mqtt_round_trip_verified"]
                         and time.monotonic() - board["last_challenge"] > 10)
                    ):
                        board["nonce"] = uuid.uuid4().hex
                        board["last_challenge"] = time.monotonic()
                        client.publish(
                            PREFIX + device + "/cmd",
                            "CHALLENGE:" + board["nonce"],
                            qos=1,
                            retain=False,
                        )
                    self.finalize(device, board)
                elif kind == "echo":
                    if board["nonce"] and payload == "ECHO:" + board["nonce"]:
                        board["mqtt_round_trip_verified"] = True
                        self.finalize(device, board)
                    else:
                        board["rejected_echo_count"] = board.get("rejected_echo_count", 0) + 1
                        self.save()
        except (ValueError, UnicodeDecodeError, KeyError) as error:
            print(f"Malformed test message rejected: {error}", flush=True)


def make_handler(verifier):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def do_GET(self):
            query = urllib.parse.urlsplit(self.path)
            if query.path == "/health":
                body = b"F407_TEST_SERVER_READY\n"
                self.respond(200, body)
                return
            device = urllib.parse.parse_qs(query.query).get("device", [""])[0]
            if query.path != "/probe" or not verifier.valid_device(device):
                self.respond(404, b"Unknown test endpoint\n")
                return
            body = verifier.http_probe(device, self.client_address[0])
            self.respond(200, body)

        def respond(self, status, body):
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=us-ascii")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
            self.close_connection = True

        def log_message(self, *args):
            pass

    return Handler


def wait_broker(port, process):
    deadline = time.monotonic() + 6
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("Test broker exited; see broker log.")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.3):
                return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError("Test broker did not open its listener.")


def run_self_test(args, verifier):
    """Exercise the real HTTP server and MQTT broker, plus reject false success."""
    device = "SIMULATED_HOST_TEST"
    base = f"http://127.0.0.1:{args.http_port}"
    with urllib.request.urlopen(base + "/health", timeout=3) as response:
        assert response.read() == b"F407_TEST_SERVER_READY\n"
    with urllib.request.urlopen(base + "/probe?device=" + device, timeout=3) as response:
        body = response.read().decode("ascii").strip()
        assert response.status == 200
        token = body.removeprefix("F407_HTTP_OK:")
        assert re.fullmatch("[0-9a-f]{32}", token)

    simulated = mqtt_client("f407_sim_" + uuid.uuid4().hex[:8])
    challenge = threading.Event()
    acknowledgment = threading.Event()
    simulated_ready = threading.Event()
    received = {}

    def on_connect(client, userdata, flags, code, properties):
        assert not code.is_failure
        client.subscribe(PREFIX + device + "/cmd", qos=1)

    def on_message(client, userdata, message):
        text = message.payload.decode("ascii")
        if text.startswith("CHALLENGE:"):
            received["nonce"] = text[10:]
            challenge.set()
        elif text.startswith("VERIFIED:"):
            acknowledgment.set()

    simulated.on_connect = on_connect
    simulated.on_message = on_message
    simulated.on_subscribe = (
        lambda client, userdata, mid, codes, properties: simulated_ready.set()
    )
    simulated.connect("127.0.0.1", args.mqtt_port, 30)
    simulated.loop_start()
    try:
        assert simulated_ready.wait(3), "Simulated MQTT subscriber not ready"
        status_topic = PREFIX + device + "/status"
        echo_topic = PREFIX + device + "/echo"
        simulated.publish(status_topic,
            "http=1;token=wrong;ip=127.0.0.1;rxerr=0;parseerr=0",
            qos=1).wait_for_publish(3)
        assert challenge.wait(3), "No MQTT challenge received"
        simulated.publish(echo_topic, "ECHO:" + "0" * 32,
                          qos=1).wait_for_publish(3)
        time.sleep(0.2)
        assert not verifier.boards[device]["mqtt_round_trip_verified"]
        simulated.publish(echo_topic, "ECHO:" + received["nonce"],
                          qos=1).wait_for_publish(3)
        time.sleep(0.2)
        assert verifier.boards[device]["mqtt_round_trip_verified"]
        assert not acknowledgment.is_set(), "Bad HTTP token caused a false PASS"
        simulated.publish(status_topic,
            f"http=1;token={token};ip=127.0.0.1;rxerr=1;parseerr=0",
            qos=1).wait_for_publish(3)
        time.sleep(0.2)
        assert not acknowledgment.is_set(), "UART errors caused a false PASS"
        simulated.publish(status_topic,
            f"http=1;token={token};ip=127.0.0.1;rxerr=0;parseerr=0",
            qos=1).wait_for_publish(3)
        assert acknowledgment.wait(3), "No host verification acknowledgment"
        assert not verifier.boards[device]["physical_board_verified"]
        verifier.save()
        print("Host self-test passed; physical board has not been tested.", flush=True)
    finally:
        simulated.disconnect()
        simulated.loop_stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-ip", default="192.0.2.10")
    parser.add_argument("--http-port", type=int, default=18080)
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--broker", default="mosquitto")
    parser.add_argument("--seconds", type=int, default=7200)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--use-existing-broker", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        args.server_ip = "127.0.0.1"
        args.http_port = 18081
        args.mqtt_port = 11883
        config = ROOT / "mosquitto_self_test.conf"
    else:
        config = ROOT / "mosquitto_test.conf"
        if args.mqtt_port != 1883:
            parser.error("Normal broker config uses port 1883.")
    if not args.use_existing_broker:
        # Refuse to take over a port used by another process.
        with socket.socket() as check:
            check.bind(("127.0.0.1", args.mqtt_port))
    verifier = Verification(args)
    http_server = ThreadingHTTPServer(
        ("127.0.0.1" if args.self_test else "0.0.0.0", args.http_port),
        make_handler(verifier),
    )
    log_path = ROOT / ("broker_self_test.log" if args.self_test else "broker.log")
    with log_path.open("w", encoding="utf-8") as broker_log:
        process = None
        if not args.use_existing_broker:
            process = subprocess.Popen(
                [args.broker, "-c", str(config)],
                stdout=broker_log, stderr=broker_log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        http_started = False
        try:
            if process is not None:
                wait_broker(args.mqtt_port, process)
            verifier.client.connect("127.0.0.1", args.mqtt_port, 30)
            verifier.client.loop_start()
            if not verifier.connected.wait(4) or not verifier.subscribed.wait(4):
                raise RuntimeError("MQTT verifier not ready.")
            thread = threading.Thread(target=http_server.serve_forever, daemon=True)
            thread.start()
            http_started = True
            print(f"HTTP :{args.http_port}; MQTT :{args.mqtt_port}; "
                  f"report: {verifier.report_path}", flush=True)
            if args.self_test:
                run_self_test(args, verifier)
            else:
                print("Waiting for physical F407; no hardware success claimed.", flush=True)
                deadline = time.monotonic() + args.seconds
                while time.monotonic() < deadline:
                    if process is not None and process.poll() is not None:
                        raise RuntimeError("Test broker stopped.")
                    time.sleep(1)
        finally:
            if http_started:
                http_server.shutdown()
            http_server.server_close()
            verifier.client.disconnect()
            verifier.client.loop_stop()
            if process is not None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
