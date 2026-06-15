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
const recordStartBtn = document.getElementById('record-start-btn');
const recordStopBtn = document.getElementById('record-stop-btn');
const recordDownload = document.getElementById('record-download');
const recordStatusEl = document.getElementById('record-status');
const wifiMode = document.getElementById('wifi-mode');
const wifiIp = document.getElementById('wifi-ip');
const wifiSsid = document.getElementById('wifi-ssid');
const wifiPass = document.getElementById('wifi-pass');
const wifiConnectBtn = document.getElementById('wifi-connect-btn');
const wifiRefreshBtn = document.getElementById('wifi-refresh-btn');
const deviceRestartBtn = document.getElementById('device-restart-btn');
const deviceShutdownBtn = document.getElementById('device-shutdown-btn');
const wifiStatus = document.getElementById('wifi-status');
const snapshotSummary = document.getElementById('snapshot-summary');
const pretriggerSummary = document.getElementById('pretrigger-summary');
const snapshotBody = document.querySelector('#snapshot-diff tbody');
const pretriggerBody = document.querySelector('#pretrigger tbody');
const signalTargetEl = document.getElementById('signal-target');
const signalStatusEl = document.getElementById('signal-status');
const signalHintsBtn = document.getElementById('signal-hints-btn');
const signalExportBtn = document.getElementById('signal-export-btn');
const signalImportBtn = document.getElementById('signal-import-btn');
const signalImportFile = document.getElementById('signal-import-file');
const signalLoadCommonBtn = document.getElementById('signal-load-common-btn');
const signalSaveCommonBtn = document.getElementById('signal-save-common-btn');
const sigLabel = document.getElementById('sig-label');
const sigStartBit = document.getElementById('sig-start-bit');
const sigBitLength = document.getElementById('sig-bit-length');
const sigEndian = document.getElementById('sig-endian');
const sigSigned = document.getElementById('sig-signed');
const sigScale = document.getElementById('sig-scale');
const sigOffset = document.getElementById('sig-offset');
const sigDisplay = document.getElementById('sig-display');
const sigSaveBtn = document.getElementById('sig-save-btn');
const signalSpecsEl = document.getElementById('signal-specs');
const signalDecodeEl = document.getElementById('signal-decode');
const signalHintsEl = document.getElementById('signal-hints');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const records = {};
const labels = new Map();
const hidden = new Set();
const whitelist = new Set();
let snapshotDiffRows = [];
let pretriggerRows = [];
let signalTarget = null;
let signalSamples = [];
let signalHints = [];
let signalSpecs = [];
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

function targetKey(ch, id) { return `${channelName(ch)}:${id}`; }
function targetMatches(target, ch, id) { return !!target && target.ch === ch && target.id === id; }
function endianToWire(endian) { return endian === 'motorola' || endian === 1 || endian === '1' ? 1 : 0; }
function endianToText(endian) { return endian === 1 || endian === '1' || endian === 'motorola' ? 'motorola' : 'intel'; }
function signedToBool(value) { return value === true || value === 1 || value === '1'; }
function displayToText(value) { return value === 'raw' || value === 'step' ? value : 'line'; }
function endianLabel(value) { return endianToText(value) === 'motorola' ? 'Motorola 大端' : 'Intel 小端'; }
function displayLabel(value) {
  const v = displayToText(value);
  if (v === 'raw') return '原始值';
  if (v === 'step') return '阶梯';
  return '折线';
}
function statusSignal(text) { signalStatusEl.textContent = `${text} · 浏览器工作集=${signalSpecs.length}`; }

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
  const isWhitelisted = whitelist.has(key);
  if (whitelistOnly.checked && !isWhitelisted) return false;
  if (!isWhitelisted && hidden.has(key)) return false;
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
  cell.className = 'selectable-id';
  cell.title = '选择信号工作台目标';
  cell.onclick = (ev) => { ev.stopPropagation(); selectSignalTarget(rec.ch, rec.id); };
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
  w.textContent = '白';
  w.title = '切换白名单';
  w.onclick = (ev) => { ev.stopPropagation(); toggleWhitelist(rec); };
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = '隐';
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
  p3Status.textContent = `白名单=${whitelist.size}`;
  repaintAll();
}

