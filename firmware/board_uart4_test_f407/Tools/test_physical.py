"""Test the actual downloaded UART4 firmware through its CH340 serial port."""
import argparse
import json
import secrets
import time
from datetime import datetime
from pathlib import Path

import serial

ROOT = Path(__file__).resolve().parent.parent
ERROR_KEYS = ("uarterr", "overflow", "lineerr", "txtimeout")


def timestamp():
    return datetime.now().astimezone().isoformat(timespec="seconds")


def counters(line):
    prefix, *parts = line.split()
    assert prefix in ("UART4", "STATUS")
    fields = dict(part.split("=", 1) for part in parts)
    return {key: int(value) for key, value in fields.items()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM9")
    options = parser.parse_args()
    report = {
        "state": "RUNNING",
        "physical_serial_verified": False,
        "started": timestamp(),
        "port": options.port,
        "baud": 115200,
        "checks": [],
    }
    traffic = []
    sent_after_baseline = []
    serial_port = None
    partial = bytearray()

    def read_line(deadline):
        while time.monotonic() < deadline:
            byte = serial_port.read(1)
            if not byte:
                continue
            if byte == b"\n":
                raw = bytes(partial)
                partial.clear()
                text = raw.rstrip(b"\r").decode("ascii", errors="strict")
                traffic.append({"at": timestamp(), "direction": "RX", "text": text})
                if text:
                    return text
            else:
                partial.extend(byte)
                assert len(partial) < 4096, "Unframed serial output"
        raise TimeoutError("No expected serial reply before deadline")

    def wait_for(predicate, timeout=3):
        deadline = time.monotonic() + timeout
        while True:
            line = read_line(deadline)
            if line.startswith("ERROR:"):
                raise AssertionError(line)
            if predicate(line):
                return line

    def transmit(data, track=True):
        traffic.append({"at": timestamp(), "direction": "TX",
                        "text": data.decode("ascii")})
        assert serial_port.write(data) == len(data)
        serial_port.flush()
        if track:
            sent_after_baseline.append(data)

    def echo(payload, ending=b"\r\n"):
        transmit(payload + ending)
        expected = b"ECHO: " + payload
        reply = wait_for(lambda line: line.startswith("ECHO: "))
        assert reply.encode("ascii") == expected, "Echo content mismatch"

    try:
        # Prepare control signals before opening; no hardware flow control.
        serial_port = serial.Serial(port=None, baudrate=115200, timeout=0.2,
                                    write_timeout=2, rtscts=False, dsrdtr=False)
        serial_port.dtr = False
        serial_port.rts = False
        serial_port.port = options.port
        serial_port.open()
        first = counters(wait_for(lambda line: line.startswith("UART4 ")))
        second = counters(wait_for(lambda line: line.startswith("UART4 ")))
        assert second["tick"] > first["tick"]
        assert second["uptime_ms"] > first["uptime_ms"]
        report["checks"].append("Received advancing heartbeat and uptime from the physical board")

        # No ending byte here: idle framing gives an unambiguous RX snapshot.
        transmit(b"STATUS", track=False)
        baseline = counters(wait_for(lambda line: line.startswith("STATUS ")))
        assert all(baseline[key] == 0 for key in ERROR_KEYS), "Existing communication errors"
        report["baseline"] = baseline

        transmit(b"PING\r\n")
        assert wait_for(lambda line: line == "PONG") == "PONG"
        report["checks"].append("PING command returned PONG through STM32")

        challenge = ("uart4_" + secrets.token_hex(16)).encode("ascii")
        echo(challenge)
        report["random_challenge"] = challenge.decode("ascii")
        report["checks"].append("Fresh random challenge echoed byte for byte")

        echo(("idle_" + secrets.token_hex(12)).encode("ascii"), ending=b"")
        report["checks"].append("Sending text without CR/LF returned the correct idle-framed echo")

        payload = ("long_" + secrets.token_hex(93)).encode("ascii")
        assert len(payload) == 191
        echo(payload)
        report["checks"].append("Maximum 191-byte text frame echoed without truncation")

        for size in (1, 8, 16, 32, 64, 128):
            payload = secrets.token_hex(64).encode("ascii")[:size]
            echo(payload, ending=b"\n")
        report["checks"].append("Six repeated random frames of different lengths all matched")

        transmit(b"STATUS")
        final = counters(wait_for(lambda line: line.startswith("STATUS ")))
        expected_rx = sum(map(len, sent_after_baseline))
        expected_messages = len(sent_after_baseline)
        assert final["rx"] - baseline["rx"] == expected_rx, "Received byte count mismatch"
        assert final["messages"] - baseline["messages"] == expected_messages, \
            "Processed message count mismatch"
        assert all(final[key] == 0 for key in ERROR_KEYS), "Serial test recorded errors"
        report["final"] = final
        report["test_rx_bytes"] = expected_rx
        report["test_messages"] = expected_messages
        report["checks"].append("Physical RX/message counters matched every transmitted byte and frame")
        report["checks"].append("UART, buffer, input-frame and transmit-timeout error counts all remained zero")
        report["state"] = "PASS"
        report["physical_serial_verified"] = True
    except Exception as error:
        report["state"] = "FAIL"
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        if serial_port is not None and serial_port.is_open:
            serial_port.close()
        report["finished"] = timestamp()
        (ROOT / "Tools/physical_test.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        (ROOT / "Tools/physical_traffic.json").write_text(
            json.dumps(traffic, ensure_ascii=False, indent=2), encoding="utf-8"
        )
    print(json.dumps(report, ensure_ascii=False, indent=2))
    if report["state"] != "PASS":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
