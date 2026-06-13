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
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const records = {};
let txState = { master: false, a: false, b: false, onlineA: false, onlineB: false };

function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }
function printable(b) { return b >= 32 && b <= 126 ? String.fromCharCode(b) : '.'; }

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

function ensureRows(rec) {
  const key = rec.key;
  if (rows[key]) return rows[key];

  const tr = document.createElement('tr');
  tr.innerHTML = '<td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>';
  const bit = document.createElement('tr');
  bit.className = 'bit-view hidden';
  bit.innerHTML = '<td colspan="8"></td>';
  tr.onclick = () => bit.classList.toggle('hidden');
  rows[key] = { tr, bit };
  tbody[rec.ch].appendChild(tr);
  tbody[rec.ch].appendChild(bit);
  return rows[key];
}

function paintRecord(rec) {
  const pair = ensureRows(rec);
  const tr = pair.tr;
  tr.className = rowClass(rec.changeScore);
  tr.classList.toggle('hidden', isStatic(rec));
  pair.bit.classList.toggle('hidden', pair.bit.classList.contains('hidden') || isStatic(rec));

  const c = tr.children;
  c[0].textContent = '0x' + hex(rec.id, 3);
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
    const key = ch * 4096 + id;
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

function connect() {
  const ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => statusEl.textContent = 'WS: 已连接';
  ws.onclose = () => { statusEl.textContent = 'WS: 断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => {
    if (!(ev.data instanceof ArrayBuffer) || ev.data.byteLength === 0) return;
    const type = new DataView(ev.data).getUint8(0);
    if (type === 0x01) parseDelta(ev.data);
    if (type === 0x02) parseStats(ev.data);
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
baseSelect.onchange = repaintAll;
suppressStatic.onchange = repaintAll;
staticSeconds.onchange = repaintAll;
sortSelect.onchange = () => { repaintAll(); sortTables(); };

connect();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
setInterval(() => { if (!freezeView.checked) repaintAll(); }, 500);
