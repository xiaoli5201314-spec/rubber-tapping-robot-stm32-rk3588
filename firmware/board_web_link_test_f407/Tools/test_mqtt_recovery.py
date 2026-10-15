"""Run the actual chassis HEX through controlled ESP response scenarios.

Only the UART fixture is simulated; no physical board or Wi-Fi is operated.
"""
import os
import unittest

os.environ.setdefault("F407_PROTOCOL_TARGET", "board_chassis_web_f407_60pct")
from test_protocol import Firmware, NONCE, TOPIC, symbol


class MqttRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.f = Firmware()
        self.f.call("Board_Init")
        self.f.call("RcWeb_Init")
        self.f.ready()

    def service(self, milliseconds):
        self.f.ticks += milliseconds
        self.f.call("service")

    def defer_during_matching_status(self):
        f = self.f
        self.assertEqual(f.call("setup_mqtt"), 1)
        f.mqtt(b"SESSION:" + NONCE)
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n> ")
        self.service(1)
        self.assertIn(b";token=" + NONCE + b";", f.tx[-1])
        request = b"11112222333344445555666677778888"
        received = f.ticks
        f.mqtt(b"LEDTEST:" + request)
        self.service(1)
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        return request, received

    def test_status_publish_does_not_block_waiting_for_prompt(self):
        f = self.f
        f.auto_esp = False
        start = f.ticks
        f.call("publish_status")
        self.assertLess(f.ticks - start, 100)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)
        # Bare OK cannot send the raw payload or complete a publish.
        f.rx.extend(b"\r\nOK\r\n")
        self.service(100)
        self.assertFalse(any(part.startswith(b"fw=") for part in f.tx))
        f.rx.extend(b"> ")
        self.service(100)
        self.assertTrue(any(part.startswith(b"fw=mqttmotor60;") for part in f.tx))
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        self.service(2200)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)

    def test_delayed_publish_ack_does_not_reconnect_or_start_another_raw(self):
        f = self.f
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n> ")
        self.service(1)
        self.service(2500)
        f.call("publish_status")
        self.assertEqual(sum(p.startswith(b"AT+MQTTPUBRAW=") for p in f.tx), 1)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        self.service(1)
        f.call("publish_status")
        self.assertEqual(sum(p.startswith(b"AT+MQTTPUBRAW=") for p in f.tx), 2)

    def test_session_command_is_accepted_without_http_probe(self):
        f = self.f
        f.write_text("s_session_token", b"0" * 32)
        f.word("g_wifi_test_status", 24, 0)
        f.call("publish_status")
        self.service(1)
        self.service(1)
        f.mqtt(b"SESSION:" + NONCE)
        self.assertEqual(f.text(symbol("s_session_token")), NONCE)

    def test_busy_is_not_a_terminal_publish_reply(self):
        f = self.f
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nbusy p...\r\n")
        self.service(1)
        f.call("publish_status")
        self.assertEqual(sum(p.startswith(b"AT+MQTTPUBRAW=") for p in f.tx), 1)

    def test_prompt_timeout_fills_raw_length_and_quarantines_late_ack(self):
        f = self.f
        f.auto_esp = False
        f.call("publish_status")
        raw_length = int(f.tx[-2].split(b",")[-3])
        self.service(10050)
        self.assertEqual(f.tx[-1], b" " * raw_length)
        f.call("publish_status")
        self.assertEqual(sum(p.startswith(b"AT+MQTTPUBRAW=") for p in f.tx), 1)
        f.rx.extend(b"\r\n> \r\n+MQTTPUB:OK\r\n")
        self.service(1)
        self.service(300)
        # Old completion is drained. Recovery is permitted without an
        # additional 20-second blind delay after a terminal result.
        self.assertEqual(f.uc.mem_read(symbol("s_at_unsynced"), 1), b"\0")
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)

    def test_late_prompt_and_bare_ok_cannot_release_raw_recovery(self):
        f = self.f
        f.auto_esp = False
        f.call("publish_status")
        self.service(10050)
        transmissions = list(f.tx)
        f.rx.extend(b"\r\nOK\r\n> ")
        self.service(1)
        self.service(300)
        f.call("publish_status")
        self.assertEqual(f.call("setup_mqtt"), 0)
        self.assertEqual(f.tx, transmissions)
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)
        self.assertEqual(f.word("g_wifi_test_status", 24), 0)

        f.rx.extend(b"\r\n+MQTTPUB:FAIL\r\n")
        self.service(1)
        self.service(300)
        f.auto_esp = True
        self.assertEqual(f.call("setup_mqtt"), 1)
        f.mqtt(b"SESSION:" + NONCE)
        f.mqtt(b"LEDTEST:" + NONCE)
        self.service(1)
        self.assertEqual(f.leds[-1][2], 0)
        f.publish_status()
        f.mqtt(b"LEDTEST:" + NONCE)
        self.service(1)
        self.assertEqual(f.leds[-1][2], 1)

    def test_explicit_publish_errors_retry_then_require_reconnection(self):
        for failure in (b"ERROR", b"FAIL", b"SEND FAIL", b"+MQTTPUB:FAIL"):
            with self.subTest(failure=failure):
                self.setUp()
                f = self.f
                f.auto_esp = False
                for attempt in range(1, 4):
                    f.call("publish_status")
                    f.rx.extend(b"\r\nOK\r\n> ")
                    self.service(1)
                    f.rx.extend(b"\r\n" + failure + b"\r\n")
                    self.service(1)
                    self.assertEqual(
                        sum(part.startswith(b"AT+MQTTPUBRAW=") for part in f.tx),
                        attempt,
                    )
                    self.assertEqual(f.word("g_wifi_test_status", 28),
                                     int(attempt < 3))
                    f.mqtt(b"LEDTEST:" + NONCE)
                    self.service(1)
                    self.assertEqual(f.leds[-1][2], 0)
                self.assertEqual(f.word("g_wifi_test_status", 24), 0)
                self.assertEqual(f.word("g_wifi_test_status", 20), 1)
                f.auto_esp = True
                self.assertEqual(f.call("setup_mqtt"), 1)
                f.mqtt(b"SESSION:" + NONCE)
                f.publish_status()
                f.mqtt(b"LEDTEST:" + NONCE)
                self.service(1)
                self.assertEqual(f.leds[-1][2], 1)

    def test_esp_restart_abandons_raw_and_cancels_command_permissions(self):
        f = self.f
        f.publish_status()
        f.mqtt(b"LEDTEST:" + NONCE)
        self.service(1)
        self.assertEqual(f.leds[-1][2], 1)
        f.auto_esp = False
        f.call("publish_status")
        transmissions = list(f.tx)
        f.rx.extend(b"\r\nready\r\n")
        self.service(1)
        self.assertEqual(f.tx, transmissions)
        self.assertEqual(f.word("g_wifi_test_status", 20), 0)
        self.assertEqual(f.word("g_wifi_test_status", 28), 0)
        self.assertEqual(f.word("g_wifi_test_status", 24), 0)
        self.assertEqual(f.text(symbol("s_session_token")), b"0" * 32)
        self.assertEqual(f.leds[-1][2], 0)
        f.rx.extend(b"\r\n> \r\n+MQTTPUB:OK\r\n")
        self.service(300)
        f.mqtt(b"SESSION:" + NONCE)
        f.mqtt(b"LEDTEST:" + NONCE)
        f.call("send_echo")
        self.assertEqual(f.tx, transmissions)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        self.assertEqual(f.word("g_wifi_test_status", 36), 0)

    def test_failed_led_ack_retries_same_nonce_without_more_flashes(self):
        f = self.f
        f.publish_status()
        f.published.clear()
        f.mqtt(b"LEDTEST:" + NONCE)
        self.service(1)
        for unused in range(3):
            self.service(100)
        self.assertEqual(f.word("g_wifi_test_status", 64), 1)
        f.leds.clear()
        f.auto_esp = False
        f.call("send_echo")
        f.rx.extend(b"\r\nOK\r\n> ")
        self.service(1)
        f.rx.extend(b"\r\n+MQTTPUB:FAIL\r\n")
        self.service(1)
        self.assertEqual(f.word("g_wifi_test_status", 36), 0)
        self.assertEqual(f.word("g_wifi_test_status", 28), 1)
        self.service(1000)
        f.auto_esp = True
        f.call("send_echo")
        self.service(1)
        self.service(1)
        self.assertEqual([part for part in f.tx if part.startswith(b"LEDACK:")],
                         [b"LEDACK:" + NONCE + b":OK"] * 2)
        self.assertEqual(f.word("g_wifi_test_status", 64), 1)
        self.assertEqual(f.word("g_wifi_test_status", 36), 1)
        self.assertTrue(all(run == 0 for unused, error, run in f.leds))

    def test_deferred_request_is_canceled_by_a_changed_session(self):
        f = self.f
        self.defer_during_matching_status()
        fresh = b"fedcba9876543210fedcba9876543210"
        f.mqtt(b"SESSION:" + fresh)
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        self.service(1)
        self.assertEqual(f.leds[-1][2], 0)
        f.auto_esp = True
        f.publish_status()
        self.assertIn(b";token=" + fresh + b";", f.published[-1][1])
        self.service(300)
        f.call("send_echo")
        self.assertEqual(f.leds[-1][2], 0)
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        self.assertFalse(any(part.startswith(b"LEDACK:") for part in f.tx))
        f.mqtt(b"LEDTEST:" + fresh)
        self.service(1)
        self.assertEqual(f.leds[-1][2], 1)

    def test_deferred_request_is_canceled_by_explicit_raw_failure(self):
        for failure in (b"ERROR", b"FAIL", b"SEND FAIL", b"+MQTTPUB:FAIL"):
            with self.subTest(failure=failure):
                self.setUp()
                f = self.f
                self.defer_during_matching_status()
                f.rx.extend(b"\r\n" + failure + b"\r\n")
                self.service(1)
                f.auto_esp = True
                f.publish_status()
                self.service(300)
                f.call("send_echo")
                self.assertEqual(f.leds[-1][2], 0)
                self.assertEqual(f.word("g_wifi_test_status", 64), 0)
                self.assertFalse(any(part.startswith(b"LEDACK:") for part in f.tx))
                f.mqtt(b"LEDTEST:" + b"fedcba9876543210fedcba9876543210")
                self.service(1)
                self.assertEqual(f.leds[-1][2], 1)

    def test_deferred_request_is_canceled_by_disconnect_or_esp_restart(self):
        for event in (b"+MQTTDISCONNECTED:0", b"WIFI DISCONNECT", b"ready"):
            with self.subTest(event=event):
                self.setUp()
                f = self.f
                self.defer_during_matching_status()
                f.rx.extend(b"\r\n" + event + b"\r\n")
                self.service(1)
                self.assertEqual(f.word("g_wifi_test_status", 28), 0)
                self.assertEqual(f.leds[-1][2], 0)
                f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
                self.service(1)
                self.service(300)
                f.ready()
                self.assertEqual(f.call("setup_mqtt"), 1)
                f.mqtt(b"SESSION:" + NONCE)
                f.publish_status()
                self.service(300)
                f.call("send_echo")
                self.assertEqual(f.leds[-1][2], 0)
                self.assertEqual(f.word("g_wifi_test_status", 64), 0)
                self.assertFalse(any(part.startswith(b"LEDACK:") for part in f.tx))

    def test_deferred_request_expires_from_receipt_before_a_late_status_ack(self):
        f = self.f
        unused_request, received = self.defer_during_matching_status()
        f.clock_step = 0
        f.ticks = received + 5100
        f.rx.extend(b"\r\n+MQTTPUB:OK\r\n")
        self.service(0)
        self.assertEqual(f.leds[-1][2], 0)
        for unused in range(4):
            self.service(100)
        f.call("send_echo")
        self.assertEqual(f.word("g_wifi_test_status", 64), 0)
        self.assertFalse(any(part.startswith(b"LEDACK:") for part in f.tx))
        f.mqtt(b"LEDTEST:" + b"fedcba9876543210fedcba9876543210")
        self.service(1)
        self.assertEqual(f.leds[-1][2], 1)

    def test_bare_and_preprompt_ok_cannot_confirm_payload(self):
        f = self.f
        f.auto_esp = False
        f.call("publish_status")
        f.rx.extend(b"\r\nOK\r\n+MQTTPUB:OK\r\n")
        self.service(1)
        f.rx.extend(b"> ")
        self.service(1)
        f.call("publish_status")
        self.assertEqual(sum(p.startswith(b"AT+MQTTPUBRAW=") for p in f.tx), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
