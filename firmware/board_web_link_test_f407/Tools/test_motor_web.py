"""Execute the compiled HEX instructions against controlled register fixtures.

No physical motor, receiver, ESP or Wi-Fi radio is operated by these tests.
"""
import json
import os
import struct
import unittest

os.environ.setdefault("F407_PROTOCOL_TARGET", "board_chassis_web_f407")
from unicorn import UC_HOOK_MEM_WRITE
from test_rc_web import RcWebTests, host
from test_protocol import ROOT, Firmware, symbol

TIM4 = 0x40000800
RCC = 0x40023800
GPIOB = 0x40020400
GPIOD = 0x40020C00
GPIOE = 0x40021000
IWDG = 0x40003000
# Hand-checked expectations for each independently compiled firmware.
LIMIT, COMPARE, VERSION, STICK_CASES = {
    "30": (300, 480, "mqttmotor30", (
        (1500, "0", 0), (1550, "0", 0), (1600, "33", 52),
        (1775, "150", 240), (2000, "300", 480))),
    "60": (600, 960, "mqttmotor60", (
        (1500, "0", 0), (1550, "0", 0), (1600, "66", 105),
        (1775, "300", 480), (2000, "600", 960))),
    "100": (1000, 1600, "mqttmotor100", (
        (1500, "0", 0), (1550, "0", 0), (1600, "111", 177),
        (1775, "500", 800), (2000, "1000", 1600))),
}[os.environ.get("F407_MOTOR_VARIANT", "100")]


