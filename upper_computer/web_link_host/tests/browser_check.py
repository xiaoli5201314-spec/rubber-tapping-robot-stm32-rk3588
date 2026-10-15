"""Headless UI checks against controlled API fixtures, never physical evidence."""
import functools
import json
import tempfile
import threading
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from time import time

from playwright.sync_api import sync_playwright

WEB = Path(__file__).resolve().parents[2] / "web_prototype"
OUTPUT = WEB / "preview" / "web_link"
DEVICE = "A1B2C3D4E5F60718293A4B5C"
checks = []
api = {"online": False, "stale": False, "available": True, "pending": False, "success": False, "posts": 0, "event": 1, "rc": None}


def status():
    now = int(time() * 1000)
    telemetry = {"deviceId": DEVICE, "uptime": 18, "seq": 19, "runled": False,
                 "cmdcount": 1, "rxerr": 0, "parseerr": 0, "mqttVerified": True,
                 "firmware": "mqttweb1"}
    if api["rc"]:
        telemetry.update(api["rc"])
    return {
        "mode": "live", "instanceId": "browser-fixture", "nowMs": now, "brokerConnected": True,
        "devices": {
            "service": {"link": True, "ip": "127.0.0.1", "lastSeen": now, "telemetry": {"brokerConnected": True}},
            "stm32": {"link": api["online"], "ip": "192.168.137.21" if api["online"] or api["stale"] else "",
                      "lastSeen": now - 6001 if api["stale"] else now if api["online"] else None,
                      "telemetry": telemetry if api["online"] or api["stale"] else {}},
            "rk3588": {"link": False, "ip": "", "lastSeen": None, "telemetry": {}}
        },
        "selfTest": {"status": "success" if api["success"] else "pending" if api["pending"] else "idle",
                     "requestId": "a" * 32 if api["pending"] or api["success"] else "",
                     "message": "收到本次 LED 自检应答" if api["success"] else "等待本次 LED 应答" if api["pending"] else "",
                     "sentAt": now if api["pending"] else None, "completedAt": now if api["success"] else None},
        "events": [{"id": api["event"], "time": now, "type": "status", "message": "受控模拟事件"}]
    }


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api/status":
            if not api["available"]:
                self.send_error(503)
                return
            self.json_response(status())
            return
        super().do_GET()

    def do_POST(self):
        assert self.path == "/api/self-test"
        assert self.rfile.read(int(self.headers["Content-Length"])) == b"{}"
        api["posts"] += 1
        api["pending"] = True
        self.json_response(status()["selfTest"], 202)

    def json_response(self, content, code=200):
        data = json.dumps(content).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *_):
        pass


