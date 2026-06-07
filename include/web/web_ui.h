#pragma once

#include <Arduino.h>
#include "can_signal_map_js.h"

const char WEB_UI_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TeslaCAN Dual Bus</title>
<style>
:root{color-scheme:dark;--bg:#0b1020;--card:#121932;--muted:#8ea0c9;--line:#233056;--accent:#56d3ff;--ok:#2fd27b;--warn:#ffb347;--danger:#ff6b6b}
*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:linear-gradient(180deg,#0a0f1d,#111735);color:#edf3ff}
.wrap{max-width:1280px;margin:0 auto;padding:20px}.hero{display:flex;justify-content:space-between;align-items:flex-start;gap:16px;flex-wrap:wrap}.title{font-size:30px;font-weight:700}.sub{margin-top:6px;color:var(--muted);max-width:900px;line-height:1.6}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px;margin-top:18px}.card{background:rgba(18,25,50,.92);border:1px solid var(--line);border-radius:16px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.2)}
.card h3{margin:0 0 14px 0;font-size:15px;color:#dce7ff}.kv{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid rgba(255,255,255,.06)}.kv:last-child{border-bottom:none}.k{color:var(--muted)}.v{font-weight:600;text-align:right;word-break:break-word}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid rgba(255,255,255,.06)}.row:last-child{border-bottom:none}
button{background:#21315f;color:#fff;border:none;border-radius:10px;padding:10px 14px;font-weight:600;cursor:pointer}button:hover{background:#2a3d77}.btn-danger{background:#743045}.btn-danger:hover{background:#9a3c57}.btn-warn{background:#6b4b1f}.btn-warn:hover{background:#8a622a}
input[type=range]{width:120px}input[type=text],input[type=password]{width:100%;background:#0a0f1d;color:#edf3ff;border:1px solid var(--line);border-radius:10px;padding:10px 12px}.form{display:grid;gap:10px}.hint{margin-top:10px;color:var(--muted);font-size:12px}.pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#1d2747}.ok{color:var(--ok)}.warn{color:var(--warn)}.danger{color:var(--danger)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace}.wide{grid-column:1/-1}small{color:var(--muted)}
#logs{max-height:280px;overflow:auto;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;line-height:1.5;background:#0a0f1d;border:1px solid var(--line);border-radius:12px;padding:10px}.log-line{padding:3px 0;border-bottom:1px dashed rgba(255,255,255,.05)}.log-line:last-child{border-bottom:none}
.capture-tools{display:grid;grid-template-columns:minmax(150px,1fr) 100px 100px auto;gap:10px;margin-bottom:12px}.table-wrap{max-height:360px;overflow:auto;border:1px solid var(--line);border-radius:12px;background:#0a0f1d}table{width:100%;border-collapse:collapse;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px}th,td{padding:8px 10px;border-bottom:1px solid rgba(255,255,255,.06);text-align:left;white-space:nowrap}th{position:sticky;top:0;background:#111936;color:var(--muted)}td.data{white-space:normal;word-break:break-word}
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <div>
      <div class="title">TeslaCAN Dual Bus</div>
      <div class="sub">双 CAN 采集 + WiFi 控制 + BLE OTA。当前页面会把状态接口已经能给出的已解析字段全部展示出来，包括控制状态、电池解析、双 CAN 统计、每条总线最后快照和日志。</div>
    </div>
    <div class="pill" id="onlineState">连接中</div>
  </div>

  <section class="card wide">
    <h3>安全状态</h3>
    <div class="kv"><div class="k">测试台运行要求</div><div class="v warn" id="benchModeHint">默认只听，TX 禁用</div></div>
    <div class="kv"><div class="k">上车前条件</div><div class="v danger">未经测试台验证，禁止接入汽车总线</div></div>
  </section>

  <div class="grid">
    <section class="card">
      <h3>整车控制状态</h3>
      <div class="kv"><div class="k">FSD 当前状态</div><div class="v" id="fsdState">--</div></div>
      <div class="kv"><div class="k">强制 FSD</div><div class="v" id="forceFsd">--</div></div>
      <div class="kv"><div class="k">速度档位</div><div class="v" id="speedProfile">--</div></div>
      <div class="kv"><div class="k">速度偏移</div><div class="v" id="speedOffset">--</div></div>
      <div class="kv"><div class="k">控制总线</div><div class="v" id="controlBus">--</div></div>
      <div class="kv"><div class="k">已解析帧数</div><div class="v" id="frameCount">--</div></div>
      <div class="kv"><div class="k">已注入帧数</div><div class="v" id="sentCount">--</div></div>
      <div class="kv"><div class="k">运行时长</div><div class="v" id="uptime">--</div></div>
    </section>

    <section class="card">
      <h3>功能开关与判定</h3>
      <div class="kv"><div class="k">紧急车辆检测</div><div class="v" id="emDetect">--</div></div>
      <div class="kv"><div class="k">ISA 速度覆盖</div><div class="v" id="isaOverride">--</div></div>
      <div class="kv"><div class="k">ISA 提示音抑制</div><div class="v" id="isaSuppress">--</div></div>
      <div class="kv"><div class="k">ISA 倍率</div><div class="v" id="isaMulState">--</div></div>
      <div class="kv"><div class="k">预热请求</div><div class="v" id="precondReq">--</div></div>
      <div class="kv"><div class="k">预热执行中</div><div class="v" id="precondActive">--</div></div>
      <div class="kv"><div class="k">允许预热</div><div class="v" id="precondAllowed">--</div></div>
      <div class="kv"><div class="k">值得预热</div><div class="v" id="precondWorth">--</div></div>
      <div class="kv"><div class="k">串口原始帧日志</div><div class="v" id="serialPrintState">--</div></div>
      <div class="kv"><div class="k">CAN 发送总开关</div><div class="v" id="canTxState">--</div></div>
      <div class="kv"><div class="k">总线模式</div><div class="v" id="canTxMode">--</div></div>
    </section>

    <section class="card">
      <h3>网络状态</h3>
      <div class="kv"><div class="k">当前模式</div><div class="v" id="wifiMode">--</div></div>
      <div class="kv"><div class="k">当前 SSID</div><div class="v" id="wifiSsid">--</div></div>
      <div class="kv"><div class="k">访问 IP</div><div class="v mono" id="wifiIp">--</div></div>
      <div class="kv"><div class="k">已保存网络</div><div class="v" id="wifiConfigured">--</div></div>
      <div class="form">
        <input id="wifiInputSsid" type="text" placeholder="SSID" autocomplete="off">
        <input id="wifiInputPassword" type="password" placeholder="Password" autocomplete="off">
        <button onclick="saveWifi()">保存网络</button>
      </div>
      <div class="hint" id="wifiSaveHint"></div>
    </section>

    <section class="card">
      <h3>电池解析状态</h3>
      <div class="kv"><div class="k">SOC</div><div class="v" id="soc">--</div></div>
      <div class="kv"><div class="k">电压</div><div class="v" id="packVoltage">--</div></div>
      <div class="kv"><div class="k">电流</div><div class="v" id="packCurrent">--</div></div>
      <div class="kv"><div class="k">功率</div><div class="v" id="packPower">--</div></div>
      <div class="kv"><div class="k">最低温度</div><div class="v" id="packTempMin">--</div></div>
      <div class="kv"><div class="k">最高温度</div><div class="v" id="packTempMax">--</div></div>
      <div class="kv"><div class="k">温度范围</div><div class="v" id="packTempRange">--</div></div>
      <div class="kv"><div class="k">Wh/km</div><div class="v" id="energy">--</div></div>
    </section>

    <section class="card">
      <h3>双 CAN 汇总</h3>
      <div class="kv"><div class="k">总接收</div><div class="v" id="totalRx">0</div></div>
      <div class="kv"><div class="k">总发送</div><div class="v" id="totalTx">0</div></div>
      <div class="kv"><div class="k">最后控制帧总线</div><div class="v" id="lastFrameBus">--</div></div>
      <div class="kv"><div class="k">最后控制帧 ID</div><div class="v mono" id="lastFrameId">--</div></div>
      <div class="kv"><div class="k">最后控制帧 DLC</div><div class="v" id="lastFrameDlc">--</div></div>
      <div class="kv"><div class="k">最后控制帧 DATA</div><div class="v mono" id="lastFrameData">--</div></div>
      <div class="kv"><div class="k">最后控制帧摘要</div><div class="v mono" id="lastFrame">--</div></div>
    </section>

    <section class="card">
      <h3>CAN_A 详情</h3>
      <div class="kv"><div class="k">名称</div><div class="v" id="busAName">--</div></div>
      <div class="kv"><div class="k">在线</div><div class="v" id="busAOnline">--</div></div>
      <div class="kv"><div class="k">接收 / 发送</div><div class="v" id="busACounters">--</div></div>
      <div class="kv"><div class="k">最后 ID</div><div class="v mono" id="busALastId">--</div></div>
      <div class="kv"><div class="k">最后 DLC</div><div class="v" id="busALastDlc">--</div></div>
      <div class="kv"><div class="k">最后 DATA</div><div class="v mono" id="busALastData">--</div></div>
      <div class="kv"><div class="k">最后接收时间</div><div class="v" id="busALastSeen">--</div></div>
      <div class="kv"><div class="k">最后注入时间</div><div class="v" id="busALastInjected">--</div></div>
    </section>

    <section class="card">
      <h3>CAN_B 详情</h3>
      <div class="kv"><div class="k">名称</div><div class="v" id="busBName">--</div></div>
      <div class="kv"><div class="k">在线</div><div class="v" id="busBOnline">--</div></div>
      <div class="kv"><div class="k">接收 / 发送</div><div class="v" id="busBCounters">--</div></div>
      <div class="kv"><div class="k">最后 ID</div><div class="v mono" id="busBLastId">--</div></div>
      <div class="kv"><div class="k">最后 DLC</div><div class="v" id="busBLastDlc">--</div></div>
      <div class="kv"><div class="k">最后 DATA</div><div class="v mono" id="busBLastData">--</div></div>
      <div class="kv"><div class="k">最后接收时间</div><div class="v" id="busBLastSeen">--</div></div>
      <div class="kv"><div class="k">最后注入时间</div><div class="v" id="busBLastInjected">--</div></div>
    </section>

    <section class="card wide">
      <h3>控制开关</h3>
      <div class="row"><span>强制 FSD</span><button onclick="toggleBool('/api/force-fsd', !state.force_fsd)">切换</button></div>
      <div class="row"><span>电池预热</span><button onclick="toggleBool('/api/precond', !state.precond_req)">切换</button></div>
      <div class="row"><span>紧急车辆检测</span><button onclick="toggleBool('/api/em-detect', !state.em_detect)">切换</button></div>
      <div class="row"><span>ISA 速度覆盖</span><button onclick="toggleBool('/api/isa-override', !state.isa_ovr)">切换</button></div>
      <div class="row"><span>ISA 提示音抑制</span><button onclick="toggleBool('/api/isa-suppress', !state.isa_sup)">切换</button></div>
      <div class="row"><span>串口日志输出</span><button onclick="toggleBool('/api/enable-print', !state.enable_print)">切换</button></div>
      <div class="row"><span>CAN 发送总开关</span><button onclick="toggleBool('/api/can-tx', !state.can_tx_enabled)">切换</button></div>
      <div class="row"><span>重启设备</span><button class="btn-warn" onclick="deviceAction('restart')">重启</button></div>
      <div class="row"><span>关闭设备</span><button class="btn-danger" onclick="deviceAction('shutdown')">关机</button></div>
      <div class="row"><span>ISA 倍率</span><div><input id="isaMul" type="range" min="0" max="7" value="7" onchange="setIsaMul(this.value)"><small id="isaMulText">7</small></div></div>
    </section>

    <section class="card wide">
      <h3>CAN 抓包</h3>
      <div class="capture-tools">
        <input id="captureFilterId" type="text" placeholder="筛选 ID，例如 0x3FD 或 1021" autocomplete="off">
        <input id="captureLimit" type="text" placeholder="每次拉取" value="64" autocomplete="off">
        <input id="captureKeep" type="text" placeholder="页面保留" value="200" autocomplete="off" onchange="trimCaptureFrames();renderCapture({enabled:captureEnabled,sequence:captureLastSeq,capacity:200,count:captureFrames.length,filtered:false,frames:[]})">
        <button id="captureToggle" onclick="toggleCapture()">开启抓取</button>
      </div>
      <div class="hint" id="captureHint">抓包默认关闭，页面最多保留 200 条，最新数据排在前面。</div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>时间 ms</th><th>来源</th><th>ID</th><th>帧名</th><th>含义</th><th>DBC字段</th><th>DLC</th><th>DATA</th></tr></thead>
          <tbody id="captureRows"><tr><td colspan="8">等待 CAN 报文</td></tr></tbody>
        </table>
      </div>
    </section>

    <section class="card wide">
      <h3>运行日志</h3>
      <div id="logs"></div>
    </section>
  </div>
</div>
<script>
)rawliteral"
CAN_SIGNAL_MAP_JS
R"rawliteral(
let state={force_fsd:false,precond_req:false,em_detect:true,isa_ovr:true,isa_sup:false,enable_print:true,can_tx_enabled:false,can_tx_mode:'LISTEN_ONLY'};
let captureEnabled=false,captureLastSeq=0,captureFrames=[],captureSeen=new Set(),captureFilterKey='';
function fmtSec(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;return `${h}h ${m}m ${sec}s`;}
function fmtBool(v,on='开',off='关'){return v?on:off;}
function fmtMs(ms){return Number(ms)>0?`${ms} ms`:'--';}
function fmtHex(id){return `0x${Number(id).toString(16).toUpperCase()}`;}
function setText(id,val){document.getElementById(id).textContent=val;}
function busCounters(bus){return `RX ${bus.rx} / TX ${bus.tx}`;}
function lastFrameLine(d){return `${d.bus}: ${fmtHex(d.id)} [${d.dlc}] ${d.data||'--'}`;}
async function post(path,value){await fetch(path,{method:'POST',headers:{'Content-Type':'text/plain'},body:String(value)});await poll();}
async function toggleBool(path,value){await post(path,value?'true':'false');}
async function setIsaMul(value){document.getElementById('isaMulText').textContent=value;await post('/api/isa-mul',value);}
async function saveWifi(){const ssid=document.getElementById('wifiInputSsid').value.trim();const password=document.getElementById('wifiInputPassword').value;const hint=document.getElementById('wifiSaveHint');if(!ssid){hint.textContent='SSID 不能为空';hint.className='hint danger';return;}const body=`${ssid}\n${password}`;const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'text/plain'},body});if(r.ok){hint.textContent='已保存，重启后优先连接该网络';hint.className='hint ok';document.getElementById('wifiInputPassword').value='';await poll();}else{hint.textContent='保存失败';hint.className='hint danger';}}
async function deviceAction(action){const text=action==='restart'?'确定要重启设备吗？':'确定要关机吗？关机后设备会进入低功耗睡眠，需要重新上电或复位唤醒。';if(!confirm(text))return;const path=action==='restart'?'/api/restart':'/api/shutdown';await fetch(path,{method:'POST'});document.getElementById('onlineState').textContent=action==='restart'?'正在重启':'已发送关机';document.getElementById('onlineState').className='pill warn';}
function resetCaptureView(){captureLastSeq=0;captureFrames=[];captureSeen=new Set();document.getElementById('captureRows').innerHTML='<tr><td colspan="8">等待 CAN 报文</td></tr>';}
function captureInfo(id){const item=canSignalMap[String(Number(id))];if(!item)return {name:'--',meaning:'未收录',signals:'--'};return {name:item[0],meaning:item[1],signals:(item[2]||[]).join(', ')||'--'};}
function captureKeepLimit(){let keep=parseInt(document.getElementById('captureKeep').value||'200',10);if(!Number.isFinite(keep)||keep<1)keep=200;if(keep>200)keep=200;document.getElementById('captureKeep').value=String(keep);return keep;}
function captureFetchLimit(){let limit=parseInt(document.getElementById('captureLimit').value||'64',10);if(!Number.isFinite(limit)||limit<1)limit=64;if(limit>200)limit=200;document.getElementById('captureLimit').value=String(limit);return limit;}
function trimCaptureFrames(){const keep=captureKeepLimit();if(captureFrames.length<=keep)return;const removed=captureFrames.splice(keep);removed.forEach(f=>captureSeen.delete(f.seq));}
function renderCapture(d){captureEnabled=!!d.enabled;document.getElementById('captureToggle').textContent=captureEnabled?'关闭抓取':'开启抓取';const rows=document.getElementById('captureRows');const hint=document.getElementById('captureHint');hint.className='hint';if(captureLastSeq>d.sequence){resetCaptureView();}captureLastSeq=Math.max(captureLastSeq,d.sequence||0);(d.frames||[]).reverse().forEach(f=>{if(captureSeen.has(f.seq))return;captureSeen.add(f.seq);captureFrames.unshift(f);});trimCaptureFrames();hint.textContent=`抓包${captureEnabled?'开启':'关闭'}，固件缓冲 ${d.count}/${d.capacity}，页面保留 ${captureFrames.length}/${captureKeepLimit()} 条${d.filtered?`，筛选 ID ${fmtHex(d.filter_id)}`:''}`;if(!captureFrames.length){rows.innerHTML=`<tr><td colspan="8">${captureEnabled?'等待 CAN 报文':'抓包已关闭'}</td></tr>`;return;}rows.innerHTML=captureFrames.map(f=>{const info=captureInfo(f.id);return `<tr><td>${f.ts}</td><td>${f.bus}</td><td>${fmtHex(f.id)}</td><td>${info.name}</td><td>${info.meaning}</td><td class="data">${info.signals}</td><td>${f.dlc}</td><td class="data">${f.data||'--'}</td></tr>`;}).join('');}
function renderCaptureIdle(){captureEnabled=false;document.getElementById('captureToggle').textContent='开启抓取';document.getElementById('captureHint').textContent=`抓包关闭，页面保留 ${captureFrames.length}/${captureKeepLimit()} 条`;document.getElementById('captureHint').className='hint';if(!captureFrames.length)document.getElementById('captureRows').innerHTML='<tr><td colspan="8">抓包已关闭</td></tr>';}
async function pollCapture(){if(!captureEnabled)return;try{const id=document.getElementById('captureFilterId').value.trim();if(id!==captureFilterKey){captureFilterKey=id;resetCaptureView();}let url=`/api/can-capture?limit=${captureFetchLimit()}&since=${captureLastSeq}`;if(id)url+=`&id=${encodeURIComponent(id)}`;const r=await fetch(url);renderCapture(await r.json());}catch(err){document.getElementById('captureHint').textContent='抓包读取失败';document.getElementById('captureHint').className='hint danger';}}
async function toggleCapture(){const next=!captureEnabled;await fetch('/api/can-capture/enabled',{method:'POST',headers:{'Content-Type':'text/plain'},body:next?'true':'false'});resetCaptureView();captureEnabled=next;if(next){await pollCapture();}else{renderCaptureIdle();}}
function renderBus(prefix,bus){setText(prefix+'Name',bus.name||'--');setText(prefix+'Online',bus.online?'在线':'离线');setText(prefix+'Counters',busCounters(bus));setText(prefix+'LastId',fmtHex(bus.last_id));setText(prefix+'LastDlc',bus.last_dlc);setText(prefix+'LastData',bus.last_data||'--');setText(prefix+'LastSeen',fmtMs(bus.last_seen_ms));setText(prefix+'LastInjected',fmtMs(bus.last_injected_ms));}
function render(d){state=d;setText('fsdState',fmtBool(d.fsd_enabled,'已开启','未开启'));setText('forceFsd',fmtBool(d.force_fsd,'已强制','未强制'));setText('speedProfile',`${d.speed_profile_name} (${d.speed_profile})`);setText('speedOffset',`${d.speed_offset} km/h`);setText('controlBus',d.control_bus);setText('frameCount',d.frame_count);setText('sentCount',d.sent_count);setText('uptime',fmtSec(d.uptime_s));setText('emDetect',fmtBool(d.em_detect));setText('isaOverride',fmtBool(d.isa_ovr));setText('isaSuppress',fmtBool(d.isa_sup));setText('isaMulState',d.isa_mul);setText('precondReq',fmtBool(d.precond_req));setText('precondActive',fmtBool(d.precond_active,'执行中','未执行'));setText('precondAllowed',fmtBool(d.precond_allowed,'允许','不允许'));setText('precondWorth',fmtBool(d.precond_worth,'值得','不值得'));setText('serialPrintState',fmtBool(d.enable_print));setText('canTxState',fmtBool(d.can_tx_enabled,'允许发送','禁止发送'));setText('canTxMode',d.can_tx_mode);setText('wifiMode',d.wifi.mode);setText('wifiSsid',d.wifi.ssid||'--');setText('wifiIp',d.wifi.ip||'--');setText('wifiConfigured',d.wifi.configured?'已配置':'未配置');setText('benchModeHint',d.can_tx_enabled?'已切到正常模式，请确认仍在测试台':'默认只听，TX 禁用');setText('soc',`${d.battery.soc.toFixed(1)} %`);setText('packVoltage',`${d.battery.voltage.toFixed(1)} V`);setText('packCurrent',`${d.battery.current.toFixed(1)} A`);setText('packPower',`${d.battery.power_kw.toFixed(2)} kW`);setText('packTempMin',`${d.battery.temp_min.toFixed(1)} °C`);setText('packTempMax',`${d.battery.temp_max.toFixed(1)} °C`);setText('packTempRange',`${d.battery.temp_min.toFixed(1)} ~ ${d.battery.temp_max.toFixed(1)} °C`);setText('energy',`${d.battery.wh_per_km.toFixed(1)} Wh/km`);setText('totalRx',d.can.total_rx);setText('totalTx',d.can.total_tx);setText('lastFrameBus',d.last_frame.bus);setText('lastFrameId',fmtHex(d.last_frame.id));setText('lastFrameDlc',d.last_frame.dlc);setText('lastFrameData',d.last_frame.data||'--');setText('lastFrame',lastFrameLine(d.last_frame));renderBus('busA',d.can.a);renderBus('busB',d.can.b);document.getElementById('onlineState').textContent='在线';document.getElementById('onlineState').className='pill ok';document.getElementById('benchModeHint').className=`v ${d.can_tx_enabled?'danger':'warn'}`;document.getElementById('isaMul').value=d.isa_mul;document.getElementById('isaMulText').textContent=d.isa_mul;document.getElementById('logs').innerHTML=d.logs.map(x=>`<div class="log-line">[${x.ts}] ${x.msg}</div>`).join('');}
async function poll(){try{const r=await fetch('/api/status');const d=await r.json();render(d);await pollCapture();}catch(err){document.getElementById('onlineState').textContent='离线';document.getElementById('onlineState').className='pill danger';}}
setInterval(poll,1000);poll();
</script>
</body>
</html>
)rawliteral";