function hideRecord(rec) {
  hidden.add(channelIdKey(rec.ch, rec.id));
  p3Status.textContent = `已隐藏=${hidden.size}`;
  repaintAll();
}

function sendCmd(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    p3Status.textContent = 'WS 未连接，命令未发送';
    if (String(obj.cmd || '').startsWith('p4_')) statusSignal('WS 未连接，信号命令未发送');
    return false;
  }
  ws.send(JSON.stringify(obj));
  return true;
}

function sendSignalWatch(target, on) {
  if (!target) return;
  sendCmd({ cmd: 'p4_watch', ch: channelName(target.ch), id: target.id, on: !!on });
}

function selectSignalTarget(ch, id) {
  if (targetMatches(signalTarget, ch, id)) return;
  const previous = signalTarget;
  signalTarget = { ch, id };
  signalSamples = [];
  signalHints = [];
  if (previous) sendSignalWatch(previous, false);
  sendSignalWatch(signalTarget, true);
  statusSignal(`正在观察 ${channelName(ch)} ${idText(id)}`);
  renderSignalWorkbench();
}

function specsForTarget() {
  if (!signalTarget) return [];
  return signalSpecs.filter(spec => targetMatches(spec, signalTarget.ch, signalTarget.id));
}

function normalizeSignalSpec(src, fallbackTarget) {
  const chToken = src.ch !== undefined ? src.ch : src.channel;
  const ch = chToken === 'B' || chToken === 1 || chToken === '1' ? 1 : (chToken === 'A' || chToken === 0 || chToken === '0' ? 0 : (fallbackTarget ? fallbackTarget.ch : null));
  const id = Number(src.id !== undefined ? src.id : (fallbackTarget ? fallbackTarget.id : NaN));
  const startBit = Number(src.start_bit);
  const bitLength = Number(src.bit_length);
  const scale = Number(src.scale === undefined ? 1 : src.scale);
  const offset = Number(src.offset === undefined ? 0 : src.offset);
  const label = String(src.label || '').trim();
  const display = displayToText(src.display);
  if (ch !== 0 && ch !== 1) return null;
  if (!Number.isInteger(id) || id < 0 || id > 0x7ff) return null;
  if (!Number.isInteger(startBit) || startBit < 0 || startBit > 63) return null;
  if (!Number.isInteger(bitLength) || bitLength < 1 || bitLength > 64 || startBit + bitLength > 64) return null;
  if (!Number.isFinite(scale) || !Number.isFinite(offset)) return null;
  if (!label) return null;
  return {
    label,
    ch,
    id,
    start_bit: startBit,
    bit_length: bitLength,
    endian: endianToText(src.endian),
    signed: signedToBool(src.signed),
    scale,
    offset,
    display,
  };
}

function currentFormSpec() {
  if (!signalTarget) return null;
  return normalizeSignalSpec({
    label: sigLabel.value,
    ch: signalTarget.ch,
    id: signalTarget.id,
    start_bit: Number(sigStartBit.value),
    bit_length: Number(sigBitLength.value),
    endian: sigEndian.value,
    signed: sigSigned.checked,
    scale: Number(sigScale.value),
    offset: Number(sigOffset.value),
    display: sigDisplay.value,
  }, signalTarget);
}

function fillSignalForm(spec) {
  sigLabel.value = spec.label || '';
  sigStartBit.value = spec.start_bit;
  sigBitLength.value = spec.bit_length;
  sigEndian.value = endianToText(spec.endian);
  sigSigned.checked = signedToBool(spec.signed);
  sigScale.value = spec.scale;
  sigOffset.value = spec.offset;
  sigDisplay.value = displayToText(spec.display);
}

function saveFormSpec() {
  const spec = currentFormSpec();
  if (!spec) {
    statusSignal('规格无效：需要目标、信号名、有效位范围，以及数值比例/偏移');
    return;
  }
  const idx = signalSpecs.findIndex(item => item.ch === spec.ch && item.id === spec.id && item.label === spec.label);
  if (idx >= 0) signalSpecs[idx] = spec;
  else signalSpecs.push(spec);
  statusSignal(idx >= 0 ? `已更新 ${spec.label}` : `已新增 ${spec.label}`);
  renderSignalWorkbench();
}

