#pragma once
// The setup portal's phone page (design board V of the private design canvas): one self-contained HTML file, no
// external resources (the phone has no internet while it is on the display's hotspot). Served by setup_portal.cpp
// at "/". Every value from the device is put in with textContent / value, never as HTML.
// JSON API: setup_portal.cpp (GET /api/state, /api/scan, /api/zones, /api/geocode?q=; POST /api/wifi,
// /api/discover, /api/save).

namespace setuppage {

constexpr const char HTML[] = R"PAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Powerwall display setup</title>
<style>
:root{--bg:#0B0F14;--card:#151B23;--hi:#1F2731;--line:#2A3440;--text:#E6EDF3;--muted:#8B98A5;--acc:#3DB2FF;--ok:#3DDC84;--warn:#F5B700;--err:#FF6B6B}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--text);font:16px/1.4 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:480px;margin:0 auto;padding:20px 16px 32px;display:flex;flex-direction:column;gap:16px}
.cap{font-size:12px;letter-spacing:1.5px;color:var(--muted)}
h1{margin:4px 0 0;font-size:24px;font-weight:600}
h2{margin:0;font-size:13px;letter-spacing:1.2px;color:var(--muted);font-weight:600}
p{margin:0}
.intro{margin-top:6px;font-size:14px;color:var(--muted)}
section{background:var(--card);border-radius:14px;padding:14px;display:flex;flex-direction:column;gap:10px}
label{font-size:13px;color:var(--muted)}
input,select{width:100%;height:44px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--text);padding:0 10px;font:inherit;font-size:16px}
input:focus,select:focus,button:focus-visible{outline:2px solid var(--acc);outline-offset:1px}
.row{display:flex;gap:8px}
.row input,.row select{flex:1;min-width:0}
button{font:inherit;cursor:pointer;border:0;border-radius:10px}
.btn2{height:44px;padding:0 14px;background:var(--hi);color:var(--acc);font-size:14px;font-weight:600;flex:none}
.btn2:disabled,.main:disabled{opacity:.5;cursor:default}
.main{height:52px;border-radius:12px;background:var(--acc);color:var(--bg);font-size:16px;font-weight:700}
.note{font-size:12px;color:var(--muted)}
.msg{font-size:13px;color:var(--muted);min-height:1em}
.ok{color:var(--ok)}.warn{color:var(--warn)}.err{color:var(--err)}
.chk{display:flex;align-items:center;gap:10px;font-size:14px;color:var(--text)}
.chk input{width:20px;height:20px;flex:none}
.list{display:flex;flex-direction:column;gap:6px}
.pick{display:flex;align-items:center;gap:10px;padding:10px;border:1px solid var(--line);border-radius:10px;font-size:14px;color:var(--text)}
.pick input{width:20px;height:20px;flex:none}
.chip{height:36px;padding:0 12px;background:var(--hi);color:var(--acc);font-size:14px;font-weight:600;align-self:flex-start}
details summary{cursor:pointer;font-size:13px;color:var(--acc)}
details .list{margin-top:10px}
.steps{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:8px}
.steps li{display:flex;gap:10px;align-items:center;font-size:14px}
.mark{width:34px;height:22px;flex:none;border-radius:11px;background:var(--line);color:var(--bg);font-size:12px;font-weight:700;display:flex;align-items:center;justify-content:center}
.m-ok{background:var(--ok)}.m-run{background:var(--warn)}.m-fail{background:var(--err)}
.sn{margin-left:auto;font-size:12px;color:var(--muted);text-align:right}
#lost{display:none;background:#2A1215;color:var(--err);border-radius:10px;padding:10px;font-size:14px}
</style>
</head>
<body>
<main>
<div>
<div class="cap">POWERWALL DISPLAY</div>
<h1>Set up your display</h1>
<p class="intro">Everything stays on the display. Your passwords are stored on the device only and never sent anywhere except your own Wi-Fi and Powerwall.</p>
</div>
<div id="lost" role="alert">Lost contact with the display. If your phone left the <b id="apName">setup</b> Wi-Fi, join it again; this page carries on by itself.</div>

<section aria-labelledby="h-wifi">
<h2 id="h-wifi">1&nbsp; HOME WI-FI (2.4 GHz)</h2>
<label for="ssid">Network</label>
<div class="row">
<select id="ssid"><option value="">Scanning...</option></select>
<button class="btn2" id="rescan" type="button">Rescan</button>
</div>
<div id="otherWrap" hidden><label for="ssidOther">Network name</label><input id="ssidOther" autocapitalize="off" autocomplete="off" spellcheck="false" maxlength="32"></div>
<label for="wpass">Password</label>
<input id="wpass" type="password" autocomplete="off" maxlength="64">
<label class="chk"><input type="checkbox" id="showw">Show password</label>
<button class="btn2" id="join" type="button">Connect</button>
<p class="msg" id="wifiMsg" role="status" aria-live="polite"></p>
</section>

<section aria-labelledby="h-gw">
<h2 id="h-gw">2&nbsp; POWERWALL 3 GATEWAY</h2>
<label for="gw">Gateway address</label>
<div class="row">
<input id="gw" autocapitalize="off" autocomplete="off" spellcheck="false" maxlength="63" placeholder="192.168.1.50">
<button class="btn2" id="find" type="button">Find it</button>
</div>
<p class="msg" id="findMsg" role="status" aria-live="polite"></p>
<div class="list" id="found"></div>
<p class="note">Find it searches your home network for the gateway. You can also look it up in your router's list of connected devices; a DHCP reservation there keeps the address from changing.</p>
<label for="pwpass">Gateway customer password</label>
<input id="pwpass" type="password" autocomplete="off" maxlength="32">
<p class="note">The last 5 characters of the gateway password printed on the QR sticker (on the gateway or in its paperwork). The display signs in as "customer" with it, once per test. Repeated wrong passwords may lock you out for a while, so check it before you press Test and save.</p>
</section>

<section aria-labelledby="h-loc">
<h2 id="h-loc">3&nbsp; LOCATION (WEATHER, SUNRISE, MOON)</h2>
<label for="town">Town or postcode</label>
<div class="row">
<input id="town" autocomplete="off" maxlength="60">
<button class="btn2" id="lookup" type="button">Look up</button>
</div>
<p class="msg" id="geoMsg" role="status" aria-live="polite"></p>
<div class="list" id="geoList" role="radiogroup" aria-label="Places found"></div>
<div id="tzWrap" hidden><label for="tzSel">Time zone</label><select id="tzSel"></select></div>
<details id="manual">
<summary>Enter the location by hand</summary>
<div class="list">
<label for="place">Name shown on the weather screen</label><input id="place" maxlength="23" autocomplete="off">
<label for="lat">Latitude (north is positive)</label><input id="lat" inputmode="decimal" autocomplete="off">
<label for="lon">Longitude (east is positive)</label><input id="lon" inputmode="decimal" autocomplete="off">
<label for="tzMan">Time zone</label><select id="tzMan"></select>
</div>
</details>
<p class="note">The town is looked up with Open-Meteo over the internet. Round the coordinates if you want less precision.</p>
<label class="chk"><input type="checkbox" id="aurora">Show UK aurora alerts (AuroraWatch UK)</label>
</section>

<button class="main" id="save" type="button">Test and save</button>
<p class="note" style="text-align:center">The display tests each step and shows the result on its screen. If something fails you come back here with the reason.</p>

<section id="result" hidden aria-labelledby="h-res">
<h2 id="h-res">TEST</h2>
<ul class="steps" id="steps"></ul>
<p class="msg" id="saveMsg" role="status" aria-live="polite"></p>
</section>
</main>
<script>
"use strict";
const $ = id => document.getElementById(id);
const STEP_NAMES = ["Wi-Fi", "Gateway found", "Gateway login", "Location and time zone"];
let prefilled = false, zones = null, chosenTz = "", cfgTz = "", lostSince = 0, geoBusy = false, testRunning = false;

function msg(el, text, cls) { el.textContent = text || ""; el.className = "msg" + (cls ? " " + cls : ""); }
function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }
async function api(path, body) {
  const opt = { cache: "no-store" };
  if (body !== undefined) { opt.method = "POST"; opt.headers = { "Content-Type": "application/json" }; opt.body = JSON.stringify(body); }
  try {
    const r = await fetch(path, opt);
    let j = {};
    try { j = await r.json(); } catch (e) { j = {}; }
    if (!r.ok && !j.error) j.error = "HTTP " + r.status;
    j.httpOk = r.ok;
    lost(false);
    return j;
  } catch (e) {
    lost(true);
    return { httpOk: false, error: "no answer from the display", offline: true };
  }
}
function lost(on) {
  if (!on) { lostSince = 0; $("lost").style.display = "none"; return; }
  if (!lostSince) lostSince = Date.now();
  if (Date.now() - lostSince > 2500) $("lost").style.display = "block";
}
function strength(rssi) { return rssi >= -60 ? "strong" : rssi >= -72 ? "good" : "weak"; }
function ssidValue() { const v = $("ssid").value; return v === "\u0001other" ? $("ssidOther").value : v; }

