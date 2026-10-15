(function () {
  "use strict";
  const $ = (selector) => document.querySelector(selector);
  const $$ = (selector) => [...document.querySelectorAll(selector)];
  const model = window.RobotStatus;
  let state = model.initial();
  let liveState = model.initial();
  let sending = false;
  let polling = false;
  let serviceInstance = "";
  let eventCursor = 0;
  let toastTimer;
  const logs = [];
  const pages = {
    devices: ["设备状态", "DEVICE STATUS", "先确认设备在线，再查看底盘和摄像头。"],
    chassis: ["底盘监视", "CHASSIS MONITOR", "遥控器负责驾驶，网页查看底盘反馈。"],
    camera: ["摄像头", "CAMERA VIEW", "预留机器人视角，后续接入RK3588摄像头画面。"],
    communications: ["通信记录", "COMMUNICATION LOG", "查看连接变化、状态超时与设备提示。"]
  };
  const labels = { waiting: "等待连接", pending: "等待反馈", online: "在线", timeout: "通信超时", offline: "离线" };
  const icons = {
    sprout: '<path d="M12 21v-8M12 16C5 17 3 12 3 7c6-1 9 2 9 7M12 13c0-7 3-10 9-10 0 6-3 10-9 10"/>',
    devices: '<rect x="3" y="3" width="8" height="8" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="13" y="13" width="8" height="8" rx="2"/>',
    robot: '<rect x="5" y="6" width="14" height="13" rx="3"/><path d="M12 3v3M3 10v6m18-6v6M9 12h.1M15 12h.1M9 16h6M8 19v2m8-2v2"/>',
    camera: '<path d="M4 7h4l2-3h4l2 3h4v13H4V7Z"/><circle cx="12" cy="13" r="3.5"/>',
    list: '<rect x="4" y="3" width="16" height="18" rx="2"/><path d="M8 7h8M8 12h8M8 17h6"/>',
    eye: '<path d="M2 12c5-9 15-9 20 0-5 9-15 9-20 0Z"/><circle cx="12" cy="12" r="3"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7h.01"/>',
    computer: '<rect x="3" y="3" width="18" height="13" rx="2"/><path d="M8 21h8M12 16v5M7 12h10"/>',
    cpu: '<rect x="6" y="6" width="12" height="12" rx="2"/><rect x="9" y="9" width="6" height="6" rx="1"/><path d="M9 2v4m6-4v4M9 18v4m6-4v4M2 9h4m-4 6h4m12-6h4m-4 6h4"/>',
    board: '<rect x="3" y="4" width="18" height="16" rx="2"/><rect x="7" y="8" width="6" height="7" rx="1"/><path d="M17 8v2m0 3v2M7 18h2m3 0h2"/>',
    wifi: '<path d="M3 8a14 14 0 0 1 18 0M6 12a9 9 0 0 1 12 0M9 16a4.5 4.5 0 0 1 6 0M12 20h.01"/>',
    pulse: '<path d="M2 12h4l3-8 6 16 3-8h4"/>',
    radio: '<circle cx="12" cy="12" r="2"/><path d="M8 8a5.7 5.7 0 0 0 0 8m8-8a5.7 5.7 0 0 1 0 8M5 5a10 10 0 0 0 0 14M19 5a10 10 0 0 1 0 14"/>',
    shield: '<path d="m12 3 8 3v6c0 5-5 8-8 10-3-2-8-5-8-10V6l8-3Z"/><path d="m8 12 3 3 5-6"/>',
    play: '<path d="m8 4 12 8-12 8V4Z"/>',
    stop: '<rect x="5" y="5" width="14" height="14" rx="2"/>',
    snapshot: '<rect x="3" y="5" width="18" height="15" rx="2"/><circle cx="12" cy="12" r="3"/><path d="M7 5V3m10 2V3"/>',
    trash: '<path d="M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7m4-7v7"/>',
    download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
    settings: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/>'
  };
  const escapeHtml = (value) => String(value).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[char]));
  const sources = { status: "设备状态", camera: "摄像头", system: "系统" };
  const timeText = (timestamp) => new Date(timestamp).toLocaleTimeString("zh-CN", { hour12: false });

  function toast(text) {
    clearTimeout(toastTimer);
    $("#toast").textContent = text;
    $("#toast").hidden = false;
    toastTimer = setTimeout(() => { $("#toast").hidden = true; }, 3000);
  }
  function renderLogs() {
    const filter = $("#log-filter").value;
    const selected = logs.filter((item) => item.mode === state.mode);
    const filtered = selected.filter((item) => filter === "all" || item.type === filter);
    $("#logs-body").innerHTML = filtered.map((item) =>
      `<tr><td>${timeText(item.time)}</td><td><span class="log-source">${sources[item.type]}</span></td><td>${escapeHtml(item.message)}</td></tr>`).join("");
    $("#logs-empty").hidden = filtered.length !== 0;
    $("#log-count").textContent = `${selected.length} 条`;
  }
  function log(type, message, mode = state.mode, time = new Date().toISOString()) {
    logs.unshift({ time, type, message, mode });
    if (logs.length > 300) logs.length = 300;
    renderLogs();
  }
  function render() {
    const now = Date.now();
    const example = state.mode === "example";
    const live = state.mode === "live";
    const telemetry = model.monitor(state, now);
    const simulation = telemetry?.simulation === true;
    const motorEnabled = telemetry?.motorEnabled === true;
    const outputLimit = telemetry?.outputLimit ?? 30;
    const boardOnline = model.status(state, "stm32", now) === "online";
    $("#preview-label").textContent = example ? "示例状态" : live ? "实时连接" : "界面预览";
    $("#mode-note").textContent = example
      ? "示例展示：IP、在线状态与底盘读数均为模拟，真实设备仍未接入。"
      : boardOnline ? motorEnabled ? `MQTT 联调：最高 ${outputLimit}% PWM；网页只监视，CH5 拨杆负责锁定。`
        : simulation ? "MQTT 遥控联调：电机禁用，网页只监视接收器输入和模拟指令。"
        : "MQTT 通信验证：双向握手已完成，可查看心跳并执行 LED 自检。"
      : live ? state.brokerConnected ? "MQTT 服务已连接，等待 STM32 的新心跳和双向握手。"
        : "MQTT 服务未连接，请检查电脑通信服务；恢复后自动更新。"
      : "请运行「启动网页联调.cmd」打开实时网页；当前为界面预览。";
    $("#mode-banner").classList.toggle("example", example);
    $("#example-toggle").setAttribute("aria-pressed", String(example));
    $("#example-label").textContent = example ? "退出示例状态" : "查看示例状态";
    $$("[data-device]").forEach((card) => {
      const id = card.dataset.device;
      const device = state.devices[id];
      const status = model.status(state, id, now);
      const online = status === "online";
      const statusElement = card.querySelector("[data-status]");
      statusElement.className = `device-status ${status}`;
      statusElement.textContent = `${labels[status]}${example ? " · 示例" : ""}`;
      card.querySelector("[data-ip]").textContent = device.ip || "—";
      card.querySelector("[data-seen]").textContent = device.lastSeen === null ? "未收到" : `${timeText(device.lastSeen)}${example ? " · 示例" : ""}`;
      card.querySelector("[data-detail]").textContent = id === "service" && live && online ? state.brokerConnected ? "MQTT 已连接" : "MQTT 未连接"
        : online ? "正常" : status === "waiting" ? "未收到" : labels[status];
      const linkElement = card.querySelector("[data-link]");
      if (linkElement) linkElement.textContent = device.link ? "已连接" : "未接入";
      const cameraElement = card.querySelector("[data-camera]");
      if (cameraElement) cameraElement.textContent = online && device.telemetry.camera === true ? "设备报告已就绪" : "未接入";
      const descriptions = {
        service: live && online ? state.brokerConnected ? "电脑通信服务与 MQTT broker 均已连接" : "电脑通信服务在线，MQTT broker 未连接"
          : "等待电脑通信服务",
        stm32: online ? "已收到STM32状态反馈" : status === "pending" ? "网络已连接，等待STM32反馈" : status === "timeout" ? "底盘状态超过5秒未更新" : "等待底盘主控发送状态",
        rk3588: online ? "已收到主控状态反馈" : status === "timeout" ? "主控状态超过5秒未更新" : "等待主控发送状态"
      };
      card.querySelector("[data-description]").textContent = `${descriptions[id]}${example ? "（示例）" : ""}`;
      card.querySelector(".device-footnote .dot").classList.toggle("neutral", !online);
      const summary = $(`[data-summary="${id}"]`);
      summary.textContent = `${labels[status]}${example ? " · 示例" : ""}`;
      summary.closest("article").querySelector(".dot").classList.toggle("neutral", !online);
    });
    const safetyLabels = { LOCKED: "锁定", WAIT: "等待解锁", ARMED: "就绪", FAILSAFE: "失效" };
    const values = {
      left: telemetry?.left ?? "—", right: telemetry?.right ?? "—",
      ch1: telemetry?.ch1 ?? "—", ch2: telemetry?.ch2 ?? "—", ch5: telemetry?.ch5 ?? "—",
      locked: telemetry?.safetyState ? safetyLabels[telemetry.safetyState] : telemetry?.locked === null || !telemetry ? "—" : telemetry.locked ? "已锁定" : "已解锁",
      direction: telemetry?.direction ?? "—",
      "rc-valid": !telemetry || telemetry.rcValid === null ? "—" : telemetry.rcValid ? "有效" : "无效",
      fault: telemetry?.fault ?? "等待 STM32 状态"
    };
    for (const channel of ["ch1", "ch2", "ch5"]) {
      const valid = telemetry?.[channel + "Valid"];
      const period = telemetry?.[channel + "Period"];
      values[channel + "-quality"] = valid === true && typeof period === "number" ? `${period.toFixed(1)} ms · 有效`
        : valid === false ? "无有效信号" : "等待 PWM";
      $(`[data-rc-channel="${channel}"]`).dataset.rcValid = typeof valid === "boolean" ? String(valid) : "";
    }
    $$("[data-reading]").forEach((element) => { element.textContent = values[element.dataset.reading]; });
    $("#monitor-mode-title").textContent = simulation ? "遥控器网页联调 · 电机禁用" : motorEnabled ? "底盘架空实测 · 电机输出已启用" : "航模遥控器控制底盘";
    $("#monitor-mode-detail").textContent = simulation ? "实测接收器输入；网页只监视，左右指令仅模拟。" : motorEnabled ? "遥控器本地驾驶；回中后 CH5 低保持 1 秒再拨高解锁。网页不发送运动指令。" : "网页只读取状态，不发送前进、转向或解锁指令。";
    $("#monitor-mode-tag").textContent = simulation ? "仅模拟" : motorEnabled ? "实物输出" : "只读监视";
    $("#motion-kind").textContent = simulation ? "模拟运动" : motorEnabled ? "运动指令" : "底盘运动";
    $("#motion-description").textContent = simulation ? "电机禁用，不代表实际运动" : motorEnabled ? "按左右输出判断，非实测运动" : "由 STM32 上报";
    $("#motor-reading-title").textContent = simulation ? "左右模拟指令" : motorEnabled ? "左右 PWM 输出指令" : "左右电机输出";
    $("#motor-reading-description").textContent = simulation ? "最高 ±30%；仅验证摇杆混控，实际电机输出关闭。" : motorEnabled ? `最高 ±${outputLimit}% PWM；实际输出已启用，不代表实测车速。` : "显示PWM占空比，不代表实测车速。";
    $$("[data-output-unit]").forEach((element) => { element.textContent = simulation ? "% 指令" : "% PWM"; });
    for (const side of ["left", "right"]) {
      const output = telemetry?.[side];
      $(`[data-bar="${side}"]`).style.width = `${typeof output === "number" ? Math.abs(output) : 0}%`;
      $(`[data-motor-direction="${side}"]`).textContent = typeof output !== "number" ? "等待状态" : output === 0 ? simulation ? "零指令 · 电机禁用" : "零输出" : output > 0 ? simulation ? "模拟正转 · 电机禁用" : "正转输出" : simulation ? "模拟反转 · 电机禁用" : "反转输出";
    }
    $("#monitor-freshness").textContent = example ? "示例反馈 · 本地模拟，不是实物数据"
      : simulation ? "实测遥控输入 · 左右仅模拟 · 约每秒刷新" : telemetry && telemetry.left === null && telemetry.right === null ? "通信测试固件未上报电机或遥控器数据" : telemetry ? "STM32 最新底盘反馈" : "未收到底盘状态，读数留空";
    const test = live && model.status(state, "stm32", now) === "online" ? state.devices.stm32.telemetry : {};
    $("#board-uid").textContent = test.deviceId || "—";
    $("#board-uptime").textContent = Number.isInteger(test.uptime) ? `${test.uptime} s` : "—";
    $("#board-seq").textContent = Number.isInteger(test.seq) ? test.seq : "—";
    $("#board-mqtt").textContent = typeof test.mqttVerified === "boolean" ? test.mqttVerified ? "已验证" : "等待握手"
      : test.deviceId ? "旧版固件" : "—";
    $("#board-runled").textContent = typeof test.runled === "boolean" ? test.runled ? "亮" : "灭" : "—";
    $("#board-cmdcount").textContent = Number.isInteger(test.cmdcount) ? test.cmdcount : "—";
    $("#board-errors").textContent = Number.isInteger(test.rxerr) && Number.isInteger(test.parseerr) ? `${test.rxerr} / ${test.parseerr}` : "—";
    $("#board-test-note").textContent = example ? "示例展示，无真实自检" : test.deviceId
      ? motorEnabled ? `${test.firmware} · MQTT 底盘固件 · 最高 ${outputLimit}%`
        : simulation ? `${test.firmware} · MQTT 遥控监视 · 电机禁用`
        : `${test.firmware} · MQTT 通信测试固件`
      : "等待 MQTT 固件状态";
    $("#led-self-test").disabled = sending || !model.canSelfTest(state, now);
    $("#self-test-result").textContent = example ? "示例模式不发送 LED 指令" : live && state.selfTest?.message ? state.selfTest.message : "尚未发起 LED 自检";
    $("#self-test-result").dataset.status = live ? state.selfTest?.status || "idle" : "idle";
    $("#sidebar-link").textContent = example ? "示例设备状态" : boardOnline ? "STM32 MQTT 在线" : "等待底盘连接";
    $("#log-mode-note").textContent = example ? "当前为示例记录" : live ? "当前为实时通信记录" : "当前记录仅来自界面预览";
    renderLogs();
    $("#clock").textContent = new Date().toLocaleString("zh-CN", { month: "2-digit", day: "2-digit", hour: "2-digit", minute: "2-digit", hour12: false });
  }
  function navigate() {
    let page = location.hash.slice(1);
    if (!Object.hasOwn(pages, page)) page = "devices";
    const [title, eyebrow, description] = pages[page];
    $("#page-title").textContent = title;
    $("#page-eyebrow").textContent = eyebrow;
    $("#page-description").textContent = description;
    $("#breadcrumb-current").textContent = title;
    $$(".page").forEach((element) => { element.hidden = element.id !== `page-${page}`; });
    $$(".nav-link").forEach((element) => {
      const active = element.dataset.page === page;
      element.classList.toggle("active", active);
      if (active) element.setAttribute("aria-current", "page");
      else element.removeAttribute("aria-current");
    });
    if (innerWidth <= 720) $(".nav-link.active")?.scrollIntoView({ block: "nearest", inline: "nearest" });
  }
  $$("[data-icon]").forEach((element) => {
    element.innerHTML = `<svg class="icon" viewBox="0 0 24 24" aria-hidden="true">${icons[element.dataset.icon] || icons.info}</svg>`;
  });
  $("#example-toggle").addEventListener("click", () => {
    const example = state.mode !== "example";
    state = example ? model.example(Date.now()) : liveState;
    render();
    log("status", example ? "打开示例状态：设备IP、在线状态与PWM读数均为模拟。" : "退出示例状态：恢复当前实时通信状态。");
  });
  async function poll() {
    if (polling || location.protocol === "file:") return;
    polling = true;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 2500);
    try {
      const response = await fetch("/api/status", { cache: "no-store", signal: controller.signal });
      if (!response.ok) throw new Error("电脑通信服务返回错误");
      liveState = model.fromApi(await response.json(), Date.now());
      if (serviceInstance !== liveState.instanceId) {
        serviceInstance = liveState.instanceId;
        eventCursor = 0;
      }
      for (const event of liveState.events) {
        if (event.id > eventCursor) {
          log(event.type, event.message, "live", event.time);
          eventCursor = event.id;
        }
      }
    } catch (error) {
      if (liveState.devices.service.link) log("system", "电脑通信服务暂时不可用，等待自动恢复。", "live");
      liveState = model.unavailable(liveState);
    } finally {
      clearTimeout(timer);
      polling = false;
      if (state.mode !== "example") state = liveState;
      render();
    }
  }
  $("#led-self-test").addEventListener("click", async () => {
    if (sending || !model.canSelfTest(state, Date.now())) return;
    sending = true;
    render();
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 3000);
    try {
      const response = await fetch("/api/self-test", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: "{}", cache: "no-store", signal: controller.signal
      });
      const result = await response.json();
      if (!response.ok) toast(result.message || "自检请求未发送");
      liveState = { ...liveState, selfTest: result };
      if (state.mode !== "example") state = liveState;
    } catch (error) {
      toast("自检请求响应未收到，请查看本次结果；没有自动重发指令。");
    } finally {
      clearTimeout(timer);
      sending = false;
      render();
      poll();
    }
  });
  $("#camera-open").addEventListener("click", () => {
    toast("摄像头未接入。后续接入RK3588视频源后，才能打开画面。");
    log("camera", "尝试打开摄像头：没有视频源，画面未接入。");
  });
  $("#log-filter").addEventListener("change", renderLogs);
  $("#clear-logs").addEventListener("click", () => {
    for (let i = logs.length - 1; i >= 0; i -= 1) if (logs[i].mode === state.mode) logs.splice(i, 1);
    renderLogs();
    toast("当前模式记录已清空");
  });
  $("#export-logs").addEventListener("click", () => {
    const content = model.exportLogs(state, logs, Date.now());
    const url = URL.createObjectURL(new Blob([JSON.stringify(content, null, 2)], { type: "application/json;charset=utf-8" }));
    const a = document.createElement("a");
    a.href = url;
    a.download = `robot-status-logs-${new Date().toISOString().slice(0, 10)}.json`;
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
  window.addEventListener("hashchange", navigate);
  log("system", "MQTT 监视台已打开，等待电脑通信服务和 STM32 心跳。");
  navigate();
  render();
  poll();
  setInterval(() => {
    if (state.mode === "example") state = model.example(Date.now());
    if (state.mode !== "example") state = liveState;
    render();
    poll();
  }, 1000);
})();
