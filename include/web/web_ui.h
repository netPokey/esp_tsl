#pragma once

#include <Arduino.h>

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
.wrap{max-width:900px;margin:0 auto;padding:20px}.hero{display:flex;justify-content:space-between;align-items:flex-start;gap:16px;flex-wrap:wrap}.title{font-size:28px;font-weight:700}.sub{margin-top:6px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:16px;margin-top:18px}.card{background:rgba(18,25,50,.92);border:1px solid var(--line);border-radius:16px;padding:16px;box-shadow:0 10px 30px rgba(0,0,0,.2)}
.card h3{margin:0 0 14px 0;font-size:15px;color:#dce7ff}.kv{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid rgba(255,255,255,.06)}.kv:last-child{border-bottom:none}.k{color:var(--muted)}.v{font-weight:600;text-align:right;word-break:break-word}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 0;border-bottom:1px solid rgba(255,255,255,.06)}.row:last-child{border-bottom:none}
button{background:#21315f;color:#fff;border:none;border-radius:10px;padding:10px 14px;font-weight:600;cursor:pointer}button:hover{background:#2a3d77}button.alt{background:#28314a}
input[type=range]{width:120px}.pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#1d2747}.ok{color:var(--ok)}.warn{color:var(--warn)}.danger{color:var(--danger)}
#logs{max-height:260px;overflow:auto;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;line-height:1.5;background:#0a0f1d;border:1px solid var(--line);border-radius:12px;padding:10px}.log-line{padding:3px 0;border-bottom:1px dashed rgba(255,255,255,.05)}.log-line:last-child{border-bottom:none}
small{color:var(--muted)}
</style>
</head>
<body>
<div class="wrap">
  <div class="hero">
    <div>
      <div class="title">TeslaCAN Dual Bus</div>
      <div class="sub">双 CAN 采集 + WiFi 控制 + BLE OTA，控制层和升级链路已解耦，后续蓝牙交互和脚本层可继续复用同一组状态接口。</div>
    </div>
    <div class="pill" id="onlineState">连接中</div>
  </div>

  <div class="grid">
    <section class="card">
      <h3>整车状态</h3>
      <div class="kv"><div class="k">FSD</div><div class="v" id="fsdState">--</div></div>
      <div class="kv"><div class="k">强制 FSD</div><div class="v" id="forceFsd">--</div></div>
      <div class="kv"><div class="k">速度档位</div><div class="v" id="speedProfile">--</div></div>
      <div class="kv"><div class="k">速度偏移</div><div class="v" id="speedOffset">--</div></div>
      <div class="kv"><div class="k">控制总线</div><div class="v" id="controlBus">--</div></div>
      <div class="kv"><div class="k">运行时长</div><div class="v" id="uptime">--</div></div>
    </section>

    <section class="card">
      <h3>电池状态</h3>
      <div class="kv"><div class="k">SOC</div><div class="v" id="soc">--</div></div>
      <div class="kv"><div class="k">电压 / 电流</div><div class="v" id="packVi">--</div></div>
      <div class="kv"><div class="k">功率</div><div class="v" id="packPower">--</div></div>
      <div class="kv"><div class="k">温度范围</div><div class="v" id="packTemp">--</div></div>
      <div class="kv"><div class="k">Wh/km</div><div class="v" id="energy">--</div></div>
      <div class="kv"><div class="k">预热</div><div class="v" id="precondState">--</div></div>
    </section>

    <section class="card">
      <h3>双 CAN 状态</h3>
      <div class="kv"><div class="k">总接收</div><div class="v" id="totalRx">0</div></div>
      <div class="kv"><div class="k">总发送</div><div class="v" id="totalTx">0</div></div>
      <div class="kv"><div class="k">CAN_A</div><div class="v" id="busA">--</div></div>
      <div class="kv"><div class="k">CAN_B</div><div class="v" id="busB">--</div></div>
      <div class="kv"><div class="k">最后帧</div><div class="v" id="lastFrame">--</div></div>
    </section>

    <section class="card">
      <h3>控制开关</h3>
      <div class="row"><span>强制 FSD</span><button onclick="toggleBool('/api/force-fsd', !state.force_fsd)">切换</button></div>
      <div class="row"><span>电池预热</span><button onclick="toggleBool('/api/precond', !state.precond_req)">切换</button></div>
      <div class="row"><span>紧急车辆检测</span><button onclick="toggleBool('/api/em-detect', !state.em_detect)">切换</button></div>
      <div class="row"><span>ISA 速度覆盖</span><button onclick="toggleBool('/api/isa-override', !state.isa_ovr)">切换</button></div>
      <div class="row"><span>ISA 提示音抑制</span><button onclick="toggleBool('/api/isa-suppress', !state.isa_sup)">切换</button></div>
      <div class="row"><span>串口日志输出</span><button onclick="toggleBool('/api/enable-print', !state.enable_print)">切换</button></div>
      <div class="row"><span>ISA 倍率</span><div><input id="isaMul" type="range" min="0" max="7" value="7" onchange="setIsaMul(this.value)"><small id="isaMulText">7</small></div></div>
    </section>

    <section class="card" style="grid-column:1/-1">
      <h3>运行日志</h3>
      <div id="logs"></div>
    </section>
  </div>
</div>
<script>
let state={force_fsd:false,precond_req:false,em_detect:true,isa_ovr:true,isa_sup:false,enable_print:true};
function fmtSec(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;return `${h}h ${m}m ${sec}s`;}
function setText(id,val){document.getElementById(id).textContent=val;}
function busLine(bus){return `${bus.online?'在线':'离线'} / RX ${bus.rx} / TX ${bus.tx}`;}
function lastFrameLine(d){return `${d.bus}: 0x${Number(d.id).toString(16).toUpperCase()} [${d.dlc}] ${d.data}`;}
async function post(path,value){await fetch(path,{method:'POST',headers:{'Content-Type':'text/plain'},body:String(value)});await poll();}
async function toggleBool(path,value){await post(path,value?'true':'false');}
async function setIsaMul(value){document.getElementById('isaMulText').textContent=value;await post('/api/isa-mul',value);}
function render(d){state=d;setText('fsdState',d.fsd_enabled?'已开启':'未开启');setText('forceFsd',d.force_fsd?'已强制':'未强制');setText('speedProfile',d.speed_profile_name+` (${d.speed_profile})`);setText('speedOffset',`${d.speed_offset} km/h`);setText('controlBus',d.control_bus);setText('uptime',fmtSec(d.uptime_s));setText('soc',`${d.battery.soc.toFixed(1)} %`);setText('packVi',`${d.battery.voltage.toFixed(1)} V / ${d.battery.current.toFixed(1)} A`);setText('packPower',`${d.battery.power_kw.toFixed(2)} kW`);setText('packTemp',`${d.battery.temp_min.toFixed(1)} ~ ${d.battery.temp_max.toFixed(1)} °C`);setText('energy',`${d.battery.wh_per_km.toFixed(1)} Wh/km`);setText('precondState',`${d.battery.precond?'执行中':'未执行'} / 请求 ${d.precond_req?'开':'关'}`);setText('totalRx',d.can.total_rx);setText('totalTx',d.can.total_tx);setText('busA',busLine(d.can.a));setText('busB',busLine(d.can.b));setText('lastFrame',lastFrameLine(d.last_frame));document.getElementById('onlineState').textContent='在线';document.getElementById('onlineState').className='pill ok';document.getElementById('isaMul').value=d.isa_mul;document.getElementById('isaMulText').textContent=d.isa_mul;document.getElementById('logs').innerHTML=d.logs.map(x=>`<div class="log-line">[${x.ts}] ${x.msg}</div>`).join('');}
async function poll(){try{const r=await fetch('/api/status');const d=await r.json();render(d);}catch(err){document.getElementById('onlineState').textContent='离线';document.getElementById('onlineState').className='pill danger';}}
setInterval(poll,1000);poll();
</script>
</body>
</html>
)rawliteral";