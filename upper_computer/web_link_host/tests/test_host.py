"""MQTT state tests plus the dashboard HTTP API boundary.

HTTP here serves the dashboard/API only; the STM32 transport is MQTT and no
direct board HTTP probe is part of the active protocol.
"""
import importlib
import json
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
host = importlib.import_module("server")

DEVICE = "A1B2C3D4E5F60718293A4B5C"
OTHER = "FFEEDDCCBBAA998877665544"
TOPIC = "f407/web_test/" + DEVICE + "/"
ZERO_TOKEN = "0" * 32


class Clock:
    def __init__(self):
        self.value = 100.0

    def __call__(self):
        return self.value

    def advance(self, seconds):
        self.value += seconds


def heartbeat(seq=1, uptime=10, token=ZERO_TOKEN, **changes):
    values = {
        "fw": "mqttweb1", "mqtt": "1", "token": token,
        "ip": "192.168.137.21", "server": "192.168.137.1",
        "uptime": str(uptime), "seq": str(seq), "runled": "0",
        "cmdcount": "0", "rxerr": "0", "parseerr": "0",
    }
    values.update(changes)
    return ";".join(f"{key}={value}" for key, value in values.items()).encode("ascii")


class StateTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.state = host.LinkState(clock=self.clock, now=self.clock)
        self.state.set_broker(True)
        self.published = []

    def publish(self, topic, payload):
        self.published.append((topic, payload))
        return True

    def board(self):
        return self.state.snapshot()["devices"]["stm32"]

    def receive(self, payload=None, retained=False, topic=None):
        return self.state.receive(topic or TOPIC + "status",
                                  payload or heartbeat(), retained, self.publish)

    def connect(self, seq=1, uptime=10):
        # A zero-token heartbeat requests a fresh challenge.  Only the next
        # heartbeat carrying that challenge proves a new MQTT session.
        self.assertFalse(self.receive(heartbeat(seq=seq, uptime=uptime)))
        self.assertTrue(self.published)
        topic, challenge = self.published[-1]
        self.assertEqual(topic, TOPIC + "cmd")
        self.assertTrue(challenge.startswith("SESSION:"))
        token = challenge[8:]
        self.assertEqual(len(token), 32)
        self.assertTrue(self.receive(heartbeat(seq=seq, uptime=uptime, token=token)))
        return token

    def test_no_heartbeat_cannot_prove_any_board_online(self):
        status = self.state.snapshot()
        self.assertTrue(status["devices"]["service"]["link"])
        self.assertTrue(status["brokerConnected"])
        self.assertFalse(self.board()["link"])
        self.assertIsNone(self.board()["lastSeen"])
        self.assertEqual(self.board()["telemetry"], {})
        self.assertFalse(status["devices"]["rk3588"]["link"])

    def test_mqtt_session_challenge_and_verified_heartbeat(self):
        token = self.connect()
        board = self.board()
        self.assertTrue(board["link"])
        self.assertEqual(board["ip"], "192.168.137.21")
        self.assertEqual(board["telemetry"]["seq"], 1)
        self.assertEqual(board["telemetry"]["uptime"], 10)
        self.assertTrue(board["telemetry"]["mqttVerified"])
        self.assertEqual(self.state.tokens[DEVICE], token)

    def test_retained_heartbeat_never_establishes_online(self):
        self.assertFalse(self.receive(heartbeat(), retained=True))
        self.assertFalse(self.board()["link"])
        self.assertIsNone(self.board()["lastSeen"])

    def test_repeated_or_out_of_order_sequence_does_not_extend_online(self):
        token = self.connect(seq=7, uptime=20)
        self.clock.advance(4)
        self.assertFalse(self.receive(heartbeat(seq=7, uptime=24, token=token)))
        self.assertFalse(self.receive(heartbeat(seq=6, uptime=25, token=token)))
        self.assertEqual(self.board()["lastSeen"], 100000)
        self.clock.advance(1.001)
        self.assertFalse(self.board()["link"])

    def test_online_timeout_boundary_is_more_than_five_seconds(self):
        self.connect()
        self.clock.advance(5)
        self.assertTrue(self.board()["link"])
        self.clock.advance(0.001)
        self.assertFalse(self.board()["link"])
        self.assertTrue(any("超时" in e["message"] for e in self.state.snapshot()["events"]))

    def test_new_heartbeat_restores_online_after_timeout(self):
        token = self.connect()
        self.clock.advance(6)
        self.assertFalse(self.board()["link"])
        self.assertTrue(self.receive(heartbeat(seq=2, uptime=16, token=token)))
        self.assertTrue(self.board()["link"])
        self.assertEqual(self.board()["telemetry"]["seq"], 2)

    def test_board_restart_requires_a_new_mqtt_session_token(self):
        old_token = self.connect(seq=70, uptime=80)
        self.assertFalse(self.receive(heartbeat(seq=1, uptime=1)))
        new_token = self.published[-1][1][8:]
        self.assertNotEqual(new_token, old_token)
        self.assertFalse(self.receive(heartbeat(seq=71, uptime=81, token=old_token)))
        self.assertTrue(self.receive(heartbeat(seq=1, uptime=1, token=new_token)))
        self.assertTrue(self.board()["link"])
        self.assertEqual(self.board()["telemetry"]["uptime"], 1)

    def test_alternating_old_heartbeats_cannot_fake_restarts_or_keep_online(self):
        token = self.connect(seq=5, uptime=5)
        self.clock.advance(6)
        self.assertFalse(self.board()["link"])
        self.assertFalse(self.receive(heartbeat(seq=5, uptime=5, token=token)))
        self.assertTrue(self.receive(heartbeat(seq=10, uptime=10, token=token)))
        self.assertTrue(self.board()["link"])
        last_seen = self.board()["lastSeen"]
        self.assertFalse(self.receive(heartbeat(seq=5, uptime=5, token=token)))
        self.assertEqual(self.board()["lastSeen"], last_seen)

    def test_uptime_regression_without_new_token_cannot_restart_or_refresh_board(self):
        token = self.connect(seq=2, uptime=80)
        self.assertEqual(self.state.request_self_test(self.publish)[0], 202)
        self.clock.advance(4)
        for seq in (1, 2, 3, 4):
            with self.subTest(seq=seq):
                self.assertFalse(self.receive(heartbeat(seq=seq, uptime=4, token=token)))
                self.assertEqual(self.board()["lastSeen"], 100000)
                self.assertEqual(self.board()["telemetry"]["uptime"], 80)
                self.assertEqual(self.state.snapshot()["selfTest"]["status"], "pending")
        self.clock.advance(1.001)
        self.assertFalse(self.board()["link"])
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "timeout")

    def test_broker_disconnect_requires_a_new_unique_heartbeat(self):
        token = self.connect()
        self.state.set_broker(False)
        self.assertFalse(self.board()["link"])
        self.state.set_broker(True)
        self.assertFalse(self.board()["link"])
        self.assertTrue(self.receive(heartbeat(seq=2, uptime=11, token=token)))
        self.assertTrue(self.board()["link"])

    def test_service_restart_has_no_cached_online_evidence(self):
        self.connect()
        fresh = host.LinkState(clock=self.clock, now=self.clock)
        fresh.set_broker(True)
        self.assertFalse(fresh.snapshot()["devices"]["stm32"]["link"])
        self.assertNotEqual(fresh.snapshot()["instanceId"], self.state.snapshot()["instanceId"])

    def test_missing_invalid_duplicate_and_extra_fields_are_rejected(self):
        malformed = [
            heartbeat().replace(b";seq=1", b""), heartbeat() + b";seq=2",
            heartbeat() + b";http=1", heartbeat(fw="old"),
            heartbeat(seq=-1), heartbeat(seq=4294967296), heartbeat(uptime="nan"),
            heartbeat(runled="2"), heartbeat(mqtt="2"),
            heartbeat(ip="999.0.0.1"), heartbeat(server="localhost"),
            heartbeat(token="short"), heartbeat(rxerr="-1"), b"\xff" + heartbeat(),
        ]
        for payload in malformed:
            with self.subTest(payload=payload):
                self.assertFalse(self.receive(payload))
                self.assertFalse(self.board()["link"])

    def test_wrong_topic_or_lowercase_uid_never_establishes_online(self):
        for topic in [TOPIC.lower() + "status", TOPIC + "status/extra",
                      "f407/wifi_test/" + DEVICE + "/status", TOPIC + "cmd"]:
            self.assertFalse(self.receive(heartbeat(), topic=topic))
        self.assertFalse(self.board()["link"])

    def test_offline_or_pending_self_test_is_rejected_without_publication(self):
        code, _ = self.state.request_self_test(self.publish)
        self.assertEqual(code, 409)
        self.assertEqual(self.published, [])
        self.connect()
        self.assertEqual(self.state.request_self_test(self.publish)[0], 202)
        self.assertEqual(self.state.request_self_test(self.publish)[0], 409)
        self.assertEqual(len(self.published), 2)  # SESSION + LEDTEST

    def test_self_test_publishes_unique_challenge_to_reported_device(self):
        self.connect()
        code, result = self.state.request_self_test(self.publish)
        self.assertEqual(code, 202)
        self.assertEqual(result["status"], "pending")
        self.assertRegex(result["requestId"], r"^[0-9a-f]{32}$")
        self.assertEqual(self.published[-1], (TOPIC + "cmd", "LEDTEST:" + result["requestId"]))

    def test_wrong_retained_foreign_and_late_ack_cannot_prove_success(self):
        self.connect()
        _, request = self.state.request_self_test(self.publish)
        nonce = request["requestId"]
        self.assertFalse(self.state.receive(TOPIC + "echo", b"LEDACK:" + ZERO_TOKEN.encode() + b":OK", False, self.publish))
        self.assertFalse(self.state.receive(TOPIC + "echo", f"LEDACK:{nonce}:OK".encode(), True, self.publish))
        self.assertFalse(self.state.receive("f407/web_test/" + OTHER + "/echo", f"LEDACK:{nonce}:OK".encode(), False, self.publish))
        self.assertFalse(self.state.receive(TOPIC + "echo", f"LEDACK:{nonce}:ERROR".encode(), False, self.publish))
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "pending")
        self.clock.advance(5.001)
        self.assertFalse(self.state.receive(TOPIC + "echo", f"LEDACK:{nonce}:OK".encode(), False, self.publish))
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "timeout")

    def test_matching_pending_ack_completes_once_and_old_ack_is_ignored(self):
        self.connect()
        _, request = self.state.request_self_test(self.publish)
        reply = f"LEDACK:{request['requestId']}:OK".encode()
        self.clock.advance(1)
        self.assertTrue(self.state.receive(TOPIC + "echo", reply, False, self.publish))
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "success")
        completed = self.state.snapshot()["selfTest"]["completedAt"]
        self.clock.advance(1)
        self.assertFalse(self.state.receive(TOPIC + "echo", reply, False, self.publish))
        self.assertEqual(self.state.snapshot()["selfTest"]["completedAt"], completed)

    def test_broker_disconnect_ends_pending_request(self):
        self.connect()
        self.state.request_self_test(self.publish)
        self.state.set_broker(False)
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "error")

    def test_events_are_bounded_and_keep_unique_increasing_ids(self):
        for _ in range(110):
            self.state.set_broker(False)
            self.state.set_broker(True)
        events = self.state.snapshot()["events"]
        self.assertLessEqual(len(events), 100)
        ids = [event["id"] for event in events]
        self.assertEqual(ids, sorted(set(ids)))


