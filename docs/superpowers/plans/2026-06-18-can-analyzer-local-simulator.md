# CAN Analyzer Local Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a local simulator that serves the existing CAN analyzer UI, replays captured CAN frames over `/ws`, and provides a control endpoint/page to inject a new CAN ID for testing focus-baseline behavior.

**Architecture:** Add one dependency-free Node.js script under `scripts/`. The script parses `data/can-record.csv` and `data/can_batches.ndjson`, normalizes both into CAN frame objects, serves `data/analyzer` as static files, implements a minimal WebSocket server compatible with the existing frontend binary protocol, and exposes simulator control APIs under `/api/sim/*`.

**Tech Stack:** Node.js standard library only (`http`, `fs`, `path`, `crypto`, `url`, `node:test`, `assert`). Existing frontend files in `data/analyzer` remain unchanged.

---

## File Structure

- Create `scripts/can_analyzer_sim.js`
  - CLI entry point: `node scripts/can_analyzer_sim.js --port 8080`.
  - Exports pure helpers for tests: `parseCsvCapture`, `parseNdjsonCapture`, `packDeltaMessage`, `packStatsMessage`, `makeTriggerFrame`, `createSimulatorState`.
  - Uses Node standard library only, so it runs without `npm install`.
- Create `test/test_can_analyzer_sim/can_analyzer_sim.test.js`
  - Node built-in tests for capture parsing and binary protocol packing.
- No changes to `data/analyzer/app.js`, `index.html`, or `style.css`.

## Existing Protocol Reference

The frontend reads `/ws` messages in `data/analyzer/app.js`:

- Delta frame type `0x01`:
  - byte 0: `0x01`
  - byte 1: record count
  - each record is 45 bytes:
    - `u8 ch`
    - `u16le id`
    - `u8 dlc`
    - 8 payload bytes
    - `u32le lastRx`
    - 8 × `u16le byteAge`
    - `u32le count`
    - `u16le delta`
    - `u16le period`
    - `u16le jitter`
    - `u16le changeScore`
    - `u8 flags`
- Stats frame type `0x02`:
  - `u8 type`
  - `u16le fpsA`
  - `u16le fpsB`
  - `u16le loadAx10`
  - `u16le loadBx10`
  - `u32le unusedA`
  - `u32le unusedB`
  - `u8 healthA`
  - `u8 healthB`
  - `u32le dropped`

---

### Task 1: Capture parsers and simulator state

**Files:**
- Create: `scripts/can_analyzer_sim.js`
- Test: `test/test_can_analyzer_sim/can_analyzer_sim.test.js`

- [ ] **Step 1: Write failing parser tests**

Create `test/test_can_analyzer_sim/can_analyzer_sim.test.js` with:

```js
const test = require('node:test');
const assert = require('node:assert/strict');

const {
  parseCsvCapture,
  parseNdjsonCapture,
  createSimulatorState,
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
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: FAIL because `scripts/can_analyzer_sim.js` does not exist.

- [ ] **Step 3: Implement minimal parser/state helpers**

Create `scripts/can_analyzer_sim.js` with these helpers first:

```js
#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { URL } = require('node:url');

function parseNumber(value) {
  if (typeof value === 'number') return value;
  const text = String(value || '').trim();
  if (!text) return 0;
  return Number.parseInt(text, text.toLowerCase().startsWith('0x') ? 16 : 10);
}

function parseChannel(value) {
  if (value === 0 || value === '0') return 0;
  if (value === 1 || value === '1') return 1;
  return String(value || '').trim().toUpperCase() === 'B' ? 1 : 0;
}

function parseData(value) {
  if (Array.isArray(value)) return value.map((n) => parseNumber(n) & 0xff);
  const clean = String(value || '').replace(/[^0-9a-fA-F]/g, '');
  const out = [];
  for (let i = 0; i + 1 < clean.length && out.length < 8; i += 2) {
    out.push(Number.parseInt(clean.slice(i, i + 2), 16));
  }
  return out;
}

function normalizeFrame(input, fallbackTimeMs = 0) {
  const data = parseData(input.data ?? input.DATA);
  const dlc = Math.max(0, Math.min(8, parseNumber(input.dlc ?? input.DLC ?? data.length)));
  while (data.length < dlc) data.push(0);
  return {
    timeMs: Math.max(0, Number(input.timeMs ?? input.time_ms ?? input.TIME_MS ?? fallbackTimeMs) || 0),
    ch: parseChannel(input.ch ?? input.CH ?? input.channel ?? input.CHANNEL),
    id: parseNumber(input.id ?? input.ID) & 0x7ff,
    dlc,
    data: data.slice(0, dlc),
  };
}

