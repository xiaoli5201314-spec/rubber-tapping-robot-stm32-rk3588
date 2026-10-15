"""Pure MQTT handshake and recovery, without a board HTTP probe."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import server
from test_host import Clock, DEVICE, TOPIC


def status(seq=1, uptime=10, token="0" * 32):
    return (
        f"fw=mqttmotor60;mqtt=1;token={token};ip=192.168.137.21;"
        f"server=192.168.137.1;uptime={uptime};seq={seq};runled=0;"
        "cmdcount=0;rxerr=0;parseerr=0;"
        "ch1=1500;ch2=1500;ch5=1000;p1=20000;p2=20000;p5=20000;"
        "v1=1;v2=1;v5=1;state=WAIT;left=0;right=0;motor=1"
    ).encode("ascii")


class SessionTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.state = server.LinkState(clock=self.clock, now=self.clock)
        self.state.set_broker(True)
        self.sent = []

    def receive(self, payload):
        return self.state.receive(TOPIC + "status", payload, False, self.publish)

    def publish(self, topic, payload):
        self.sent.append((topic, payload))
        return True

    def handshake(self, seq=2, uptime=11):
        self.assertFalse(self.receive(status(seq - 1, uptime - 1)))
        self.assertEqual(len(self.sent), 1)
        self.assertEqual(self.sent[0][0], TOPIC + "cmd")
        self.assertRegex(self.sent[0][1], r"^SESSION:[a-f0-9]{32}$")
        token = self.sent[0][1][8:]
        self.assertTrue(self.receive(status(seq, uptime, token)))
        return token

    def test_parser_accepts_three_mqtt_variants_with_exact_output_limits(self):
        for percent in (30, 60, 100):
            payload = status().replace(b"mqttmotor60", f"mqttmotor{percent}".encode())
            fields = server.parse_heartbeat(payload)
            self.assertIsNotNone(fields)
            self.assertEqual(server.rc_telemetry(fields)["outputLimit"], percent)
            self.assertTrue(server.rc_telemetry(fields)["motorEnabled"])

    def test_mqtt_handshake_proves_round_trip_without_http(self):
        self.handshake()
        board = self.state.snapshot()["devices"]["stm32"]
        self.assertTrue(board["link"])
        self.assertTrue(board["telemetry"]["mqttVerified"])
        self.assertNotIn("httpVerified", board["telemetry"])

    def test_lost_challenge_is_retried_with_same_token_and_no_false_online(self):
        self.receive(status())
        token = self.sent[-1]
        self.clock.advance(0.2)
        self.receive(status(2))
        self.assertEqual(len(self.sent), 1)
        self.clock.advance(1)
        self.receive(status(3))
        self.assertEqual(self.sent, [token, token])
        self.assertFalse(self.state.snapshot()["devices"]["stm32"]["link"])

    def test_reboot_uses_new_session_and_rejects_old_heartbeat(self):
        token = self.handshake()
        self.receive(status(70, 80, token))
        self.state.request_self_test(self.publish)
        self.clock.advance(1)
        self.receive(status(1, 1))
        new_token = self.sent[-1][1][8:]
        self.assertNotEqual(token, new_token)
        self.assertTrue(self.receive(status(2, 2, new_token)))
        self.assertFalse(self.receive(status(71, 81, token)))
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "error")

    def assert_reset_epoch_is_accepted(self, first_seq):
        # A board that joined after running for a while can restart before its
        # small observed sequence is overtaken by the next visible heartbeat.
        old_token = self.handshake(seq=2, uptime=80)
        code, pending = self.state.request_self_test(self.publish)
        self.assertEqual(code, 202)
        self.assertEqual(pending["status"], "pending")
        self.clock.advance(1)
        self.assertFalse(self.receive(status(1, 1)))
        topic, challenge = self.sent[-1]
        self.assertEqual(topic, TOPIC + "cmd")
        self.assertRegex(challenge, r"^SESSION:[a-f0-9]{32}$")
        new_token = challenge[8:]
        self.assertNotEqual(new_token, old_token)
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "pending")

        first = status(first_seq, 4, new_token)
        self.assertTrue(self.receive(first), "A new authenticated epoch must accept its first visible sequence")
        board = self.state.snapshot()["devices"]["stm32"]
        self.assertTrue(board["link"])
        self.assertTrue(board["telemetry"]["mqttVerified"])
        self.assertEqual((board["telemetry"]["seq"], board["telemetry"]["uptime"]), (first_seq, 4))
        self.assertEqual(board["lastSeen"], 101000)
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "error")
        self.assertFalse(self.state.receive(
            TOPIC + "echo", f"LEDACK:{pending['requestId']}:OK".encode("ascii"),
            False, self.publish,
        ))

        # The old epoch would have timed out now. Only the accepted reset frame
        # supplies fresh evidence; retired tokens and duplicates cannot extend it.
        self.clock.advance(4.1)
        self.assertTrue(self.state.snapshot()["devices"]["stm32"]["link"])
        self.assertFalse(self.receive(status(3, 81, old_token)))
        self.assertFalse(self.receive(first))
        self.assertEqual(self.state.snapshot()["devices"]["stm32"]["lastSeen"], 101000)
        self.clock.advance(1)
        self.assertFalse(self.state.snapshot()["devices"]["stm32"]["link"])
        self.assertTrue(self.receive(status(first_seq + 1, 9, new_token)))
        self.assertTrue(self.state.snapshot()["devices"]["stm32"]["link"])
        self.assertEqual(self.state.snapshot()["selfTest"]["status"], "error")

    def test_reboot_accepts_new_token_with_equal_first_sequence(self):
        self.assert_reset_epoch_is_accepted(2)

    def test_reboot_accepts_new_token_after_one_initial_status_is_lost(self):
        self.assert_reset_epoch_is_accepted(3)

    def test_reboot_accepts_new_token_after_two_initial_statuses_are_lost(self):
        self.assert_reset_epoch_is_accepted(4)

    def test_host_restart_challenges_existing_board_token(self):
        token = self.handshake()
        self.state = server.LinkState(clock=self.clock, now=self.clock)
        self.state.set_broker(True)
        self.receive(status(3, 12, token))
        new_token = self.sent[-1][1][8:]
        self.assertNotEqual(token, new_token)
        self.assertTrue(self.receive(status(4, 13, new_token)))

    def test_real_timeout_is_not_hidden_and_fresh_session_recovers(self):
        token = self.handshake()
        self.clock.advance(5.001)
        self.assertFalse(self.state.snapshot()["devices"]["stm32"]["link"])
        self.assertTrue(self.receive(status(3, 16, token)))
        self.assertTrue(self.state.snapshot()["devices"]["stm32"]["link"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
