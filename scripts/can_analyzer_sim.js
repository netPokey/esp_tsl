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
  const framesByChannel = [frames.filter((frame) => frame.ch === 0), frames.filter((frame) => frame.ch === 1)];
  if (!framesByChannel[0].length) framesByChannel[0] = frames;
  if (!framesByChannel[1].length) framesByChannel[1] = frames;
  let index = 0;
  const channelIndex = [0, 0];
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
    nextChannelFrames(ch, limit, nowMs) {
      const out = [];
      while (pending.length && out.length < limit) out.push(enrich(pending.shift(), nowMs));
      const source = framesByChannel[ch & 1];
      while (out.length < limit) {
        const frame = source[channelIndex[ch & 1]];
        channelIndex[ch & 1] = (channelIndex[ch & 1] + 1) % source.length;
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

function defaultTrafficProfile() {
  return { fpsA: 1440, fpsB: 2893, loadAx10: 293, loadBx10: 601 };
}

function createRateScheduler(profile, intervalMs) {
  const ticksPerSecond = 1000 / intervalMs;
  let carryA = 0;
  let carryB = 0;
  return {
    nextTickCounts() {
      carryA += profile.fpsA / ticksPerSecond;
      carryB += profile.fpsB / ticksPerSecond;
      const A = Math.floor(carryA);
      const B = Math.floor(carryB);
      carryA -= A;
      carryB -= B;
      return { A, B };
    },
  };
}

function splitDeltaBatches(records) {
  const batches = [];
  for (let i = 0; i < records.length; i += 255) batches.push(records.slice(i, i + 255));
  return batches;
}

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
  const profile = defaultTrafficProfile();
  const args = { port: 8080, fpsA: profile.fpsA, fpsB: profile.fpsB };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === '--port') args.port = Number(argv[++i]);
    if (argv[i] === '--fps-a') args.fpsA = Number(argv[++i]);
    if (argv[i] === '--fps-b') args.fpsB = Number(argv[++i]);
    if (argv[i] === '--rate') {
      const rate = Number(argv[++i]);
      args.fpsA = Math.floor(rate / 3);
      args.fpsB = rate - args.fpsA;
    }
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
  const baseProfile = defaultTrafficProfile();
  const profile = {
    fpsA: Math.max(0, options.fpsA ?? baseProfile.fpsA),
    fpsB: Math.max(0, options.fpsB ?? baseProfile.fpsB),
    loadAx10: baseProfile.loadAx10,
    loadBx10: baseProfile.loadBx10,
  };
  const intervalMs = 100;
  const scheduler = createRateScheduler(profile, intervalMs);

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
    const counts = scheduler.nextTickCounts();
    const records = [
      ...state.nextChannelFrames(0, counts.A, nowMs),
      ...state.nextChannelFrames(1, counts.B, nowMs),
    ];
    for (const batch of splitDeltaBatches(records)) broadcast(clients, packDeltaMessage(batch));
    broadcast(clients, packStatsMessage({
      fpsA: profile.fpsA,
      fpsB: profile.fpsB,
      loadAx10: profile.loadAx10,
      loadBx10: profile.loadBx10,
      healthA: 1,
      healthB: 1,
      dropped: 0,
    }));
  }, intervalMs);

  server.on('close', () => clearInterval(timer));
  server.listen(port, () => {
    console.log(`CAN analyzer simulator: http://localhost:${port}/`);
    console.log(`Loaded ${frames.length} capture frames; A=${profile.fpsA} fps, B=${profile.fpsB} fps`);
    console.log(`Trigger: http://localhost:${port}/api/sim/trigger?ch=A&id=0x700&data=11223344`);
  });
  return { server, state, clients };
}

if (require.main === module) {
  startServer(parseArgs(process.argv));
}

module.exports = {
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
  startServer,
};