function signalExtractUnsigned(data, startBit, bitLength, endian) {
  if (bitLength < 1 || bitLength > 64 || startBit < 0 || startBit + bitLength > 64) return 0n;
  let raw = 0n;
  if (endianToText(endian) === 'intel') {
    for (let i = 0; i < bitLength; i++) {
      const bitIndex = startBit + i;
      const byteIndex = Math.floor(bitIndex / 8);
      const bitInByte = bitIndex % 8;
      const bit = (BigInt(data[byteIndex] || 0) >> BigInt(bitInByte)) & 1n;
      raw |= bit << BigInt(i);
    }
    return raw;
  }
  for (let i = 0; i < bitLength; i++) {
    const bitIndex = startBit + i;
    const byteIndex = Math.floor(bitIndex / 8);
    const bitInByte = 7 - (bitIndex % 8);
    const bit = (BigInt(data[byteIndex] || 0) >> BigInt(bitInByte)) & 1n;
    raw = (raw << 1n) | bit;
  }
  return raw;
}

function signalSignExtend(raw, bitLength) {
  if (!bitLength) return 0n;
  if (bitLength === 64) return BigInt.asIntN(64, raw);
  const signBit = 1n << BigInt(bitLength - 1);
  return (raw & signBit) ? (raw | (~0n << BigInt(bitLength))) : raw;
}

function decodeSignalSample(sample, spec) {
  const rawUnsigned = signalExtractUnsigned(sample.data, spec.start_bit, spec.bit_length, spec.endian);
  const rawSigned = spec.signed ? signalSignExtend(rawUnsigned, spec.bit_length) : rawUnsigned;
  const raw = Number(rawSigned);
  return {
    rawText: rawSigned.toString(),
    value: raw * spec.scale + spec.offset,
  };
}

function formatValue(value) {
  if (!Number.isFinite(value)) return '无';
  if (Math.abs(value) >= 1000 || Math.abs(value) < 0.01) return value.toExponential(3);
  return value.toFixed(3).replace(/\.0+$/, '').replace(/(\.\d*?)0+$/, '$1');
}

function sparklineSvg(values, display) {
  const w = 260;
  const h = 68;
  if (values.length < 2) return `<svg class="sparkline" viewBox="0 0 ${w} ${h}"><text x="8" y="38">至少需要 2 个样本</text></svg>`;
  const min = Math.min(...values);
  const max = Math.max(...values);
  const span = max === min ? 1 : max - min;
  const xFor = i => (i * (w - 8)) / (values.length - 1) + 4;
  const yFor = v => h - 6 - ((v - min) * (h - 12)) / span;
  let points = '';
  if (display === 'step') {
    const step = [];
    for (let i = 0; i < values.length; i++) {
      const x = xFor(i);
      const y = yFor(values[i]);
      if (i > 0) step.push(`${x},${yFor(values[i - 1])}`);
      step.push(`${x},${y}`);
    }
    points = step.join(' ');
  } else {
    points = values.map((v, i) => `${xFor(i)},${yFor(v)}`).join(' ');
  }
  return `<svg class="sparkline" viewBox="0 0 ${w} ${h}"><polyline points="${points}"/></svg>`;
}

function renderSignalSpecs() {
  clearNode(signalSpecsEl);
  const specs = specsForTarget();
  signalSpecsEl.classList.toggle('empty', specs.length === 0);
  if (!signalTarget) {
    signalSpecsEl.textContent = '未选中目标';
    return;
  }
  if (specs.length === 0) {
    signalSpecsEl.textContent = '当前目标没有浏览器工作集规格';
    return;
  }
  for (const spec of specs) {
    const item = document.createElement('div');
    item.className = 'signal-item';
    const meta = document.createElement('div');
    meta.textContent = `${spec.label} · ${endianLabel(spec.endian)}${spec.signed ? ' · 有符号' : ' · 无符号'} · 位 ${spec.start_bit}+${spec.bit_length} · 比例×${spec.scale} · 偏移 ${spec.offset} · ${displayLabel(spec.display)}`;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.textContent = '编辑';
    btn.onclick = () => fillSignalForm(spec);
    item.appendChild(meta);
    item.appendChild(btn);
    signalSpecsEl.appendChild(item);
  }
}