function parseCsvLine(line) {
  const out = [];
  let cur = '';
  let quoted = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (c === '"') {
      quoted = !quoted;
    } else if (c === ',' && !quoted) {
      out.push(cur);
      cur = '';
    } else {
      cur += c;
    }
  }
  out.push(cur);
  return out;
}

function parseCsvCapture(text) {
  const lines = String(text || '').split(/\r?\n/).filter(Boolean);
  if (lines.length < 2) return [];
  const header = parseCsvLine(lines[0]).map((h) => h.trim());
  return lines.slice(1).map((line) => {
    const cols = parseCsvLine(line);
    const row = {};
    header.forEach((name, index) => { row[name] = cols[index]; });
    return normalizeFrame({
      timeMs: Math.round(Number(row.time_s || 0) * 1000),
      channel: row.channel,
      id: row.id,
      dlc: row.dlc,
      data: row.data,
    });
  }).filter((frame) => Number.isFinite(frame.id));
}

function parseNdjsonCapture(text) {
  const frames = [];
  for (const line of String(text || '').split(/\r?\n/)) {
    if (!line.trim()) continue;
    const entry = JSON.parse(line);
    const payload = entry.payload || entry.PAYLOAD || entry;
    const fallbackTimeMs = Number(payload.UPTIME_MS ?? payload.uptime_ms ?? 0) || 0;
    const list = payload.FRAMES || payload.frames || [];
    for (const item of list) frames.push(normalizeFrame(item, fallbackTimeMs));
  }
  return frames;
}

function frameKey(frame) {
  return `${frame.ch}:${frame.id}`;
}

function createSimulatorState(captureFrames) {
  const frames = captureFrames.length ? captureFrames : [
    { timeMs: 0, ch: 0, id: 0x100, dlc: 1, data: [0] },
  ];
  let index = 0;
  const records = new Map();
  const pending = [];

  function enrich(frame, nowMs) {
    const key = frameKey(frame);
    const previous = records.get(key);
    let changeScore = previous ? previous.changeScore : 0;
    if (previous && previous.data.some((v, i) => v !== (frame.data[i] || 0))) changeScore += 1;
    const count = previous ? previous.count + 1 : 1;
    const delta = previous ? Math.min(65535, Math.max(0, nowMs - previous.lastRx)) : 0;
    const record = {
      ...frame,
      data: frame.data.slice(0, frame.dlc),
      lastRx: nowMs >>> 0,
      byteAge: new Array(8).fill(0),
      count,
      delta,
      period: delta,
      jitter: 0,
      changeScore,
      flags: 0,
    };
    records.set(key, record);
    return record;
  }

  return {
    nextFrames(limit, nowMs) {
      const out = [];
      while (pending.length && out.length < limit) out.push(enrich(pending.shift(), nowMs));
      while (out.length < limit) {
        const frame = frames[index];
        index = (index + 1) % frames.length;
        out.push(enrich(frame, nowMs));
      }
      return out;
    },
    inject(frame) {
      pending.push(normalizeFrame(frame, Date.now() >>> 0));
    },
    stats() {
      let fpsA = 0;
      let fpsB = 0;
      for (const record of records.values()) {
        if (record.ch === 0) fpsA += 1;
        if (record.ch === 1) fpsB += 1;
      }
      return { fpsA, fpsB, loadAx10: 0, loadBx10: 0, healthA: 1, healthB: 1, dropped: 0 };
    },
  };
}

module.exports = {
  parseCsvCapture,
  parseNdjsonCapture,
  createSimulatorState,
};
```

- [ ] **Step 4: Run parser tests to verify they pass**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: PASS for 3 tests.

---

### Task 2: Binary protocol packers

**Files:**
- Modify: `scripts/can_analyzer_sim.js`
- Test: `test/test_can_analyzer_sim/can_analyzer_sim.test.js`

- [ ] **Step 1: Add failing binary protocol tests**

Append to `test/test_can_analyzer_sim/can_analyzer_sim.test.js`:

```js
const {
  packDeltaMessage,
  packStatsMessage,
  makeTriggerFrame,
} = require('../../scripts/can_analyzer_sim');

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
```

Then fix the duplicate import by changing the first import block to include all exports once:

```js
const {
  parseCsvCapture,
  parseNdjsonCapture,
  createSimulatorState,
  packDeltaMessage,
  packStatsMessage,
  makeTriggerFrame,
} = require('../../scripts/can_analyzer_sim');
```

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: FAIL because `packDeltaMessage`, `packStatsMessage`, and `makeTriggerFrame` are not exported.

- [ ] **Step 3: Implement protocol packers**

Add to `scripts/can_analyzer_sim.js` before `module.exports`:

```js
function clampU16(value) {
  return Math.max(0, Math.min(65535, Number(value) || 0));
}

