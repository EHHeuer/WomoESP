// Webseite: simuliert das spaetere E-Ink-Display (Waveshare 1,54", 200x200, 1 Bit,
// Teil- und Voll-Refresh). Holt die Daten von /api/status. Layout ist Vorlage fuer Phase 4.
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bordbatterie</title>
<style>
:root{--bg:#f2f1ee;--fg:#222;--muted:#777;--bezel:#2b2b2b}
@media (prefers-color-scheme:dark){:root{--bg:#161616;--fg:#ddd;--muted:#888;--bezel:#3a3a3a}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;padding:24px 16px}
.bezel{background:var(--bezel);padding:14px 14px 22px;border-radius:10px;box-shadow:0 4px 18px rgba(0,0,0,.25);max-width:100%;box-sizing:border-box}
canvas{display:block;image-rendering:pixelated;max-width:100%;height:auto}
.bar{display:flex;gap:16px;margin-top:14px;color:var(--muted);flex-wrap:wrap;justify-content:center}
a{color:inherit}
#net{color:#b33}
</style></head><body>
<div class="bezel"><canvas id="c" width="200" height="200"></canvas></div>
<div class="bar"><span>Simulation Waveshare 1,54" (200x200)</span><a href="/api/status">/api/status</a><span id="net"></span></div>
<script>
const W=200,H=200;
const PAPER=[0xe6,0xe4,0xdc],INK=[0x15,0x15,0x15];
const MIN_REDRAW_MS=10000,FULL_EVERY=10;
const c=document.getElementById('c'),ctx=c.getContext('2d',{willReadFrequently:true});
let data=null,fetchedAt=0,lastText='',lastDraw=0,draws=0,busy=false;

function scale(){c.style.width=(W*Math.min(3,Math.max(1,Math.floor((innerWidth-60)/W))))+'px'}
function dur(m){return Math.floor(m/60)+' h '+String(m%60).padStart(2,'0')+' min'}
function age(){if(!data||!data.bms.valid)return null;return data.bms.age_s+Math.round((Date.now()-fetchedAt)/1000)}
function ageText(a){if(a<30)return'aktuell';if(a<3600)return'vor '+Math.round(a/60)+' min';return'vor '+Math.round(a/3600)+' h'}

function model(){
  if(!data)return{state:'Warte auf ESP ...'};
  const b=data.bms;
  if(!b.valid)return{state:b.connected?'BMS verbunden':'Suche BMS ...'};
  let rl='Ruhe',rv='--';
  if(b.minutes_to_empty!==null){rl='Leer in';rv=dur(b.minutes_to_empty)}
  else if(b.minutes_to_full!==null){rl='Voll in';rv=dur(b.minutes_to_full)}
  const a=age();
  return{soc:b.soc,v:b.voltage_v.toFixed(2)+' V',i:(b.current_a>0?'+':'')+b.current_a.toFixed(2)+' A',
    rl,rv,stale:a>=30,age:ageText(a),state:b.connected?'BMS ok':'BMS getrennt'};
}

function text(t,x,y,px,bold,align){ctx.font=(bold?'700 ':'')+px+'px Arial,Helvetica,sans-serif';ctx.textAlign=align||'left';ctx.fillText(t,x,y)}
function footer(m){
  const px=12,y=H-px-8;
  if(m.stale){ctx.fillRect(0,y-4,W,px+12);ctx.fillStyle='#fff';text('VERALTET '+m.age,6,H-8,px,true);text(m.state,W-6,H-8,px,true,'right');ctx.fillStyle='#000';return}
  ctx.fillRect(0,y-4,W,2);text(m.age,6,H-8,px);text(m.state,W-6,H-8,px,false,'right');
}
function draw(m){
  if(m.soc===undefined){text(m.state,W/2,H/2,16,true,'center');return}
  text(m.soc+'%',W/2,80,72,true,'center');
  ctx.fillRect(0,92,W,2);
  text(m.v,6,122,20,true);text(m.i,W-6,122,20,true,'right');
  text(m.rl,6,154,14);text(m.rv,W-6,154,20,true,'right');
  footer(m);
}

function threshold(){
  const img=ctx.getImageData(0,0,W,H),d=img.data;
  for(let p=0;p<d.length;p+=4){const on=(d[p]*0.3+d[p+1]*0.59+d[p+2]*0.11)<128;const col=on?INK:PAPER;d[p]=col[0];d[p+1]=col[1];d[p+2]=col[2];d[p+3]=255}
  ctx.putImageData(img,0,0);
}
function paint(m){ctx.fillStyle='#fff';ctx.fillRect(0,0,W,H);ctx.fillStyle='#000';draw(m);threshold()}
function flash(col){ctx.fillStyle='rgb('+col.join(',')+')';ctx.fillRect(0,0,W,H)}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));

async function render(force){
  if(busy)return;
  const m=model(),t=JSON.stringify(m);
  if(m.soc!==undefined&&!lastText.includes('"soc"'))force=true;
  if(!force&&(t===lastText||Date.now()-lastDraw<MIN_REDRAW_MS))return;
  busy=true;
  if(force||draws%FULL_EVERY===0){flash(INK);await sleep(180);flash(PAPER);await sleep(180);flash(INK);await sleep(120)}
  paint(m);
  lastText=t;lastDraw=Date.now();draws++;busy=false;
}

async function poll(){
  try{const r=await fetch('/api/status',{cache:'no-store'});data=await r.json();fetchedAt=Date.now();document.getElementById('net').textContent=''}
  catch(e){document.getElementById('net').textContent='ESP nicht erreichbar'}
  render(false);
}
scale();addEventListener('resize',scale);render(true);poll();setInterval(poll,5000);
</script></body></html>)HTML";