function renderSignalDecode() {
  clearNode(signalDecodeEl);
  const specs = specsForTarget();
  signalDecodeEl.classList.toggle('empty', !signalTarget || specs.length === 0 || signalSamples.length === 0);
  if (!signalTarget) {
    signalDecodeEl.textContent = '未选中目标';
    return;
  }
  if (signalSamples.length === 0) {
    signalDecodeEl.textContent = '等待信号样本（可点击“请求候选”获取最近样本）';
    return;
  }
  if (specs.length === 0) {
    signalDecodeEl.textContent = '当前目标没有信号规格，保存手动信号后即可即时解码';
    return;
  }
  for (const spec of specs) {
    const decoded = signalSamples.map(sample => decodeSignalSample(sample, spec));
    const values = decoded.map(d => spec.display === 'raw' ? Number(d.rawText) : d.value).filter(Number.isFinite);
    const current = decoded[decoded.length - 1];
    const min = values.length ? Math.min(...values) : NaN;
    const max = values.length ? Math.max(...values) : NaN;
    const item = document.createElement('div');
    item.className = 'signal-item signal-decode-item';
    const line = document.createElement('div');
    const name = document.createElement('strong');
    name.textContent = spec.label;
    line.appendChild(name);
    line.appendChild(textNode(` 当前=${formatValue(spec.display === 'raw' ? Number(current.rawText) : current.value)} 原始=${current.rawText} 最小=${formatValue(min)} 最大=${formatValue(max)} 样本=${signalSamples.length}`));
    item.appendChild(line);
    item.insertAdjacentHTML('beforeend', sparklineSvg(values, spec.display));
    signalDecodeEl.appendChild(item);
  }
}

function hintKindText(kind) {
  if (kind === 1) return '多路复用字段';
  if (kind === 2) return '滚动计数器';
  if (kind === 3) return '校验候选';
  return `类型 ${kind}`;
}

function renderSignalHints() {
  clearNode(signalHintsEl);
  signalHintsEl.classList.toggle('empty', signalHints.length === 0);
  if (!signalTarget) {
    signalHintsEl.textContent = '未选中目标';
    return;
  }
  if (signalHints.length === 0) {
    signalHintsEl.textContent = '暂无候选提示';
    return;
  }
  for (const hint of signalHints) {
    const item = document.createElement('div');
    item.className = 'signal-item';
    const text = document.createElement('div');
    text.textContent = `${hintKindText(hint.kind)} · 位 ${hint.start_bit}+${hint.bit_length} · 置信度=${hint.confidence.toFixed(3)} · 依据=${hint.evidence || '-'}`;
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.textContent = '带入表单';
    btn.onclick = () => {
      sigStartBit.value = hint.start_bit;
      sigBitLength.value = hint.bit_length;
      statusSignal('候选已带入表单，尚未自动保存');
    };
    item.appendChild(text);
    item.appendChild(btn);
    signalHintsEl.appendChild(item);
  }
}

function renderSignalWorkbench() {
  if (signalTarget) signalTargetEl.textContent = `当前目标：${channelName(signalTarget.ch)} ${idText(signalTarget.id)}`;
  else signalTargetEl.textContent = '未选中目标：点击实时表行或 快照/回看结果 ID 单元选择（通道,ID）';
  signalHintsBtn.disabled = !signalTarget;
  sigSaveBtn.disabled = !signalTarget;
  renderSignalSpecs();
  renderSignalDecode();
  renderSignalHints();
}

