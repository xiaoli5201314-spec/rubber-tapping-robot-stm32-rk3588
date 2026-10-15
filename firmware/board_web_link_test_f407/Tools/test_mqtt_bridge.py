"""Actual HEX -> controlled ESP transport -> real broker -> host -> LEDACK.

No physical ESP, WiFi radio, receiver or motor is operated by this test.
"""
import os
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

os.environ.setdefault("F407_PROTOCOL_TARGET", "board_chassis_web_f407_60pct")
from test_protocol import Firmware, TOPIC

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "upper_computer/web_link_host"))
import server

BROKER = Path(shutil.which("mosquitto") or "mosquitto")


def wait_until(condition, seconds=4):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.02)
    raise AssertionError("Timed out waiting for real broker/host evidence")


class CompiledBridgeTests(unittest.TestCase):
    def test_compiled_heartbeat_session_led_restart_and_reconnection(self):
        with socket.socket() as port_socket:
            port_socket.bind(("127.0.0.1", 0))
            port = port_socket.getsockname()[1]
        with tempfile.TemporaryDirectory(prefix="f407-mqtt-hex-") as folder:
            config = Path(folder) / "broker.conf"
            config.write_text(f"listener {port} 127.0.0.1\nallow_anonymous true\npersistence false\n", "ascii")
            with (Path(folder) / "broker.log").open("wb") as log:
                broker = subprocess.Popen([str(BROKER), "-c", str(config)],
                                          stdout=log, stderr=log,
                                          creationflags=subprocess.CREATE_NO_WINDOW)
                client = server.mqtt_client(f"hex_transport_{port}")
                inbox = queue.Queue()
                subscribed = []
                link = None
                try:
                    wait_until(lambda: server.check_broker("127.0.0.1", port))
                    client.on_connect = lambda c, *unused: c.subscribe(TOPIC.decode(), 0)
                    client.on_subscribe = lambda *unused: subscribed.append(True)
                    client.on_message = lambda c, u, message: inbox.put(message)
                    client.connect("127.0.0.1", port, 20)
                    client.loop_start()
                    wait_until(lambda: subscribed)
                    state = server.LinkState()
                    link = server.MqttLink(state, "127.0.0.1", port)
                    link.client.loop_start()
                    wait_until(lambda: state.broker_connected)

                    def new_board():
                        board = Firmware()
                        board.call("Board_Init")
                        board.call("RcWeb_Init")
                        board.ready()
                        self.assertEqual(board.call("setup_mqtt"), 1)
                        return board

                    f = new_board()

                    def flush():
                        while f.published:
                            topic, payload, unused = f.published.pop(0)
                            client.publish(topic.decode(), payload, qos=1, retain=False).wait_for_publish(3)

                    def command(prefix):
                        message = inbox.get(timeout=4)
                        self.assertEqual(message.qos, 0)
                        self.assertFalse(message.retain)
                        self.assertTrue(message.payload.startswith(prefix))
                        f.rx.extend(
                            b'+MQTTSUBRECV:0,"' + TOPIC + b'",'
                            + str(len(message.payload)).encode("ascii")
                            + b"," + message.payload + b"\r\n"
                        )
                        f.call("service")
                        return message.payload

                    def heartbeat():
                        f.publish_status()
                        flush()

                    def board():
                        return state.snapshot()["devices"]["stm32"]

                    heartbeat()
                    old_session = command(b"SESSION:")
                    early = b"LEDTEST:" + b"123456789abcdef0123456789abcdef0"
                    self.assertTrue(link.publish(TOPIC.decode(), early))
                    self.assertEqual(command(b"LEDTEST:"), early)
                    self.assertEqual(f.leds[-1][2], 0)
                    self.assertEqual(f.word("g_wifi_test_status", 64), 0)
                    heartbeat()
                    wait_until(lambda: board()["link"])
                    self.assertTrue(board()["telemetry"]["mqttVerified"])
                    self.assertEqual(board()["telemetry"]["firmware"], "mqttmotor60")
                    self.assertEqual(board()["telemetry"]["outputLimit"], 60)
                    self.assertNotIn("httpVerified", board()["telemetry"])

                    code, request = state.request_self_test(link.publish)
                    self.assertEqual(code, 202)
                    led = command(b"LEDTEST:")
                    self.assertEqual(led.decode(), "LEDTEST:" + request["requestId"])
                    for _ in range(20):
                        f.ticks += 20
                        f.call("service")
                    f.call("send_echo")
                    f.call("service")
                    f.call("service")
                    flush()
                    wait_until(lambda: state.snapshot()["selfTest"]["status"] == "success")
                    self.assertEqual(f.word("g_wifi_test_status", 64), 1)  # cmdcount
                    self.assertEqual(f.word("g_wifi_test_status", 36), 1)  # completed echoes

                    # Board restart rotates the MQTT session, with no HTTP probe.
                    f.ticks += 80000
                    heartbeat()
                    wait_until(lambda: board()["telemetry"]["uptime"] >= 80)
                    f = new_board()
                    heartbeat()
                    fresh_session = command(b"SESSION:")
                    self.assertNotEqual(fresh_session, old_session)
                    heartbeat()
                    wait_until(lambda: board()["telemetry"]["uptime"] < 5)
                    self.assertTrue(board()["link"])

                    # Host restart challenges the still-running firmware over MQTT.
                    link.client.disconnect()
                    link.client.loop_stop()
                    state = server.LinkState()
                    link = server.MqttLink(state, "127.0.0.1", port)
                    link.client.loop_start()
                    wait_until(lambda: state.broker_connected)
                    heartbeat()
                    host_session = command(b"SESSION:")
                    self.assertNotEqual(host_session, fresh_session)
                    heartbeat()
                    wait_until(lambda: board()["link"])
                    broker.terminate()
                    broker.wait(5)
                    wait_until(lambda: not state.broker_connected)
                    self.assertFalse(board()["link"])

                    # Restart the same isolated broker; fresh heartbeat restores
                    # online only after both subscriptions are established.
                    subscribed.clear()
                    broker = subprocess.Popen([str(BROKER), "-c", str(config)],
                                              stdout=log, stderr=log,
                                              creationflags=subprocess.CREATE_NO_WINDOW)
                    wait_until(lambda: state.broker_connected, 10)
                    wait_until(lambda: subscribed, 10)
                    heartbeat()
                    wait_until(lambda: board()["link"])
                finally:
                    if link:
                        link.client.disconnect()
                        link.client.loop_stop()
                    client.disconnect()
                    client.loop_stop()
                    if broker.poll() is None:
                        broker.terminate()
                        broker.wait(5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
