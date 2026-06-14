const banner = document.getElementById('tx-banner');
const masterToggle = document.getElementById('master-toggle');
const txAToggle = document.getElementById('tx-a-toggle');
const txBToggle = document.getElementById('tx-b-toggle');
const busHealth = document.getElementById('bus-health');
const busStats = document.getElementById('bus-stats');
const statusEl = document.getElementById('status');
const baseSelect = document.getElementById('base-select');
const suppressStatic = document.getElementById('suppress-static');
const staticSeconds = document.getElementById('static-seconds');
const freezeView = document.getElementById('freeze-view');
const sortSelect = document.getElementById('sort-select');
const baselineBtn = document.getElementById('baseline-btn');
const snapABtn = document.getElementById('snapshot-a-btn');
const snapBBtn = document.getElementById('snapshot-b-btn');
const diffBtn = document.getElementById('diff-btn');
const markBtn = document.getElementById('mark-btn');
const channelFilter = document.getElementById('channel-filter');
const idFilter = document.getElementById('id-filter');
const rangeFrom = document.getElementById('range-from');
const rangeTo = document.getElementById('range-to');
const searchBox = document.getElementById('search-box');
const whitelistOnly = document.getElementById('whitelist-only');
const p3Status = document.getElementById('p3-status');
const snapshotSummary = document.getElementById('snapshot-summary');
const pretriggerSummary = document.getElementById('pretrigger-summary');
const snapshotBody = document.querySelector('#snapshot-diff tbody');
const pretriggerBody = document.querySelector('#pretrigger tbody');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const records = {};
const labels = new Map();
const hidden = new Set();
const whitelist = new Set();
let snapshotDiffRows = [];
let pretriggerRows = [];
let ws = null;
let txState = { master: false, a: false, b: false, onlineA: false, onlineB: false };