function parseSignalSamples(buf, dv, count) {
  if (count === 0) signalSamples = [];
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 18 > buf.byteLength) break;
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = Array.from(new Uint8Array(buf.slice(o, o + 8))); o += 8;
    const sampleAgeMs = dv.getUint16(o, true); o += 2;
    const sequence = dv.getUint32(o, true); o += 4;
    if (targetMatches(signalTarget, ch, id)) signalSamples.push({ ch, id, dlc, data, sampleAgeMs, sequence });
  }
  signalSamples.sort((a, b) => a.sequence - b.sequence);
  if (signalSamples.length > 96) signalSamples = signalSamples.slice(signalSamples.length - 96);
  renderSignalDecode();
}

function parseSignalHints(buf, dv, count) {
  signalHints = [];
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 21 > buf.byteLength) break;
    const kind = dv.getUint8(o); o += 1;
    const startBit = dv.getUint8(o); o += 1;
    const bitLength = dv.getUint8(o); o += 1;
    const confidence = dv.getUint16(o, true) / 1000; o += 2;
    const bytes = Array.from(new Uint8Array(buf.slice(o, o + 16))); o += 16;
    const zero = bytes.indexOf(0);
    const evidenceBytes = zero >= 0 ? bytes.slice(0, zero) : bytes;
    const evidence = String.fromCharCode(...evidenceBytes);
    signalHints.push({ kind, start_bit: startBit, bit_length: bitLength, confidence, evidence });
  }
  renderSignalHints();
}

function parseSignal(buf) {
  if (buf.byteLength < 3) return;
  const dv = new DataView(buf);
  const subtype = dv.getUint8(1);
  const count = dv.getUint8(2);
  if (subtype === 1) parseSignalSamples(buf, dv, count);
  else if (subtype === 2) parseSignalHints(buf, dv, count);
}

function exportSignalSpecs() {
  const doc = { version: 1, exported_at: new Date().toISOString(), signals: signalSpecs };
  const blob = new Blob([JSON.stringify(doc, null, 2)], { type: 'application/json' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = `signal-workset-${Date.now()}.json`;
  a.click();
  URL.revokeObjectURL(a.href);
  statusSignal('已导出浏览器工作集 JSON');
}

function importSignalDoc(doc) {
  if (!doc || doc.version !== 1 || !Array.isArray(doc.signals)) {
    statusSignal('导入失败：JSON 需要包含 {version:1, signals:[...]}');
    return false;
  }
  const next = [];
  for (const item of doc.signals) {
    const spec = normalizeSignalSpec(item, null);
    if (!spec) {
      statusSignal('导入失败：signals 中存在非法规格，整份已拒绝');
      return false;
    }
    next.push(spec);
  }
  signalSpecs = next;
  statusSignal(`已导入 ${signalSpecs.length} 条规格（替换当前浏览器工作集）`);
  renderSignalWorkbench();
  return true;
}

async function loadCommonSignals() {
  try {
    const r = await fetch('/api/p4/common');
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    const doc = await r.json();
    const signals = Array.isArray(doc.signals) ? doc.signals : [];
    const next = [];
    for (const item of signals) {
      const spec = normalizeSignalSpec({ ...item, display: item.display || 'line' }, null);
      if (spec) next.push(spec);
    }
    signalSpecs = next;
    statusSignal(`已加载设备常用项 ${signalSpecs.length} 条（替换当前浏览器工作集）`);
    renderSignalWorkbench();
  } catch (e) {
    statusSignal(`加载设备常用项失败：${e.message || e}`);
  }
}

async function saveCommonSignals() {
  try {
    const signals = signalSpecs.map(spec => ({
      ch: channelName(spec.ch),
      id: spec.id,
      label: spec.label,
      start_bit: spec.start_bit,
      bit_length: spec.bit_length,
      endian: endianToWire(spec.endian),
      signed: spec.signed ? 1 : 0,
      scale: spec.scale,
      offset: spec.offset,
    }));
    const r = await fetch('/api/p4/common', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ signals }),
    });
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    statusSignal(`已保存 ${signals.length} 条到设备常用项`);
  } catch (e) {
    statusSignal(`保存设备常用项失败：${e.message || e}`);
  }
}

