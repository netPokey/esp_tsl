const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const {
  parseCsvCapture,
  parseNdjsonCapture,
  createSimulatorState,
  packDeltaMessage,
  packStatsMessage,
  makeTriggerFrame,
  loadCaptureFiles,
  contentTypeForPath,
  encodeWsFrame,
  defaultTrafficProfile,
  createRateScheduler,
  splitDeltaBatches,
} = require('../../scripts/can_analyzer_sim');

test('parseCsvCapture reads channel, id, dlc, and hex payload', () => {
  const csv = 'time_s,channel,id,dlc,data\n0.001,A,0x123,3,0102ff\n0.002,B,291,2,0a0b\n';

  const frames = parseCsvCapture(csv);

  assert.deepEqual(frames, [
    { timeMs: 1, ch: 0, id: 0x123, dlc: 3, data: [0x01, 0x02, 0xff] },
    { timeMs: 2, ch: 1, id: 291, dlc: 2, data: [0x0a, 0x0b] },
  ]);
});

test('parseNdjsonCapture reads FRAMES from batch payloads', () => {
  const ndjson = [
    JSON.stringify({
      server_ts: '2026-06-18T00:00:00.000Z',
      payload: {
        UPTIME_MS: 1000,
        FRAMES: [
          { CH: 'A', ID: '0x321', DLC: 2, DATA: 'aabb' },
          { ch: 'B', id: 0x322, dlc: 1, data: [0xcc] },
        ],
      },
    }),
  ].join('\n');

  const frames = parseNdjsonCapture(ndjson);

  assert.deepEqual(frames, [
    { timeMs: 1000, ch: 0, id: 0x321, dlc: 2, data: [0xaa, 0xbb] },
    { timeMs: 1000, ch: 1, id: 0x322, dlc: 1, data: [0xcc] },
  ]);
});

test('createSimulatorState cycles capture frames and keeps per-id counts', () => {
  const state = createSimulatorState([
    { timeMs: 0, ch: 0, id: 0x100, dlc: 1, data: [1] },
    { timeMs: 1, ch: 0, id: 0x100, dlc: 1, data: [2] },
  ]);

  const first = state.nextFrames(1, 10);
  const second = state.nextFrames(1, 20);
  const third = state.nextFrames(1, 30);

  assert.equal(first[0].count, 1);
  assert.equal(second[0].count, 2);
  assert.equal(second[0].changeScore, 1);
  assert.equal(third[0].count, 3);
});

test('packDeltaMessage writes the frontend delta binary format', () => {
  const message = packDeltaMessage([{
    ch: 1,
    id: 0x456,
    dlc: 2,
    data: [0xde, 0xad],
    lastRx: 1234,
    byteAge: [0, 1, 2, 3, 4, 5, 6, 7],
    count: 9,
    delta: 10,
    period: 11,
    jitter: 12,
    changeScore: 13,
    flags: 1,
  }]);

  assert.equal(message.length, 47);
  assert.equal(message.readUInt8(0), 0x01);
  assert.equal(message.readUInt8(1), 1);
  assert.equal(message.readUInt8(2), 1);
  assert.equal(message.readUInt16LE(3), 0x456);
  assert.equal(message.readUInt8(5), 2);
  assert.equal(message.readUInt8(6), 0xde);
  assert.equal(message.readUInt8(7), 0xad);
  assert.equal(message.readUInt32LE(14), 1234);
  assert.equal(message.readUInt16LE(18), 0);
  assert.equal(message.readUInt16LE(32), 7);
  assert.equal(message.readUInt32LE(34), 9);
  assert.equal(message.readUInt16LE(38), 10);
  assert.equal(message.readUInt16LE(40), 11);
  assert.equal(message.readUInt16LE(42), 12);
  assert.equal(message.readUInt16LE(44), 13);
  assert.equal(message.readUInt8(46), 1);
});

test('packStatsMessage writes the frontend stats binary format', () => {
  const message = packStatsMessage({ fpsA: 20, fpsB: 30, loadAx10: 4, loadBx10: 5, healthA: 1, healthB: 0, dropped: 7 });

  assert.equal(message.length, 24);
  assert.equal(message.readUInt8(0), 0x02);
  assert.equal(message.readUInt16LE(1), 20);
  assert.equal(message.readUInt16LE(3), 30);
  assert.equal(message.readUInt16LE(5), 4);
  assert.equal(message.readUInt16LE(7), 5);
  assert.equal(message.readUInt8(17), 1);
  assert.equal(message.readUInt8(18), 0);
  assert.equal(message.readUInt32LE(19), 7);
});

test('makeTriggerFrame creates a visible new standard CAN ID', () => {
  const frame = makeTriggerFrame({ ch: 'B', id: '0x701', data: '11223344' });

  assert.deepEqual(frame, { timeMs: 0, ch: 1, id: 0x701, dlc: 4, data: [0x11, 0x22, 0x33, 0x44] });
});

test('loadCaptureFiles combines csv and ndjson captures from disk', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'can-sim-'));
  const csvPath = path.join(dir, 'sample.csv');
  const ndjsonPath = path.join(dir, 'sample.ndjson');
  fs.writeFileSync(csvPath, 'time_s,channel,id,dlc,data\n0.001,A,0x100,1,01\n');
  fs.writeFileSync(ndjsonPath, JSON.stringify({ payload: { UPTIME_MS: 2, FRAMES: [{ CH: 'B', ID: '0x200', DLC: 1, DATA: '02' }] } }));

  const frames = loadCaptureFiles([csvPath, ndjsonPath]);

  assert.deepEqual(frames.map((frame) => frame.id), [0x100, 0x200]);
});

test('contentTypeForPath returns browser-safe content types', () => {
  assert.equal(contentTypeForPath('/x/app.js'), 'text/javascript; charset=utf-8');
  assert.equal(contentTypeForPath('/x/style.css'), 'text/css; charset=utf-8');
  assert.equal(contentTypeForPath('/x/index.html'), 'text/html; charset=utf-8');
});

test('encodeWsFrame creates an unmasked binary server frame', () => {
  const frame = encodeWsFrame(Buffer.from([0x01, 0x02, 0x03]));

  assert.deepEqual([...frame], [0x82, 0x03, 0x01, 0x02, 0x03]);
});

test('defaultTrafficProfile matches the requested capture speed display', () => {
  assert.deepEqual(defaultTrafficProfile(), {
    fpsA: 1440,
    fpsB: 2893,
    loadAx10: 293,
    loadBx10: 601,
  });
});

test('createRateScheduler emits requested A and B frame counts without overlarge delta packets', () => {
  const scheduler = createRateScheduler(defaultTrafficProfile(), 100);
  let totalA = 0;
  let totalB = 0;

  for (let i = 0; i < 10; i++) {
    const tick = scheduler.nextTickCounts();
    totalA += tick.A;
    totalB += tick.B;
    assert.ok(splitDeltaBatches(new Array(tick.A + tick.B).fill({})).every((batch) => batch.length <= 255));
  }

  assert.equal(totalA, 1440);
  assert.equal(totalB, 2893);
  assert.deepEqual(splitDeltaBatches(new Array(433).fill({})).map((batch) => batch.length), [255, 178]);
});