async function scan() {
  const j = await api("/api/scan");
  if (!j.httpOk) { setTimeout(scan, 3000); return; }
  const sel = $("ssid"), keep = sel.value;
  sel.textContent = "";
  for (const n of j.networks || []) {
    const o = document.createElement("option");
    o.value = n.ssid;
    o.textContent = n.ssid + "  (" + strength(n.rssi) + (n.secure ? "" : ", open") + ")";
    sel.appendChild(o);
  }
  const other = document.createElement("option");
  other.value = "\u0001other"; other.textContent = "Other network...";
  sel.appendChild(other);
  if (keep && [...sel.options].some(o => o.value === keep)) sel.value = keep;
  else if (window.cfgSsid && [...sel.options].some(o => o.value === window.cfgSsid)) sel.value = window.cfgSsid;
  else if (!(j.networks || []).length) sel.value = "\u0001other";
  sel.dispatchEvent(new Event("change"));
  if (j.scanning) setTimeout(scan, 2000);
}

async function join() {
  const ssid = ssidValue().trim();
  if (!ssid) { msg($("wifiMsg"), "Choose your network first.", "err"); return; }
  const j = await api("/api/wifi", { ssid: ssid, password: $("wpass").value });
  if (!j.httpOk) { msg($("wifiMsg"), j.error, "err"); return; }
  msg($("wifiMsg"), "Joining " + ssid + "... The setup Wi-Fi may drop for a few seconds while the display changes channel; your phone should reconnect by itself.", "warn");
}

