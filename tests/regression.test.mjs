import assert from "node:assert/strict";
import { test } from "node:test";

import { Monitor } from "../app/js/engine.js";
import { LineSplitter } from "../app/js/serial.js";

test("a sustained above-threshold chest movement is one breath event", () => {
  const monitor = new Monitor();
  monitor.processFrame([16000, 18000], [20000, 20000]);
  monitor.processFrame([19000, 19000], [20000, 20000]);

  assert.equal(monitor.breathOnsets.length, 1);
  assert.equal(monitor.lastBreath, 1 / monitor.s.fsChest);
  assert.equal(monitor.wasAbove, true);
});

test("serial splitter flushes a final unterminated line at EOF", () => {
  const splitter = new LineSplitter();
  assert.deepEqual(splitter.push("SPO2= 97, 1"), []);
  assert.deepEqual(splitter.flush(), ["SPO2= 97, 1"]);
  assert.deepEqual(splitter.flush(), []);
});

test("serial splitter does not emit an empty final line", () => {
  const splitter = new LineSplitter();
  assert.deepEqual(splitter.push("Buffer: 1, 2\r\n"), ["Buffer: 1, 2"]);
  assert.deepEqual(splitter.flush(), []);
});

test("invalid SpO2 samples clear the previous valid value", () => {
  const monitor = new Monitor();
  monitor.feed("SPO2= 97, 1");
  assert.equal(monitor.spo2, 97);
  monitor.feed("SPO2= -999, 0");
  assert.equal(monitor.spo2, null);
  assert.equal(monitor.lastSpo2ReceivedAt, null);
  assert.equal(monitor.spo2Alarm, false);
});

test("stale SpO2 is expired using elapsed wall time, not sample count", () => {
  const monitor = new Monitor();
  monitor.feed("SPO2= 96, 1");
  const receivedAt = monitor.lastSpo2ReceivedAt;
  assert.equal(monitor.expireStale(receivedAt + 3001), true);
  assert.equal(monitor.spo2, null);
  assert.equal(monitor.spo2Stale, true);
  assert.equal(monitor.spo2Alarm, false);
  assert.equal(monitor.expireStale(receivedAt + 4000), false);
});

test("a stopped frame stream is marked stale and fresh frames recover it", () => {
  const monitor = new Monitor();
  monitor.processFrame([16000, 18000], [20000, 20000]);
  const receivedAt = monitor.lastFrameReceivedAt;
  monitor.expireStale(receivedAt + 2001);
  assert.equal(monitor.frameStale, true);
  assert.equal(monitor.hr, 0);
  monitor.processFrame([16000, 18000], [20000, 20000]);
  assert.equal(monitor.frameStale, false);
});