function packDeltaMessage(records) {
  const count = Math.min(255, records.length);
  const buffer = Buffer.alloc(2 + count * 45);
  buffer.writeUInt8(0x01, 0);
  buffer.writeUInt8(count, 1);
  let offset = 2;
  for (let i = 0; i < count; i++) {
    const record = records[i];
    buffer.writeUInt8(record.ch & 1, offset); offset += 1;
    buffer.writeUInt16LE(record.id & 0x7ff, offset); offset += 2;
    buffer.writeUInt8(record.dlc & 0x0f, offset); offset += 1;
    for (let b = 0; b < 8; b++) buffer.writeUInt8(record.data[b] || 0, offset + b);
    offset += 8;
    buffer.writeUInt32LE(record.lastRx >>> 0, offset); offset += 4;
    const ages = record.byteAge || [];
    for (let b = 0; b < 8; b++) {
      buffer.writeUInt16LE(clampU16(ages[b]), offset);
      offset += 2;
    }
    buffer.writeUInt32LE(record.count >>> 0, offset); offset += 4;
    buffer.writeUInt16LE(clampU16(record.delta), offset); offset += 2;
    buffer.writeUInt16LE(clampU16(record.period), offset); offset += 2;
    buffer.writeUInt16LE(clampU16(record.jitter), offset); offset += 2;
    buffer.writeUInt16LE(clampU16(record.changeScore), offset); offset += 2;
    buffer.writeUInt8(record.flags || 0, offset); offset += 1;
  }
  return buffer;
}

function packStatsMessage(stats) {
  const buffer = Buffer.alloc(24);
  buffer.writeUInt8(0x02, 0);
  buffer.writeUInt16LE(clampU16(stats.fpsA), 1);
  buffer.writeUInt16LE(clampU16(stats.fpsB), 3);
  buffer.writeUInt16LE(clampU16(stats.loadAx10), 5);
  buffer.writeUInt16LE(clampU16(stats.loadBx10), 7);
  buffer.writeUInt32LE(0, 9);
  buffer.writeUInt32LE(0, 13);
  buffer.writeUInt8(stats.healthA ? 1 : 0, 17);
  buffer.writeUInt8(stats.healthB ? 1 : 0, 18);
  buffer.writeUInt32LE((stats.dropped || 0) >>> 0, 19);
  buffer.writeUInt8(0, 23);
  return buffer;
}

function makeTriggerFrame(query) {
  return normalizeFrame({
    timeMs: 0,
    ch: query.ch ?? query.channel ?? 'A',
    id: query.id ?? '0x700',
    dlc: query.dlc ?? parseData(query.data ?? '1122334455667788').length,
    data: query.data ?? '1122334455667788',
  });
}
```

Update `module.exports` to:

```js
module.exports = {
  parseCsvCapture,
  parseNdjsonCapture,
  createSimulatorState,
  packDeltaMessage,
  packStatsMessage,
  makeTriggerFrame,
};
```

- [ ] **Step 4: Run protocol tests to verify they pass**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: PASS for 6 tests.

---

### Task 3: HTTP static server and simulator APIs

**Files:**
- Modify: `scripts/can_analyzer_sim.js`
- Test: `test/test_can_analyzer_sim/can_analyzer_sim.test.js`

- [ ] **Step 1: Add failing tests for loading captures from disk**

Append to `test/test_can_analyzer_sim/can_analyzer_sim.test.js`:

```js
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {
  loadCaptureFiles,
  contentTypeForPath,
} = require('../../scripts/can_analyzer_sim');

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
```

Then fix the import block to include `loadCaptureFiles` and `contentTypeForPath` in the existing destructuring import.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: FAIL because `loadCaptureFiles` and `contentTypeForPath` are not exported.

- [ ] **Step 3: Implement disk loading, static file helpers, and API handlers**

Add to `scripts/can_analyzer_sim.js` before `module.exports`:

```js
function loadCaptureFiles(files) {
  const frames = [];
  for (const file of files) {
    if (!fs.existsSync(file)) continue;
    const text = fs.readFileSync(file, 'utf8');
    if (file.endsWith('.csv')) frames.push(...parseCsvCapture(text));
    if (file.endsWith('.ndjson') || file.endsWith('.jsonl')) frames.push(...parseNdjsonCapture(text));
  }
  return frames.sort((a, b) => a.timeMs - b.timeMs);
}

