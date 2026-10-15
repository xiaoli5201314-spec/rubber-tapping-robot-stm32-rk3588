"""Run the compiled RC web firmware against controlled timer/ESP fixtures.

These tests execute the HEX instructions; they are not physical receiver tests.
Use F407_PROTOCOL_TARGET=board_rc_web_test_f407 after building that target.
"""
import json
import importlib.util
import os
import struct
import unittest

os.environ.setdefault("F407_PROTOCOL_TARGET", "board_rc_web_test_f407")
import test_protocol as harness
from test_protocol import Firmware, symbol

TIM2 = 0x40000000
TIM10 = 0x40014400
GPIOB = 0x40020400
GPIOF = 0x40021400
RCC = 0x40023800
host_spec = importlib.util.spec_from_file_location(
    "rc_contract_host", harness.ROOT.parents[1] / "upper_computer/web_link_host/server.py")
host = importlib.util.module_from_spec(host_spec)
host_spec.loader.exec_module(host)


class RcWebTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()
        self.f.call("Board_Init")
        self.f.call("RcWeb_Init")
        self.counter = 0

    def write(self, address, value):
        self.f.uc.mem_write(address, struct.pack("<I", value))

    def read(self, address):
        return struct.unpack("<I", self.f.uc.mem_read(address, 4))[0]

    def edge(self, channel, tick, high, overcapture=False):
        timer, slot, gpio, pin, irq = {
            1: (TIM2, 3, GPIOB, 10, "TIM2_IRQHandler"),
            2: (TIM2, 4, GPIOB, 11, "TIM2_IRQHandler"),
            5: (TIM10, 1, GPIOF, 6, "TIM1_UP_TIM10_IRQHandler"),
        }[channel]
        self.write(gpio + 0x10, (self.read(gpio + 0x10) & ~(1 << pin)) |
                   (int(high) << pin))
        self.write(timer + 0x34 + 4 * (slot - 1),
                   tick & (0xFFFF if channel == 5 else 0xFFFFFFFF))
        self.write(timer + 0x10, (1 << slot) |
                   ((1 << (slot + 8)) if overcapture else 0))
        self.f.call(irq)
        # Model the timer status register after a write-to-clear operation.
        self.write(timer + 0x10, 0)

    def frame(self, ch1=1500, ch2=1500, ch5=1000, period=20000):
        self.counter += period
        for channel, width in ((1, ch1), (2, ch2), (5, ch5)):
            self.edge(channel, self.counter, True)
            self.edge(channel, self.counter + width, False)
        self.f.ticks += period // 1000
        self.f.call("RcWeb_Service", self.f.ticks)

    def hold(self, frames=60, **values):
        for _ in range(frames):
            self.frame(**values)

    def publish(self):
        self.f.ready()
        self.f.call("publish_status")
        self.f.call("service")
        self.f.call("service")
        self.assertTrue(self.f.published)
        raw = self.f.published[-1][1]
        self.assertIsNotNone(host.parse_heartbeat(raw),
                             "Compiled firmware must satisfy the actual host parser")
        payload = raw.decode("ascii")
        return dict(item.split("=", 1) for item in payload.split(";"))

    def arm(self):
        self.hold(ch5=1000)
        self.hold(frames=3, ch5=2000)
        self.assertEqual(self.publish()["state"], "ARMED")

    def test_initialization_uses_microseconds_and_keeps_motor_pwm_disabled(self):
        self.f.call("Board_Init")
        self.f.call("RcWeb_Init")
        self.assertEqual(self.read(TIM2 + 0x28), 15)
        self.assertEqual(self.read(TIM10 + 0x28), 15)
        self.assertEqual(self.read(TIM2 + 0x2C), 0xFFFFFFFF)
        self.assertEqual(self.read(TIM10 + 0x2C), 0xFFFF)
        self.assertEqual((self.read(GPIOB + 0x24) >> 8) & 0xFF, 0x11)
        self.assertEqual((self.read(GPIOF + 0x20) >> 24) & 0xF, 3)
        self.assertEqual(self.read(GPIOB + 0x0C) & (0xF << 20), 0)
        self.assertEqual(self.read(GPIOF + 0x0C) & (3 << 12), 0)
        self.assertEqual(self.read(RCC + 0x40) & (1 << 2), 0)  # TIM4 off
        self.assertEqual(self.read(RCC + 0x38), 0)  # FSMC never enabled
        for gpio, pins in ((GPIOB, (6, 7)), (0x40020C00, (6, 7)),
                           (0x40021000, (5, 6))):
            for pin in pins:
                self.assertEqual((self.read(gpio) >> (2 * pin)) & 3, 1)
                self.assertTrue(self.read(gpio + 0x18) & (1 << (pin + 16)))

    def test_measured_pulse_and_period_reach_raw_mqtt_payload(self):
        self.hold(frames=3, ch1=1000, ch2=2000, ch5=1500)
        data = self.publish()
        self.assertEqual(data["fw"], "mqttrc1")
        fields = host.parse_heartbeat(self.f.published[-1][1])
        telemetry = host.rc_telemetry(fields)
        self.assertTrue(telemetry["simulation"])
        self.assertFalse(telemetry["motorEnabled"])
        self.assertEqual(data["mqtt"], "1")
        self.assertEqual([data[x] for x in ("ch1", "ch2", "ch5")],
                         ["1000", "2000", "1500"])
        self.assertEqual([data[x] for x in ("p1", "p2", "p5")], ["20000"] * 3)
        self.assertEqual([data[x] for x in ("v1", "v2", "v5")], ["1"] * 3)
        self.assertEqual(data["motor"], "0")
        self.assertEqual((data["left"], data["right"]), ("0", "0"))

    def test_16_bit_wrap_does_not_corrupt_ch5(self):
        self.counter = 45000
        self.hold(frames=5, ch5=2000)
        data = self.publish()
        self.assertEqual((data["ch5"], data["p5"], data["v5"]),
                         ("2000", "20000", "1"))

    def test_high_switch_at_boot_cannot_arm(self):
        self.hold(frames=70, ch5=2000)
        data = self.publish()
        self.assertEqual(data["state"], "LOCKED")
        self.assertEqual((data["left"], data["right"]), ("0", "0"))

    def test_non_neutral_input_cannot_arm(self):
        self.hold(ch1=2000, ch5=1000)
        self.hold(frames=70, ch1=2000, ch5=2000)
        self.assertEqual(self.publish()["state"], "LOCKED")

    def test_neutral_hold_arm_and_all_motion_signs(self):
        self.arm()
        for ch1, ch2, outputs in (
            (1500, 2000, ("300", "300")),
            (1500, 1000, ("-300", "-300")),
            (1000, 1500, ("-300", "300")),
            (2000, 1500, ("300", "-300")),
            (1500, 1500, ("0", "0")),
        ):
            with self.subTest(ch1=ch1, ch2=ch2):
                # This fixture services once per frame. Allow deceleration,
                # the 50 ms reversal brake, and acceleration to settle.
                self.hold(frames=80, ch1=ch1, ch2=ch2, ch5=2000)
                data = self.publish()
                self.assertEqual(data["state"], "ARMED")
                self.assertEqual((data["left"], data["right"]), outputs)

    def test_disarm_clears_simulation_without_ramping(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.frame(ch2=2000, ch5=1000)
        data = self.publish()
        self.assertEqual(data["state"], "LOCKED")
        self.assertEqual((data["left"], data["right"]), ("0", "0"))

    def test_stale_pwm_enters_failsafe_and_hides_last_pulses(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.f.ticks += 110
        self.f.call("RcWeb_Service", self.f.ticks)
        data = self.publish()
        self.assertEqual(data["state"], "FAILSAFE")
        for x in ("ch1", "ch2", "ch5", "p1", "p2", "p5",
                  "v1", "v2", "v5", "left", "right"):
            self.assertEqual(data[x], "0", x)
        self.hold(frames=10, ch5=2000)
        self.assertEqual(self.publish()["state"], "FAILSAFE")
        self.arm()

    def test_fast_period_and_overcapture_are_invalidated(self):
        self.arm()
        self.hold(frames=4, ch5=2000, period=3000)
        data = self.publish()
        self.assertEqual(data["state"], "FAILSAFE")
        self.assertEqual([data[x] for x in ("v1", "v2", "v5")], ["0"] * 3)
        self.hold(frames=3, ch5=1000)
        self.edge(1, self.counter + 20000, True, overcapture=True)
        self.f.call("RcWeb_Service", self.f.ticks + 10)
        data = self.publish()
        self.assertEqual((data["ch1"], data["p1"], data["v1"]), ("0", "0", "0"))

    def test_mqtt_connection_wait_keeps_running_receiver_safety(self):
        self.arm()
        self.f.mqtt_connected_delay = 350
        self.assertEqual(self.f.call("setup_mqtt"), 1)
        data = self.publish()
        self.assertEqual(data["state"], "FAILSAFE")
        self.assertEqual((data["left"], data["right"]), ("0", "0"))
        self.assertFalse(any(part.startswith(b"AT+CIPSTART=") for part in self.f.tx))

    def test_snapshot_clock_cannot_precede_a_just_arrived_capture(self):
        self.arm()
        old_now = self.f.ticks
        self.counter += 20000
        # A timer IRQ may run after the caller sampled its millisecond clock.
        for channel, width in ((1, 1500), (2, 1500), (5, 2000)):
            self.edge(channel, self.counter, True)
            self.edge(channel, self.counter + width, False)
        self.f.call("RcWeb_Service", old_now)
        self.assertEqual(self.publish()["state"], "ARMED")


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(RcWebTests))
    report = dict(state="PASS" if result.wasSuccessful() else "FAIL",
                  tests_run=result.testsRun, failures=len(result.failures),
                  errors=len(result.errors), physical_receiver_verified=False)
    (harness.ROOT / "Tools/rc_web_test.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
