// =====================================================================
//  Páginas web del Monitor del calentador solar
//  Diseño visual basado en el portal de los compañeros (mismos colores,
//  tarjetas, botones y menú). Las páginas son estáticas: los datos se
//  cargan con JavaScript desde la API JSON (/status, /data, ...), la
//  misma que usa la app de Delphi.
// =====================================================================
#pragma once
#include <pgmspace.h>

// ---------------------------------------------------------------------
// Estilos comunes
// ---------------------------------------------------------------------
const char WEB_CSS[] PROGMEM = R"HTML(<style>
body{font-family:Arial;background:#f2f2f2;margin:0;padding:20px;}
h1{text-align:center;color:#333;}
.card{background:white;padding:20px;border-radius:12px;box-shadow:0 2px 6px rgba(0,0,0,.15);margin-bottom:20px;}
button{width:100%;padding:14px;margin-top:12px;background:#1976D2;color:white;border:none;border-radius:8px;font-size:16px;cursor:pointer;}
.calibrar{background:#388E3C;}
.danger{background:#D32F2F;}
.grid-superior{display:grid;grid-template-columns:1fr 1fr;gap:20px;align-items:start;}
.dato-card{min-height:310px;}
table{width:100%;border-collapse:collapse;font-size:14px;}
th,td{padding:8px;border-bottom:1px solid #ddd;text-align:center;white-space:nowrap;}
th{background:#1976D2;color:white;position:sticky;top:0;}
.log-scroll{max-height:310px;overflow:auto;}
.log-actions{display:flex;gap:10px;flex-wrap:wrap;}
.log-actions a,.log-actions button{flex:1;}
.log-actions button{margin-top:0;}
.log-status{padding:10px;background:#f7f7f7;border-radius:8px;margin-top:12px;color:#555;font-size:14px;}
.log-hidden{display:none;}
label{display:block;font-weight:bold;margin-top:12px;}
input{width:100%;padding:12px;margin-top:8px;box-sizing:border-box;border-radius:8px;border:1px solid #ccc;font-size:16px;}
.ok{color:green;font-weight:bold;}
.bad{color:red;font-weight:bold;}
.warn{color:#e67e22;font-weight:bold;}
#mensaje{margin-top:15px;font-weight:bold;}
.big{font-size:60px;font-weight:bold;text-align:center;}
.centro{text-align:center;}
.pill{display:inline-block;padding:5px 16px;border-radius:999px;color:white;font-weight:bold;background:#999;}
.thermo{height:16px;background:#e8e8e8;border-radius:8px;overflow:hidden;margin-top:16px;}
.thermo-fill{height:100%;width:0;border-radius:8px;background:#999;transition:width .5s;}
.escala{display:flex;justify-content:space-between;font-size:12px;color:#888;margin-top:3px;}
.stats{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;margin-top:14px;}
.stat{background:#f7f7f7;border-radius:8px;padding:8px 14px;min-width:90px;text-align:center;}
.stat small{display:block;color:#777;}
.nota{color:#666;}
canvas{width:100%;height:160px;background:#fafafa;border:1px solid #eee;border-radius:8px;}
@media(max-width:900px){.grid-superior{grid-template-columns:1fr;}}
</style>)HTML";

// ---------------------------------------------------------------------
// Menú superior (igual al de los compañeros)
// ---------------------------------------------------------------------
const char WEB_MENU[] PROGMEM = R"HTML(<div style='margin-bottom:20px;'>
<a href='/'><button style='width:19%;padding:12px;'>Inicio</button></a>
<a href='/monitor'><button style='width:19%;padding:12px;'>Monitor</button></a>
<a href='/calibracion'><button style='width:19%;padding:12px;'>Calibración</button></a>
<a href='/energia'><button style='width:19%;padding:12px;'>Energía</button></a>
<a href='/wifi'><button style='width:19%;padding:12px;'>WiFi</button></a>
</div>)HTML";

// ---------------------------------------------------------------------
// Utilidades JavaScript comunes
// ---------------------------------------------------------------------
const char WEB_JS_COMUN[] PROGMEM = R"HTML(<script>
async function api(url){
  const r=await fetch(url+(url.includes('?')?'&':'?')+'_='+Date.now(),{cache:'no-store'});
  const d=await r.json();
  if(!r.ok) throw new Error(d.error||('HTTP '+r.status));
  return d;
}
function txt(id,v){const e=document.getElementById(id);if(e)e.innerText=v;}
function duracion(s){s=Math.max(0,Math.round(s));const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return (h?h+' h ':'')+(h||m?m+' min ':'')+x+' s';}
function modoTexto(m){
  if(m==='cpu80_wifi_on')return 'CPU a 80 MHz, Wi-Fi siempre activo';
  if(m==='light')return 'Light sleep + Wi-Fi en ahorro';
  if(m==='modem')return 'Modem sleep + CPU a 80 MHz';
  return m||'--';
}
function estadoTexto(e){return e==='activo'?'<span class="ok">ACTIVO</span>':'<span class="warn">EN REPOSO</span>';}
</script>)HTML";

// =====================================================================
// PÁGINA: INICIO  (/)
// =====================================================================
const char WEB_INICIO[] PROGMEM = R"HTML(
<div class='card'>
<h2>Información del ESP32</h2>
<b>Firmware:</b> <span id='fw'>--</span><br>
<b>MAC:</b> <span id='mac'>--</span><br>
<b>Red WiFi (SSID):</b> <span id='ssid'>--</span><br>
<b>IP:</b> <span id='ip'>--</span><br>
<b>RSSI:</b> <span id='rssi'>--</span><br>
<b>mDNS:</b> http://nivel.local<br>
<b>Tiempo encendido:</b> <span id='uptime'>--</span>
</div>

<div class='card'>
<h2>Menú</h2>
<a href='/monitor'><button>Ver temperatura y nivel</button></a>
<a href='/calibracion'><button>Calibrar la sonda de nivel</button></a>
<a href='/energia'><button>Modo de reposo (energía)</button></a>
<a href='/wifi'><button>Configurar red WiFi</button></a>
<a href='/data'><button>Ver JSON de los sensores</button></a>
<a href='/status'><button>Ver JSON del ESP32</button></a>
</div>

<div class='card'>
<h2>Estado</h2>
WiFi: <span id='wifi' class='bad'>SIN DATOS</span><br><br>
Energía: <span id='energia'>--</span><br><br>
<b>Modo de reposo:</b> <span id='modo'>--</span><br>
<b>Se dormirá en:</b> <span id='restante'>--</span>
</div>

<p style='text-align:center;color:#999;'>Monitor del calentador solar · Firmware <span id='fw2'>--</span></p>

<script>
async function cargar(){
  try{
    const s=await api('/status');
    txt('fw',s.fw);txt('fw2',s.fw);txt('mac',s.mac);txt('ssid',s.ssid);txt('ip',s.ip);
    txt('rssi',s.rssi+' dBm');txt('uptime',duracion(s.uptime));
    const w=document.getElementById('wifi');w.className='ok';w.innerText='CONECTADO';
    document.getElementById('energia').innerHTML=estadoTexto(s.state);
    txt('modo',modoTexto(s.sleepMode));
    txt('restante',s.state==='activo'?duracion(s.awakeRemaining)+' sin peticiones':'—');
  }catch(e){console.error(e);}
}
window.addEventListener('load',cargar);
</script>
)HTML";

// =====================================================================
// PÁGINA: MONITOR  (/monitor)
// =====================================================================
const char WEB_MONITOR[] PROGMEM = R"HTML(
<div class='grid-superior'>

<div class='card dato-card'>
<h2>Temperatura del agua</h2>
<div id='temp' class='big' style='color:#999;'>--</div>
<div class='centro'><span id='tempEstado' class='pill'>Sin datos</span></div>
<div class='thermo'><div id='thermo' class='thermo-fill'></div></div>
<div class='escala'><span>0 °C</span><span>50 °C</span><span>100 °C</span></div>
<div class='stats'>
<div class='stat'><small>Mínima</small><b id='tMin'>--</b></div>
<div class='stat'><small>Promedio</small><b id='tProm'>--</b></div>
<div class='stat'><small>Máxima</small><b id='tMax'>--</b></div>
</div>
</div>

<div class='card dato-card'>
<h2>Nivel de agua</h2>
<div id='nivel' class='big' style='color:#999;'>--</div>
<p class='centro' style='font-size:18px;'><b id='nivelTexto'>--</b></p>
<p class='centro' style='color:#777;' id='ultima'>Última lectura</p>
<p class='centro'>Sin filtro: <b id='crudo'>--</b><br>ADC: <b id='adc'>--</b> · Voltaje: <b id='volt'>--</b></p>
</div>

</div>

<div class='card'>
<h2>Gráfica</h2>
<canvas id='grafica' width='900' height='160'></canvas>
<p class='nota' style='font-size:13px;'><span style='color:#e65100;font-weight:bold;'>━ Temperatura</span> (escala automática) &nbsp; <span style='color:#1976D2;font-weight:bold;'>━ Nivel</span> (0–100 %)</p>
</div>

<div class='card'>
<h2>Log de lecturas · cada 2 s</h2>
<p class='nota'>El historial se guarda en este navegador mientras la sesión esté abierta (máximo 2000 lecturas). Mientras esta página esté abierta, el ESP32 se mantiene despierto.</p>
<div class='log-actions'>
<button id='botonLog' type='button' onclick='alternarLog()'>Ocultar log en vivo</button>
<button id='botonPausa' type='button' onclick='alternarPausa()'>Pausar lecturas</button>
</div>
<div id='estadoLog' class='log-status'>Log visible en vivo.</div>
<div id='panelLog'>
<div class='log-scroll'>
<table>
<thead><tr><th>#</th><th>Hora</th><th>ADC</th><th>Voltaje</th><th>Nivel</th><th>Sin filtro</th><th>Temp.</th><th>Estado</th></tr></thead>
<tbody id='logBody'><tr><td colspan='8'>Aún no hay lecturas</td></tr></tbody>
</table>
</div>
</div>
<div class='log-actions' style='margin-top:12px;'>
<button type='button' onclick='descargarCSV()'>Descargar CSV</button>
<button type='button' class='danger' onclick='limpiar()'>Limpiar historial</button>
</div>
</div>

<div class='card'>
<a href='/'><button>Regresar al menú principal</button></a>
</div>

<script>
const CLAVE='calentador_registros';
let registros=[];
try{registros=JSON.parse(sessionStorage.getItem(CLAVE)||'[]');}catch(e){registros=[];}
let pausado=false;

function guardar(){try{sessionStorage.setItem(CLAVE,JSON.stringify(registros));}catch(e){}}
function hora(){const d=new Date();return [d.getHours(),d.getMinutes(),d.getSeconds()].map(x=>String(x).padStart(2,'0')).join(':');}
function colorNivel(p){return p<=0?'#e74c3c':(p<50?'#e67e22':(p<80?'#f1c40f':'#2ecc71'));}
function estadoTemp(t){
  if(t<25)return{t:'Fría',c:'#1e88e5'};
  if(t<40)return{t:'Tibia',c:'#43a047'};
  if(t<60)return{t:'Caliente',c:'#fb8c00'};
  return{t:'Muy caliente',c:'#e53935'};
}

function pintarTemperatura(){
  const ult=registros.length?registros[registros.length-1]:null;
  const t=ult?ult.temp:null;
  const el=document.getElementById('temp'),pill=document.getElementById('tempEstado'),bar=document.getElementById('thermo');
  if(t===null){el.innerText='--';el.style.color='#999';pill.innerText=ult?'Sensor desconectado':'Sin datos';pill.style.background='#999';bar.style.width='0%';}
  else{const e=estadoTemp(t);el.innerText=t.toFixed(1)+' °C';el.style.color=e.c;pill.innerText=e.t;pill.style.background=e.c;
    bar.style.width=Math.max(0,Math.min(100,t))+'%';bar.style.background=e.c;}
  const ts=registros.map(r=>r.temp).filter(x=>x!==null);
  if(ts.length){txt('tMin',Math.min(...ts).toFixed(1)+' °C');txt('tMax',Math.max(...ts).toFixed(1)+' °C');
    txt('tProm',(ts.reduce((a,b)=>a+b,0)/ts.length).toFixed(1)+' °C');}
  else{txt('tMin','--');txt('tMax','--');txt('tProm','--');}
}

function pintarNivel(){
  const ult=registros.length?registros[registros.length-1]:null;
  if(!ult)return;
  const el=document.getElementById('nivel');
  el.innerText=Math.round(ult.nivel)+'%';el.style.color=colorNivel(ult.nivel);
  txt('nivelTexto',ult.nivel<=0?'Tanque vacío':(ult.nivel>=100?'Tanque lleno':'Nivel estimado'));
  txt('ultima','#'+registros.length+' · '+ult.hora);
  txt('crudo',ult.crudo.toFixed(1)+' %');txt('adc',ult.adc);txt('volt',ult.volt.toFixed(3)+' V');
}

function pintarLog(){
  const cuerpo=document.getElementById('logBody');
  if(!registros.length){cuerpo.innerHTML="<tr><td colspan='8'>Aún no hay lecturas</td></tr>";return;}
  const ini=Math.max(0,registros.length-100);
  let h='';
  for(let i=registros.length-1;i>=ini;i--){const r=registros[i];
    h+='<tr><td>'+(i+1)+'</td><td>'+r.hora+'</td><td>'+r.adc+'</td><td>'+r.volt.toFixed(3)+' V</td><td>'+r.nivel.toFixed(1)+' %</td><td>'+r.crudo.toFixed(1)+' %</td><td>'+(r.temp===null?'--':r.temp.toFixed(1)+' °C')+'</td><td>'+r.estado+'</td></tr>';}
  cuerpo.innerHTML=h;
}

function pintarGrafica(){
  const c=document.getElementById('grafica'),ctx=c.getContext('2d'),w=c.width,h=c.height;
  ctx.clearRect(0,0,w,h);
  const d=registros.slice(-100);
  if(d.length<2){ctx.fillStyle='#999';ctx.font='14px Arial';ctx.fillText('La gráfica aparece al recibir lecturas',12,h/2);return;}
  const ts=d.map(r=>r.temp).filter(x=>x!==null);
  let mn=ts.length?Math.min(...ts):0,mx=ts.length?Math.max(...ts):100;
  if(mx-mn<1){mn-=0.5;mx+=0.5;}
  const X=i=>6+i/(d.length-1)*(w-12);
  ctx.lineWidth=2;
  ctx.strokeStyle='#1976D2';ctx.beginPath();
  d.forEach((r,i)=>{const y=h-8-(Math.max(0,Math.min(100,r.nivel))/100)*(h-24);i?ctx.lineTo(X(i),y):ctx.moveTo(X(i),y);});
  ctx.stroke();
  ctx.strokeStyle='#e65100';ctx.beginPath();let primero=true;
  d.forEach((r,i)=>{if(r.temp===null)return;const y=h-8-((r.temp-mn)/(mx-mn))*(h-24);primero?ctx.moveTo(X(i),y):ctx.lineTo(X(i),y);primero=false;});
  ctx.stroke();
  ctx.font='12px Arial';ctx.fillStyle='#e65100';ctx.fillText(mx.toFixed(1)+' °C',8,14);ctx.fillText(mn.toFixed(1)+' °C',8,h-4);
}

function pintarTodo(){pintarTemperatura();pintarNivel();pintarLog();pintarGrafica();}

async function leer(){
  if(pausado)return;
  try{
    const d=await api('/data');
    registros.push({hora:hora(),adc:d.adc,volt:Number(d.voltage),nivel:Number(d.level),crudo:Number(d.levelRaw),
      temp:d.temperatureC===null?null:Number(d.temperatureC),estado:d.state});
    if(registros.length>2000)registros.shift();
    guardar();pintarTodo();
  }catch(e){console.error(e);document.getElementById('estadoLog').innerText='Sin respuesta del ESP32: '+e.message;}
}

function visibleLog(){try{return localStorage.getItem('calentador_log_visible')!=='0';}catch(e){return true;}}
function aplicarEstadoLog(){
  const v=visibleLog();
  document.getElementById('panelLog').classList.toggle('log-hidden',!v);
  document.getElementById('botonLog').innerText=v?'Ocultar log en vivo':'Mostrar log en vivo';
  document.getElementById('estadoLog').innerText=pausado?'Lecturas en pausa: el ESP32 podrá entrar en reposo.':(v?'Log visible en vivo.':'Log oculto; las lecturas se siguen registrando.');
}
function alternarLog(){try{localStorage.setItem('calentador_log_visible',visibleLog()?'0':'1');}catch(e){}aplicarEstadoLog();}
function alternarPausa(){pausado=!pausado;document.getElementById('botonPausa').innerText=pausado?'Reanudar lecturas':'Pausar lecturas';aplicarEstadoLog();if(!pausado)leer();}

function descargarCSV(){
  if(!registros.length){alert('Todavía no hay datos.');return;}
  let csv='Hora,ADC,Voltaje (V),Nivel (%),Nivel sin filtro (%),Temperatura (C),Estado ESP32\n';
  registros.forEach(r=>{csv+=[r.hora,r.adc,r.volt.toFixed(3),r.nivel.toFixed(2),r.crudo.toFixed(2),r.temp===null?'':r.temp.toFixed(2),r.estado].join(',')+'\n';});
  const b=new Blob(['﻿'+csv],{type:'text/csv;charset=utf-8'});
  const a=document.createElement('a');a.href=URL.createObjectURL(b);a.download='calentador_solar.csv';a.click();URL.revokeObjectURL(a.href);
}
function limpiar(){if(!confirm('¿Borrar el historial guardado en este navegador?'))return;registros=[];guardar();pintarTodo();
  document.getElementById('nivel').innerText='--';}

window.addEventListener('load',function(){aplicarEstadoLog();pintarTodo();leer();setInterval(leer,2000);});
</script>
)HTML";

// =====================================================================
// PÁGINA: CALIBRACIÓN  (/calibracion)
// =====================================================================
const char WEB_CALIBRACION[] PROGMEM = R"HTML(
<div class='card'>
<h2>Calibración de la sonda de nivel</h2>
<p>La sonda entrega un valor ADC alto con el tanque vacío y uno más bajo con el tanque lleno.
El nivel en porcentaje se calcula entre estos dos puntos. La calibración se guarda en la memoria del ESP32 y se conserva al reiniciar.</p>
<table>
<tr><th>Dato</th><th>Valor</th></tr>
<tr><td>ADC actual de la sonda</td><td id='adcActual'>--</td></tr>
<tr><td>Nivel actual</td><td id='nivelActual'>--</td></tr>
<tr><td>ADC guardado como VACÍO (0 %)</td><td id='vacio'>--</td></tr>
<tr><td>ADC guardado como LLENO (100 %)</td><td id='lleno'>--</td></tr>
</table>
<button type='button' onclick='cargar()'>Actualizar lectura</button>
</div>

<div class='card'>
<h2>Guardar puntos de calibración</h2>
<p>Haz cada paso con el tanque <b>realmente</b> vacío o lleno y espera unos segundos a que la lectura se estabilice.</p>
<button class='calibrar' type='button' onclick="calibrar('empty','VACÍO')">Guardar VACÍO (tanque vacío)</button>
<button class='calibrar' type='button' onclick="calibrar('full','LLENO')">Guardar LLENO (tanque lleno)</button>
<button class='danger' type='button' onclick="calibrar('default','valores por defecto')">Restaurar valores por defecto</button>
<div id='mensaje'></div>
</div>

<script>
async function cargar(){
  try{
    const [s,d]=await Promise.all([api('/status'),api('/data')]);
    txt('vacio',s.emptyAdc);txt('lleno',s.fullAdc);txt('adcActual',d.adc);
    txt('nivelActual',Number(d.level).toFixed(1)+' % (sin filtro '+Number(d.levelRaw).toFixed(1)+' %)');
  }catch(e){console.error(e);}
}
async function calibrar(punto,nombre){
  const m=document.getElementById('mensaje');
  const pregunta=punto==='default'?'¿Restaurar la calibración de fábrica (vacío 3400, lleno 2449)?':'Asegúrate de que el tanque esté '+nombre+'. ¿Guardar la lectura actual como '+nombre+'?';
  if(!confirm(pregunta))return;
  m.innerText='Guardando...';m.style.color='#555';
  try{
    const r=await api('/calibrate?point='+punto);
    m.innerText='Calibración guardada: vacío = '+r.emptyAdc+', lleno = '+r.fullAdc;m.style.color='green';
    cargar();
  }catch(e){m.innerText=e.message;m.style.color='red';}
}
window.addEventListener('load',cargar);
</script>
)HTML";

// =====================================================================
// PÁGINA: ENERGÍA  (/energia)
// =====================================================================
const char WEB_ENERGIA[] PROGMEM = R"HTML(
<div class='card'>
<h2>Estado de energía</h2>
<b>Estado:</b> <span id='estado'>--</span><br><br>
<b>Modo de reposo:</b> <span id='modo'>--</span><br>
<b>Se dormirá en:</b> <span id='restante'>--</span><br>
<b>Tiempo sin peticiones para dormir:</b> <span id='idle'>--</span><br>
<b>Tiempo encendido:</b> <span id='uptime'>--</span>
<button type='button' onclick='cargar()'>Actualizar estado</button>
<p class='nota' style='font-size:14px;'>Nota: consultar el estado también es una petición, así que despierta al ESP32 si estaba en reposo.</p>
</div>

<div class='card'>
<h2>¿Cómo funciona?</h2>
<p>Si nadie consulta al ESP32 durante el tiempo indicado, entra en <b>reposo</b>: baja la frecuencia del procesador de 240 MHz a 80 MHz y deja de leer los sensores.
El Wi-Fi se mantiene encendido en todo momento, de modo que el ESP32 siempre puede responder.
<b>Cualquier petición</b> (esta página, la app o el JSON) lo regresa de inmediato al modo activo.</p>
</div>

<div class='card'>
<h2>Control manual</h2>
<label>Mantener despierto (minutos, 1 a 60)</label>
<input id='minutos' type='number' min='1' max='60' value='5'>
<button class='calibrar' type='button' onclick='despertar()'>Despertar</button>
<button class='danger' type='button' onclick='dormir()'>Dormir ahora</button>
<div id='mensaje'></div>
</div>

<script>
async function cargar(){
  try{
    const s=await api('/status');
    document.getElementById('estado').innerHTML=estadoTexto(s.state);
    txt('modo',modoTexto(s.sleepMode));
    txt('restante',s.state==='activo'?duracion(s.awakeRemaining):'—');
    txt('idle',duracion(s.idleTimeout));txt('uptime',duracion(s.uptime));
  }catch(e){console.error(e);}
}
async function despertar(){
  const m=document.getElementById('mensaje');
  let n=parseInt(document.getElementById('minutos').value,10);
  if(isNaN(n)||n<1||n>60){m.innerText='Escribe un número de minutos entre 1 y 60';m.style.color='red';return;}
  try{const r=await api('/wake?min='+n);m.innerText='ESP32 despierto durante al menos '+r.awakeMinutes+' min';m.style.color='green';cargar();}
  catch(e){m.innerText='Error: '+e.message;m.style.color='red';}
}
async function dormir(){
  const m=document.getElementById('mensaje');
  try{await api('/sleep');m.innerText='El ESP32 entró en reposo. Cualquier petición lo volverá a despertar.';m.style.color='green';
    document.getElementById('estado').innerHTML=estadoTexto('reposo');txt('restante','—');}
  catch(e){m.innerText='Error: '+e.message;m.style.color='red';}
}
window.addEventListener('load',cargar);
</script>
)HTML";

// =====================================================================
// PÁGINA: WIFI  (/wifi)
// =====================================================================
const char WEB_WIFI[] PROGMEM = R"HTML(
<div class='card'>
<h2>Estado de la conexión</h2>
<b>Estado:</b> <span id='estado' class='bad'>Sin datos</span><br><br>
<b>SSID:</b> <span id='ssid'>--</span><br>
<b>IP:</b> <span id='ip'>--</span><br>
<b>RSSI:</b> <span id='rssi'>--</span> <span id='senal'></span><br>
<b>Gateway:</b> <span id='gw'>--</span><br>
<b>Máscara:</b> <span id='mask'>--</span><br>
<b>MAC:</b> <span id='mac'>--</span><br>
<b>mDNS:</b> http://nivel.local
</div>

<div class='card'>
<h2>Cambiar de red</h2>
<p>La red se configura con un portal cautivo. Para cambiarla:</p>
<ol>
<li>Presiona <b>Borrar red guardada</b> (o mantén presionado el botón <b>BOOT</b> al encender el ESP32).</li>
<li>Conéctate a la red <b>SolarTank-Config</b> (contraseña <b>12345678</b>).</li>
<li>Abre <b>http://192.168.4.1</b>, elige tu red y escribe la contraseña.</li>
</ol>
<p class='nota'>Después de borrar la red, esta página dejará de responder hasta que configures la nueva.</p>
<button class='danger' type='button' onclick='borrar()'>Borrar red guardada y reiniciar</button>
<div id='mensaje'></div>
</div>

<script>
function calidad(r){return r>=-60?'(excelente)':(r>=-70?'(buena)':(r>=-80?'(regular)':'(débil)'));}
async function cargar(){
  try{
    const s=await api('/status');
    const e=document.getElementById('estado');e.className='ok';e.innerText='Conectado';
    txt('ssid',s.ssid);txt('ip',s.ip);txt('rssi',s.rssi+' dBm');txt('senal',calidad(s.rssi));
    txt('gw',s.gateway||'--');txt('mask',s.subnet||'--');txt('mac',s.mac);
  }catch(e){console.error(e);}
}
async function borrar(){
  if(!confirm('¿Deseas borrar la red guardada? El ESP32 se reiniciará en modo de configuración.'))return;
  const m=document.getElementById('mensaje');
  try{await api('/resetwifi');}catch(e){}
  m.innerText='Red borrada. Conéctate a SolarTank-Config y abre http://192.168.4.1';m.style.color='green';
}
window.addEventListener('load',cargar);
</script>
)HTML";
