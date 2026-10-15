(function (root, factory) {
  "use strict";
  if (typeof module === "object" && module.exports) module.exports = factory();
  else root.RobotStatus = factory();
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";
  const newDevice = () => ({ link: false, ip: "", lastSeen: null, telemetry: {} });
  const reading = (value, min, max) => typeof value === "number" && Number.isFinite(value) && value >= min && value <= max ? value : null;
  function initial() {
    return { mode: "preview", devices: { service: newDevice(), stm32: newDevice(), rk3588: newDevice() } };
  }
  function replace(state, id, changes) {
    if (!Object.hasOwn(state.devices, id)) return state;
    return { ...state, devices: { ...state.devices, [id]: { ...state.devices[id], ...changes } } };
  }
  function link(state, id, connected, ip) {
    if (!Object.hasOwn(state.devices, id)) return state;
    const changes = { link: Boolean(connected) };
    if (typeof ip === "string") changes.ip = ip;
    if (connected) changes.lastSeen = null;
    return replace(state, id, changes);
  }
  function receive(state, id, payload, now) {
    if (!Object.hasOwn(state.devices, id) || !payload || typeof payload !== "object" || !Number.isFinite(now)) return state;
    return replace(state, id, {
      link: true, ip: typeof payload.ip === "string" ? payload.ip : state.devices[id].ip,
      lastSeen: now, telemetry: { ...payload }
    });
  }
  function status(state, id, now) {
    const device = state.devices[id];
    if (!device) return "waiting";
    if (!device.link) {
      if (state.mode === "live" && state.brokerConnected && device.lastSeen !== null && now - device.lastSeen > 5000) return "timeout";
      return device.lastSeen === null ? "waiting" : "offline";
    }
    if (device.lastSeen === null) return "pending";
    return now - device.lastSeen > 5000 ? "timeout" : "online";
  }
  function monitor(state, now) {
    if (status(state, "stm32", now) !== "online") return null;
    const telemetry = state.devices.stm32.telemetry;
    const result = {
      left: reading(telemetry.left, -100, 100), right: reading(telemetry.right, -100, 100),
      ch1: reading(telemetry.ch1, 500, 2500), ch2: reading(telemetry.ch2, 500, 2500), ch5: reading(telemetry.ch5, 500, 2500),
      locked: typeof telemetry.locked === "boolean" ? telemetry.locked : null,
      direction: ["静止", "前进", "后退", "左转", "右转"].includes(telemetry.direction) ? telemetry.direction : "—",
      rcValid: typeof telemetry.rcValid === "boolean" ? telemetry.rcValid : null,
      fault: typeof telemetry.fault === "string" ? telemetry.fault : "—"
    };
    result.simulation = telemetry.simulation === true;
    result.motorEnabled = typeof telemetry.motorEnabled === "boolean" ? telemetry.motorEnabled : null;
    result.outputLimit = reading(telemetry.outputLimit, 1, 100);
    result.safetyState = ["LOCKED", "WAIT", "ARMED", "FAILSAFE"].includes(telemetry.safetyState) ? telemetry.safetyState : null;
    for (const channel of ["ch1", "ch2", "ch5"]) {
      result[channel + "Valid"] = typeof telemetry[channel + "Valid"] === "boolean" ? telemetry[channel + "Valid"] : null;
      result[channel + "Period"] = reading(telemetry[channel + "Period"], 15, 25);
      if (result[channel + "Valid"] === false) {
        result[channel] = null;
        result[channel + "Period"] = null;
      }
    }
    return result;
  }
  function example(now) {
    let state = initial();
    state.mode = "example";
    state = receive(state, "service", { ip: "10.0.0.1" }, now);
    state = receive(state, "stm32", {
      ip: "10.0.0.21", left: 18, right: 18, ch1: 1500, ch2: 1800, ch5: 2000,
      locked: false, direction: "前进", rcValid: true, fault: "无故障"
    }, now);
    return receive(state, "rk3588", { ip: "10.0.0.22", camera: false }, now);
  }
  function fromApi(payload, receivedAt) {
    if (!payload || payload.mode !== "live" || !Number.isFinite(payload.nowMs) || !Number.isFinite(receivedAt)
        || typeof payload.brokerConnected !== "boolean" || typeof payload.instanceId !== "string"
        || !payload.devices || !payload.selfTest || !Array.isArray(payload.events)) throw new Error("Invalid status response");
    const state = { mode: "live", brokerConnected: payload.brokerConnected, instanceId: payload.instanceId,
      selfTest: { ...payload.selfTest }, events: payload.events, devices: {} };
    for (const id of ["service", "stm32", "rk3588"]) {
      const device = payload.devices[id];
      if (!device || typeof device.link !== "boolean" || typeof device.ip !== "string"
          || (device.lastSeen !== null && !Number.isFinite(device.lastSeen))
          || !device.telemetry || typeof device.telemetry !== "object") throw new Error("Invalid device response");
      state.devices[id] = { ...device, telemetry: { ...device.telemetry },
        reportedLastSeen: device.lastSeen,
        lastSeen: device.lastSeen === null ? null : receivedAt - Math.max(0, payload.nowMs - device.lastSeen) };
    }
    return state;
  }
  function unavailable(state) {
    if (state.mode !== "live") return initial();
    return { ...state, brokerConnected: false, devices: Object.fromEntries(
      Object.entries(state.devices).map(([id, device]) => [id, { ...device, link: false }])) };
  }
  function canSelfTest(state, now) {
    return state.mode === "live" && state.brokerConnected === true
      && status(state, "service", now) === "online" && status(state, "stm32", now) === "online"
      && state.selfTest?.status !== "pending";
  }
  function exportLogs(state, logs, now) {
    return { version: "0.5", mode: state.mode, exportedAt: new Date(now).toISOString(),
      realDevicesConnected: state.mode === "live" && status(state, "stm32", now) === "online",
      instanceId: state.mode === "live" ? state.instanceId : "",
      selfTest: state.mode === "live" ? state.selfTest : null,
      logs: logs.filter((item) => item.mode === state.mode) };
  }
  return { initial, link, receive, status, monitor, example, fromApi, unavailable, canSelfTest, exportLogs };
});