function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }
function printable(b) { return b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'; }
function channelName(ch) { return ch === 1 ? 'B' : 'A'; }
function channelIdKey(ch, id) { return `${channelName(ch)}:${id}`; }
function recordKey(ch, id) { return ch * 4096 + id; }
function idText(id) { return '0x' + hex(id, 3); }

function textNode(text) { return document.createTextNode(String(text)); }
function clearNode(node) { while (node.firstChild) node.removeChild(node.firstChild); }

function parseId(text) {
  const s = String(text || '').trim();
  if (!s) return null;
  const n = /^0x/i.test(s) ? Number.parseInt(s, 16) : Number.parseInt(s, 16);
  return Number.isFinite(n) && n >= 0 && n <= 0x7ff ? n : null;
}

function formatByte(b) {
  switch (baseSelect.value) {
    case 'dec': return String(b);
    case 'bin': return b.toString(2).padStart(8, '0');
    case 'ascii': return printable(b);
    default: return hex(b, 2);
  }
}

function byteClass(ageMs) {
  if (ageMs < 500) return 'hot';
  if (ageMs < 2500) return 'warm';
  return '';
}

function dataHtml(rec) {
  const parts = [];
  for (let i = 0; i < rec.dlc; i++) {
    const cls = byteClass(rec.byteAge[i]);
    parts.push(`<span class="byte ${cls}">${formatByte(rec.data[i])}</span>`);
  }
  return parts.join('');
}

function bitHtml(rec) {
  let out = '';
  for (let byte = 0; byte < rec.dlc; byte++) {
    out += `B${byte}: `;
    for (let bit = 7; bit >= 0; bit--) {
      const one = (rec.data[byte] >> bit) & 1;
      out += `<span class="bit ${one ? 'one' : ''}">${one}</span>`;
    }
    out += ' ';
  }
  return out;
}

function dataText(data, dlc) {
  return data.slice(0, dlc).map(formatByte).join(' ');
}

function isStatic(rec) {
  if (!suppressStatic.checked) return false;
  const thresholdMs = Math.max(1, Number(staticSeconds.value) || 5) * 1000;
  for (let i = 0; i < rec.dlc; i++) {
    if (rec.byteAge[i] < thresholdMs) return false;
  }
  return true;
}

function rowClass(score) {
  if (score >= 20) return 'activity-high';
  if (score >= 5) return 'activity-med';
  return 'activity-low';
}

function passesLocalFilters(rec) {
  const key = channelIdKey(rec.ch, rec.id);
  if (hidden.has(key)) return false;
  if (channelFilter.value === 'A' && rec.ch !== 0) return false;
  if (channelFilter.value === 'B' && rec.ch !== 1) return false;
  const exact = parseId(idFilter.value);
  if (idFilter.value.trim() && exact === null) return false;
  if (exact !== null && rec.id !== exact) return false;
  const from = parseId(rangeFrom.value);
  const to = parseId(rangeTo.value);
  if (rangeFrom.value.trim() && from === null) return false;
  if (rangeTo.value.trim() && to === null) return false;
  if (from !== null && rec.id < from) return false;
  if (to !== null && rec.id > to) return false;
  if (whitelistOnly.checked && !whitelist.has(key)) return false;
  const q = searchBox.value.trim().toLowerCase();
  if (q) {
    const label = (labels.get(key) || '').toLowerCase();
    const idHex = idText(rec.id).toLowerCase();
    const idBare = hex(rec.id, 3).toLowerCase();
    if (!label.includes(q) && !idHex.includes(q) && !idBare.includes(q)) return false;
  }
  return true;
}

function passesP3Filter(rec) {
  return passesLocalFilters({ ch: rec.ch, id: rec.id });
}

function rowHidden(rec) {
  return isStatic(rec) || !passesLocalFilters(rec);
}

function appendIdCell(cell, rec) {
  clearNode(cell);
  const idSpan = document.createElement('span');
  idSpan.className = 'id-text';
  idSpan.textContent = idText(rec.id);
  cell.appendChild(idSpan);

  const label = labels.get(channelIdKey(rec.ch, rec.id));
  if (label) {
    const badge = document.createElement('span');
    badge.className = 'label-badge';
    badge.title = label;
    badge.textContent = label;
    cell.appendChild(badge);
  }

  const actions = document.createElement('span');
  actions.className = 'row-actions';
  const w = document.createElement('button');
  w.type = 'button';
  w.textContent = 'W';
  w.title = '切换白名单';
  w.onclick = (ev) => { ev.stopPropagation(); toggleWhitelist(rec); };
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = 'B';
  b.title = '隐藏到本地黑名单';
  b.onclick = (ev) => { ev.stopPropagation(); hideRecord(rec); };
  actions.appendChild(w);
  actions.appendChild(b);
  cell.appendChild(actions);
}

function ensureRows(rec) {
  const key = rec.key;
  if (rows[key]) return rows[key];

  const tr = document.createElement('tr');
  tr.innerHTML = '<td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>';
  const bit = document.createElement('tr');
  bit.className = 'bit-view hidden';
  bit.innerHTML = '<td colspan="8"></td>';
  tr.onclick = () => bit.classList.toggle('hidden');
  tr.ondblclick = (ev) => { ev.stopPropagation(); editLabel(rec); };
  rows[key] = { tr, bit };
  tbody[rec.ch].appendChild(tr);
  tbody[rec.ch].appendChild(bit);
  return rows[key];
}

function paintRecord(rec) {
  const pair = ensureRows(rec);
  const tr = pair.tr;
  const hiddenRow = rowHidden(rec);
  tr.className = rowClass(rec.changeScore);
  tr.classList.toggle('hidden', hiddenRow);
  tr.classList.toggle('whitelisted', whitelist.has(channelIdKey(rec.ch, rec.id)));
  tr.classList.toggle('baselined', hidden.has(channelIdKey(rec.ch, rec.id)));
  if (hiddenRow) pair.bit.classList.add('hidden');

  const c = tr.children;
  appendIdCell(c[0], rec);
  c[1].textContent = rec.dlc;
  c[2].innerHTML = dataHtml(rec);
  c[3].textContent = rec.count;
  c[4].textContent = rec.deltaMs;
  c[5].textContent = rec.periodMs;
  c[6].textContent = rec.jitterMs;
  c[7].textContent = rec.changeScore;
  pair.bit.children[0].innerHTML = bitHtml(rec);
}

function sortTables() {
  for (const ch of [0, 1]) {
    const list = Object.values(records)
      .filter(r => r.ch === ch)
      .sort((a, b) => sortSelect.value === 'activity' ? (b.changeScore - a.changeScore || a.id - b.id) : (a.id - b.id));
    for (const rec of list) {
      const pair = ensureRows(rec);
      tbody[ch].appendChild(pair.tr);
      tbody[ch].appendChild(pair.bit);
    }
  }
}

function repaintAll() {
  for (const rec of Object.values(records)) paintRecord(rec);
  sortTables();
  renderSnapshotDiffRows();
  renderPretriggerRows();
}

function toggleWhitelist(rec) {
  const key = channelIdKey(rec.ch, rec.id);
  if (whitelist.has(key)) whitelist.delete(key);
  else whitelist.add(key);
  p3Status.textContent = `whitelist=${whitelist.size}`;
  repaintAll();
}

function hideRecord(rec) {
  hidden.add(channelIdKey(rec.ch, rec.id));
  p3Status.textContent = `hidden=${hidden.size}`;
  repaintAll();
}

function sendCmd(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    p3Status.textContent = 'WS 未连接，命令未发送';
    return;
  }
  ws.send(JSON.stringify(obj));
}