function contentTypeForPath(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  if (ext === '.html') return 'text/html; charset=utf-8';
  if (ext === '.js') return 'text/javascript; charset=utf-8';
  if (ext === '.css') return 'text/css; charset=utf-8';
  if (ext === '.json') return 'application/json; charset=utf-8';
  if (ext === '.svg') return 'image/svg+xml';
  return 'application/octet-stream';
}

function sendJson(res, status, body) {
  const data = Buffer.from(JSON.stringify(body));
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': data.length,
    'cache-control': 'no-store',
  });
  res.end(data);
}

function serveStatic(req, res, rootDir) {
  const parsed = new URL(req.url, 'http://localhost');
  const pathname = decodeURIComponent(parsed.pathname === '/' ? '/index.html' : parsed.pathname);
  const filePath = path.resolve(rootDir, `.${pathname}`);
  if (!filePath.startsWith(path.resolve(rootDir))) {
    sendJson(res, 403, { ok: false, error: 'forbidden' });
    return;
  }
  fs.readFile(filePath, (err, data) => {
    if (err) {
      sendJson(res, 404, { ok: false, error: 'not_found' });
      return;
    }
    res.writeHead(200, {
      'content-type': contentTypeForPath(filePath),
      'content-length': data.length,
      'cache-control': 'no-store',
    });
    res.end(data);
  });
}

function handleApi(req, res, state) {
  const parsed = new URL(req.url, 'http://localhost');
  if (parsed.pathname === '/api/status') {
    sendJson(res, 200, {
      tx_master: false,
      can_a_tx: false,
      can_b_tx: false,
      can_a_online: true,
      can_b_online: true,
      sim: true,
    });
    return true;
  }
  if (parsed.pathname === '/api/sim/status') {
    sendJson(res, 200, { ok: true, sim: true });
    return true;
  }
  if (parsed.pathname === '/api/sim/trigger') {
    const frame = makeTriggerFrame(Object.fromEntries(parsed.searchParams.entries()));
    state.inject(frame);
    sendJson(res, 200, { ok: true, frame });
    return true;
  }
  return false;
}
```

- [ ] **Step 4: Export helpers and run tests**

Update `module.exports` to include:

```js
  loadCaptureFiles,
  contentTypeForPath,
```

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: PASS for 8 tests.

---

### Task 4: Minimal WebSocket server and CLI

**Files:**
- Modify: `scripts/can_analyzer_sim.js`
- Test: `test/test_can_analyzer_sim/can_analyzer_sim.test.js`

- [ ] **Step 1: Add failing WebSocket framing test**

Append to `test/test_can_analyzer_sim/can_analyzer_sim.test.js`:

```js
const { encodeWsFrame } = require('../../scripts/can_analyzer_sim');

test('encodeWsFrame creates an unmasked binary server frame', () => {
  const frame = encodeWsFrame(Buffer.from([0x01, 0x02, 0x03]));

  assert.deepEqual([...frame], [0x82, 0x03, 0x01, 0x02, 0x03]);
});
```

Then move `encodeWsFrame` into the existing destructuring import.

- [ ] **Step 2: Run tests to verify they fail**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
```

Expected: FAIL because `encodeWsFrame` is not exported.

- [ ] **Step 3: Implement WebSocket framing, upgrade, broadcast loop, and CLI**

Add to `scripts/can_analyzer_sim.js` before `module.exports`:

