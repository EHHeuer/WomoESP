// Webseite: simuliert das spaetere E-Ink-Display (1 Bit, Teil- und Voll-Refresh).
// Holt die Daten von /api/status. Layout ist Vorlage fuer das GxEPD2-Layout in Phase 4.
#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="de"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bordbatterie</title>
<style>
:root{--bg:#f2f1ee;--fg:#222;--muted:#777;--bezel:#2b2b2b;--paper:#e6e4dc;--ink:#151515}
@media (prefers-color-scheme:dark){:root{--bg:#161616;--fg:#ddd;--muted:#888;--bezel:#3a3a3a}}
body{margin:0;background:var(--bg);color:var(--fg);font:14px system-ui,sans-serif;display:flex;flex-direction:column;align-items:center;padding:24px 16px}
.bezel{background:var(--bezel);padding:14px 14px 22px;border-radius:10px;box-shadow:0 4px 18px rgba(0,0,0,.25);max-width:100%;box-sizing:border-box}
canvas{display:block;image-rendering:pixelated;max-width:100%;height:auto}
.bar{display:flex;gap:16px;align-items:center;margin-top:14px;color:var(--muted);flex-wrap:wrap;justify-content:center}
select{font:inherit}
a{color:inherit}
#net{color:#b33}
</style></head><body>
<div class="bezel"><canvas id="c"></canvas></div>
<div class="bar">
  <label>Display <select id="size">
    <option value="29">2,9" (296x128)</option>
    <option value="42">4,2" (400x300)</option>
  </select></label>
  <a href="/api/status">/api/status</a>
  <span id="net"></span>
</div>
<script>
const SIZES={29:[296,128],42:[400,300]};
const PAPER=[0xe6,0xe4,0xdc],INK=[0x15,0x15,0x15];
const MIN_REDRAW_MS=10000,FULL_EVERY=10;
const c=document.getElementById('c'),ctx=c.getContext('2d',{willReadFrequently:true});
const sel=document.getElementById('size');
let size='42',data=null,fetchedAt=0,lastText='',lastDraw=0,draws=0,busy=false;
try{size=localStorage.getItem('size')||size}catch(e){}
sel.value=size;
sel.onchange=()=>{size=sel.value;try{localStorage.setItem('size',size)}catch(e){};setup();render(true)};

function setup(){const[w,h]=SIZES[size];c.width=w;c.height=h;c.style.width=(w*Math.min(2,Math.max(1,Math.floor((innerWidth-60)/w))))+'px'}
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
    rl,rv,ah:b.remain_ah.toFixed(1)+'/'+b.nominal_ah.toFixed(1)+' Ah',
    stale:a>=30,age:ageText(a),state:b.connected?'BMS ok':'BMS getrennt'};
}

function font(px,bold){ctx.font=(bold?'700 ':'')+px+'px Arial,Helvetica,sans-serif'}
function text(t,x,y,px,bold,align){font(px,bold);ctx.textAlign=align||'left';ctx.fillText(t,x,y)}
function battery(x,y,w,h,soc){
  ctx.lineWidth=3;ctx.strokeRect(x+1.5,y+1.5,w-3,h-3);
  ctx.fillRect(x+w,y+h*0.3,Math.max(4,w*0.03),h*0.4);
  const fw=Math.round((w-12)*Math.max(0,Math.min(100,soc))/100);
  ctx.fillRect(x+6,y+6,fw,h-12);
}
function footer(m,w,h,px){
  const y=h-px-8;
  if(m.stale){ctx.fillRect(0,y-4,w,px+12);ctx.fillStyle='#fff';text('VERALTET  '+m.age,8,h-8,px,true);text(m.state,w-8,h-8,px,true,'right');ctx.fillStyle='#000';return}
  ctx.fillRect(0,y-4,w,2);text(m.age,8,h-8,px);text(m.state,w-8,h-8,px,false,'right');
}

function draw42(m,w,h){
  text('BORDBATTERIE',12,26,16,true);text(m.ah||'',w-12,26,16,false,'right');ctx.fillRect(0,36,w,2);
  if(m.soc===undefined){text(m.state,w/2,h/2,24,true,'center');return}
  text(m.soc+'%',12,150,96,true);
  battery(12,170,206,52,m.soc);
  ctx.fillRect(236,52,2,186);
  const x=252;
  text('Spannung',x,70,15);text(m.v,x,98,26,true);
  text('Strom',x,135,15);text(m.i,x,163,26,true);
  text(m.rl,x,200,15);text(m.rv,x,228,24,true);
  footer(m,w,h,16);
}
function draw29(m,w,h){
  if(m.soc===undefined){text(m.state,w/2,h/2,18,true,'center');return}
  text(m.soc+'%',6,62,60,true);
  battery(6,70,124,26,m.soc);
  const x=142;
  text(m.v,x,24,18,true);text(m.i,x,48,18,true);
  text(m.rl,x,68,12);text(m.rv,x,88,18,true);
  footer(m,w,h,12);
}

function threshold(){
  const img=ctx.getImageData(0,0,c.width,c.height),d=img.data;
  for(let p=0;p<d.length;p+=4){const on=(d[p]*0.3+d[p+1]*0.59+d[p+2]*0.11)<128;const col=on?INK:PAPER;d[p]=col[0];d[p+1]=col[1];d[p+2]=col[2];d[p+3]=255}
  ctx.putImageData(img,0,0);
}
function paint(m){
  const[w,h]=SIZES[size];
  ctx.fillStyle='#fff';ctx.fillRect(0,0,w,h);ctx.fillStyle='#000';ctx.strokeStyle='#000';
  (size==='42'?draw42:draw29)(m,w,h);
  threshold();
}
function flash(col){ctx.fillStyle='rgb('+col.join(',')+')';ctx.fillRect(0,0,c.width,c.height)}
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
setup();render(true);poll();setInterval(poll,5000);
</script></body></html>)HTML";
