const busHealth = document.getElementById('bus-health');
const busStats = document.getElementById('bus-stats');
const statusEl = document.getElementById('status');
const baseSelect = document.getElementById('base-select');
const suppressStatic = document.getElementById('suppress-static');
const staticSeconds = document.getElementById('static-seconds');
const freezeView = document.getElementById('freeze-view');
const sortSelect = document.getElementById('sort-select');
const channelFilter = document.getElementById('channel-filter');
const idFilter = document.getElementById('id-filter');
const rangeFrom = document.getElementById('range-from');
const rangeTo = document.getElementById('range-to');
const searchBox = document.getElementById('search-box');
const wifiMode = document.getElementById('wifi-mode');
const wifiIp = document.getElementById('wifi-ip');
const wifiSsid = document.getElementById('wifi-ssid');
const wifiPass = document.getElementById('wifi-pass');
const wifiConnectBtn = document.getElementById('wifi-connect-btn');
const wifiRefreshBtn = document.getElementById('wifi-refresh-btn');
const deviceRestartBtn = document.getElementById('device-restart-btn');
const deviceShutdownBtn = document.getElementById('device-shutdown-btn');
const wifiStatus = document.getElementById('wifi-status');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const records = {};
let ws = null;
let needSort = false;
let sortQueued = false;
let recordRenderQueued = false;
let fullRecordRenderQueued = false;
const dirtyRecordKeys = new Set();
const lastOrder = { 0: [], 1: [] };

