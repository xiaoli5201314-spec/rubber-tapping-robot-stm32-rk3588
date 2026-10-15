"""Run the actual PowerShell launchers on spare ports; stop only our own services."""
import json
import os
import socket
import subprocess
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def unused_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def script(name, http_port, mqtt_port):
    with tempfile.TemporaryFile() as output:
        result = subprocess.run([
            "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
            str(ROOT / name), "-HttpPort", str(http_port), "-MqttPort", str(mqtt_port),
        ] + (["-NoBrowser"] if name == "start.ps1" else []),
            stdin=subprocess.DEVNULL, stdout=output, stderr=output, timeout=45)
        output.seek(0)
        result.stdout, result.stderr = output.read(), b""
        return result


@unittest.skipUnless(os.name == "nt", "Windows launcher")
class LauncherTests(unittest.TestCase):
    def test_bomless_utf8_manifest_is_usable_with_chinese_project_paths(self):
        """PowerShell 7's UTF-8 manifest must also work with Windows PowerShell."""
        http_port, mqtt_port = unused_port(), unused_port()
        state_path = ROOT / "runtime" / f"state-{http_port}-{mqtt_port}.json"
        try:
            started = script("start.ps1", http_port, mqtt_port)
            self.assertEqual(started.returncode, 0, started.stdout + started.stderr)
            with urllib.request.urlopen(f"http://127.0.0.1:{http_port}/health", timeout=3) as response:
                original_health = json.load(response)
            manifest = json.loads(state_path.read_text(encoding="utf-8-sig"))
            self.assertTrue(any(not character.isascii() for character in manifest["processes"][0]["marker"]))
            state_path.write_text(json.dumps(manifest, ensure_ascii=False), encoding="utf-8")
            original_manifest = state_path.read_bytes()

            repeated = script("start.ps1", http_port, mqtt_port)
            self.assertEqual(repeated.returncode, 0, repeated.stdout + repeated.stderr)
            self.assertEqual(state_path.read_bytes(), original_manifest)
            with urllib.request.urlopen(f"http://127.0.0.1:{http_port}/health", timeout=3) as response:
                self.assertEqual(json.load(response), original_health, "Repeat start replaced the running host")

            stopped = script("stop.ps1", http_port, mqtt_port)
            self.assertEqual(stopped.returncode, 0, stopped.stdout + stopped.stderr)
            self.assertFalse(state_path.exists(), "Stop could not recognize its own UTF-8 manifest")
            for port in [http_port, mqtt_port]:
                with socket.socket() as probe:
                    probe.settimeout(1)
                    self.assertNotEqual(probe.connect_ex(("127.0.0.1", port)), 0)
        finally:
            if state_path.exists():
                # Keep cleanup working even against the old ANSI-reading scripts.
                state_path.write_text(state_path.read_text(encoding="utf-8-sig"), encoding="utf-8-sig")
            script("stop.ps1", http_port, mqtt_port)

    def test_failed_restart_preserves_a_preexisting_ownership_manifest(self):
        self.assertTrue((ROOT / "start.ps1").is_file(), "The one-click launcher is not implemented")
        http_port, mqtt_port = unused_port(), unused_port()
        state_path = ROOT / "runtime" / f"state-{http_port}-{mqtt_port}.json"
        state_path.parent.mkdir(exist_ok=True)
        original = b'{"application":"f407-web-link-host","processes":[{"id":999999}]}'
        state_path.write_bytes(original)
        try:
            result = script("start.ps1", http_port, mqtt_port)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(state_path.exists(), "Failure discarded ownership of a previous process")
            self.assertEqual(state_path.read_bytes(), original)
        finally:
            state_path.unlink(missing_ok=True)

    def test_real_start_serves_waiting_state_and_stop_releases_owned_ports(self):
        self.assertTrue((ROOT / "start.ps1").is_file(), "The one-click launcher is not implemented")
        self.assertTrue((ROOT / "stop.ps1").is_file(), "The ownership-aware stop script is not implemented")
        http_port, mqtt_port = unused_port(), unused_port()
        try:
            result = script("start.ps1", http_port, mqtt_port)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            with urllib.request.urlopen(f"http://127.0.0.1:{http_port}/api/status", timeout=3) as response:
                status = json.load(response)
            self.assertFalse(status["devices"]["stm32"]["link"])
            self.assertTrue(status["brokerConnected"])
            state_path = ROOT / "runtime" / f"state-{http_port}-{mqtt_port}.json"
            manifest = json.loads(state_path.read_text(encoding="utf-8-sig"))
            self.assertEqual({item["kind"] for item in manifest["processes"]}, {"server", "broker"})
            self.assertTrue(all(item["marker"] and item["startedTicks"] for item in manifest["processes"]))
            stopped = script("stop.ps1", http_port, mqtt_port)
            self.assertEqual(stopped.returncode, 0, stopped.stdout + stopped.stderr)
            self.assertFalse(state_path.exists())
            for port in [http_port, mqtt_port]:
                with socket.socket() as probe:
                    probe.settimeout(1)
                    self.assertNotEqual(probe.connect_ex(("127.0.0.1", port)), 0)
        finally:
            script("stop.ps1", http_port, mqtt_port)

    def test_http_port_conflict_preserves_the_external_listener(self):
        self.assertTrue((ROOT / "start.ps1").is_file(), "The one-click launcher is not implemented")
        mqtt_port = unused_port()
        with socket.socket() as external:
            external.bind(("0.0.0.0", 0))
            external.listen()
            http_port = external.getsockname()[1]
            result = script("start.ps1", http_port, mqtt_port)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(external.getsockname()[1], http_port)
            with socket.socket() as probe:
                probe.settimeout(1)
                self.assertEqual(probe.connect_ex(("127.0.0.1", http_port)), 0)
            self.assertFalse((ROOT / "runtime" / f"state-{http_port}-{mqtt_port}.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
