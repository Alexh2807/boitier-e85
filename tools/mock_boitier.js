// Faux boitier pour tester l'interface sur PC :  node tools/mock_boitier.js  puis http://localhost:8085
// Simule un moteur avec l'aide au ralenti (pour voir la courbe), avec une latence reseau aleatoire.
const http = require('http'), fs = require('fs'), path = require('path'), url = require('url');
const html = path.join(__dirname, '../android/app/src/main/assets/index.html');
const S = { fw: '2.1.0', build: 210, rpm: 0, rate: 0, on: 1, en: 1, pct: 25, trim: [0, 0, 0], dead: 0, maxext: 15000, maxduty: 90, filt: 3,
  inv: 0, ipc: 1, sim: 0, simrpm: 900, simpw: 3000, sv: 1, rev: 12, up: 0, rst: 1, heap: 210000, cyl: [],
  ped: { link: 1, build: 210, age: 20, in1: 396, in2: 186, out1: 396, out2: 186, vref: 5000, pp: 0, op: 0, ap: 0, lrn: 2, mode: 1, flags: 11,
    cal: [396, 186, 3793, 1888], cfg: { aen: 1, tgt: 1500, tol: 50, eng: 1800, off: 2500, min: 5, max: 15, kp: 1, ki: 1, kd: 0.5, ant: 300, rel: 3 }, pend: 0 },
  pota: { blind: 0, st: 0, done: 0, size: 960000, err: 0 },
  e85u: { st: 2, done: 0, size: 1955000, err: '', psram: 8192 },
  etx: { ok: 1200, fail: 3, direct: 1 }, rx: { n: 5000, age: 40, len: 92, type: 10, ver: 2, magic: 59482, exp: 92 },
  diag: { cnt: [0, 7, 0, 1, 0, 0, 1, 0, 0, 2, 0, 0], last: { r: 6, up: 732, stage: 1, heap: 181000, cl: 1, rx: 5000, txf: 2, pst: 1, pdone: 300000, rpm: 0, hang: 0 } } };
Object.assign(S.ped, { txok: 4200, txfail: 0, prst: 1, direct: 1, pup: 3600 });
const t0 = Date.now(); let rpm = 3000, prev = 3000, ap = 0, lrn = 2, act = false;
setInterval(() => {                          // petit modele moteur 50 Hz
  const c = S.ped.cfg, t = (Date.now() - t0) / 1000;
  const foot = (t % 20) < 4 ? 30 : 0;        // coup d'accelerateur toutes les 20 s
  const pred = rpm + (rpm - prev) * 50 * c.ant / 1000;
  if (foot) act = false; else if (!act && rpm < c.off && (rpm < c.eng || pred < c.eng)) { act = true; ap = Math.max(ap, c.min + lrn); }
  let tgt = 0;
  if (act) { const err = (c.tgt - rpm) / 100, rate = (rpm - prev) * 50;
    if (Math.abs(c.tgt - rpm) > c.tol) lrn = Math.max(0, Math.min(c.max - c.min, lrn + c.ki * err * 0.02));
    tgt = Math.max(c.min, Math.min(c.max, c.min + lrn + c.kp * err - c.kd * rate / 100)); }
  ap = tgt > ap ? tgt : Math.max(tgt, ap - 0.8);
  const thr = Math.max(foot, ap); prev = rpm;
  rpm += ((650 + thr * 120) - rpm) * 0.04;
  Object.assign(S.ped, { pp: foot, ap: +ap.toFixed(1), op: thr, lrn: +lrn.toFixed(1), mode: ap > foot ? 2 : 1, flags: 11 | (act ? 16 : 0) });
  S.rpm = Math.round(rpm); S.rate = Math.round((rpm - prev) * 50);
}, 20);
http.createServer((req, res) => {
  const u = url.parse(req.url, true);
  if (u.pathname === '/') { res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); return res.end(fs.readFileSync(html)); }
  const q = u.query;
  if (u.pathname === '/api/set') { for (const k in q) { if (/^t[123]$/.test(k)) S.trim[+k[1] - 1] = +q[k]; else S[k] = +q[k]; } S.rev++; }
  if (u.pathname === '/api/ped') { for (const k in q) S.ped.cfg[k] = +q[k]; }
  const t = (Date.now() - t0) / 1000;
  S.up = Math.floor(t);
  const per = 120e6 / Math.max(S.rpm, 300), pw = 2800;
  S.cyl = [0, 1, 2].map(i => { const ext = Math.round(pw * (S.pct + S.trim[i]) / 100);
    return { pw, ext, per: Math.round(per), duty: 100 * (pw + ext) / per, n: Math.floor(t * 10) }; });
  const body = JSON.stringify(S);
  setTimeout(() => { res.writeHead(200, { 'Content-Type': 'application/json', 'Access-Control-Allow-Origin': '*' }); res.end(body); },
    u.pathname === '/api/status' ? 100 + Math.random() * 400 : 50);   // reponses lentes : teste le bug des curseurs
}).listen(8085, () => console.log('Faux boitier sur http://localhost:8085'));