function setText(node, value) {
  const v = String(value);
  if (node.textContent !== v) node.textContent = v;
}
function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }
function printable(b) { return b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'; }
function recordKey(ch, id) { return ch * 4096 + id; }
function idText(id) { return '0x' + hex(id, 3); }

function parseBoundedIntText(text, max) {
  const raw = String(text || '').trim();
  if (!raw) throw new Error('请输入数值');
  const base = /^0x/i.test(raw) ? 16 : 10;
  const body = base === 16 ? raw.slice(2) : raw;
  const pattern = base === 16 ? /^[0-9a-fA-F]+$/ : /^[0-9]+$/;
  if (!body || !pattern.test(body)) throw new Error(`非法数值：${raw}`);
  const value = Number.parseInt(body, base);
  if (!Number.isInteger(value) || value < 0 || value > max) throw new Error(`数值超出范围：${raw}`);
  return value;
}
function parseId(text) {
  try { return parseBoundedIntText(text, 0x7ff); } catch (e) { return null; }
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
  const q = searchBox.value.trim().toLowerCase();
  if (q) {
    const idHex = idText(rec.id).toLowerCase();
    const idBare = hex(rec.id, 3).toLowerCase();
    if (!idHex.includes(q) && !idBare.includes(q)) return false;
  }
  return true;
}
function rowHidden(rec) {
  return isStatic(rec) || !passesLocalFilters(rec);
}
function ensureRows(rec) {
  const key = rec.key;
  if (rows[key]) return rows[key];

  const tr = document.createElement('tr');
  const bit = document.createElement('tr');
  bit.className = 'bit-view hidden';
  bit.dataset.manualHidden = '1';
  const bitTd = document.createElement('td');
  bitTd.colSpan = 8;
  bit.appendChild(bitTd);

  const idTd = document.createElement('td');
  const idSpan = document.createElement('span');
  idSpan.className = 'id-text';
  idSpan.textContent = idText(rec.id);
  idTd.appendChild(idSpan);

  const dataTd = document.createElement('td');
  const byteSpans = [];
  for (let i = 0; i < 8; i++) {
    const s = document.createElement('span');
    s.className = 'byte';
    s.style.display = 'none';
    dataTd.appendChild(s);
    byteSpans.push(s);
  }

  const dlcTd = document.createElement('td');
  const countTd = document.createElement('td');
  const deltaTd = document.createElement('td');
  const periodTd = document.createElement('td');
  const jitterTd = document.createElement('td');
  const scoreTd = document.createElement('td');

  tr.appendChild(idTd);
  tr.appendChild(dlcTd);
  tr.appendChild(dataTd);
  tr.appendChild(countTd);
  tr.appendChild(deltaTd);
  tr.appendChild(periodTd);
  tr.appendChild(jitterTd);
  tr.appendChild(scoreTd);

  tr.onclick = () => {
    const hiddenNow = bit.classList.toggle('hidden');
    bit.dataset.manualHidden = hiddenNow ? '1' : '0';
    if (!hiddenNow) {
      const r = records[key];
      if (r) bitTd.innerHTML = bitHtml(r);
    }
  };

  rows[key] = {
    tr, bit, bitTd, byteSpans,
    dlcTd, countTd, deltaTd, periodTd, jitterTd, scoreTd,
    lastClass: '',
  };
  tbody[rec.ch].appendChild(tr);
  tbody[rec.ch].appendChild(bit);
  needSort = true;
  return rows[key];
}
function paintRecord(rec) {
  const pair = ensureRows(rec);
  const tr = pair.tr;
  const hiddenRow = rowHidden(rec);

  const cls = rowClass(rec.changeScore);
  if (cls !== pair.lastClass) { tr.className = cls; pair.lastClass = cls; }
  tr.classList.toggle('hidden', hiddenRow);

  if (hiddenRow || pair.bit.dataset.manualHidden !== '0') pair.bit.classList.add('hidden');
  else pair.bit.classList.remove('hidden');

  if (hiddenRow) return;

  for (let i = 0; i < 8; i++) {
    const s = pair.byteSpans[i];
    if (i < rec.dlc) {
      const txt = formatByte(rec.data[i]);
      if (s.textContent !== txt) s.textContent = txt;
      const c = ('byte ' + byteClass(rec.byteAge[i])).trim();
      if (s.className !== c) s.className = c;
      if (s.style.display !== '') s.style.display = '';
    } else if (s.style.display !== 'none') {
      s.style.display = 'none';
    }
  }

  setText(pair.dlcTd, rec.dlc);
  setText(pair.countTd, rec.count);
  setText(pair.deltaTd, rec.deltaMs);
  setText(pair.periodTd, rec.periodMs);
  setText(pair.jitterTd, rec.jitterMs);
  setText(pair.scoreTd, rec.changeScore);

  if (!pair.bit.classList.contains('hidden')) pair.bitTd.innerHTML = bitHtml(rec);
}

function sortTables() {
  for (const ch of [0, 1]) {
    const list = Object.values(records)
      .filter(r => r.ch === ch && !rowHidden(r))
      .sort((a, b) => sortSelect.value === 'activity'
        ? (b.changeScore - a.changeScore || a.id - b.id)
        : (a.id - b.id));

    const order = list.map(r => r.key);
    const prev = lastOrder[ch];
    let same = order.length === prev.length;
    if (same) {
      for (let i = 0; i < order.length; i++) {
        if (order[i] !== prev[i]) { same = false; break; }
      }
    }
    if (same) continue;

    lastOrder[ch] = order;
    const frag = document.createDocumentFragment();
    for (const rec of list) {
      const pair = rows[rec.key];
      if (!pair) continue;
      frag.appendChild(pair.tr);
      frag.appendChild(pair.bit);
    }
    tbody[ch].appendChild(frag);
  }
  needSort = false;
}
function scheduleSort() {
  if (sortQueued) return;
  sortQueued = true;
  setTimeout(() => { sortQueued = false; sortTables(); }, 300);
}

function scheduleRecordRender(full = false) {
  if (full) fullRecordRenderQueued = true;
  if (recordRenderQueued) return;
  recordRenderQueued = true;
  requestAnimationFrame(() => {
    recordRenderQueued = false;
    if (fullRecordRenderQueued) {
      fullRecordRenderQueued = false;
      for (const rec of Object.values(records)) {
        if (rows[rec.key] || !rowHidden(rec)) paintRecord(rec);
      }
    } else {
      for (const key of dirtyRecordKeys) {
        const rec = records[key];
        if (rec && (rows[key] || !rowHidden(rec))) paintRecord(rec);
      }
    }
    dirtyRecordKeys.clear();
    if (needSort) scheduleSort();
  });
}

function repaintAll() {
  needSort = true;
  scheduleRecordRender(true);
  scheduleSort();
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
    records[key] = { key, ch, id, dlc, data, byteAge, count: countRx, lastRx, deltaMs, periodMs, jitterMs, changeScore, flags };
    dirtyRecordKeys.add(key);
  }
  scheduleRecordRender();
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
  busStats.textContent = `A: ${fpsA} 帧/秒 ${loadA.toFixed(1)}% · B: ${fpsB} 帧/秒 ${loadB.toFixed(1)}% · 丢弃=${dropped}`;
}

