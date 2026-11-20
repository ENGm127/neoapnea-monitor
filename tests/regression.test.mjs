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
  assert.deepEqual(splitter.push("Buffer: 1, 2\\r\\n"), ["Buffer: 1, 2"]);
  assert.deepEqual(splitter.flush(), []);
});