async function editLabel(rec) {
  const key = channelIdKey(rec.ch, rec.id);
  const current = labels.get(key) || '';
  const next = prompt(`${channelName(rec.ch)} ${idText(rec.id)} 标注`, current);
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
  busStats.textContent = `A: ${fpsA} 帧/秒 ${loadA.toFixed(1)}% · B: ${fpsB} 帧/秒 ${loadB.toFixed(1)}% · 丢弃=${dropped}`;
}

function diffKindKey(kind) {
  if (kind === 1) return 'added';
  if (kind === 2) return 'removed';
  if (kind === 3) return 'changed';
  return 'unknown';
}
function diffKindText(kind) {
  if (kind === 1) return '新增';
  if (kind === 2) return '消失';
  if (kind === 3) return '变化';
  return `类型 ${kind}`;
}

function appendCell(tr, value, cls) {
  const td = document.createElement('td');
  if (cls) td.className = cls;
  td.textContent = String(value);
  tr.appendChild(td);
}

function appendIdWithLabel(tr, ch, id) {
  const td = document.createElement('td');
  td.className = 'selectable-id';
  td.title = '选择信号工作台目标';
  td.onclick = (ev) => { ev.stopPropagation(); selectSignalTarget(ch, id); };
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
    const key = diffKindKey(row.kind);
    const text = diffKindText(row.kind);
    if (totals[key] !== undefined) totals[key]++;
    const tr = document.createElement('tr');
    appendCell(tr, channelName(row.ch));
    appendIdWithLabel(tr, row.ch, row.id);
    appendCell(tr, text, `kind-${key}`);
    appendCell(tr, row.dlcA);
    appendCell(tr, dataText(row.dataA, row.dlcA));
    appendCell(tr, row.dlcB);
    appendCell(tr, dataText(row.dataB, row.dlcB));
    snapshotBody.appendChild(tr);
  }
  snapshotSummary.textContent = `新增=${totals.added} 消失=${totals.removed} 变化=${totals.changed}`;
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
  pretriggerSummary.textContent = `记录=${shown} 帧数=${totalFrames} 变化=${totalChanges}`;
}

function clearSnapshotResults() {
  snapshotDiffRows = [];
  renderSnapshotDiffRows();
}

function clearPretriggerResults() {
  pretriggerRows = [];
  renderPretriggerRows();
}

function parseDiff(buf, dv, count) {
  let o = 3;
  for (let i = 0; i < count; i++) {
    if (o + 22 > buf.byteLength) break;
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
  p3Status.textContent = `基线隐藏 +${added}，总计=${hidden.size}`;
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
  ws.onopen = () => {
    statusEl.textContent = 'WS：已连接';
    if (signalTarget) sendSignalWatch(signalTarget, true);
  };
  ws.onclose = () => { statusEl.textContent = 'WS：断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => {
    if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength === 0) return;
    const type = new DataView(ev.data).getUint8(0);
    if (type === 0x01) parseDelta(ev.data);
    if (type === 0x02) parseStats(ev.data);
    if (type === 0x03) parseP3(ev.data);
    if (type === 0x04) parseSignal(ev.data);
  };
}

function paintTxState() {
  const anyTx = txState.master && ((txState.a && txState.onlineA) || (txState.b && txState.onlineB));
  banner.className = 'banner ' + (anyTx ? 'tx' : 'listen');
  banner.textContent = anyTx ? '可发送（至少一个通道 TX 开启）' : '只监听模式（TX 关闭）';
  masterToggle.classList.toggle('on', txState.master);
  txAToggle.classList.toggle('on', txState.a);
  txBToggle.classList.toggle('on', txState.b);
  txAToggle.disabled = !txState.onlineA;
  txBToggle.disabled = !txState.onlineB;
  busHealth.textContent = `CAN_A: ${txState.onlineA ? '在线' : '离线'} · CAN_B: ${txState.onlineB ? '在线' : '离线'}`;
}

function wifiModeText(mode) {
  if (mode === 'sta') return '已连接路由器（STA）';
  if (mode === 'ap') return '热点模式（AP）';
  return '无线已关闭';
}