function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => { statusEl.textContent = 'WS：已连接'; };
  ws.onclose = () => { statusEl.textContent = 'WS：断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => {
    if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength === 0) return;
    const type = new DataView(ev.data).getUint8(0);
    if (type === 0x01) parseDelta(ev.data);
    if (type === 0x02) parseStats(ev.data);
  };
}
function wifiModeText(mode) {
  if (mode === 'sta') return '已连接路由器（STA）';
  if (mode === 'ap') return '热点模式（AP）';
  return '无线已关闭';
}

async function refreshWifiStatus() {
  try {
    const r = await fetch('/api/wifi');
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const s = await r.json();
    wifiMode.value = wifiModeText(s.mode);
    wifiIp.value = s.ip || '';
    wifiSsid.value = s.ssid || '';
    wifiPass.value = s.pass || '';
    wifiStatus.textContent = `网络状态：${wifiModeText(s.mode)} ${s.ip || ''}`;
  } catch (e) {
    wifiStatus.textContent = `网络状态获取失败：${e.message || e}`;
  }
}

async function connectWifi() {
  wifiConnectBtn.disabled = true;
  wifiStatus.textContent = '正在连接 WiFi…';
  try {
    const r = await fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid: wifiSsid.value.trim(), pass: wifiPass.value })
    });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const s = await r.json();
    wifiStatus.textContent = s.pending
      ? 'WiFi 配置已保存，设备正在切换网络；请稍后刷新状态，或打开新 IP / AP 地址。'
      : `连接请求已返回：${JSON.stringify(s)}`;
  } catch (e) {
    wifiStatus.textContent = `连接请求失败：${e.message || e}`;
  } finally {
    wifiConnectBtn.disabled = false;
  }
}

async function postDeviceAction(path, message, button) {
  if (button) button.disabled = true;
  try {
    const r = await fetch(path, { method: 'POST' });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    wifiStatus.textContent = message;
  } catch (e) {
    wifiStatus.textContent = `操作请求失败：${e.message || e}`;
    if (button) button.disabled = false;
  }
}

async function refreshBusHealth() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    busHealth.textContent = `CAN_A: ${s.can_a_online ? '在线' : '离线'} · CAN_B: ${s.can_b_online ? '在线' : '离线'}`;
  } catch (e) {}
}
wifiConnectBtn.onclick = connectWifi;
wifiRefreshBtn.onclick = refreshWifiStatus;
deviceRestartBtn.onclick = () => {
  if (confirm('确定要重启设备吗？网页会短暂断开。'))
    postDeviceAction('/api/restart', '设备正在重启…', deviceRestartBtn);
};
deviceShutdownBtn.onclick = () => {
  if (confirm('确定要关机（进入深度睡眠）吗？需要按复位或重新上电恢复。'))
    postDeviceAction('/api/shutdown', '设备正在进入深度睡眠…', deviceShutdownBtn);
};
baseSelect.onchange = repaintAll;
suppressStatic.onchange = repaintAll;
staticSeconds.onchange = repaintAll;
sortSelect.onchange = repaintAll;
for (const el of [channelFilter, idFilter, rangeFrom, rangeTo, searchBox]) {
  el.oninput = repaintAll;
  el.onchange = repaintAll;
}

function refreshVisible() {
  if (freezeView.checked) return;
  for (const key in rows) {
    const rec = records[key];
    if (rec && !rowHidden(rec)) paintRecord(rec);
  }
  scheduleSort();
}
setInterval(refreshVisible, 500);

connect();
refreshWifiStatus();
refreshBusHealth();
setInterval(refreshBusHealth, 2000);
