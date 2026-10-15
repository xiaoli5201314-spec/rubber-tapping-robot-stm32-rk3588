"""Real isolated Mosquitto and dashboard/API tests with a simulated board."""
import json
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import server

BROKER = Path(shutil.which("mosquitto") or "mosquitto")
DEVICE = "ABCDEF0123456789ABCDEF01"
OTHER = "A1B2C3D4E5F60718293A4B5C"
TOPIC = "f407/web_test/" + DEVICE + "/"
ZERO_TOKEN = "0" * 32
ROOT = Path(__file__).resolve().parents[1]


def unused_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def wait_until(condition, message, seconds=4):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.02)
    raise AssertionError(message)


def heartbeat(seq=1, uptime=10, token=ZERO_TOKEN, **changes):
    fields = {
        "fw": "mqttweb1", "mqtt": "1", "token": token,
        "ip": "127.0.0.1", "server": "127.0.0.1",
        "uptime": str(uptime), "seq": str(seq), "runled": "0",
        "cmdcount": "0", "rxerr": "0", "parseerr": "0",
    }
    fields.update(changes)
    return ";".join(f"{key}={value}" for key, value in fields.items()).encode("ascii")


def rc_heartbeat(seq, token, **changes):
    fields = {
        "fw": "mqttrc1", "ch1": "2000", "ch2": "1500", "ch5": "2000",
        "p1": "20000", "p2": "20000", "p5": "20000",
        "v1": "1", "v2": "1", "v5": "1", "state": "ARMED",
        "left": "300", "right": "-300", "motor": "0",
    }
    fields.update(changes)
    return heartbeat(seq, seq + 10, token, **fields)


def stop_mqtt(client):
    client.disconnect()
    client.loop_stop()