```js
function encodeWsFrame(payload) {
  const length = payload.length;
  let header;
  if (length < 126) {
    header = Buffer.from([0x82, length]);
  } else if (length <= 65535) {
    header = Buffer.alloc(4);
    header.writeUInt8(0x82, 0);
    header.writeUInt8(126, 1);
    header.writeUInt16BE(length, 2);
  } else {
    header = Buffer.alloc(10);
    header.writeUInt8(0x82, 0);
    header.writeUInt8(127, 1);
    header.writeBigUInt64BE(BigInt(length), 2);
  }
  return Buffer.concat([header, payload]);
}

function acceptWebSocket(req, socket, clients) {
  const key = req.headers['sec-websocket-key'];
  if (!key) {
    socket.destroy();
    return;
  }
  const accept = crypto
    .createHash('sha1')
    .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
    .digest('base64');
  socket.write([
    'HTTP/1.1 101 Switching Protocols',
    'Upgrade: websocket',
    'Connection: Upgrade',
    `Sec-WebSocket-Accept: ${accept}`,
    '',
    '',
  ].join('\r\n'));
  clients.add(socket);
  socket.on('close', () => clients.delete(socket));
  socket.on('error', () => clients.delete(socket));
}

function broadcast(clients, payload) {
  const frame = encodeWsFrame(payload);
  for (const client of clients) {
    if (!client.destroyed) client.write(frame);
  }
}

function parseArgs(argv) {
  const args = { port: 8080, rate: 20 };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === '--port') args.port = Number(argv[++i]);
    if (argv[i] === '--rate') args.rate = Number(argv[++i]);
  }
  return args;
}

function startServer(options = {}) {
  const repoRoot = path.resolve(__dirname, '..');
  const rootDir = path.join(repoRoot, 'data', 'analyzer');
  const captureFiles = [
    path.join(repoRoot, 'data', 'can-record.csv'),
    path.join(repoRoot, 'data', 'can_batches.ndjson'),
  ];
  const frames = loadCaptureFiles(captureFiles);
  const state = createSimulatorState(frames);
  const clients = new Set();
  const port = options.port || 8080;
  const rate = Math.max(1, options.rate || 20);
  const perTick = Math.max(1, Math.min(50, Math.ceil(rate / 10)));

  const server = http.createServer((req, res) => {
    if (handleApi(req, res, state)) return;
    serveStatic(req, res, rootDir);
  });

  server.on('upgrade', (req, socket) => {
    const parsed = new URL(req.url, 'http://localhost');
    if (parsed.pathname !== '/ws') {
      socket.destroy();
      return;
    }
    acceptWebSocket(req, socket, clients);
  });

  const timer = setInterval(() => {
    if (!clients.size) return;
    const nowMs = Date.now() >>> 0;
    broadcast(clients, packDeltaMessage(state.nextFrames(perTick, nowMs)));
    broadcast(clients, packStatsMessage(state.stats()));
  }, 100);

  server.on('close', () => clearInterval(timer));
  server.listen(port, () => {
    console.log(`CAN analyzer simulator: http://localhost:${port}/`);
    console.log(`Loaded ${frames.length} capture frames; rate=${rate} frames/s`);
    console.log(`Trigger: http://localhost:${port}/api/sim/trigger?ch=A&id=0x700&data=11223344`);
  });
  return { server, state, clients };
}

if (require.main === module) {
  startServer(parseArgs(process.argv));
}
```

Update `module.exports` to include:

```js
  encodeWsFrame,
  startServer,
```

- [ ] **Step 4: Run tests and syntax check**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
node --check scripts/can_analyzer_sim.js
```

Expected: tests PASS and syntax check exits 0.

---

### Task 5: Manual browser verification

**Files:**
- No code changes expected.

- [ ] **Step 1: Start simulator**

Run:

```bash
node scripts/can_analyzer_sim.js --port 8080 --rate 40
```

Expected output includes:

```text
CAN analyzer simulator: http://localhost:8080/
Loaded <N> capture frames; rate=40 frames/s
Trigger: http://localhost:8080/api/sim/trigger?ch=A&id=0x700&data=11223344
```

- [ ] **Step 2: Open frontend**

Open:

```text
http://localhost:8080/
```

Expected:

- The page title is `CAN 分析仪`.
- `WS：已连接` appears.
- CAN_A/CAN_B tables fill with replayed capture data.

- [ ] **Step 3: Verify baseline focus behavior**

In the browser:

1. Wait until rows are visible.
2. Click `设为基线`.
3. Confirm rows remain visible, not cleared.
4. Open this URL in another tab:

```text
http://localhost:8080/api/sim/trigger?ch=A&id=0x700&data=11223344
```

Expected:

- API returns `{"ok":true,...}`.
- A new CAN_A row with ID `0x700` appears near the top of the CAN_A table.
- Existing baseline rows remain visible below focused rows.

- [ ] **Step 4: Verify existing ID change behavior**

Open this URL twice:

```text
http://localhost:8080/api/sim/trigger?ch=A&id=0x700&data=55667788
```

Expected:

- The CAN_A `0x700` row updates data.
- The row stays in the focused group because `changeScore` increased beyond the baseline threshold.

- [ ] **Step 5: Final verification commands**

Run:

```bash
node --test test/test_can_analyzer_sim/can_analyzer_sim.test.js
node --check scripts/can_analyzer_sim.js
node --check data/analyzer/app.js
```

Expected: all commands exit 0.

---

## Self-Review Checklist

- Spec coverage: covered static analyzer hosting, CSV and NDJSON parsing, `/ws` replay, `/api/sim/trigger`, and browser baseline verification.
- Placeholder scan: no TODO/TBD/fill-in placeholders remain.
- Type consistency: exported helper names are consistent across tests and implementation steps.
- Scope check: no frontend rewrite, no dependency installation, no production firmware changes.