async function editLabel(rec) {
  const key = channelIdKey(rec.ch, rec.id);
  const current = labels.get(key) || '';
  const next = prompt(`${channelName(rec.ch)} ${idText(rec.id)} label`, current);
  if (next === null) return;
  const text = next.trim();
  if (text) labels.set(key, text);
  else labels.delete(key);
  sendCmd({ cmd: 'label_set', ch: channelName(rec.ch), id: rec.id, text });
  repaintAll();
}

function parseDelta(buf) {
  if (freezeView.checked || buf.byteLength < 2) return;
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    if (o + 45 > buf.byteLength) return;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    const lastRx = dv.getUint32(o, true); o += 4;
    const byteAge = [];
    for (let b = 0; b < 8; b++) { byteAge.push(dv.getUint16(o, true)); o += 2; }
    const countRx = dv.getUint32(o, true); o += 4;
    const deltaMs = dv.getUint16(o, true); o += 2;
    const periodMs = dv.getUint16(o, true); o += 2;
    const jitterMs = dv.getUint16(o, true); o += 2;
    const changeScore = dv.getUint16(o, true); o += 2;
    const flags = dv.getUint8(o); o += 1;
    const key = recordKey(ch, id);
    const rec = { key, ch, id, dlc, data, byteAge, count: countRx, lastRx, deltaMs, periodMs, jitterMs, changeScore, flags };
    records[key] = rec;
    paintRecord(rec);
  }
  sortTables();
}

function parseStats(buf) {
  if (buf.byteLength < 23) return;
  const dv = new DataView(buf);
  let o = 1;
  const fpsA = dv.getUint16(o, true); o += 2;
  const fpsB = dv.getUint16(o, true); o += 2;
  const loadA = dv.getUint16(o, true) / 10; o += 2;
  const loadB = dv.getUint16(o, true) / 10; o += 2;
  o += 4 + 4 + 1 + 1;
  const dropped = dv.getUint32(o, true);
  busStats.textContent = `A: ${fpsA}fps ${loadA.toFixed(1)}% · B: ${fpsB}fps ${loadB.toFixed(1)}% · dropped=${dropped}`;
}

function diffKindText(kind) {
  if (kind === 1) return 'added';
  if (kind === 2) return 'removed';
  if (kind === 3) return 'changed';
  return `kind ${kind}`;
}