class MotorWebTests(unittest.TestCase):
    write = RcWebTests.write
    read = RcWebTests.read
    edge = RcWebTests.edge
    frame = RcWebTests.frame
    publish = RcWebTests.publish
    arm = RcWebTests.arm

    def setUp(self):
        self.f = Firmware()
        self.trace = []
        # Register before first execution: Unicorn caches translated blocks.
        self.f.uc.hook_add(UC_HOOK_MEM_WRITE, self.on_write)
        self.f.call("Board_Init")
        self.f.call("RcWeb_Init")
        self.counter = 0
        self.trace.clear()

    def on_write(self, uc, access, address, size, value, unused):
        if TIM4 <= address <= TIM4 + 0x40 or address in (GPIOD + 0x18, GPIOE + 0x18):
            self.trace.append((self.f.ticks, address, value))
        # The fixture implements GPIO BSRR's hardware effect, not control logic.
        for gpio in (GPIOB, GPIOD, GPIOE):
            if address == gpio + 0x18:
                odr = self.read(gpio + 0x14)
                self.write(gpio + 0x14, (odr | (value & 0xFFFF)) & ~(value >> 16))

    def directions(self):
        return ((self.read(GPIOD + 0x14) >> 6) & 3,
                (self.read(GPIOE + 0x14) >> 5) & 3)

    def assert_stopped(self):
        self.assertEqual(self.read(TIM4 + 0x34), 0)
        self.assertEqual(self.read(TIM4 + 0x38), 0)
        self.assertEqual(self.read(TIM4 + 0x20) & 0x11, 0)
        self.assertEqual(self.directions(), (0, 0))

    def tick(self, milliseconds=10):
        for _ in range(milliseconds):
            self.f.ticks += 1
            self.f.word("s_ticks", value=self.f.ticks - 1)
            self.f.call("SysTick_Handler")

    def hold(self, frames=110, **values):
        # This shared edge fixture invokes control once per 20 ms frame,
        # whereas physical SysTick runs it twice. Allow 100 profile steps
        # for a 0 -> 1000 ramp, and 220 for a settled full reversal.
        for _ in range(frames):
            self.frame(**values)

    def test_boot_configures_10khz_but_never_energizes_an_output(self):
        self.assertEqual(self.read(RCC + 0x40) & 4, 4)
        self.assertEqual(self.read(TIM4 + 0x28), 0)
        self.assertEqual(self.read(TIM4 + 0x2C), 1599)
        self.assertEqual((self.read(GPIOB) >> 12) & 15, 10)
        self.assertEqual((self.read(GPIOB + 0x20) >> 24) & 0xFF, 0x22)
        self.assertEqual(self.read(RCC + 0x38), 0, "LCD/FSMC must stay disabled")
        self.assert_stopped()
        self.assertEqual(self.publish()["fw"], VERSION)
        self.assertEqual(self.publish()["motor"], "1")

    def test_high_ch5_at_boot_and_non_neutral_sticks_never_drive(self):
        self.hold(frames=70, ch5=2000)
        self.assertEqual(self.publish()["state"], "LOCKED")
        self.assert_stopped()
        self.hold(ch1=2000, ch5=1000)
        self.hold(ch1=2000, ch5=2000)
        self.assert_stopped()

    def test_early_unlock_cannot_finish_neutral_hold_in_high_position(self):
        self.hold(frames=10, ch5=1000)
        self.hold(frames=70, ch5=2000)
        self.assertEqual(self.publish()["state"], "LOCKED")
        self.assert_stopped()
        self.arm()

    def test_forward_reverse_left_right_and_center_reach_pwm_pins(self):
        self.arm()
        for ch1, ch2, direction, signed in (
            (1500, 2000, (1, 1), (str(LIMIT), str(LIMIT))),
            (1500, 1000, (2, 2), (str(-LIMIT), str(-LIMIT))),
            (1000, 1500, (2, 1), (str(-LIMIT), str(LIMIT))),
            (2000, 1500, (1, 2), (str(LIMIT), str(-LIMIT))),
            (1500, 1500, (0, 0), ("0", "0")),
        ):
            with self.subTest(ch1=ch1, ch2=ch2):
                self.hold(frames=220, ch1=ch1, ch2=ch2, ch5=2000)
                self.assertEqual(self.directions(), direction)
                expected = 0 if direction == (0, 0) else COMPARE
                self.assertEqual(self.read(TIM4 + 0x34), expected)
                self.assertEqual(self.read(TIM4 + 0x38), expected)
                data = self.publish()
                self.assertEqual((data["left"], data["right"]), signed)
                self.assertEqual(data["motor"], "1")

    def test_high_pulse_is_clamped_to_variant_limit(self):
        self.arm()
        self.hold(frames=120, ch1=2200, ch2=2200, ch5=2000)
        self.assertLessEqual(self.read(TIM4 + 0x34), COMPARE)
        self.assertLessEqual(self.read(TIM4 + 0x38), COMPARE)
        self.assertEqual(self.read(TIM4 + 0x34), COMPARE)

    def test_stick_displacement_increases_output_to_full_pwm(self):
        self.arm()
        for pulse, output, compare in STICK_CASES:
            with self.subTest(pulse=pulse):
                self.hold(frames=55, ch1=1500, ch2=pulse, ch5=2000)
                self.assertEqual(self.read(TIM4 + 0x34), compare)
                self.assertEqual(self.read(TIM4 + 0x38), compare)
                data = self.publish()
                self.assertEqual((data["left"], data["right"]), (output, output))

    def test_output_module_clamps_over_range_requests_to_full_pwm(self):
        self.f.call("MotorWeb_Apply", 2000, (-2000) & 0xFFFFFFFF)
        self.assertEqual((self.read(TIM4 + 0x34), self.read(TIM4 + 0x38)), (COMPARE, COMPARE))
        self.assertEqual(self.directions(), (1, 2))
        self.f.call("MotorWeb_Stop")
        self.assert_stopped()

    def test_middle_and_low_switch_stop_without_waiting_for_ramp(self):
        for width in (1500, 1000):
            with self.subTest(ch5=width):
                self.arm()
                self.hold(ch2=2000, ch5=2000)
                self.frame(ch2=2000, ch5=width)
                self.assert_stopped()
                self.assertEqual(self.publish()["state"], "LOCKED")

    def test_timer_interrupt_stops_stale_pwm_with_no_network_foreground(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.tick(115)
        self.assert_stopped()
        self.assertEqual(self.publish()["state"], "FAILSAFE")
        self.hold(frames=10, ch5=2000)
        self.assertEqual(self.publish()["state"], "FAILSAFE")
        self.arm()

    def test_control_interrupt_alone_arms_and_drives_without_any_wifi_service(self):
        def interrupt_frame(ch2, ch5):
            self.counter += 20000
            for channel, width in ((1, 1500), (2, ch2), (5, ch5)):
                self.edge(channel, self.counter, True)
                self.edge(channel, self.counter + width, False)
            self.tick(20)
        for _ in range(60):
            interrupt_frame(1500, 1000)
        for _ in range(3):
            interrupt_frame(1500, 2000)
        for _ in range(60):
            interrupt_frame(2000, 2000)
        self.assertEqual(self.directions(), (1, 1))
        self.assertEqual((self.read(TIM4 + 0x34), self.read(TIM4 + 0x38)), (COMPARE, COMPARE))
        interrupt_frame(2000, 1000)
        self.assert_stopped()

    def test_short_or_overcaptured_pwm_cannot_keep_driving(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.hold(frames=4, ch5=2000, period=3000)
        self.assert_stopped()
        self.assertEqual(self.publish()["state"], "FAILSAFE")
        self.arm()
        self.edge(1, self.counter + 20000, True, overcapture=True)
        self.f.call("RcWeb_Service", self.f.ticks)
        self.assert_stopped()

    def test_network_foreground_does_not_run_a_second_control_owner(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.f.ready()
        self.trace.clear()
        self.f.call("service")
        self.assertEqual(self.trace, [], "Foreground must not rewrite motor registers")
        self.f.ticks += 115
        self.f.call("service")
        self.assertEqual(self.trace, [])
        self.tick(10)
        self.assert_stopped()

    def test_mqtt_connection_wait_leaves_control_to_the_timer_interrupt(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.f.ready()
        self.f.mqtt_connected_delay = 350
        self.trace.clear()
        self.assertEqual(self.f.call("setup_mqtt"), 1)
        self.assertEqual(self.trace, [])
        self.assertEqual(self.directions(), (1, 1))
        self.tick(10)
        self.assert_stopped()

    def test_snapshot_reads_do_not_advance_control_or_motor_outputs(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.trace.clear()
        data = self.publish()
        self.assertEqual(self.trace, [])
        fields = host.parse_heartbeat(self.f.published[-1][1])
        telemetry = host.rc_telemetry(fields)
        self.assertFalse(telemetry["simulation"])
        self.assertTrue(telemetry["motorEnabled"])
        self.assertEqual(data["fw"], VERSION)
        self.assertEqual(telemetry["outputLimit"], LIMIT / 10)

    def test_reversal_passes_through_zero_for_at_least_50ms(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        braked_at = None
        reversed_at = None
        # Full output needs 100 decrement steps plus the zero hold before
        # the first reverse step in this once-per-frame control fixture.
        for _ in range(120):
            self.frame(ch2=1000, ch5=2000)
            if self.read(TIM4 + 0x34) == 0 and braked_at is None:
                braked_at = self.f.ticks
                self.assert_stopped()
            if self.directions() == (2, 2):
                reversed_at = self.f.ticks
                break
        self.assertIsNotNone(braked_at)
        self.assertIsNotNone(reversed_at)
        self.assertGreaterEqual(reversed_at - braked_at, 50)

    def test_neutral_during_reversal_cannot_shorten_zero_hold(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        # A Board_Millis read consumes no wall time on hardware. Disable the
        # harness's AT-wait clock increments for exact 20/40/60 ms assertions.
        self.f.clock_step = 0
        for _ in range(110):
            self.frame(ch2=1000, ch5=2000)
            if self.read(TIM4 + 0x34) == 0:
                break
        self.assert_stopped()
        self.frame(ch2=1500, ch5=2000)
        self.frame(ch2=1000, ch5=2000)  # 40 ms after reaching zero.
        self.assert_stopped()
        self.frame(ch2=1000, ch5=2000)  # 60 ms after reaching zero.
        self.assertEqual(self.directions(), (2, 2))
        self.assertGreater(self.read(TIM4 + 0x34), 0)

    def test_direction_is_changed_only_with_its_pwm_channel_disabled(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.trace.clear()
        self.hold(frames=220, ch2=1000, ch5=2000)
        enabled = 0x11
        for _, address, value in self.trace:
            if address == TIM4 + 0x20:
                enabled = value
            elif address in (GPIOD + 0x18, GPIOE + 0x18):
                mask = 1 if address == GPIOD + 0x18 else 0x10
                self.assertEqual(enabled & mask, 0, "Direction changed under live PWM")

    def test_watchdog_is_configured_and_refreshed_by_control_tick(self):
        self.assertEqual(self.read(IWDG + 4), 4)  # LSI / 64
        self.assertEqual(self.read(IWDG + 8), 249)
        self.write(IWDG, 0)
        self.tick(20)
        self.assertEqual(self.read(IWDG), 0xAAAA)

    def test_emergency_stop_disables_pwm_before_direction_outputs(self):
        self.arm()
        self.hold(ch2=2000, ch5=2000)
        self.f.call("MotorWeb_Stop")
        self.assert_stopped()

    def test_ch5_timer_wrap_and_snapshot_timestamp_are_still_valid(self):
        self.counter = 45000
        self.arm()
        previous_time = self.f.ticks
        self.counter += 20000
        for channel, width in ((1, 1500), (2, 1500), (5, 2000)):
            self.edge(channel, self.counter, True)
            self.edge(channel, self.counter + width, False)
        self.f.call("RcWeb_Service", previous_time)
        self.assertEqual(self.publish()["state"], "ARMED")


if __name__ == "__main__":
    result = unittest.TextTestRunner(verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(MotorWebTests))
    report = dict(state="PASS" if result.wasSuccessful() else "FAIL",
                  tests_run=result.testsRun, failures=len(result.failures),
                  errors=len(result.errors), output_limit_percent=LIMIT / 10,
                  firmware=VERSION, physical_motor_verified=False)
    variant = os.environ.get("F407_MOTOR_VARIANT", "100")
    filename = "motor_web_test.json" if variant == "100" else f"motor_web_{variant}pct_test.json"
    (ROOT / "Tools" / filename).write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    raise SystemExit(not result.wasSuccessful())
