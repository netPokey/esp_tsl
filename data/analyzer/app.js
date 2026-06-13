const banner = document.getElementById('tx-banner');
const statusEl = document.getElementById('status');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const lastRxMs = {};

function hex(n, w) { return n.toString(16).toUpperCase().padStart(w, '0'); }

function upsert(ch, id, dlc, data, count, lastRx) {
  const key = ch * 4096 + id;
  let tr = rows[key];
  if (!tr) {
    tr = document.createElement('tr');
    tr.innerHTML = '<td></td><td></td><td></td><td></td><td></td>';
    rows[key] = tr;
    tbody[ch].appendChild(tr);
  }

  const prev = lastRxMs[key];
  const deltaMs = prev === undefined ? 0 : (lastRx - prev);
  lastRxMs[key] = lastRx;

  const c = tr.children;
  c[0].textContent = '0x' + hex(id, 3);
  c[1].textContent = dlc;
  c[2].textContent = Array.from(data.slice(0, dlc)).map(b => hex(b, 2)).join(' ');
  c[3].textContent = count;
  c[4].textContent = deltaMs.toFixed(0);
}

function parseDelta(buf) {
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    const ch = dv.getUint8(o); o += 1;
    const id = dv.getUint16(o, true); o += 2;
    const dlc = dv.getUint8(o); o += 1;
    const data = new Uint8Array(buf.slice(o, o + 8)); o += 8;
    const lastRx = dv.getUint32(o, true); o += 4;
    o += 16; // byte_age_ms[8]，P2 高亮用。
    const rxCount = dv.getUint32(o, true); o += 4;
    upsert(ch, id, dlc, data, rxCount, lastRx);
  }
}

function connect() {
  const ws = new WebSocket('ws://' + location.host + '/ws');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => statusEl.textContent = 'WS: 已连接';
  ws.onclose = () => { statusEl.textContent = 'WS: 断开，重连中…'; setTimeout(connect, 1000); };
  ws.onmessage = (ev) => { if (ev.data instanceof ArrayBuffer) parseDelta(ev.data); };
}

async function refreshTxBanner() {
  try {
    const r = await fetch('/api/status');
    const s = await r.json();
    const on = !!s.can_tx_enabled;
    banner.className = 'banner ' + (on ? 'tx' : 'listen');
    banner.textContent = on ? '可发送（TX 开启）' : '监听-only（TX 关闭）';
  } catch (e) {}
}

banner.onclick = async () => {
  const on = banner.classList.contains('tx');
  await fetch('/api/can-tx', { method: 'POST', body: on ? 'false' : 'true' });
  refreshTxBanner();
};

connect();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