@unittest.skipUnless(BROKER.is_file(), "Mosquitto executable is unavailable")
class RealBrokerTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory(prefix="f407-mqtt-simulated-")
        self.addCleanup(folder.cleanup)
        mqtt_port = unused_port()
        config = Path(folder.name) / "mosquitto.conf"
        config.write_text(
            f"listener {mqtt_port} 127.0.0.1\nallow_anonymous true\npersistence false\n",
            encoding="ascii",
        )
        log = (Path(folder.name) / "broker.log").open("wb")
        self.addCleanup(log.close)
        self.broker = subprocess.Popen(
            [str(BROKER), "-c", str(config)], stdout=log, stderr=log,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.addCleanup(self.stop_broker)
        wait_until(
            lambda: server.check_broker("127.0.0.1", mqtt_port),
            "Isolated broker not ready",
        )

        self.commands = queue.Queue()
        command_ready = threading.Event()
        self.simulated = server.mqtt_client("f407_simulated_board_" + str(mqtt_port))
        self.addCleanup(stop_mqtt, self.simulated)

        def on_connect(client, userdata, flags, reason, properties):
            client.subscribe(TOPIC + "cmd", 1)

        self.simulated.on_connect = on_connect
        self.simulated.on_message = lambda client, userdata, message: self.commands.put(message)
        self.simulated.on_subscribe = lambda *_: command_ready.set()
        self.simulated.connect("127.0.0.1", mqtt_port, 20)
        self.simulated.loop_start()
        self.assertTrue(command_ready.wait(3), "Simulated command subscriber not ready")

        # Seed before subscribing so Mosquitto actually delivers a retained packet.
        self.publish(DEVICE, "status", heartbeat(), retain=True)
        self.state = server.LinkState()
        self.received = queue.Queue()
        self.link = server.MqttLink(self.state, "127.0.0.1", mqtt_port)
        self.addCleanup(stop_mqtt, self.link.client)

        def observe_message(client, userdata, message):
            self.link.on_message(client, userdata, message)
            self.received.put((message.topic, message.payload, message.retain))

        self.link.client.on_message = observe_message
        self.link.client.loop_start()
        wait_until(
            lambda: self.state.snapshot()["brokerConnected"],
            "Host MQTT subscription not ready",
        )
        self.retained = self.received.get(timeout=3)

        self.http = ThreadingHTTPServer(
            ("127.0.0.1", 0), server.make_handler(self.state, self.link.publish),
        )
        self.http.daemon_threads = True
        self.thread = threading.Thread(target=self.http.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.stop_http)
        self.base = f"http://127.0.0.1:{self.http.server_port}"

    def stop_broker(self):
        if self.broker.poll() is None:
            self.broker.terminate()
            self.broker.wait(5)

    def stop_http(self):
        self.http.shutdown()
        self.http.server_close()
        self.thread.join(2)

    def publish(self, device, kind, payload, retain=False):
        publication = self.simulated.publish(
            "f407/web_test/" + device + "/" + kind,
            payload, qos=1, retain=retain,
        )
        publication.wait_for_publish(3)
        self.assertTrue(publication.is_published(), "Simulated publication not acknowledged")

    def deliver(self, payload, kind="status", device=DEVICE):
        self.publish(device, kind, payload)
        # The observation runs after the real handler, so rejected packets need
        # no timing sleep before their unchanged API state can be checked.
        self.assertEqual(
            self.received.get(timeout=3),
            ("f407/web_test/" + device + "/" + kind, payload, False),
        )

    def api(self, path="/api/status", data=None):
        request = urllib.request.Request(
            self.base + path, data=data,
            headers={"Content-Type": "application/json"},
        )
        try:
            response = urllib.request.urlopen(request, timeout=3)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, json.loads(response.read())

    def board(self):
        code, response = self.api()
        self.assertEqual(code, 200)
        return response["devices"]["stm32"]

    def command(self):
        message = self.commands.get(timeout=3)
        self.assertEqual(message.topic, TOPIC + "cmd")
        self.assertEqual(message.qos, 0)
        self.assertFalse(message.retain)
        return message.payload.decode("ascii")

    def connect_board(self):
        self.deliver(heartbeat())
        self.assertFalse(self.board()["link"])
        challenge = self.command()
        self.assertRegex(challenge, r"^SESSION:[0-9a-f]{32}$")
        token = challenge[8:]
        self.assertNotEqual(token, ZERO_TOKEN)
        self.deliver(heartbeat(seq=2, uptime=11, token=token))
        self.assertTrue(self.board()["link"])
        self.assertTrue(self.board()["telemetry"]["mqttVerified"])
        return token

    def test_mqtt_session_requires_fresh_round_trip_and_rejects_replays(self):
        self.assertEqual(self.retained, (TOPIC + "status", heartbeat(), True))
        self.assertFalse(self.board()["link"])
        self.assertIsNone(self.board()["lastSeen"])
        self.assertEqual(self.board()["telemetry"], {})

        self.deliver(heartbeat(seq=2, uptime=11))
        self.assertFalse(self.board()["link"])
        self.assertIsNone(self.board()["lastSeen"])
        challenge = self.command()
        self.assertRegex(challenge, r"^SESSION:[0-9a-f]{32}$")
        token = challenge[8:]
        self.assertNotEqual(token, ZERO_TOKEN)

        self.deliver(heartbeat(seq=3, uptime=12))
        self.assertFalse(self.board()["link"])
        self.deliver(heartbeat(seq=4, uptime=13, token=token))
        before = self.board()
        self.assertTrue(before["link"])
        self.assertTrue(before["telemetry"]["mqttVerified"])
        self.assertEqual(before["telemetry"]["seq"], 4)

        self.deliver(heartbeat(seq=4, uptime=13, token=token))
        self.assertEqual(self.board(), before)
        self.deliver(heartbeat(seq=3, uptime=14, token=token))
        self.assertEqual(self.board(), before)

    def test_only_matching_current_led_ack_completes_api_self_test(self):
        self.connect_board()
        code, request = self.api("/api/self-test", b"{}")
        self.assertEqual(code, 202)
        self.assertRegex(request["requestId"], r"^[0-9a-f]{32}$")
        self.assertEqual(self.command(), "LEDTEST:" + request["requestId"])
        reply = f"LEDACK:{request['requestId']}:OK".encode("ascii")

        for payload, device in (
            (f"LEDACK:{ZERO_TOKEN}:OK".encode("ascii"), DEVICE),
            (reply, OTHER),
            (f"LEDACK:{request['requestId']}:ERROR".encode("ascii"), DEVICE),
        ):
            with self.subTest(payload=payload, device=device):
                self.deliver(payload, kind="echo", device=device)
                self.assertEqual(self.api()[1]["selfTest"]["status"], "pending")

        self.deliver(reply, kind="echo")
        self.assertEqual(self.api()[1]["selfTest"]["status"], "success")
        code, next_request = self.api("/api/self-test", b"{}")
        self.assertEqual(code, 202)
        self.assertNotEqual(next_request["requestId"], request["requestId"])
        self.assertEqual(self.command(), "LEDTEST:" + next_request["requestId"])
        self.deliver(reply, kind="echo")
        self.assertEqual(self.api()[1]["selfTest"]["status"], "pending")
        self.deliver(
            f"LEDACK:{next_request['requestId']}:OK".encode("ascii"),
            kind="echo",
        )
        self.assertEqual(self.api()[1]["selfTest"]["status"], "success")

    def test_rc_and_motor_limits_reach_api_and_invalid_outputs_are_rejected(self):
        token = self.connect_board()
        self.deliver(rc_heartbeat(3, token))
        rc = self.board()["telemetry"]
        self.assertEqual(rc["firmware"], "mqttrc1")
        self.assertEqual((rc["ch1"], rc["ch2"], rc["ch5"]), (2000, 1500, 2000))
        self.assertEqual((rc["ch1Period"], rc["ch2Period"], rc["ch5Period"]), (20, 20, 20))
        self.assertEqual((rc["left"], rc["right"]), (30, -30))
        self.assertEqual(rc["direction"], "右转")
        self.assertTrue(rc["simulation"])
        self.assertFalse(rc["motorEnabled"])

        before = self.board()
        self.deliver(rc_heartbeat(4, token, motor="1"))
        self.assertEqual(self.board(), before)
        self.deliver(rc_heartbeat(
            4, token, ch1="0", p1="0", v1="0",
            state="FAILSAFE", left="0", right="0",
        ))
        missing = self.board()
        self.assertTrue(missing["link"])
        self.assertIsNone(missing["telemetry"]["ch1"])
        self.assertIsNone(missing["telemetry"]["ch1Period"])
        self.assertFalse(missing["telemetry"]["rcValid"])
        self.assertEqual(missing["telemetry"]["safetyState"], "FAILSAFE")

        for seq, (firmware, output, percent, excessive) in enumerate((
            ("mqttmotor30", "300", 30, "301"),
            ("mqttmotor60", "600", 60, "601"),
            ("mqttmotor100", "1000", 100, "1001"),
        ), 5):
            with self.subTest(firmware=firmware):
                self.deliver(rc_heartbeat(
                    seq, token, fw=firmware, motor="1",
                    left=output, right="-" + output,
                ))
                motor = self.board()["telemetry"]
                self.assertEqual(motor["firmware"], firmware)
                self.assertEqual(motor["outputLimit"], percent)
                self.assertEqual((motor["left"], motor["right"]), (percent, -percent))
                self.assertEqual(motor["direction"], "右转")
                self.assertTrue(motor["motorEnabled"])
                self.assertFalse(motor["simulation"])
                self.assertTrue(motor["mqttVerified"])
                before = self.board()
                for invalid in (
                    dict(left=excessive, right="-" + output),
                    dict(left=output, right="-" + excessive),
                    dict(motor="0", left=output, right="-" + output),
                    dict(state="LOCKED", left=output, right="-" + output),
                ):
                    fields = dict(fw=firmware, motor="1")
                    fields.update(invalid)
                    self.deliver(rc_heartbeat(seq + 1, token, **fields))
                    self.assertEqual(self.board(), before)

    def test_broker_disconnect_clears_online_and_aborts_pending_self_test(self):
        self.connect_board()
        self.assertEqual(self.api("/api/self-test", b"{}")[0], 202)
        self.assertTrue(self.command().startswith("LEDTEST:"))
        self.stop_broker()
        wait_until(
            lambda: not self.state.snapshot()["brokerConnected"],
            "Broker disconnect not observed",
        )
        code, status = self.api()
        self.assertEqual(code, 200)
        self.assertFalse(status["brokerConnected"])
        self.assertFalse(status["devices"]["stm32"]["link"])
        self.assertEqual(status["selfTest"]["status"], "error")


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(RealBrokerTests),
    )
    report = {
        "scope": "SIMULATED_CLIENT_REAL_MOSQUITTO_DASHBOARD_API",
        "physicalBoardVerified": False, "tests": result.testsRun,
        "passed": result.wasSuccessful(), "failures": len(result.failures),
        "errors": len(result.errors),
    }
    (ROOT / "simulated_self_test.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    raise SystemExit(0 if result.wasSuccessful() else 1)