def check(name, condition):
    checks.append({"name": name, "passed": bool(condition)})
    assert condition, name


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Handler, directory=str(WEB)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    url = f"http://127.0.0.1:{server.server_port}"
    errors = []
    try:
        with sync_playwright() as playwright:
            browser = playwright.chromium.launch(executable_path=r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe", headless=True)
            page = browser.new_page(viewport={"width": 1440, "height": 1050}, accept_downloads=True)
            page.on("pageerror", lambda error: errors.append(str(error)))
            page.goto(url)
            page.wait_for_function("document.querySelector('#preview-label').textContent === '实时连接'", timeout=5000)
            check("API已连接，STM32尚无心跳保持等待", "等待连接" in page.locator("[data-device=stm32] [data-status]").inner_text())
            check("离线LED自检禁用", page.locator("#led-self-test").is_disabled())
            page.screenshot(path=str(OUTPUT / "desktop_waiting.png"), full_page=True)
            api["online"] = True
            page.wait_for_function("document.querySelector('[data-device=stm32] [data-status]').textContent === '在线'")
            check("真实心跳显示UID和序号", page.locator("#board-uid").inner_text() == DEVICE and page.locator("#board-seq").inner_text() == "19")
            check("MQTT双向握手证据可见", page.locator("#board-mqtt").inner_text() == "已验证")
            check("纯通信固件不虚报PWM上限", "最高" not in page.locator("#mode-note").inner_text()
                  and "最高" not in page.locator("#board-test-note").inner_text())
            check("纯通信固件说明用途", "通信" in page.locator("#board-test-note").inner_text())
            check("HTTP直连设置已移除", page.locator("[data-page=settings]").count() == 0)
            check("HTTP客户端不加载", page.locator("script[src*=http-client]").count() == 0)
            check("在线板子允许LED自检", page.locator("#led-self-test").is_enabled())
            check("RK3588保持等待", "等待连接" in page.locator("[data-device=rk3588] [data-status]").inner_text())
            page.screenshot(path=str(OUTPUT / "desktop_simulated_online.png"), full_page=True)
            page.locator("#led-self-test").click()
            page.wait_for_function("document.querySelector('#self-test-result').textContent.includes('等待')")
            check("自检只发送一次空JSON请求", api["posts"] == 1)
            check("等待ACK时按钮禁用", page.locator("#led-self-test").is_disabled())
            api["pending"], api["success"] = False, True
            page.wait_for_function("document.querySelector('#self-test-result').textContent.includes('应答')")
            check("匹配结果反馈可见", "应答" in page.locator("#self-test-result").inner_text())
            page.locator(".nav-link[data-page=chassis]").click()
            check("真实测试固件缺失运动数据留空", page.locator("[data-reading=left]").inner_text() == "—" and page.locator("[data-reading=ch1]").inner_text() == "—")
            check("底盘仍无运动控制按钮", page.locator("#page-chassis button").count() == 0)
            api["rc"] = {
                "firmware": "mqttrc1", "simulation": True, "motorEnabled": False, "outputLimit": 30,
                "safetyState": "ARMED", "locked": False, "rcValid": True,
                "ch1": 2000, "ch2": 1500, "ch5": 2000,
                "ch1Valid": True, "ch2Valid": True, "ch5Valid": True,
                "ch1Period": 20, "ch2Period": 20, "ch5Period": 20,
                "left": 30, "right": -30, "direction": "右转", "fault": "无故障",
            }
            page.wait_for_function("document.querySelector('[data-reading=ch1]').textContent === '2000'")
            check("遥控测试突出电机禁用", "电机禁用" in page.locator("#monitor-mode-title").inner_text())
            check("原始PWM与周期可见", page.locator("[data-reading=ch2]").inner_text() == "1500" and
                  page.locator("[data-reading=ch1-quality]").inner_text() == "20.0 ms · 有效")
            check("左右模拟指令与右转符号一致", page.locator("[data-reading=left]").inner_text() == "30" and
                  page.locator("[data-reading=right]").inner_text() == "-30" and
                  page.locator("[data-reading=direction]").inner_text() == "右转")
            check("模拟输出不冒充实际运动", "模拟" in page.locator("#motor-reading-title").inner_text() and
                  "模拟" in page.locator("#motion-kind").inner_text() and
                  "电机禁用" in page.locator("[data-motor-direction=right]").inner_text())
            check("RC测试保留只读监视", page.locator("#page-chassis button").count() == 0)
            page.screenshot(path=str(OUTPUT / "desktop_rc_web_fixture.png"), full_page=True)
            page.set_viewport_size({"width": 390, "height": 980})
            check("手机RC测试无横向溢出", page.evaluate("document.documentElement.scrollWidth <= innerWidth"))
            page.screenshot(path=str(OUTPUT / "mobile_rc_web_fixture.png"), full_page=True)
            page.set_viewport_size({"width": 1440, "height": 1050})
            api["rc"].update(firmware="mqttmotor30", simulation=False, motorEnabled=True,
                             left=29.6, right=-30)
            page.wait_for_function("document.querySelector('#monitor-mode-title').textContent.includes('电机输出已启用')")
            check("电机实测明确标记已启用和30%上限", "最高 30%" in page.locator("#mode-note").inner_text()
                  and page.locator("#monitor-mode-tag").inner_text() == "实物输出")
            check("电机指令不冒充实测车速或实测运动", "非实测运动" in page.locator("#motion-description").inner_text()
                  and "不代表实测车速" in page.locator("#motor-reading-description").inner_text())
            check("电机实测仍是只读监视", page.locator("#page-chassis button").count() == 0)
            check("实测输出保留不等幅右转读数", page.locator("[data-reading=left]").inner_text() == "29.6"
                  and page.locator("[data-reading=right]").inner_text() == "-30"
                  and page.locator("[data-reading=direction]").inner_text() == "右转")
            check("实际输出不再带有模拟禁用标签", "模拟" not in page.locator("#motor-reading-title").inner_text()
                  and "禁用" not in page.locator("[data-motor-direction=right]").inner_text())
            page.screenshot(path=str(OUTPUT / "desktop_motor_web_fixture.png"), full_page=True)
            api["rc"].update(firmware="mqttmotor60", outputLimit=60, left=60, right=-60)
            page.wait_for_function("document.querySelector('[data-reading=left]').textContent === '60'")
            check("同一网页显示60%固件和上限", "最高 60%" in page.locator("#mode-note").inner_text()
                  and "±60%" in page.locator("#motor-reading-description").inner_text()
                  and "mqttmotor60" in page.locator("#board-test-note").inner_text()
                  and "最高 60%" in page.locator("#board-test-note").inner_text())
            check("60%输出保持实际比例且不会显示为满条", page.locator("[data-reading=right]").inner_text() == "-60"
                  and page.locator("[data-bar=left]").evaluate("el => el.style.width") == "60%"
                  and page.locator("[data-bar=right]").evaluate("el => el.style.width") == "60%")
            check("60%版本保留实物输出和只读监视", page.locator("#monitor-mode-tag").inner_text() == "实物输出"
                  and page.locator("#page-chassis button").count() == 0)
            page.screenshot(path=str(OUTPUT / "desktop_motor_web_60pct_fixture.png"), full_page=True)
            api["rc"].update(firmware="mqttmotor100", outputLimit=100, left=100, right=-100)
            page.wait_for_function("document.querySelector('[data-reading=left]').textContent === '100'")
            check("满输出固件显示100%上限", "最高 100%" in page.locator("#mode-note").inner_text()
                  and "±100%" in page.locator("#motor-reading-description").inner_text())
            check("满输出左右读数和进度条到100%", page.locator("[data-reading=right]").inner_text() == "-100"
                  and page.locator("[data-bar=left]").evaluate("el => el.style.width") == "100%"
                  and page.locator("[data-bar=right]").evaluate("el => el.style.width") == "100%")
            check("满输出保留正确转向和只读监视", page.locator("[data-reading=direction]").inner_text() == "右转"
                  and page.locator("#page-chassis button").count() == 0)
            check("满输出设备说明显示新版本和上限", "mqttmotor100" in page.locator("#board-test-note").inner_text()
                  and "最高 100%" in page.locator("#board-test-note").inner_text())
            page.screenshot(path=str(OUTPUT / "desktop_motor_web_full_fixture.png"), full_page=True)
            api["rc"].update(firmware="mqttrc1", simulation=True, motorEnabled=False,
                             outputLimit=30, left=30, right=-30)
            page.wait_for_function("document.querySelector('#monitor-mode-title').textContent.includes('电机禁用')")
            api["rc"].update(safetyState="FAILSAFE", locked=True, rcValid=False,
                             ch1=None, ch1Period=None, ch1Valid=False, left=0, right=0,
                             direction="静止", fault="CH1 无有效 PWM")
            page.wait_for_function("document.querySelector('[data-reading=locked]').textContent === '失效'")
            check("PWM失效清空旧脉宽与周期", page.locator("[data-reading=ch1]").inner_text() == "—" and
                  page.locator("[data-reading=ch1-quality]").inner_text() == "无有效信号")
            check("遥控失效仍区分联网正常", page.locator("[data-device=stm32] [data-status]").inner_text() == "在线" and
                  page.locator("[data-reading=left]").inner_text() == "0")
            api["rc"].update(safetyState="WAIT", ch1=1500, ch1Period=20, ch1Valid=True,
                             rcValid=True, fault="无故障")
            page.wait_for_function("document.querySelector('[data-reading=locked]').textContent === '等待解锁'")
            check("等待解锁保持零模拟指令", page.locator("[data-reading=right]").inner_text() == "0")
            api["rc"] = None
            page.wait_for_function("document.querySelector('[data-reading=ch1]').textContent === '—'")
            check("旧固件不残留遥控数据或模拟标签", page.locator("[data-reading=left]").inner_text() == "—" and
                  page.locator("#monitor-mode-tag").inner_text() == "只读监视")
            api["online"], api["stale"] = False, True
            page.locator(".nav-link[data-page=devices]").click()
            page.wait_for_function("document.querySelector('[data-device=stm32] [data-status]').textContent === '通信超时'")
            check("后端超时显示通信超时", page.locator("#led-self-test").is_disabled() and page.locator("#board-seq").inner_text() == "—")
            api["online"], api["stale"] = True, False
            page.wait_for_function("document.querySelector('[data-device=stm32] [data-status]').textContent === '在线'")
            page.locator(".nav-link[data-page=chassis]").click()
            page.locator("#example-toggle").click()
            check("示例明确标记", "示例" in page.locator("#mode-note").inner_text())
            check("演示显示PWM", page.locator("[data-reading=left]").inner_text() == "18")
            page.locator(".nav-link[data-page=devices]").click()
            check("示例不可发送真实自检", page.locator("#led-self-test").is_disabled())
            page.locator("#example-toggle").click()
            page.wait_for_function("document.querySelector('[data-device=stm32] [data-status]').textContent === '在线'")
            check("退出示例恢复实时数据", page.locator("#board-seq").inner_text() == "19")
            page.locator(".nav-link[data-page=communications]").click()
            page.wait_for_timeout(1200)
            check("重复API事件只出现一次", page.locator("#logs-body").inner_text().count("受控模拟事件") == 1)
            with page.expect_download() as download:
                page.locator("#export-logs").click()
            with tempfile.TemporaryDirectory(prefix="web-link-export-") as folder:
                artifact = Path(folder) / "logs.json"
                download.value.save_as(artifact)
                exported = json.loads(artifact.read_text(encoding="utf-8"))
                check("实时导出使用当前真实通道证据", exported["mode"] == "live" and exported["realDevicesConnected"] is True)
                check("实时导出不混入示例事件", all(item["mode"] == "live" for item in exported["logs"]))
            page.locator("#clear-logs").click()
            page.wait_for_timeout(1200)
            check("清空后不因轮询重放旧事件", page.locator("#logs-body tr").count() == 0)
            api["available"] = False
            page.locator(".nav-link[data-page=devices]").click()
            page.wait_for_function("document.querySelector('[data-device=service] [data-status]').textContent === '离线'")
            check("API断开后板子不遗留在线", "离线" in page.locator("[data-device=stm32] [data-status]").inner_text())
            check("API断开禁用自检", page.locator("#led-self-test").is_disabled())
            api["available"], api["online"] = True, False
            page.reload()
            page.wait_for_function("document.querySelector('#preview-label').textContent === '实时连接'")
            check("浏览器刷新不缓存在线设备", "等待连接" in page.locator("[data-device=stm32] [data-status]").inner_text())
            for width in [390, 768, 1024, 1440]:
                page.set_viewport_size({"width": width, "height": 980})
                for name in ["devices", "chassis", "camera", "communications"]:
                    page.goto(url + "#" + name)
                    page.wait_for_selector(f"#page-{name}:visible")
                    check(f"{width}px {name}无横向溢出", page.evaluate("document.documentElement.scrollWidth <= innerWidth"))
                    if width == 390:
                        page.screenshot(path=str(OUTPUT / f"mobile_{name}.png"), full_page=True)
            check("无浏览器脚本错误", not errors)
            browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(2)
    report = {"scope": "SIMULATED_API_UI_TEST", "physicalBoardVerified": False, "passed": len(checks), "checks": checks, "errors": errors}
    (OUTPUT / "checks.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"scope": report["scope"], "physicalBoardVerified": False, "passed": len(checks)}))


if __name__ == "__main__":
    main()
