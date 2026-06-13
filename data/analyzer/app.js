const banner = document.getElementById('tx-banner');
const masterToggle = document.getElementById('master-toggle');
const txAToggle = document.getElementById('tx-a-toggle');
const txBToggle = document.getElementById('tx-b-toggle');
const busHealth = document.getElementById('bus-health');
const statusEl = document.getElementById('status');
const tbody = { 0: document.querySelector('#tbl-a tbody'), 1: document.querySelector('#tbl-b tbody') };
const rows = {};
const lastRxMs = {};
let txState = { master: false, a: false, b: false, onlineA: false, onlineB: false };

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
  if (buf.byteLength < 2) return;
  const dv = new DataView(buf);
  let o = 0;
  if (dv.getUint8(o++) !== 0x01) return;
  const count = dv.getUint8(o++);
  for (let i = 0; i < count; i++) {
    if (o + 36 > buf.byteLength) return;
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

connect();
refreshTxBanner();
setInterval(refreshTxBanner, 2000);
