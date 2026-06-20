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
const hideSelectedBtn = document.getElementById('hide-selected-btn');
const unhideAllBtn = document.getElementById('unhide-all-btn');
const hiddenInfo = document.getElementById('hidden-info');
const hiddenList = document.getElementById('hidden-list');
const selectAll = { 0: document.getElementById('select-all-a'), 1: document.getElementById('select-all-b') };
// 信号定位（bit 级噪音掩码）控件。
const learnBtn = document.getElementById('learn-btn');
const captureBtn = document.getElementById('capture-btn');
const analysisResetBtn = document.getElementById('analysis-reset-btn');
const analysisStatus = document.getElementById('analysis-status');
const candOnly = document.getElementById('cand-only');
// records 保存每个 (channel,id) 的最新数据；rows 保存对应 DOM 节点引用，避免每帧重建表格。
const rows = {};
const records = {};
// hiddenKeys：用户手动隐藏的 (channel,id) 拒绝列表，与筛选控件相互独立。键 = recordKey(ch,id)。
const hiddenKeys = new Set();
// flt：预编译的筛选条件，只在 applyFilters()（点确认/回车）时算一次。
// passesLocalFilters 每行只做数值比较，不再每帧重复正则解析 ID 文本。
// invalid=true 表示 ID/起始/结束 任一非空但解析失败 —— 此时全部隐藏（沿用原行为）。
const flt = {
  idExact: null, rangeFrom: null, rangeTo: null,
  search: '', invalid: false,
  metricIsActivity: false, metricMin: null, metricMax: null,
};
let ws = null;
// 'off' = 不分析；'learning' = 累积静止噪音位；'watching' = 冻结噪音、累积触发后变化位。
let analysisPhase = 'off';
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
  const analysisOn = analysisPhase !== 'off';
  let out = '';
  for (let byte = 0; byte < rec.dlc; byte++) {
    out += `B${byte}: `;
    for (let bit = 7; bit >= 0; bit--) {
      const one = (rec.data[byte] >> bit) & 1;
      const cand = (rec.candMask[byte] >> bit) & 1;
      const noise = analysisOn && ((rec.noiseMask[byte] >> bit) & 1);
      let cls = 'bit';
      if (one) cls += ' one';
      if (cand) cls += ' cand';
      else if (noise) cls += ' noise';
      out += `<span class="${cls}">${one}</span>`;
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
// ===== 信号定位（bit 级噪音掩码）=====
// 思路：先"学习静止噪音"几秒，记下静止时就在跳的 bit（计数器/校验/实时量）；再"开始抓取"后
// 触发车内操作，只看 candMask = moveMask & ~noiseMask —— 即"静止时稳定、触发后才变"的 bit。
// 因为是 bit 级 + 时间维度（静止 vs 触发），即便目标信号和噪音挤在同一条 ID/同一字节也能拎出来。
function popcount8(x) { x &= 0xff; x = x - ((x >> 1) & 0x55); x = (x & 0x33) + ((x >> 2) & 0x33); return (x + (x >> 4)) & 0x0f; }
function recomputeCand(rec) {
  let bits = 0;
  for (let b = 0; b < 8; b++) {
    const m = rec.moveMask[b] & ~rec.noiseMask[b] & 0xff;
    rec.candMask[b] = m;
    bits += popcount8(m);
  }
  rec.candBits = bits;
}
// clearNoise=true 连静止噪音一起清（学习/关闭）；false 只清触发掩码（进入抓取时冻结噪音）。
function clearRecAnalysis(rec, clearNoise) {
  for (let b = 0; b < 8; b++) {
    rec.moveMask[b] = 0;
    rec.candMask[b] = 0;
    if (clearNoise) rec.noiseMask[b] = 0;
  }
  rec.candBits = 0;
  rec.isNew = false;
}
function isCandRow(rec) { return rec.candBits > 0 || rec.isNew; }
// 候选 bit 的可读标签，如 B1.5 = 第1字节 bit5（与位视图 B{n}: 标注一致）。
function candBitLabels(rec) {
  const out = [];
  for (let b = 0; b < 8; b++)
    for (let bit = 0; bit < 8; bit++)
      if ((rec.candMask[b] >> bit) & 1) out.push('B' + b + '.' + bit);
  return out.join(',');
}
// 所有筛选都在浏览器本地完成：后端始终推送 dirty ID，前端决定是否显示。
// 通道实时读 DOM；其余条件读 flt 预编译值，点"确认"后才更新。
function passesLocalFilters(rec) {
  if (channelFilter.value === 'A' && rec.ch !== 0) return false;
  if (channelFilter.value === 'B' && rec.ch !== 1) return false;
  if (candOnly.checked && !isCandRow(rec)) return false;
  if (flt.invalid) return false;
  if (flt.idExact !== null && rec.id !== flt.idExact) return false;
  if (flt.rangeFrom !== null && rec.id < flt.rangeFrom) return false;
  if (flt.rangeTo !== null && rec.id > flt.rangeTo) return false;
  if (flt.search) {
    const idHex = idText(rec.id).toLowerCase();
    const idBare = hex(rec.id, 3).toLowerCase();
    if (!idHex.includes(flt.search) && !idBare.includes(flt.search)) return false;
  }
  const metricVal = flt.metricIsActivity ? rec.changeScore : rec.count;
  if (flt.metricMin !== null && metricVal < flt.metricMin) return false;
  if (flt.metricMax !== null && metricVal > flt.metricMax) return false;
  return true;
}
function rowHidden(rec) {
  return hiddenKeys.has(rec.key) || !passesLocalFilters(rec);
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
  bitTd.colSpan = 9;  // 新增了行末的勾选列
  bit.appendChild(bitTd);

  const idTd = document.createElement('td');
  const idSpan = document.createElement('span');
  idSpan.className = 'id-text';
  idSpan.textContent = idText(rec.id);
  idTd.appendChild(idSpan);
  const idCandSpan = document.createElement('span');
  idCandSpan.className = 'cand-bit-list';
  idTd.appendChild(idCandSpan);

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

  // 行末勾选列：勾选后可被"隐藏所选"收集。点勾选框不触发整行的位视图展开。
  const selTd = document.createElement('td');
  selTd.className = 'sel-col';
  const cb = document.createElement('input');
  cb.type = 'checkbox';
  cb.onclick = (e) => e.stopPropagation();
  selTd.appendChild(cb);

  tr.appendChild(idTd);
  tr.appendChild(dlcTd);
  tr.appendChild(dataTd);
  tr.appendChild(countTd);
  tr.appendChild(deltaTd);
  tr.appendChild(periodTd);
  tr.appendChild(jitterTd);
  tr.appendChild(scoreTd);
  tr.appendChild(selTd);

  tr.onclick = () => {
    const hiddenNow = bit.classList.toggle('hidden');
    bit.dataset.manualHidden = hiddenNow ? '1' : '0';
    if (!hiddenNow) {
      const r = records[key];
      if (r) bitTd.innerHTML = bitHtml(r);
    }
  };

  rows[key] = {
    tr, bit, bitTd, byteSpans, cb, key,idCandSpan,
    dlcTd, countTd, deltaTd, periodTd, jitterTd, scoreTd,
    lastClass: '',
  };
  tbody[rec.ch].appendChild(tr);
  tbody[rec.ch].appendChild(bit);
  needSort = true;
  return rows[key];
}
// 根据一条记录增量更新已有 DOM。隐藏行不刷新内容，节省不可见行的渲染成本。
// hiddenRow 可由调用方传入（已算过），省去一次 rowHidden/筛选计算。
function paintRecord(rec, hiddenRow) {
  const pair = ensureRows(rec);
  const tr = pair.tr;
  if (hiddenRow === undefined) hiddenRow = rowHidden(rec);

  const cls = rowClass(rec.changeScore);
  if (cls !== pair.lastClass) { tr.className = cls; pair.lastClass = cls; }
  tr.classList.toggle('hidden', hiddenRow);
  tr.classList.toggle('new-id', !!rec.isNew);

  if (hiddenRow || pair.bit.dataset.manualHidden !== '0') pair.bit.classList.add('hidden');
  else pair.bit.classList.remove('hidden');

  if (hiddenRow) return;

  for (let i = 0; i < 8; i++) {
    const s = pair.byteSpans[i];
    if (i < rec.dlc) {
      const txt = formatByte(rec.data[i]);
      if (s.textContent !== txt) s.textContent = txt;
      let c = ('byte ' + byteClass(rec.byteAge[i])).trim();
      if (rec.candMask[i]) c += ' cand';
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

  setText(pair.idCandSpan, rec.isNew ? '新ID' : (rec.candBits ? candBitLabels(rec) : ''));

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
          case 'cand':     return (b.candBits + (b.isNew ? 1000 : 0)) - (a.candBits + (a.isNew ? 1000 : 0)) || a.id - b.id;
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
        const hidden = rowHidden(rec);
        if (rows[rec.key] || !hidden) paintRecord(rec, hidden);
      }
    } else {
      for (const key of dirtyRecordKeys) {
        const rec = records[key];
        if (!rec) continue;
        const hidden = rowHidden(rec);
        if (rows[key] || !hidden) paintRecord(rec, hidden);
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
// 高频场景下原地复用 records[key] 及其 data/byteAge 数组，避免每条记录产生新对象/新数组的 GC 压力。
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
    const key = recordKey(ch, id);
    let rec = records[key];
    if (!rec) {
      rec = { key, ch, id, dlc: 0, data: new Array(8).fill(0), byteAge: new Array(8).fill(0),
        count: 0, lastRx: 0, deltaMs: 0, periodMs: 0, jitterMs: 0, changeScore: 0, flags: 0,
        noiseMask: new Array(8).fill(0), moveMask: new Array(8).fill(0), candMask: new Array(8).fill(0),
        candBits: 0, sampled: false, isNew: analysisPhase === 'watching' };
      records[key] = rec;
    }
    rec.dlc = dv.getUint8(o); o += 1;
    // 按帧把"相对上一次快照变化的 bit"累积进掩码：学习期进 noiseMask，抓取期进 moveMask。
    // sampled 守卫跳过该 ID 的首帧（否则会把首次出现的全部 1 误当成变化）。
    for (let b = 0; b < 8; b++) {
      const nb = dv.getUint8(o + b);
      if (rec.sampled && analysisPhase !== 'off') {
        const changed = (rec.data[b] ^ nb) & 0xff;
        if (changed) {
          if (analysisPhase === 'learning') rec.noiseMask[b] |= changed;
          else rec.moveMask[b] |= changed;
        }
      }
      rec.data[b] = nb;
    }
    o += 8;
    rec.sampled = true;
    if (analysisPhase === 'watching') {
      const had = rec.candBits > 0;
      recomputeCand(rec);
      if (had !== (rec.candBits > 0)) needSort = true;  // 候选集变化时触发重排
    }
    rec.lastRx = dv.getUint32(o, true); o += 4;
    for (let b = 0; b < 8; b++) { rec.byteAge[b] = dv.getUint16(o, true); o += 2; }
    rec.count = dv.getUint32(o, true); o += 4;
    rec.deltaMs = dv.getUint16(o, true); o += 2;
    rec.periodMs = dv.getUint16(o, true); o += 2;
    rec.jitterMs = dv.getUint16(o, true); o += 2;
    rec.changeScore = dv.getUint16(o, true); o += 2;
    rec.flags = dv.getUint8(o); o += 1;
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
  const errA = dv.getUint32(o, true); o += 4;  // rx_err_a：CAN_A(MCP2515) 溢出/错误事件数（低估真实丢帧）
  const errB = dv.getUint32(o, true); o += 4;  // rx_err_b：CAN_B(TWAI) rx_missed_count（精确丢帧数）
  o += 1 + 1;  // bus_off_a / bus_off_b 仍保留
  const dropped = dv.getUint32(o, true);
  const aLoss = errA > 0 ? ` 丢${errA}` : '';
  const bLoss = errB > 0 ? ` 丢${errB}` : '';
  busStats.textContent = `A: ${fpsA} 帧/秒 ${loadA.toFixed(1)}%${aLoss} · B: ${fpsB} 帧/秒 ${loadB.toFixed(1)}%${bLoss} · 队列丢弃=${dropped}`;
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

// 点确认/回车时把输入预编译进 flt（解析只在这里做一次），再整表重绘。
function applyFilters() {
  const idExact = parseId(idFilter.value);
  const from = parseId(rangeFrom.value);
  const to = parseId(rangeTo.value);
  flt.invalid =
    (idFilter.value.trim() !== '' && idExact === null) ||
    (rangeFrom.value.trim() !== '' && from === null) ||
    (rangeTo.value.trim() !== '' && to === null);
  flt.idExact = idExact;
  flt.rangeFrom = from;
  flt.rangeTo = to;
  flt.search = searchBox.value.trim().toLowerCase();
  flt.metricIsActivity = metricSelect.value === 'activity';
  const mn = Number(metricMin.value);
  const mx = Number(metricMax.value);
  flt.metricMin = (metricMin.value.trim() !== '' && Number.isFinite(mn)) ? mn : null;
  flt.metricMax = (metricMax.value.trim() !== '' && Number.isFinite(mx)) ? mx : null;
  repaintAll();
}
filterApplyBtn.onclick = applyFilters;
idApplyBtn.onclick = applyFilters;
for (const el of [idFilter, rangeFrom, rangeTo, searchBox, metricMin, metricMax]) {
  el.addEventListener('keydown', (e) => { if (e.key === 'Enter') applyFilters(); });
}

// ===== ID 隐藏（拒绝列表）=====
// 刷新"已隐藏"提示与可点击标签；点标签即把对应 ID 移出拒绝列表。
function updateHiddenInfo() {
  hiddenList.textContent = '';
  if (hiddenKeys.size === 0) {
    hiddenInfo.textContent = '未隐藏任何 ID';
    return;
  }
  hiddenInfo.textContent = `已隐藏 ${hiddenKeys.size} 个 ID（点标签取消）：`;
  for (const k of [...hiddenKeys].sort((a, b) => a - b)) {
    const ch = Math.floor(k / 4096);  // recordKey = ch*4096 + id
    const id = k % 4096;
    const chip = document.createElement('button');
    chip.className = 'hidden-chip';
    chip.textContent = `${ch === 0 ? 'A' : 'B'} ${idText(id)} ✕`;
    chip.onclick = () => { hiddenKeys.delete(k); updateHiddenInfo(); repaintAll(); };
    hiddenList.appendChild(chip);
  }
}
// 把所有已勾选的行加入拒绝列表，并清掉它们的勾选状态。
function hideSelected() {
  let added = 0;
  for (const k in rows) {
    const pair = rows[k];
    if (pair.cb && pair.cb.checked) {
      hiddenKeys.add(pair.key);
      pair.cb.checked = false;
      added++;
    }
  }
  selectAll[0].checked = false;
  selectAll[1].checked = false;
  if (added) { updateHiddenInfo(); repaintAll(); }
}
function unhideAll() {
  if (hiddenKeys.size === 0) return;
  hiddenKeys.clear();
  updateHiddenInfo();
  repaintAll();
}
// 表头全选：只勾选当前可见（未被筛掉、未被隐藏）的本通道行。
function setAllChecks(ch, checked) {
  for (const k in rows) {
    const pair = rows[k];
    const rec = records[k];
    if (!pair.cb || !rec || rec.ch !== ch || rowHidden(rec)) continue;
    pair.cb.checked = checked;
  }
}
hideSelectedBtn.onclick = hideSelected;
unhideAllBtn.onclick = unhideAll;
selectAll[0].onchange = () => setAllChecks(0, selectAll[0].checked);
selectAll[1].onchange = () => setAllChecks(1, selectAll[1].checked);
updateHiddenInfo();
// ===== 信号定位三段式控制 =====
// ① 学习静止噪音：清掉所有掩码，开始把静止时变化的 bit 记成噪音（车上先别动）。
// ② 开始抓取：冻结噪音掩码、清空触发掩码，之后去触发车内操作，看绿色候选位。
// 清除：回到关闭，所有掩码清零。
function setAnalysisStatus() {
  const txt = analysisPhase === 'learning' ? '学习静止噪音中…（车上先别操作）'
    : analysisPhase === 'watching' ? '抓取中：现在去触发车内操作，看绿色高亮位'
    : '关闭';
  analysisStatus.textContent = '分析：' + txt;
  learnBtn.classList.toggle('active', analysisPhase === 'learning');
  captureBtn.classList.toggle('active', analysisPhase === 'watching');
}
function setAnalysisPhase(phase, clearNoise) {
  analysisPhase = phase;
  for (const rec of Object.values(records)) clearRecAnalysis(rec, clearNoise);
  setAnalysisStatus();
  repaintAll();
}
learnBtn.onclick = () => setAnalysisPhase('learning', true);
captureBtn.onclick = () => setAnalysisPhase('watching', false);
analysisResetBtn.onclick = () => setAnalysisPhase('off', true);
candOnly.onchange = repaintAll;
setAnalysisStatus();

// 周期性刷新可见行的字节年龄颜色；冻结视图时完全停止刷新 DOM。
function refreshVisible() {
  if (freezeView.checked) return;
  for (const key in rows) {
    const rec = records[key];
    if (!rec) continue;
    const hidden = rowHidden(rec);
    if (!hidden) paintRecord(rec, hidden);
  }
  scheduleSort();
}
setInterval(refreshVisible, 500);

connect();
refreshWifiStatus();
refreshBusHealth();
setInterval(refreshBusHealth, 2000);