async function findGw() {
  const j = await api("/api/discover", {});
  if (!j.httpOk) msg($("findMsg"), j.error, "err");
  else msg($("findMsg"), "Searching your network...", "warn");
}

function placeText(r) { return [r.name, r.admin1, r.country].filter(Boolean).join(", "); }
function chooseGeo(r) {
  $("place").value = r.name.slice(0, 23);
  $("lat").value = (+r.lat).toFixed(4);
  $("lon").value = (+r.lon).toFixed(4);
  chosenTz = r.posix ? r.tz : "";
  $("tzWrap").hidden = !!r.posix;
  if (!r.posix) loadZones();
  else setZone(r.tz);
  $("aurora").checked = r.cc === "GB";
}
async function lookup() {
  const q = $("town").value.trim();
  if (q.length < 2) { msg($("geoMsg"), "Type at least 2 letters.", "err"); return; }
  if (geoBusy) return;
  geoBusy = true; $("lookup").disabled = true;
  msg($("geoMsg"), "Looking up...", "warn");
  $("geoList").textContent = "";
  let j = {};
  for (let i = 0; i < 40; i++) {
    j = await api("/api/geocode?q=" + encodeURIComponent(q));
    if (j.state !== "running" && !j.offline) break;
    await sleep(1000);
  }
  geoBusy = false; $("lookup").disabled = false;
  if (j.state !== "done") { msg($("geoMsg"), j.error || "Lookup failed.", "err"); return; }
  const res = j.results || [];
  if (!res.length) { msg($("geoMsg"), "Nothing found. Try another spelling, or enter the location by hand below.", "err"); return; }
  msg($("geoMsg"), res.length > 1 ? "Choose your place:" : "", "");
  res.forEach((r, i) => {
    const l = document.createElement("label"); l.className = "pick";
    const inp = document.createElement("input"); inp.type = "radio"; inp.name = "geo"; inp.checked = i === 0;
    inp.addEventListener("change", () => chooseGeo(r));
    const t = document.createElement("span");
    t.textContent = placeText(r) + " / " + (+r.lat).toFixed(2) + ", " + (+r.lon).toFixed(2) + " / " + (r.tz || "unknown time zone");
    l.appendChild(inp); l.appendChild(t); $("geoList").appendChild(l);
  });
  chooseGeo(res[0]);
}

