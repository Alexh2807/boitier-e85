// GENERE AUTOMATIQUEMENT par tools/gen_web_ui.py - ne pas modifier a la main
#pragma once
#include <pgmspace.h>
static const char WEB_UI[] PROGMEM = R"WEBUI(<!DOCTYPE html>
<html lang="fr"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>Boitier E85</title>
<style>
:root{--bg:#0d1117;--card:#161b22;--line:#2a313c;--tx:#e6edf3;--mut:#8b949e;--acc:#f0b429;--ok:#3fb950;--bad:#f85149;--blu:#58a6ff}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{margin:0;background:var(--bg);color:var(--tx);font:15px/1.4 system-ui,-apple-system,Roboto,sans-serif;padding-bottom:70px}
header{position:sticky;top:0;z-index:5;background:#0d1117ee;backdrop-filter:blur(6px);display:flex;align-items:center;gap:8px;padding:10px 16px;border-bottom:1px solid var(--line)}
header h1{font-size:17px;margin:0;flex:1}
.dot{width:10px;height:10px;border-radius:50%;background:var(--bad);display:inline-block}
.dot.ok{background:var(--ok)}
.pill{font-size:12px;padding:3px 8px;border-radius:99px;background:var(--card);border:1px solid var(--line);color:var(--mut);white-space:nowrap}
main{padding:12px 16px;max-width:720px;margin:auto}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px;margin-bottom:12px}
.card h2{font-size:13px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 10px}
.card h3{font-size:14px;margin:16px 0 4px;color:var(--acc)}
.big{font-size:64px;font-weight:700;text-align:center;font-variant-numeric:tabular-nums;line-height:1}
.unit{text-align:center;color:var(--mut);margin-top:4px}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}
.grid2{display:grid;grid-template-columns:repeat(2,1fr);gap:8px}
.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row label{flex:1;color:var(--tx)}
.row small{color:var(--mut);display:block;font-size:12px}
input[type=range]{width:100%;accent-color:var(--acc);height:32px;touch-action:none}
input[type=number],input[type=text],select{width:96px;background:#0d1117;color:var(--tx);border:1px solid var(--line);border-radius:8px;padding:8px;font-size:15px;text-align:right}
input[type=text]{width:160px;text-align:left}
select{width:auto;text-align:left}
.val{min-width:70px;text-align:right;font-variant-numeric:tabular-nums;font-weight:600;color:var(--acc)}
button{background:#21262d;color:var(--tx);border:1px solid var(--line);border-radius:10px;padding:11px 14px;font-size:15px;cursor:pointer}
button.pri{background:var(--acc);color:#111;border:none;font-weight:700}
button.dng{border-color:var(--bad);color:var(--bad)}
button:disabled{opacity:.4}
button:active{transform:scale(.98)}
.row label.sw,.sw{position:relative;width:52px;height:30px;flex:0 0 52px}
.sw input{display:none}
.sw i{position:absolute;inset:0;background:#30363d;border-radius:99px;transition:.2s}
.sw i:after{content:"";position:absolute;width:24px;height:24px;left:3px;top:3px;background:#fff;border-radius:50%;transition:.2s}
.sw input:checked+i{background:var(--ok)}
.sw input:checked+i:after{left:25px}
.bar{height:12px;background:#0d1117;border:1px solid var(--line);border-radius:99px;overflow:hidden;position:relative}
.bar i{position:absolute;left:0;top:0;bottom:0;background:var(--blu)}
.bar i.a{background:var(--acc)}
nav{position:fixed;bottom:0;left:0;right:0;display:flex;background:#161b22f2;border-top:1px solid var(--line);z-index:5}
nav button{flex:1;border:none;border-radius:0;background:none;padding:12px 4px;font-size:13px;color:var(--mut)}
nav button.on{color:var(--acc);font-weight:700}
.tab{display:none}.tab.on{display:block}
.warn{background:#3d2a00;border:1px solid #8a6100;color:#ffd479;border-radius:10px;padding:10px;font-size:13px;margin:8px 0}
.err{background:#3d0d0d;border-color:#8a1f1f;color:#ffb3ad}
.info{background:#0c2d4a;border:1px solid #1f6feb;color:#a5d6ff;border-radius:10px;padding:10px;font-size:13px;margin:8px 0}
.muted{color:var(--mut);font-size:13px}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
td,th{padding:6px 4px;text-align:right;border-bottom:1px solid var(--line);font-size:14px}
td:first-child,th:first-child{text-align:left}
th{color:var(--mut);font-weight:500;font-size:12px}
canvas{width:100%;height:170px;display:block;background:#0d1117;border:1px solid var(--line);border-radius:10px}
.leg{display:flex;gap:12px;font-size:12px;color:var(--mut);margin-top:6px;flex-wrap:wrap}
.leg b{display:inline-block;width:10px;height:3px;vertical-align:middle;margin-right:4px}
.prog{height:10px;background:#0d1117;border:1px solid var(--line);border-radius:99px;overflow:hidden;margin:8px 0}
.prog i{display:block;height:100%;background:var(--ok);width:0}
#toast{position:fixed;left:50%;bottom:80px;transform:translateX(-50%);background:#21262d;border:1px solid var(--line);padding:10px 16px;border-radius:10px;display:none;z-index:9;max-width:90%}
</style></head><body>
<header>
  <span class="dot" id="dot"></span><h1>Boîtier E85</h1>
  <span class="pill" id="pSave">–</span>
  <span class="pill" id="pPed">Pédale ✕</span>
</header>
<main>
<div class="info" id="appUpd" style="display:none"></div>
<div class="warn err" id="offline" style="display:none">Pas de connexion au boîtier.<br>
  <span id="offmsg">Connecte le téléphone au Wi-Fi <b>BoitierE85</b>.</span><br><br>
  <button class="pri" id="btnConn" onclick="reconnect()" style="display:none">Se connecter au boîtier</button>
</div>
<div class="info" id="fwUpd" style="display:none"></div>

<!-- ===================== MOTEUR ===================== -->
<section class="tab on" id="t0">
  <div class="card">
    <div class="big" id="rpm">0</div><div class="unit">tr/min <span id="eng"></span></div>
  </div>
  <div class="card">
    <h2>Prolongement injection</h2>
    <div class="row"><label>Actif</label><label class="sw"><input type="checkbox" id="en" data-k="en"><i></i></label></div>
    <div class="row"><label>Essence en plus</label><span class="val" id="pctV">0 %</span></div>
    <input type="range" id="pct" min="0" max="100" step="0.5" data-k="pct" data-v="pctV" data-u=" %">
    <div class="grid" style="margin-top:8px">
      <button onclick="bump(-1)">−1 %</button><button onclick="bump(1)">+1 %</button><button onclick="bump(5)">+5 %</button>
    </div>
  </div>
  <div class="card">
    <h2>Cylindres</h2>
    <table><thead><tr><th></th><th>ECU</th><th>Ajout</th><th>Total</th><th>Duty</th></tr></thead>
      <tbody id="cylT"></tbody></table>
    <p class="muted" style="margin:8px 0 0">Durées en millisecondes.</p>
  </div>
  <div class="card">
    <h2>Pédale</h2>
    <div class="row"><label>Conducteur</label><span class="val" id="ppV">–</span></div>
    <div class="bar"><i id="ppB" style="width:0"></i></div>
    <div class="row"><label>Envoyé au calculateur</label><span class="val" id="opV">–</span></div>
    <div class="bar"><i class="a" id="opB" style="width:0"></i></div>
    <div class="row"><label>Mode</label><span class="val" id="modeV">–</span></div>
  </div>
</section>

<!-- ===================== INJECTION ===================== -->
<section class="tab" id="t1">
  <div class="card">
    <h2>Correction par cylindre</h2>
    <p class="muted">S'ajoute au pourcentage global (ex. −2 % si un cylindre est plus riche).</p>
    <div id="trims"></div>
  </div>
  <div class="card">
    <h2>Calcul</h2>
    <div class="row"><label>Temps mort injecteur (µs)<small>Partie de l'impulsion qui n'injecte pas (ouverture). 0 = % de la durée totale. Typique : 700–1000.</small></label><input type="number" id="dead" min="0" max="3000" step="10" data-k="dead"></div>
    <div class="row"><label>Prolongement maxi (µs)</label><input type="number" id="maxext" min="500" max="30000" step="100" data-k="maxext"></div>
    <div class="row"><label>Rapport cyclique maxi (%)<small>Sécurité : l'injecteur n'est jamais ouvert plus que ça.</small></label><input type="number" id="maxduty" min="30" max="98" data-k="maxduty"></div>
    <div class="row"><label>Injection<small>Pour le calcul du régime</small></label>
      <select id="ipc" data-k="ipc"><option value="1">Séquentielle</option><option value="2">Semi-séquentielle</option></select></div>
  </div>
  <div class="card">
    <h2>Entrées</h2>
    <div class="row"><label>Filtre anti-parasite (µs)</label><input type="number" id="filt" min="0" max="20" step="0.5" data-k="filt"></div>
    <div class="row"><label>Inverser les entrées<small>Seulement si ton PC817 donne 0 V pendant l'injection.</small></label><label class="sw"><input type="checkbox" id="inv" data-k="inv"><i></i></label></div>
  </div>
</section>

<!-- ===================== PEDALE ===================== -->
<section class="tab" id="t2">
  <div class="warn err" id="pedOff">Boîtier pédale non détecté (ESP-NOW).</div>
  <div class="card">
    <h2>Régime et aide en direct</h2>
    <canvas id="chart" width="700" height="340"></canvas>
    <div class="leg"><span><b style="background:#e6edf3"></b>Régime</span><span><b style="background:#3fb950"></b>Cible ± tolérance</span><span><b style="background:#f0b429"></b>Aide %</span><span><b style="background:#58a6ff"></b>Pédale %</span></div>
    <div class="grid" style="margin-top:10px">
      <div class="muted">Aide<br><b class="val" id="apV">–</b></div>
      <div class="muted">Apprise<br><b class="val" id="lrnV">–</b></div>
      <div class="muted">Variation<br><b class="val" id="rateV">–</b></div>
    </div>
  </div>
  <div class="card">
    <h2>Aide au ralenti</h2>
    <div class="row"><label>Active<small>Ouvre l'accélérateur pied levé pour tenir le régime.</small></label><label class="sw"><input type="checkbox" id="aen" data-p="aen"><i></i></label></div>
    <h3>Régimes</h3>
    <div class="row"><label>Régime visé<small>Le régime à tenir pied levé.</small></label><input type="number" id="tgt" min="400" max="4000" step="25" data-p="tgt"></div>
    <div class="row"><label>Tolérance ±<small>Dans cette plage, l'ouverture apprise ne bouge plus (pas de pompage).</small></label><input type="number" id="tol" min="0" max="500" step="10" data-p="tol"></div>
    <div class="row"><label>S'enclenche sous<small>Pied levé, l'aide prend la main sous ce régime (ou avant, grâce à l'anticipation).</small></label><input type="number" id="eng2" min="500" max="5000" step="50" data-p="eng"></div>
    <div class="row"><label>Se coupe au-dessus<small>Au-delà, l'aide se retire (frein moteur, coupure d'injection en décélération).</small></label><input type="number" id="off" min="800" max="7000" step="50" data-p="off"></div>
    <h3>Seuil critique</h3>
    <div class="row"><label>Régime critique<small>Sous ce régime, l'ouverture est forcée <b>immédiatement</b> (pied levé ou posé). 0 = désactivé.</small></label><input type="number" id="crit" min="0" max="3000" step="50" data-p="crit"></div>
    <div class="row"><label>Ouverture forcée (%)<small>Ouverture minimale appliquée d'un coup sous le régime critique.</small></label><input type="number" id="critp" min="0" max="40" step="0.5" data-p="critp"></div>
    <h3>Ouverture</h3>
    <div class="row"><label>Plancher mini (%)<small>Ouverture minimale tant que l'aide est enclenchée : elle ne retombe jamais en dessous.</small></label><input type="number" id="min" min="0" max="40" step="0.5" data-p="min"></div>
    <div class="row"><label>Maxi (%)</label><input type="number" id="max" min="0" max="60" step="0.5" data-p="max"></div>
    <h3>Régulation</h3>
    <div class="row"><label>P (réaction)<small>% d'ouverture par 100 tr/min d'écart. Commence à 1–2.</small></label><input type="number" id="kp" min="0" max="100" step="0.1" data-p="kp"></div>
    <div class="row"><label>I (apprentissage)<small>Vitesse à laquelle l'ouverture « apprise » se corrige. Elle est gardée en mémoire entre deux lâchers de pied.</small></label><input type="number" id="ki" min="0" max="100" step="0.1" data-p="ki"></div>
    <div class="row"><label>D (anticipation)<small>% ajouté par 100 tr/min/s de chute du régime. Freine aussi les remontées trop rapides.</small></label><input type="number" id="kd" min="0" max="100" step="0.1" data-p="kd"></div>
    <div class="row"><label>Horizon d'anticipation (ms)<small>L'aide s'enclenche si le régime <b>sera</b> sous le seuil dans ce délai, vu sa vitesse de chute.</small></label><input type="number" id="ant" min="0" max="2000" step="50" data-p="ant"></div>
    <div class="row"><label>Seuil pédale relâchée (%)</label><input type="number" id="rel" min="0.5" max="20" step="0.5" data-p="rel"></div>
  </div>
  <div class="card">
    <h2>Tensions</h2>
    <table><thead><tr><th></th><th>Piste 1</th><th>Piste 2</th></tr></thead><tbody>
      <tr><td>Pédale</td><td id="in1">–</td><td id="in2">–</td></tr>
      <tr><td>Vers ECU</td><td id="o1">–</td><td id="o2">–</td></tr>
      <tr><td>Relâchée (cal.)</td><td id="c1">–</td><td id="c2">–</td></tr>
      <tr><td>À fond (cal.)</td><td id="c3">–</td><td id="c4">–</td></tr>
    </tbody></table>
  </div>
  <div class="card">
    <h2>Calibration</h2>
    <p class="muted">Contact mis, moteur arrêté. Pied levé → bouton 1. Pédale au plancher → bouton 2. C'est enregistré automatiquement.</p>
    <div class="grid2"><button onclick="pcmd(1)">1. Pédale relâchée</button><button onclick="pcmd(2)">2. Pédale à fond</button></div>
    <p id="calSt" class="muted"></p>
  </div>
</section>

<!-- ===================== SYSTEME ===================== -->
<section class="tab" id="t3">
  <div class="card">
    <h2>Mises à jour</h2>
    <table><tbody>
      <tr><td>Application</td><td id="vApp">–</td></tr>
      <tr><td>Boîtier E85</td><td id="vE85">–</td></tr>
      <tr><td>Boîtier pédale</td><td id="vPed">–</td></tr>
    </tbody></table>
    <div id="updBox"></div>
    <p class="muted">L'appli contient le firmware E85, qui contient lui-même celui de la pédale : mettre à jour le boîtier E85 suffit, il met ensuite la pédale à jour tout seul (moteur arrêté).</p>
  </div>
  <div class="card">
    <h2>Sauvegarde</h2>
    <p class="muted">Chaque réglage est enregistré automatiquement. Moteur tournant, les réglages E85 sont gardés dans le boîtier pédale, puis recopiés dans le boîtier E85 à l'arrêt du moteur (une écriture en mémoire bloquerait l'injection).</p>
    <p id="saveSt"></p>
  </div>
  <div class="card">
    <h2>Simulateur (banc d'essai)</h2>
    <div class="warn">Les sorties injecteurs sont commandées pendant la simulation : à utiliser sur établi seulement. Refusé si le moteur tourne.</div>
    <div class="row"><label>Simulation ECU</label><label class="sw"><input type="checkbox" id="sim" data-k="sim"><i></i></label></div>
    <div class="row"><label>Régime simulé</label><input type="number" id="simrpm" min="300" max="8000" step="50" data-k="simrpm"></div>
    <div class="row"><label>Impulsion simulée (µs)</label><input type="number" id="simpw" min="300" max="30000" step="100" data-k="simpw"></div>
  </div>
  <div class="card">
    <h2>Wi-Fi du boîtier</h2>
    <div class="row"><label>Nom</label><input type="text" id="ssid" value="BoitierE85"></div>
    <div class="row"><label>Mot de passe</label><input type="text" id="pass"></div>
    <div class="grid2"><button onclick="saveWifiLocal()">Utiliser dans l'appli</button><button onclick="changeBoxPass()">Changer sur le boîtier</button></div>
    <p class="muted">« Changer sur le boîtier » enregistre le nouveau mot de passe dans le boîtier E85 (moteur arrêté) puis reconnecte l'appli.</p>
  </div>
  <div class="card">
    <h2>Infos</h2>
    <div id="info" class="muted"></div>
    <div class="grid2" style="margin-top:12px"><button class="dng" onclick="if(confirm('Remettre les réglages E85 par défaut ?'))api('/api/defaults')">E85 par défaut</button>
    <button class="dng" onclick="if(confirm('Remettre la pédale par défaut (calibration d\'origine) ?'))pcmd(4)">Pédale par défaut</button></div>
  </div>
</section>
</main>
<nav>
  <button class="on" onclick="tab(0)">Moteur</button><button onclick="tab(1)">Injection</button>
  <button onclick="tab(2)">Pédale</button><button onclick="tab(3)">Système</button>
</nav>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
const APP=!!window.Android;
const BASE=location.protocol.startsWith('http')?'':'http://192.168.4.1';
let S=null,fails=0,busy=false,lastEdit=0,curTab=0,uploading=false;
let FW=null;   // firmware E85 embarque dans l'appli {version,build}
const MODES=['Bypass','Recopie','Aide active','Défaut (pédale relâchée)'];
const fmt=(v,d=0)=>Number(v).toFixed(d).replace('.',',');
function toast(t){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',2500)}
function tab(i){curTab=i;document.querySelectorAll('.tab').forEach((t,k)=>t.classList.toggle('on',k==i));document.querySelectorAll('nav button').forEach((b,k)=>b.classList.toggle('on',k==i))}

// ---------- requetes : une reponse partie AVANT une modif de l'utilisateur n'ecrase pas les champs ----------
async function api(path,q){
  const t0=Date.now();
  const url=BASE+path+(q?'?'+new URLSearchParams(q):'');
  const ctl=new AbortController();const to=setTimeout(()=>ctl.abort(),2000);
  try{const r=await fetch(url,{signal:ctl.signal,cache:'no-store'});clearTimeout(to);
    const j=await r.json();if(j.err)toast(j.err);else render(j,t0);return j}
  catch(e){clearTimeout(to);throw e}
}
function touched(){lastEdit=Date.now()}
function set(q){touched();api('/api/set',q).catch(()=>toast('Envoi impossible'))}
function ped(q){touched();api('/api/ped',q).catch(()=>toast('Envoi impossible'))}
function pcmd(c){api('/api/pedcmd',{c}).then(()=>toast('Commande envoyée')).catch(()=>toast('Envoi impossible'))}
function bump(d){if(!S)return;const v=Math.max(0,Math.min(100,S.pct+d));$('pct').value=v;$('pctV').textContent=fmt(v,1)+' %';set({pct:v})}

// liaison des champs : data-k = reglage E85, data-p = reglage pedale
function bind(){
  document.querySelectorAll('[data-k],[data-p]').forEach(e=>{
    const send=()=>{const v=e.type==='checkbox'?(e.checked?1:0):e.value;const o={};
      if(e.dataset.k)o[e.dataset.k]=v,set(o);else o[e.dataset.p]=v,ped(o)};
    ['pointerdown','touchstart','focus','input'].forEach(ev=>e.addEventListener(ev,touched,{passive:true}));
    if(e.type==='range'){
      e.addEventListener('input',()=>{touched();if(e.dataset.v)$(e.dataset.v).textContent=(e.dataset.sgn&&e.value>0?'+':'')+fmt(e.value,1)+(e.dataset.u||'')});
      e.addEventListener('change',send);
    }else e.addEventListener('change',send);
  });
}
function setIn(id,v,t0){const e=$(id);if(!e||document.activeElement===e||t0<lastEdit+400||Date.now()-lastEdit<1200)return;
  if(e.type==='checkbox')e.checked=!!v;else if(String(e.value)!==String(v))e.value=v;
  if(e.dataset&&e.dataset.v)$(e.dataset.v).textContent=(e.dataset.sgn&&v>0?'+':'')+fmt(v,1)+(e.dataset.u||'')}

// ---------- courbe ----------
const H=[];
function pushHist(j){const p=j.ped||{};H.push({r:j.rpm,a:p.ap||0,pp:p.pp||0,t:p.cfg?p.cfg.tgt:0,tol:p.cfg?p.cfg.tol:0,act:p.flags&16});if(H.length>150)H.shift()}
function draw(){
  const c=$('chart');if(!c||curTab!==2)return;const g=c.getContext('2d');const W=c.width,Hh=c.height;g.clearRect(0,0,W,Hh);
  const maxR=Math.max(2000,...H.map(h=>h.r))*1.1,maxA=Math.max(20,...H.map(h=>h.a))*1.2;
  const x=i=>i*(W/149),yR=v=>Hh-10-(v/maxR)*(Hh-20),yA=v=>Hh-10-(v/maxA)*(Hh-20);
  g.font='20px system-ui';g.fillStyle='#8b949e';
  [1000,2000,3000,4000,5000,6000].filter(v=>v<maxR).forEach(v=>{g.strokeStyle='#21262d';g.beginPath();g.moveTo(0,yR(v));g.lineTo(W,yR(v));g.stroke();g.fillText(v,4,yR(v)-4)});
  const last=H[H.length-1];
  if(last&&last.t){g.fillStyle='#3fb95033';g.fillRect(0,yR(last.t+last.tol),W,yR(last.t-last.tol)-yR(last.t+last.tol));
    g.strokeStyle='#3fb950';g.setLineDash([8,6]);g.beginPath();g.moveTo(0,yR(last.t));g.lineTo(W,yR(last.t));g.stroke();g.setLineDash([])}
  H.forEach((h,i)=>{if(h.act){g.fillStyle='#f0b42918';g.fillRect(x(i),0,W/149+1,Hh)}});
  const line=(f,col,w)=>{g.strokeStyle=col;g.lineWidth=w;g.beginPath();H.forEach((h,i)=>{i?g.lineTo(x(i),f(h)):g.moveTo(x(i),f(h))});g.stroke();g.lineWidth=1};
  line(h=>yA(h.pp),'#58a6ff',2);line(h=>yA(h.a),'#f0b429',3);line(h=>yR(h.r),'#e6edf3',3);
  g.fillStyle='#f0b429';g.fillText('aide 0–'+Math.round(maxA)+' %',W-170,24);
}

// ---------- affichage ----------
function render(j,t0=0){
  S=j;fails=0;$('offline').style.display='none';$('dot').classList.add('ok');
  $('rpm').textContent=j.rpm;$('eng').textContent=j.sim?'· SIMULATION':(j.on?'· moteur tournant':'· moteur arrêté');
  setIn('en',j.en,t0);setIn('pct',j.pct,t0);
  ['dead','maxext','maxduty','filt','ipc','simrpm','simpw'].forEach(k=>setIn(k,j[k],t0));setIn('inv',j.inv,t0);setIn('sim',j.sim,t0);
  let h='';j.cyl.forEach((c,i)=>{h+=`<tr><td>Cyl ${i+1}</td><td>${fmt(c.pw/1000,2)}</td><td>${fmt(c.ext/1000,2)}</td><td>${fmt((c.pw+c.ext)/1000,2)}</td><td>${fmt(c.duty,0)} %</td></tr>`});
  $('cylT').innerHTML=h;
  if(!$('trims').children.length){$('trims').innerHTML=[0,1,2].map(i=>`<div class="row"><label>Cylindre ${i+1}</label><span class="val" id="trV${i}"></span></div><input type="range" id="tr${i}" min="-30" max="30" step="0.5" data-k="t${i+1}" data-v="trV${i}" data-u=" %" data-sgn="1">`).join('');bind2('trims')}
  j.trim.forEach((t,i)=>setIn('tr'+i,t,t0));
  const SV=[['Enregistré ✓','var(--ok)','Tous les réglages sont enregistrés dans le boîtier E85.'],
            ['Enregistré ✓','var(--ok)','Réglages enregistrés dans le boîtier pédale. Recopie dans le boîtier E85 à l\'arrêt du moteur.'],
            ['Enregistrement…','var(--acc)','En attente : boîtier pédale absent. Enregistré dans le boîtier E85 dès l\'arrêt du moteur.']];
  const sv=SV[j.sv]||SV[2];$('pSave').textContent=sv[0];$('pSave').style.color=sv[1];$('saveSt').textContent=sv[2];
  const p=j.ped||{};const L=p.link;
  $('pPed').textContent=L?'Pédale ✓':'Pédale ✕';$('pPed').style.color=L?'var(--ok)':'var(--bad)';
  $('pedOff').style.display=L?'none':'';
  $('rateV').textContent=(j.rate>0?'+':'')+j.rate+' tr/min/s';
  if(p.in1!==undefined){
    const mv=v=>fmt(v/1000,3)+' V';
    $('in1').textContent=mv(p.in1);$('in2').textContent=mv(p.in2);$('o1').textContent=mv(p.out1);$('o2').textContent=mv(p.out2);
    $('c1').textContent=mv(p.cal[0]);$('c2').textContent=mv(p.cal[1]);$('c3').textContent=mv(p.cal[2]);$('c4').textContent=mv(p.cal[3]);
    $('ppV').textContent=fmt(p.pp,1)+' %';$('ppB').style.width=p.pp+'%';
    $('opV').textContent=fmt(p.op,1)+' %';$('opB').style.width=p.op+'%';
    $('modeV').textContent=L?((p.flags&32)?'⚠ Seuil critique':(MODES[p.mode]||'?')):'Hors ligne';$('apV').textContent=fmt(p.ap,1)+' %';$('lrnV').textContent=fmt(p.lrn,1)+' %';
    $('calSt').textContent=(p.flags&1)?'✓ Pédale calibrée':'⚠ Pédale non calibrée : aide au ralenti impossible';
    const c=p.cfg;['aen','tgt','tol','off','min','max','kp','ki','kd','ant','rel','crit','critp'].forEach(k=>setIn(k,c[k],t0));setIn('eng2',c.eng,t0);
  }
  pushHist(j);draw();
  // versions
  $('vE85').textContent='v'+j.fw+' (build '+j.build+')';
  $('vPed').textContent=p.build?('build '+p.build+(p.build==j.build?' ✓':' → mise à jour '+j.build)):'–';
  updUI(j);
  const RST=['?','mise sous tension','ext','logiciel','PLANTAGE','watchdog int','watchdog tâche','watchdog','veille','BROWNOUT (alim)','SDIO'];
  $('info').innerHTML=`En marche depuis ${Math.floor(j.up/60)} min · dernier redémarrage : ${RST[j.rst]||j.rst}<br>Mémoire libre : ${Math.round(j.heap/1024)} Ko · révision réglages : ${j.rev}`;
}
function bind2(id){document.querySelectorAll('#'+id+' [data-k]').forEach(e=>{
  ['pointerdown','touchstart','focus','input'].forEach(ev=>e.addEventListener(ev,touched,{passive:true}));
  e.addEventListener('input',()=>{touched();$(e.dataset.v).textContent=(e.value>0?'+':'')+fmt(e.value,1)+' %'});
  e.addEventListener('change',()=>{const o={};o[e.dataset.k]=e.value;set(o)})})}

// ---------- mises a jour du firmware ----------
function updUI(j){
  const b=$('updBox'),banner=$('fwUpd');let h='',bn='';
  const po=j.pota||{};
  if(po.st==1){const pc=po.size?Math.round(100*po.done/po.size):0;
    h=`<p>Mise à jour du boîtier pédale : ${pc} %</p><div class="prog"><i style="width:${pc}%"></i></div>`;bn='Mise à jour du boîtier pédale en cours : '+pc+' %'}
  else if(po.st==2)h='<p style="color:var(--ok)">✓ Boîtier pédale mis à jour.</p>';
  else if(po.st==3)h=`<p style="color:var(--bad)">Échec de la mise à jour pédale (étape ${po.err}).</p><button onclick="api('/api/pedota')">Réessayer</button>`;
  else if(j.ped&&j.ped.build&&j.ped.build!=j.build&&j.ped.link)h=`<button onclick="api('/api/pedota')">Mettre à jour la pédale maintenant</button>`;
  if(FW&&!uploading){
    if(FW.build>j.build){bn=bn||`Nouvelle version du boîtier E85 disponible : v${FW.version}`;
      h=`<button class="pri" style="width:100%" onclick="flashE85()">Mettre à jour le boîtier E85 → v${FW.version}</button>`+h}
    else if(FW.build<j.build)h+=`<p class="muted">Le boîtier a une version plus récente que l'appli : mets l'appli à jour.</p>`;
  }
  if(uploading)return;
  if(b.innerHTML!==h)b.innerHTML=h;
  banner.style.display=bn?'':'none';if(bn)banner.textContent=bn;
}
function loadFw(){const x=new XMLHttpRequest();x.open('GET','firmware.json');x.onload=()=>{try{FW=JSON.parse(x.responseText)}catch(e){}};x.send()}
function flashE85(){
  if(!S)return;if(S.on){toast('Arrête le moteur avant la mise à jour');return}
  if(!confirm('Mettre à jour le boîtier E85 en v'+FW.version+' ?\nNe coupe pas le contact pendant 1 minute.'))return;
  uploading=true;const b=$('updBox');b.innerHTML='<p>Lecture du firmware…</p><div class="prog"><i id="upP"></i></div>';
  const x=new XMLHttpRequest();x.open('GET','e85.bin');x.responseType='arraybuffer';
  x.onload=()=>{const fd=new FormData();fd.append('fw',new Blob([x.response]),'e85.bin');
    const u=new XMLHttpRequest();u.open('POST',BASE+'/api/ota');u.timeout=180000;
    u.upload.onprogress=e=>{if(e.lengthComputable){const pc=Math.round(100*e.loaded/e.total);$('upP').style.width=pc+'%';b.firstChild.textContent='Envoi au boîtier : '+pc+' %'}};
    u.onload=()=>{let j={};try{j=JSON.parse(u.responseText)}catch(e){}
      if(u.status==200){b.innerHTML='<p style="color:var(--ok)">✓ Boîtier E85 mis à jour, redémarrage… La pédale sera mise à jour juste après.</p>';setTimeout(()=>{uploading=false},15000)}
      else{b.innerHTML='<p style="color:var(--bad)">Échec : '+(j.err||u.status)+'</p>';uploading=false}};
    u.onerror=u.ontimeout=()=>{b.innerHTML='<p style="color:var(--bad)">Échec de l\'envoi. Réessaie près du boîtier.</p>';uploading=false};
    u.send(fd)};
  x.onerror=()=>{b.innerHTML='<p style="color:var(--bad)">Firmware introuvable dans l\'appli.</p>';uploading=false};
  x.send();
}

// ---------- appli (Android) ----------
function appUpdate(){if(!APP)return;let i={};try{i=JSON.parse(Android.updateInfo())}catch(e){}
  $('vApp').textContent='v'+(i.current||'?')+(i.latest&&i.newer?' → v'+i.latest+' disponible':(i.checked?' ✓ à jour':''));
  if(i.newer){$('appUpd').style.display='';$('appUpd').innerHTML=`Nouvelle version de l'appli : <b>v${i.latest}</b><br><br><button class="pri" onclick="Android.installUpdate();toast('Téléchargement…')">Installer</button>`}
  else $('appUpd').style.display='none';
  if(i.error)$('vApp').textContent+=' ('+i.error+')';
}
function reconnect(){if(APP){Android.connect(lsGet('ssid','BoitierE85'),lsGet('pass','ethanol123'));toast('Connexion…')}}
function lsGet(k,d){try{return localStorage[k]||d}catch(e){return d}}
function saveWifiLocal(){try{localStorage.ssid=$('ssid').value;localStorage.pass=$('pass').value}catch(e){}reconnect()}
function changeBoxPass(){const p=$('pass').value;if(p.length<8){toast('8 caractères minimum');return}
  if(!confirm('Changer le mot de passe du boîtier en « '+p+' » ?'))return;
  api('/api/wifi',{pass:p}).then(j=>{if(j.ok){try{localStorage.pass=p}catch(e){}toast('Mot de passe changé, reconnexion…');setTimeout(reconnect,1500)}}).catch(()=>toast('Envoi impossible'))}

async function poll(){
  if(!busy&&!uploading){busy=true;
    try{await api('/api/status')}catch(e){
      if(++fails>=3){$('offline').style.display='';$('dot').classList.remove('ok');
        if(APP){$('btnConn').style.display='';$('offmsg').textContent=Android.netState()}}
    }busy=false}
  setTimeout(poll,curTab===2?150:(S&&S.on?200:400));
}
bind();
$('ssid').value=lsGet('ssid','BoitierE85');$('pass').value=lsGet('pass','ethanol123');
if(APP){loadFw();reconnect();setInterval(appUpdate,2000);appUpdate()}else{$('vApp').textContent='navigateur'}
poll();
</script>
</body></html>
)WEBUI";
