"""RC firmware -> host/API contract. Fixtures are software evidence only."""
import unittest

from test_host import Clock, DEVICE, TOPIC, heartbeat, host


def rc_heartbeat(**changes):
    fields = dict(
        fw="mqttrc1", mqtt="1", token="0" * 32, seq="3",
        ch1="2000", ch2="1500", ch5="2000",
        p1="20000", p2="20000", p5="20000",
        v1="1", v2="1", v5="1", state="ARMED",
        left="300", right="-300", motor="0",
    )
    fields.update(changes)
    return heartbeat(**fields)


class RcTelemetryTests(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.state = host.LinkState(clock=self.clock, now=self.clock)
        self.state.set_broker(True)
        self.published = []
        self.token = None

    def publish(self, topic, payload):
        self.published.append((topic, payload))
        return True

    def connect(self):
        self.assertFalse(self.state.receive(
            TOPIC + "status", heartbeat(seq=1, uptime=1), publish=self.publish,
        ))
        self.assertEqual(len(self.published), 1)
        topic, challenge = self.published[0]
        self.assertEqual(topic, TOPIC + "cmd")
        self.assertRegex(challenge, r"^SESSION:[0-9a-f]{32}$")
        self.token = challenge[8:]
        self.assertNotEqual(self.token, "0" * 32)
        self.assertTrue(self.state.receive(
            TOPIC + "status", heartbeat(seq=2, uptime=2, token=self.token),
            publish=self.publish,
        ))
        self.assertTrue(self.telemetry()["mqttVerified"])

    def receive(self, payload):
        self.assertIsNotNone(self.token, "Connect the simulated board before sending telemetry")
        payload = payload.replace(
            b"token=" + b"0" * 32, b"token=" + self.token.encode("ascii"),
        )
        return self.state.receive(TOPIC + "status", payload, publish=self.publish)

    def telemetry(self):
        return self.state.snapshot()["devices"]["stm32"]["telemetry"]

    def test_actual_rc_fields_reach_api_as_simulation_with_correct_units(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat()))
        data = self.telemetry()
        self.assertEqual(data["firmware"], "mqttrc1")
        self.assertEqual((data["ch1"], data["ch2"], data["ch5"]), (2000, 1500, 2000))
        self.assertEqual((data["left"], data["right"]), (30, -30))
        self.assertEqual((data["ch1Period"], data["ch2Period"], data["ch5Period"]), (20, 20, 20))
        self.assertEqual(data["safetyState"], "ARMED")
        self.assertEqual(data["direction"], "右转")
        self.assertTrue(data["simulation"])
        self.assertFalse(data["locked"])
        self.assertTrue(data["rcValid"])
        self.assertFalse(data["motorEnabled"])
        self.assertEqual(len(self.published), 1)

    def test_missing_pwm_does_not_make_online_stm32_look_offline_or_reuse_pulses(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat()))
        self.assertEqual(self.telemetry()["ch1"], 2000)
        self.assertTrue(self.receive(rc_heartbeat(
            seq="4", uptime="11", ch1="0", p1="0", v1="0",
            state="FAILSAFE", left="0", right="0")))
        status = self.state.snapshot()
        data = self.telemetry()
        self.assertTrue(status["devices"]["stm32"]["link"])
        self.assertIsNone(data["ch1"])
        self.assertIsNone(data["ch1Period"])
        self.assertFalse(data["ch1Valid"])
        self.assertFalse(data["rcValid"])
        self.assertTrue(data["locked"])
        self.assertIn("CH1", data["fault"])
        self.assertEqual(data["direction"], "静止")

    def test_locked_wait_and_failsafe_cannot_claim_nonzero_simulated_motion(self):
        for state in ("LOCKED", "WAIT", "FAILSAFE"):
            with self.subTest(state=state):
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(state=state)))
                self.assertIsNotNone(host.parse_heartbeat(
                    rc_heartbeat(state=state, left="0", right="0")))

    def test_invalid_rc_contract_never_establishes_or_refreshes_online(self):
        invalid = [
            rc_heartbeat(motor="1"), rc_heartbeat(v1="2"),
            rc_heartbeat(ch1="0"), rc_heartbeat(p1="3000"),
            rc_heartbeat(left="301"), rc_heartbeat(right="-301"),
            rc_heartbeat(state="unknown"),
            rc_heartbeat(v1="0"), rc_heartbeat(ch5="1000"),
            rc_heartbeat(v1="0", ch1="0", p1="0"),  # ARMED with missing PWM
            rc_heartbeat().replace(b";p5=20000", b""),
            rc_heartbeat() + b";extra=1",
        ]
        for payload in invalid:
            with self.subTest(payload=payload):
                self.assertIsNone(host.parse_heartbeat(payload))
                self.assertFalse(self.state.receive(
                    TOPIC + "status", payload, publish=self.publish,
                ))
                self.assertFalse(self.state.snapshot()["devices"]["stm32"]["link"])
        self.assertEqual(self.published, [])
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat()))
        self.clock.advance(6)
        self.assertFalse(self.receive(rc_heartbeat(seq="4", p1="3000")))
        self.assertFalse(self.state.snapshot()["devices"]["stm32"]["link"])

    def test_motion_signs_map_to_forward_reverse_left_and_right(self):
        self.connect()
        cases = [
            ("1500", "2000", "300", "300", "前进"),
            ("1500", "1000", "-300", "-300", "后退"),
            ("1000", "1500", "-300", "300", "左转"),
            ("2000", "1500", "300", "-300", "右转"),
            ("1500", "1500", "0", "0", "静止"),
        ]
        for index, (ch1, ch2, left, right, direction) in enumerate(cases, 3):
            self.assertTrue(self.receive(rc_heartbeat(
                seq=str(index), ch1=ch1, ch2=ch2, left=left, right=right)))
            self.assertEqual(self.telemetry()["direction"], direction)

    def test_turn_direction_wins_over_rounding_residual_in_output_sum(self):
        self.connect()
        cases = [
            ("1995", "296", "-300", "右转"),
            ("1995", "300", "-296", "右转"),
            ("1005", "-296", "300", "左转"),
            ("1005", "-300", "296", "左转"),
        ]
        for index, (ch1, left, right, direction) in enumerate(cases, 3):
            with self.subTest(left=left, right=right):
                self.assertTrue(self.receive(rc_heartbeat(
                    seq=str(index), ch1=ch1, ch2="1497", ch5="1996",
                    left=left, right=right)))
                self.assertEqual(self.telemetry()["direction"], direction)

    def test_real_motor_firmware_reports_enabled_output_without_simulation(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat(
            fw="mqttmotor30", motor="1", left="296", right="-300")))
        data = self.telemetry()
        self.assertEqual(data["firmware"], "mqttmotor30")
        self.assertFalse(data["simulation"])
        self.assertTrue(data["motorEnabled"])
        self.assertEqual((data["left"], data["right"]), (29.6, -30))
        self.assertEqual(data["direction"], "右转")

    def test_full_output_firmware_reaches_api_with_100_percent_limit(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat(
            fw="mqttmotor100", motor="1", left="1000", right="-1000")))
        data = self.telemetry()
        self.assertEqual(data["firmware"], "mqttmotor100")
        self.assertFalse(data["simulation"])
        self.assertTrue(data["motorEnabled"])
        self.assertEqual(data["outputLimit"], 100)
        self.assertEqual((data["left"], data["right"]), (100, -100))
        self.assertEqual(data["direction"], "右转")

    def test_medium_output_firmware_reaches_same_api_with_60_percent_limit(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat(
            fw="mqttmotor60", motor="1", left="600", right="-600")))
        data = self.telemetry()
        self.assertEqual(data["firmware"], "mqttmotor60")
        self.assertFalse(data["simulation"])
        self.assertTrue(data["motorEnabled"])
        self.assertEqual(data["outputLimit"], 60)
        self.assertEqual((data["left"], data["right"]), (60, -60))
        self.assertEqual(data["direction"], "右转")

    def test_one_host_accepts_all_three_limits_and_preserves_safety_validation(self):
        self.connect()
        for seq, (fw, output, percent) in enumerate((
            ("mqttmotor30", "300", 30),
            ("mqttmotor60", "600", 60),
            ("mqttmotor100", "1000", 100),
        ), 3):
            with self.subTest(fw=fw):
                self.assertTrue(self.receive(rc_heartbeat(
                    fw=fw, motor="1", seq=str(seq), left=output, right="-" + output)))
                self.assertEqual(self.telemetry()["outputLimit"], percent)
                self.assertEqual(self.telemetry()["left"], percent)
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(
                    fw=fw, motor="1", left="1001")))
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(
                    fw=fw, motor="1", state="LOCKED", left=output)))
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(
                    fw=fw, motor="0", left=output)))

    def test_output_limits_follow_firmware_and_never_relax_locking(self):
        for fw, motor in (("mqttrc1", "0"), ("mqttmotor30", "1")):
            fields = host.parse_heartbeat(rc_heartbeat(
                fw=fw, motor=motor, left="300", right="-300"))
            self.assertIsNotNone(fields)
            self.assertEqual(host.rc_telemetry(fields).get("outputLimit"), 30)
            self.assertIsNone(host.parse_heartbeat(rc_heartbeat(
                fw=fw, motor=motor, left="301")))
        for changes in (
            dict(left="1001"), dict(right="-1001"), dict(left="9999"),
            dict(motor="0"), dict(state="LOCKED"), dict(state="WAIT"),
            dict(state="FAILSAFE"), dict(ch5="1500"),
        ):
            with self.subTest(changes=changes):
                fields = dict(fw="mqttmotor100", motor="1", left="1000", right="-1000")
                fields.update(changes)
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(**fields)))
        self.assertIsNotNone(host.parse_heartbeat(rc_heartbeat(
            fw="mqttmotor100", motor="1", state="LOCKED", left="0", right="0")))

    def test_firmware_version_and_motor_enabled_flag_must_agree(self):
        for changes in [
            dict(fw="mqttmotor30", motor="0"),
            dict(fw="mqttrc1", motor="1"),
            dict(fw="mqttmotor30", motor="2"),
            dict(fw="mqttmotor30", motor="1", state="LOCKED"),
            dict(fw="mqttmotor30", motor="1", ch5="1500"),
        ]:
            with self.subTest(changes=changes):
                self.assertIsNone(host.parse_heartbeat(rc_heartbeat(**changes)))
        self.assertIsNotNone(host.parse_heartbeat(rc_heartbeat(
            fw="mqttmotor30", motor="1", state="WAIT", left="0", right="0")))

    def test_mqtt_web_firmware_clears_previous_rc_values(self):
        self.connect()
        self.assertTrue(self.receive(rc_heartbeat()))
        self.assertTrue(self.receive(heartbeat(seq=4, uptime=11, token=self.token)))
        self.assertNotIn("ch1", self.telemetry())
        self.assertNotIn("simulation", self.telemetry())


if __name__ == "__main__":
    unittest.main(verbosity=2)