async function loadZones() {
  if (!zones) {
    const j = await api("/api/zones");
    if (!j.httpOk) return;
    zones = j.zones || [];
    for (const id of ["tzSel", "tzMan"]) {
      const sel = $(id);
      sel.textContent = "";
      const none = document.createElement("option"); none.value = ""; none.textContent = "Choose...";
      sel.appendChild(none);
      if (cfgTz) { const o = document.createElement("option"); o.value = "\u0001cfg"; o.textContent = "Keep saved (" + cfgTz + ")"; sel.appendChild(o); }
      for (const z of zones) { const o = document.createElement("option"); o.value = z; o.textContent = z; sel.appendChild(o); }
    }
  }
}
function setZone(z) { loadZones().then(() => { if ([...$("tzMan").options].some(o => o.value === z)) $("tzMan").value = z; }); }

function zoneChoice() {
  if (chosenTz) return { tzName: chosenTz, tz: "" };
  const a = !$("tzWrap").hidden ? $("tzSel").value : $("tzMan").value;
  if (a === "\u0001cfg") return { tzName: "", tz: cfgTz };
  return { tzName: a, tz: "" };
}

async function save() {
  const z = zoneChoice();
  const body = {
    ssid: ssidValue().trim(), password: $("wpass").value,
    pwHost: $("gw").value.trim(), pwPassword: $("pwpass").value,
    place: $("place").value.trim(), lat: parseFloat($("lat").value), lon: parseFloat($("lon").value),
    tzName: z.tzName, tz: z.tz, auroraUk: $("aurora").checked
  };
  const sm = $("saveMsg");
  $("result").hidden = false;
  if (!body.ssid) { msg(sm, "Step 1: choose your Wi-Fi network.", "err"); return; }
  if (!body.pwHost) { msg(sm, "Step 2: enter the gateway address (or press Find it).", "err"); return; }
  if (!body.place || isNaN(body.lat) || isNaN(body.lon)) { msg(sm, "Step 3: look up your town, or enter the location by hand.", "err"); return; }
  if (!body.tzName && !body.tz) { msg(sm, "Step 3: choose your time zone.", "err"); return; }
  $("save").disabled = true;
  msg(sm, "Starting the test...", "warn");
  const j = await api("/api/save", body);
  body.password = body.pwPassword = "";
  if (!j.httpOk) { msg(sm, j.error, "err"); $("save").disabled = false; return; }
  testRunning = true;
}

function renderSteps(t) {
  const ul = $("steps");
  if (!ul.children.length) STEP_NAMES.forEach(n => {
    const li = document.createElement("li");
    li.innerHTML = '<span class="mark"></span><span class="sl"></span><span class="sn"></span>';
    li.children[1].textContent = n; ul.appendChild(li);
  });
  (t.steps || []).forEach((s, i) => {
    const li = ul.children[i]; if (!li) return;
    const m = li.children[0];
    m.className = "mark" + (s.s === "ok" ? " m-ok" : s.s === "run" ? " m-run" : s.s === "fail" ? " m-fail" : "");
    m.textContent = s.s === "ok" ? "OK" : s.s === "run" ? "..." : s.s === "fail" ? "X" : "";
    li.children[2].textContent = s.note || "";
  });
}

