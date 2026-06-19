// 最小版 CAN 浏览器前端：只接收设备通过 WS 推送的帧增量/统计，
// 本地完成表格渲染、筛选、排序、WiFi/电源面板交互；不再包含 TX/录制/回放等控制功能。

const busHealth = document.getElementById('bus-health');
const busStats = document.getElementById('bus-stats');
const statusEl = document.getElementById('status');
const baseSelect = document.getElementById('base-select');
const metricSelect = document.getElementById('metric-select');
const metricMin = document.getElementById('metric-min');
const metricMax = document.getElementById('metric-max');
const filterApplyBtn = document.getElementById('filter-apply-btn');
const idApplyBtn = document.getElementById('id-apply-btn');
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
// records 保存每个 (channel,id) 的最新数据；rows 保存对应 DOM 节点引用，避免每帧重建表格。
const rows = {};
const records = {};
// applied：只在点"确认"（或回车）时更新的筛选快照。通道/进制/排序仍实时生效，
// 但 ID、起始、结束、搜索、计数/活跃度范围都从这里读取，避免边输入边重排。
const applied = {
  idFilter: '', rangeFrom: '', rangeTo: '', search: '',
  metric: 'count', metricMin: '', metricMax: '',
};
let ws = null;
let needSort = false;
let sortQueued = false;
let recordRenderQueued = false;
let fullRecordRenderQueued = false;
const dirtyRecordKeys = new Set();
const lastOrder = { 0: [], 1: [] };

// 只在文本变化时写 DOM，降低高频刷新时的 layout/repaint 压力。
function setText(node, value) {
  const v = String(value);
  if (node.textContent !== v) node.textContent = v;
}
function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }
function printable(b) { return b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'; }
function recordKey(ch, id) { return ch * 4096 + id; }
function idText(id) { return '0x' + hex(id, 3); }

// 支持十进制或 0x 前缀十六进制输入；筛选框解析失败时不抛到 UI，而是让过滤条件不匹配。
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
function rowClass(score) {
  if (score >= 20) return 'activity-high';
  if (score >= 5) return 'activity-med';
  return 'activity-low';
}
// 所有筛选都在浏览器本地完成：后端始终推送 dirty ID，前端决定是否显示。
// 通道实时读 DOM；其余条件读 applied 快照，点"确认"后才更新。
function passesLocalFilters(rec) {
  if (channelFilter.value === 'A' && rec.ch !== 0) return false;
  if (channelFilter.value === 'B' && rec.ch !== 1) return false;
  const exact = parseId(applied.idFilter);
  if (applied.idFilter.trim() && exact === null) return false;
  if (exact !== null && rec.id !== exact) return false;
  const from = parseId(applied.rangeFrom);
  const to = parseId(applied.rangeTo);
  if (applied.rangeFrom.trim() && from === null) return false;
  if (applied.rangeTo.trim() && to === null) return false;
  if (from !== null && rec.id < from) return false;
  if (to !== null && rec.id > to) return false;
  const q = applied.search.trim().toLowerCase();
  if (q) {
    const idHex = idText(rec.id).toLowerCase();
    const idBare = hex(rec.id, 3).toLowerCase();
    if (!idHex.includes(q) && !idBare.includes(q)) return false;
  }
  // 计数 / 活跃度 范围过滤：按下拉选中的指标，取空表示该侧不限。
  const metricVal = applied.metric === 'activity' ? rec.changeScore : rec.count;
  if (applied.metricMin !== '') {
    const mn = Number(applied.metricMin);
    if (Number.isFinite(mn) && metricVal < mn) return false;
  }
  if (applied.metricMax !== '') {
    const mx = Number(applied.metricMax);
    if (Number.isFinite(mx) && metricVal > mx) return false;
  }
  return true;
}
function rowHidden(rec) {
  return !passesLocalFilters(rec);
}
// 懒创建每个 ID 的主行 + 位视图行。
// 创建后只更新文本/样式和重排位置，避免高频 WS 下反复创建/销毁 DOM。
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
// 根据一条记录增量更新已有 DOM。隐藏行不刷新内容，节省不可见行的渲染成本。
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

// 排序只在顺序真的变化时移动 DOM。用 DocumentFragment 一次性重排，减少 reflow 次数。
function sortTables() {
  for (const ch of [0, 1]) {
    const list = Object.values(records)
      .filter(r => r.ch === ch && !rowHidden(r))
      .sort((a, b) => {
        switch (sortSelect.value) {
          case 'activity': return b.changeScore - a.changeScore || a.id - b.id;
          case 'count':    return a.count - b.count || a.id - b.id;
          default:         return a.id - b.id;
        }
      });

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

// 渲染调度合并到 requestAnimationFrame：WS 可以高频到达，但 DOM 每帧最多刷一次。
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
// 解析 WS_MSG_FRAME_DELTA。每条 WsFrameRecord 固定 45 字节，字段顺序必须与 ws_protocol.h 保持一致。
function parseDelta(buf) {
  if (freezeView.checked || buf.byteLength < 2) return;
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    if (o + 45 > buf.byteLength) return;  // 半包/损坏包直接丢弃，等待下一条完整 WS 消息。
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

// 解析 WS_MSG_BUS_STATS。中间 10 字节为后端保留的 rx_err/bus_off 占位字段。
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

// 建立只读 WS 连接；断线后 1 秒重连。前端不再向设备发送任何 WS 命令。
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

// 从设备读取当前网络模式/IP 与已保存凭据，用于面板回显。
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

// 提交新 STA 凭据。后端返回 pending 后会在主循环里保存并切网，当前页面可能短暂失联。
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

// 重启/关机是不可逆的本次会话动作：点击前二次确认，提交后按钮保持禁用。
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
sortSelect.onchange = repaintAll;
channelFilter.oninput = repaintAll;
channelFilter.onchange = repaintAll;

// 把当前输入快照进 applied 再整表重绘：ID/起始/结束/搜索、计数/活跃度范围都在此"确认"后才生效。
function applyFilters() {
  applied.idFilter = idFilter.value;
  applied.rangeFrom = rangeFrom.value;
  applied.rangeTo = rangeTo.value;
  applied.search = searchBox.value;
  applied.metric = metricSelect.value;
  applied.metricMin = metricMin.value.trim();
  applied.metricMax = metricMax.value.trim();
  repaintAll();
}
filterApplyBtn.onclick = applyFilters;
idApplyBtn.onclick = applyFilters;
for (const el of [idFilter, rangeFrom, rangeTo, searchBox, metricMin, metricMax]) {
  el.addEventListener('keydown', (e) => { if (e.key === 'Enter') applyFilters(); });
}

// 周期性刷新可见行的字节年龄颜色；冻结视图时完全停止刷新 DOM。
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