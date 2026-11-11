#pragma once
/* Страница портала настроек (одна, без внешних ресурсов — телефон подключён только к точке доступа HUD). */
static const char HUD_PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>HUD SuperMini</title>
<style>
:root{color-scheme:dark;--bg:#101214;--c:#1b1f23;--t:#e8eaed;--m:#8b949e;--a:#2ea043;--e:#f85149;--l:#2b3138}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--t);font:16px/1.4 system-ui,sans-serif;padding:0 16px 40px;max-width:560px;margin:auto}
h1{font-size:20px;margin:18px 0 4px}.sub{color:var(--m);font-size:13px;margin-bottom:14px}
.card{background:var(--c);border-radius:12px;padding:6px 14px;margin:12px 0}
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:11px 0;border-bottom:1px solid var(--l)}
.row:last-child{border:0}.row label{flex:1}
select,input[type=number]{background:var(--bg);color:var(--t);border:1px solid var(--l);border-radius:8px;padding:8px;font-size:16px;width:110px}
input[type=checkbox]{width:44px;height:26px;accent-color:var(--a)}input[type=range]{width:150px;accent-color:var(--a)}
button{background:var(--a);color:#fff;border:0;border-radius:8px;padding:10px 14px;font-size:15px}button.s{background:var(--l)}
h2{font-size:15px;color:var(--m);margin:18px 0 0;font-weight:600}
progress{width:100%;height:10px}.msg{font-size:14px;margin:6px 0;min-height:20px}.ok{color:var(--a)}.er{color:var(--e)}
#left{font-variant-numeric:tabular-nums}
</style></head><body>
<h1>HUD SuperMini</h1><div class="sub"><span id="ver"></span> · <span data-t="open">портал закроется через</span> <b id="left">--:--</b></div>
<h2 data-t="h_main">Основное</h2><div class="card">
<div class="row"><label data-t="lang">Язык</label><select id="lang"><option value="0">Русский</option><option value="1">English</option></select></div>
<div class="row"><label data-t="units">Единицы</label><select id="units"><option value="0">км/ч, км, м</option><option value="1">mph, mi, ft</option></select></div>
<div class="row"><label data-t="psd">Знаки из прогноза маршрута (PSD)</label><input type="checkbox" id="psd"></div>
<div class="row"><label data-t="vze">Знаки из камеры/карты приборки (VZE)</label><input type="checkbox" id="vze"></div>
<div class="row"><label data-t="tol">Допуск превышения, км/ч</label><input type="number" id="tol" min="0" max="100"></div>
</div>
<h2 data-t="h_cam">Камеры</h2><div class="card">
<div class="row"><label data-t="cams">Показывать камеры</label><input type="checkbox" id="cams"></div>
<div class="row"><label data-t="beep">Сигнал при камере</label><input type="checkbox" id="cam_beep"></div>
<div class="row"><label data-t="vol">Громкость</label><input type="range" id="volume" min="0" max="100" step="5"></div>
</div>
<h2 data-t="h_light">Яркость</h2><div class="card">
<div class="row"><label data-t="ldr">Фоторезистор</label><input type="checkbox" id="ldr"></div>
<div class="row"><label><span data-t="mv">Сигнал датчика</span>: <b id="mv">-</b> mV</label></div>
<div class="row"><button class="s" onclick="cal('dark')" data-t="cal_d">Запомнить «темно»</button><button class="s" onclick="cal('bright')" data-t="cal_b">Запомнить «ярко»</button></div>
</div>
<div class="msg" id="smsg"></div>
<button onclick="save()" data-t="save">Сохранить</button>
<h2 data-t="h_cams">База камер (cams.bin)</h2><div class="card"><div class="row" style="display:block">
<input type="file" id="fc" accept=".bin"><br><br><button onclick="up('cams','fc')" data-t="send">Загрузить</button>
<progress id="pc" value="0" max="100" hidden></progress><div class="msg" id="mc"></div></div></div>
<h2 data-t="h_log">Журнал</h2><div class="card"><div class="row" style="display:block">
<a href="/logs.tar" download="hud-logs.tar"><button type="button" data-t="dl">Скачать журнал (.tar)</button></a></div></div>
<h2 data-t="h_fw">Прошивка (OTA)</h2><div class="card"><div class="row" style="display:block">
<input type="file" id="ff" accept=".bin"><br><br><button onclick="up('update','ff')" data-t="send">Загрузить</button>
<progress id="pf" value="0" max="100" hidden></progress><div class="msg" id="mf"></div></div></div>
<script>
const T={ru:{open:'портал закроется через',h_main:'Основное',lang:'Язык',units:'Единицы',psd:'Знаки из прогноза маршрута (PSD)',vze:'Знаки из камеры/карты приборки (VZE)',tol:'Допуск превышения, км/ч',h_cam:'Камеры',cams:'Показывать камеры',beep:'Сигнал при камере',vol:'Громкость',h_light:'Яркость',ldr:'Фоторезистор',mv:'Сигнал датчика',cal_d:'Запомнить «темно»',cal_b:'Запомнить «ярко»',save:'Сохранить',h_cams:'База камер (cams.bin)',h_log:'Журнал',dl:'Скачать журнал (.tar)',h_fw:'Прошивка (OTA)',send:'Загрузить',saved:'Сохранено',fail:'Ошибка',closed:'портал закрыт',done:'Готово',reboot:'Готово, HUD перезагружается'},
en:{open:'portal closes in',h_main:'General',lang:'Language',units:'Units',psd:'Route-prediction signs (PSD)',vze:'Cluster/map signs (VZE)',tol:'Overspeed tolerance, km/h',h_cam:'Cameras',cams:'Show cameras',beep:'Camera beep',vol:'Volume',h_light:'Brightness',ldr:'Photoresistor',mv:'Sensor level',cal_d:'Set "dark" point',cal_b:'Set "bright" point',save:'Save',h_cams:'Camera database (cams.bin)',h_log:'Log',dl:'Download log (.tar)',h_fw:'Firmware (OTA)',send:'Upload',saved:'Saved',fail:'Error',closed:'portal closed',done:'Done',reboot:'Done, HUD is rebooting'}};
let L=(navigator.language||'ru').startsWith('en')?'en':'ru',left=0,up_busy=false;
const $=id=>document.getElementById(id),tr=k=>T[L][k]||k;
function i18n(){document.querySelectorAll('[data-t]').forEach(e=>e.textContent=tr(e.dataset.t))}
const F=['psd','vze','cams','cam_beep','ldr'],N=['lang','units','tol','volume'];
function fill(s){F.forEach(k=>$(k).checked=!!s[k]);N.forEach(k=>$(k).value=s[k])}
async function st(){try{const s=await(await fetch('/api/state')).json();$('ver').textContent='v'+s.ver;left=s.left;$('mv').textContent=s.mv;
 if(!st.f){fill(s);st.f=1;L=s.lang?'en':'ru';i18n()}}catch(e){$('left').textContent=tr('closed')}}
function tick(){if(left>0&&!up_busy)left--;const m=Math.floor(left/60),s=left%60;if(left>0)$('left').textContent=m+':'+String(s).padStart(2,'0')}
async function save(){const b=new URLSearchParams();F.forEach(k=>b.set(k,$(k).checked?1:0));N.forEach(k=>b.set(k,$(k).value));
 let s={};try{const r=await fetch('/api/set',{method:'POST',body:b});s=await r.json()}catch(e){}
 $('smsg').className='msg '+(s.ok?'ok':'er');$('smsg').textContent=tr(s.ok?'saved':'fail');
 if(s.ok){fill(s);L=s.lang==1?'en':'ru';i18n()}}
async function cal(p){let s={};try{s=await(await fetch('/api/cal?p='+p,{method:'POST'})).json()}catch(e){}
 $('smsg').className='msg '+(s.ok?'ok':'er');$('smsg').textContent=tr(s.ok?'saved':'fail');if(s.ok)$('mv').textContent=s.mv}
function up(path,id){const f=$(id).files[0];if(!f)return;const k=path=='cams'?'c':'f',p=$('p'+k),m=$('m'+k);p.hidden=false;p.value=0;m.textContent='';up_busy=true;
 const fd=new FormData();fd.append('file',f,f.name);const x=new XMLHttpRequest();x.open('POST','/'+path+'?size='+f.size);
 x.upload.onprogress=e=>{if(e.lengthComputable)p.value=100*e.loaded/e.total};
 x.onload=()=>{up_busy=false;let j={};try{j=JSON.parse(x.responseText)}catch(e){}m.className='msg '+(x.status==200&&j.ok?'ok':'er');m.textContent=x.status==200&&j.ok?tr(path=='update'?'reboot':'done'):(tr('fail')+': '+(j.err||x.status))};
 x.onerror=()=>{up_busy=false;m.className='msg er';m.textContent=tr('fail')};x.send(fd)}
i18n();st();setInterval(tick,1000);setInterval(()=>{if(!up_busy)st()},5000);
</script></body></html>)HTML";