function render(j) {
  const ap = j.ap || {}, sta = j.sta || {}, c = j.config || {}, d = j.disc || {}, t = j.test || {};
  if (ap.ssid) $("apName").textContent = ap.ssid;
  if (!prefilled && j.config) {
    prefilled = true;
    window.cfgSsid = c.ssid || "";
    if (c.ssid) { $("ssidOther").value = c.ssid; if (c.wifiPasswordSet) $("wpass").placeholder = "saved - leave empty to keep"; }
    if (c.pwHost) $("gw").value = c.pwHost;
    if (c.pwPasswordSet) $("pwpass").placeholder = "saved - leave empty to keep";
    if (c.place) { $("place").value = c.place; $("lat").value = c.lat; $("lon").value = c.lon; }
    if (c.tz) { cfgTz = c.tz; loadZones().then(() => { $("tzMan").value = "\u0001cfg"; }); }
    $("aurora").checked = !!c.auroraUk;
  }
  const wm = $("wifiMsg");
  if (sta.state === "connecting") msg(wm, "Joining " + sta.ssid + "...", "warn");
  else if (sta.state === "ok") msg(wm, "Connected to " + sta.ssid + " (" + strength(sta.rssi) + ", " + sta.rssi + " dBm).", "ok");
  else if (sta.state === "failed") msg(wm, "Could not join " + sta.ssid + ": " + sta.reason, "err");
  const connected = sta.state === "ok";
  $("find").disabled = !connected || d.running;
  $("lookup").disabled = !connected || geoBusy;
  if (!connected && !d.running && !d.done) msg($("findMsg"), "Connect to your Wi-Fi first (step 1).", "");
  if (!connected && !geoBusy && !$("geoList").children.length) msg($("geoMsg"), "Connect to your Wi-Fi first (step 1).", "");
  else if (connected && $("geoMsg").textContent.indexOf("Connect to") === 0) msg($("geoMsg"), "", "");
  if (connected && $("findMsg").textContent.indexOf("Connect to") === 0) msg($("findMsg"), "", "");
  if (d.running) msg($("findMsg"), "Searching your network... " + d.scanned + " of " + d.total, "warn");
  else if (d.done) {
    const box = $("found");
    if (box.dataset.n !== String(d.count)) {
      box.dataset.n = String(d.count); box.textContent = "";
      for (const h of d.hosts || []) {
        const b = document.createElement("button"); b.type = "button"; b.className = "chip"; b.textContent = "Use " + h;
        b.addEventListener("click", () => { $("gw").value = h; });
        box.appendChild(b);
      }
      if (d.count === 1 && !$("gw").value) $("gw").value = d.hosts[0];
    }
    if (d.error) msg($("findMsg"), d.error, "err");
    else if (d.count) msg($("findMsg"), "Found " + d.count + " gateway" + (d.count > 1 ? "s" : "") + " on your network.", "ok");
    else msg($("findMsg"), "Not found on this network. If your gateway is on another network or VLAN, type its address; Test and save checks it.", "err");
  }
  if (t.state && t.state !== "idle") {
    $("result").hidden = false;
    renderSteps(t);
    const sm = $("saveMsg");
    if (t.state === "running") { msg(sm, "Testing... watch the display too.", "warn"); $("save").disabled = true; }
    else if (t.state === "failed") { msg(sm, t.error || "Test failed.", "err"); $("save").disabled = false; testRunning = false; }
    else if (t.state === "saved") { msg(sm, "Saved. The display restarts now and shows your Powerwall. You can close this page; your phone goes back to your home Wi-Fi.", "ok"); $("save").disabled = true; }
  }
}

async function poll() {
  const j = await api("/api/state");
  if (j.httpOk) render(j);
  const busy = testRunning || (j.sta && j.sta.state === "connecting") || (j.disc && j.disc.running);
  setTimeout(poll, busy ? 1000 : 2500);
}

$("ssid").addEventListener("change", () => { $("otherWrap").hidden = $("ssid").value !== "\u0001other"; });
$("showw").addEventListener("change", () => { $("wpass").type = $("showw").checked ? "text" : "password"; });
$("rescan").addEventListener("click", scan);
$("join").addEventListener("click", join);
$("find").addEventListener("click", findGw);
$("lookup").addEventListener("click", lookup);
$("town").addEventListener("keydown", e => { if (e.key === "Enter") lookup(); });
$("tzSel").addEventListener("change", () => { chosenTz = ""; });
$("manual").addEventListener("toggle", () => { if ($("manual").open) loadZones(); });
for (const id of ["lat", "lon", "tzMan"]) $(id).addEventListener("input", () => { chosenTz = ""; });
$("save").addEventListener("click", save);
scan();
poll();
</script>
</body>
</html>
)PAGE";

} // namespace setuppage