async function refreshWifiStatus() {
  try {
    const r = await fetch('/api/wifi');
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

async function postDeviceAction(path, message) {
  try {
    await fetch(path, { method: 'POST' });
    wifiStatus.textContent = message;
  } catch (e) {
    wifiStatus.textContent = `操作请求失败：${e.message || e}`;
  }
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
    paintRecordStatus(s);
  } catch (e) {}
}

function paintRecordStatus(s) {
  const recording = !!s.recording;
  const count = Number(s.record_count || 0);
  const dropped = Number(s.record_dropped || 0);
  recordStartBtn.disabled = recording;
  recordStopBtn.disabled = !recording;
  if (recording) {
    recordStatusEl.textContent = `录制中 · ${count} 帧 · 丢弃=${dropped}`;
  } else if (count > 0) {
    recordStatusEl.textContent = `空闲 · ${count} 帧可下载 · 丢弃=${dropped}`;
  } else {
    recordStatusEl.textContent = '录制：空闲';
  }
  const canDownload = !recording && count > 0;
  recordDownload.classList.toggle('disabled', !canDownload);
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
    p3Status.textContent = '标注加载失败';
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
wifiConnectBtn.onclick = connectWifi;
wifiRefreshBtn.onclick = refreshWifiStatus;
deviceRestartBtn.onclick = () => {
  if (confirm('确定要重启设备吗？网页会短暂断开。'))
    postDeviceAction('/api/restart', '设备正在重启…');
};
deviceShutdownBtn.onclick = () => {
  if (confirm('确定要关机（进入深度睡眠）吗？需要按复位或重新上电恢复。'))
    postDeviceAction('/api/shutdown', '设备正在进入深度睡眠…');
};
recordStartBtn.onclick = () => {
  if (sendCmd({ cmd: 'record_start' })) setTimeout(refreshTxBanner, 100);
};
recordStopBtn.onclick = () => {
  if (sendCmd({ cmd: 'record_stop' })) setTimeout(refreshTxBanner, 100);
};
recordDownload.addEventListener('click', (e) => {
  if (recordDownload.classList.contains('disabled')) e.preventDefault();
});
baselineBtn.onclick = () => {
  p3Status.textContent = '已请求设置基线';
  sendCmd({ cmd: 'baseline' });
};
snapABtn.onclick = () => sendCmd({ cmd: 'snapshot', slot: 'A' });
snapBBtn.onclick = () => sendCmd({ cmd: 'snapshot', slot: 'B' });
diffBtn.onclick = () => {
  clearSnapshotResults();
  sendCmd({ cmd: 'diff' });
};
markBtn.onclick = () => {
  clearPretriggerResults();
  sendCmd({ cmd: 'mark' });
};
signalHintsBtn.onclick = () => {
  if (!signalTarget) return;
  signalSamples = [];
  signalHints = [];
  statusSignal(`请求候选 ${channelName(signalTarget.ch)} ${idText(signalTarget.id)}`);
  renderSignalWorkbench();
  sendCmd({ cmd: 'p4_hints', ch: channelName(signalTarget.ch), id: signalTarget.id });
};
sigSaveBtn.onclick = saveFormSpec;
signalExportBtn.onclick = exportSignalSpecs;
signalImportBtn.onclick = () => signalImportFile.click();
signalImportFile.onchange = async () => {
  const file = signalImportFile.files && signalImportFile.files[0];
  signalImportFile.value = '';
  if (!file) return;
  try {
    importSignalDoc(JSON.parse(await file.text()));
  } catch (e) {
    statusSignal(`导入失败：JSON 解析错误 ${e.message || e}`);
  }
};
signalLoadCommonBtn.onclick = loadCommonSignals;
signalSaveCommonBtn.onclick = saveCommonSignals;
baseSelect.onchange = repaintAll;
suppressStatic.onchange = repaintAll;
staticSeconds.onchange = repaintAll;
sortSelect.onchange = () => { repaintAll(); sortTables(); };
for (const el of [channelFilter, idFilter, rangeFrom, rangeTo, searchBox, whitelistOnly]) {
  el.oninput = repaintAll;
  el.onchange = repaintAll;
}

renderSignalWorkbench();
connect();
loadLabels();
refreshWifiStatus();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
setInterval(() => { if (!freezeView.checked) repaintAll(); }, 500);