function appendCell(tr, value, cls) {
  const td = document.createElement('td');
  if (cls) td.className = cls;
  td.textContent = String(value);
  tr.appendChild(td);
}

function appendIdWithLabel(tr, ch, id) {
  const td = document.createElement('td');
  td.appendChild(textNode(idText(id)));
  const label = labels.get(channelIdKey(ch, id));
  if (label) {
    const badge = document.createElement('span');
    badge.className = 'label-badge';
    badge.title = label;
    badge.textContent = label;
    td.appendChild(badge);
  }
  tr.appendChild(td);
}

function renderSnapshotDiffRows() {
  clearNode(snapshotBody);
  const totals = { added: 0, removed: 0, changed: 0 };
  for (const row of snapshotDiffRows) {
    if (!passesP3Filter(row)) continue;
    const text = diffKindText(row.kind);
    if (totals[text] !== undefined) totals[text]++;
    const tr = document.createElement('tr');
    appendCell(tr, channelName(row.ch));
    appendIdWithLabel(tr, row.ch, row.id);
    appendCell(tr, text, `kind-${text}`);
    appendCell(tr, row.dlcA);
    appendCell(tr, dataText(row.dataA, row.dlcA));
    appendCell(tr, row.dlcB);
    appendCell(tr, dataText(row.dataB, row.dlcB));
    snapshotBody.appendChild(tr);
  }
  snapshotSummary.textContent = `added=${totals.added} removed=${totals.removed} changed=${totals.changed}`;
}

function renderPretriggerRows() {
  clearNode(pretriggerBody);
  let shown = 0;
  let totalFrames = 0;
  let totalChanges = 0;
  const list = pretriggerRows
    .filter(passesP3Filter)
    .sort((a, b) => b.changes - a.changes || b.frames - a.frames || a.lastAgo - b.lastAgo || a.id - b.id);
  for (const row of list) {
    shown++;
    totalFrames += row.frames;
    totalChanges += row.changes;
    const tr = document.createElement('tr');
    appendCell(tr, channelName(row.ch));
    appendIdWithLabel(tr, row.ch, row.id);
    appendCell(tr, `${row.firstAgo}ms`);
    appendCell(tr, `${row.lastAgo}ms`);
    appendCell(tr, row.frames);
    appendCell(tr, row.changes);
    appendCell(tr, dataText(row.data, row.dlc));
    pretriggerBody.appendChild(tr);
  }
  pretriggerSummary.textContent = `records=${shown} frames=${totalFrames} changes=${totalChanges}`;
}

function parseDiff(buf, dv, count) {
  snapshotDiffRows = [];
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 21 > buf.byteLength) break;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const kind = dv.getUint8(o); o += 1;
    const dlcA = dv.getUint8(o); o += 1;
    const dataA = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    const dlcB = dv.getUint8(o); o += 1;
    const dataB = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    snapshotDiffRows.push({ ch, id, kind, dlcA, dataA, dlcB, dataB });
  }
  renderSnapshotDiffRows();
}

function parsePretrigger(buf, dv, count) {
  pretriggerRows = [];
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 20 > buf.byteLength) break;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const firstAgo = dv.getUint16(o, true); o += 2;
    const lastAgo = dv.getUint16(o, true); o += 2;
    const frames = dv.getUint16(o, true); o += 2;
    const changes = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    pretriggerRows.push({ ch, id, firstAgo, lastAgo, frames, changes, dlc, data });
  }
  renderPretriggerRows();
}

function parseBaseline(buf, dv, count) {
  let added = 0;
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 3 > buf.byteLength) break;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const key = channelIdKey(ch, id);
    if (!hidden.has(key)) added++;
    hidden.add(key);
  }
  p3Status.textContent = `baseline hidden +${added}, total=${hidden.size}`;
  repaintAll();
}