class HttpTests(unittest.TestCase):
    """The dashboard/API HTTP boundary, not a board transport test."""

    def setUp(self):
        self.state = host.LinkState()
        self.state.set_broker(True)
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), host.make_handler(self.state, lambda *_: True))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)

    def request(self, path, data=None):
        request = urllib.request.Request(self.base + path, data=data,
                                          headers={"Content-Type": "application/json"})
        try:
            response = urllib.request.urlopen(request, timeout=3)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            return response.status, dict(response.headers), response.read()

    def test_status_is_no_store_live_and_bound_address_is_reported(self):
        code, headers, body = self.request("/api/status")
        status = json.loads(body)
        self.assertEqual(code, 200)
        self.assertIn("no-store", headers["Cache-Control"])
        self.assertEqual(status["mode"], "live")
        self.assertEqual(status["devices"]["service"]["ip"], "127.0.0.1")
        self.assertFalse(status["devices"]["stm32"]["link"])

    def test_direct_board_probe_endpoint_is_removed(self):
        self.assertEqual(self.request("/probe?device=" + DEVICE)[0], 404)

    def test_post_accepts_empty_object_only_and_rejects_offline(self):
        self.assertEqual(self.request("/api/self-test", b"{}")[0], 409)
        self.assertEqual(self.request("/api/self-test", b'{"drive":true}')[0], 400)
        self.assertEqual(self.request("/api/self-test", b"[]")[0], 400)
        self.assertEqual(self.request("/api/self-test", b"bad")[0], 400)

    def test_static_page_is_served_but_traversal_and_private_files_are_not(self):
        code, headers, body = self.request("/")
        self.assertEqual(code, 200)
        self.assertIn(b"status.js", body)
        self.assertIn("no-store", headers["Cache-Control"])
        for path in ["/../web_link_host/server.py", "/%2e%2e/web_link_host/server.py",
                     "/archive/v01/app.js", "/tests/status.test.cjs", "/server.py"]:
            with self.subTest(path=path):
                self.assertEqual(self.request(path)[0], 404)


if __name__ == "__main__":
    unittest.main(verbosity=2)