function parseP3(buf) {
  if (buf.byteLength < 3) return;
  const dv = new DataView(buf);
  const subtype = dv.getUint8(1);
  const count = dv.getUint8(2);
  if (subtype === 1) parseDiff(buf, dv, count);
  else if (subtype === 2) parsePretrigger(buf, dv, count);
  else if (subtype === 3) parseBaseline(buf, dv, count);
  else if (subtype === 4) loadLabels();
}

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => statusEl.textContent = 'WS: 已连接';
  ws.onclose = () => { statusEl.textContent = 'WS: 断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => {
    if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength === 0) return;
    const type = new DataView(ev.data).getUint8(0);
    if (type === 0x01) parseDelta(ev.data);
    if (type === 0x02) parseStats(ev.data);
    if (type === 0x03) parseP3(ev.data);
  };
}

function paintTxState() {
  const anyTx = txState.master && ((txState.a && txState.onlineA) || (txState.b && txState.onlineB));
  banner.className = 'banner ' + (anyTx ? 'tx' : 'listen');
  banner.textContent = anyTx ? '可发送（至少一个通道 TX 开启）' : '监听-only（TX 关闭）';
  masterToggle.classList.toggle('on', txState.master);
  txAToggle.classList.toggle('on', txState.a);
  txBToggle.classList.toggle('on', txState.b);
  txAToggle.disabled = !txState.onlineA;
  txBToggle.disabled = !txState.onlineB;
  busHealth.textContent = `CAN_A: ${txState.onlineA ? '在线' : '离线'} · CAN_B: ${txState.onlineB ? '在线' : '离线'}`;
}

async function refreshTxBanner() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    txState = {
      master: !!s.can_tx_enabled,
      a: !!s.tx_a_enabled,
      b: !!s.tx_b_enabled,
      onlineA: !!s.can_a_online,
      onlineB: !!s.can_b_online,
    };
    paintTxState();
  } catch (e) {}
}

async function loadLabels() {
  try {
    const r = await fetch('/api/labels');
    const list = await r.json();
    labels.clear();
    for (const item of Array.isArray(list) ? list : []) {
      const ch = item.ch === 'B' ? 'B' : 'A';
      const id = Number(item.id);
      const text = String(item.text || '');
      if (Number.isFinite(id) && text) labels.set(`${ch}:${id}`, text);
    }
    repaintAll();
  } catch (e) {
    p3Status.textContent = 'labels load failed';
  }
}

masterToggle.onclick = async () => {
  await fetch('/api/can-tx', { method: 'POST', body: txState.master ? 'false' : 'true' });
  refreshTxBanner();
};
txAToggle.onclick = async () => {
  await fetch('/api/can-tx-a', { method: 'POST', body: txState.a ? 'false' : 'true' });
  refreshTxBanner();
};
txBToggle.onclick = async () => {
  await fetch('/api/can-tx-b', { method: 'POST', body: txState.b ? 'false' : 'true' });
  refreshTxBanner();
};
banner.onclick = masterToggle.onclick;
baselineBtn.onclick = () => sendCmd({ cmd: 'baseline' });
snapABtn.onclick = () => sendCmd({ cmd: 'snapshot', slot: 'A' });
snapBBtn.onclick = () => sendCmd({ cmd: 'snapshot', slot: 'B' });
diffBtn.onclick = () => sendCmd({ cmd: 'diff' });
markBtn.onclick = () => sendCmd({ cmd: 'mark' });
baseSelect.onchange = repaintAll;
suppressStatic.onchange = repaintAll;
staticSeconds.onchange = repaintAll;
sortSelect.onchange = () => { repaintAll(); sortTables(); };
for (const el of [channelFilter, idFilter, rangeFrom, rangeTo, searchBox, whitelistOnly]) {
  el.oninput = repaintAll;
  el.onchange = repaintAll;
}

connect();
loadLabels();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
setInterval(() => { if (!freezeView.checked) repaintAll(); }, 500);
