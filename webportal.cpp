// ============================================================================
//  webportal.cpp — AP-only captive portal for WiFi config
// ============================================================================
#include <ESP8266WiFi.h>
#include "webportal.h"

// ── Minimal config page ─────────────────────────────────────────────────────
const char WebPortal::PAGE_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>PUNKCYBER Clock</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Courier New',monospace;background:#0a0a0a;color:#0ff;
  padding:20px;max-width:400px;margin:0 auto}
h1{text-align:center;font-size:1.2em;margin-bottom:20px;
  text-shadow:0 0 10px #0ff}
.section{background:#111;border:1px solid #333;border-radius:8px;
  padding:15px;margin-bottom:15px}
h2{font-size:0.85em;color:#ff0;margin-bottom:10px}
label{display:block;font-size:0.7em;color:#888;margin:8px 0 4px}
input,select{width:100%;padding:8px;background:#1a1a1a;border:1px solid #333;
  border-radius:4px;color:#0ff;font-family:inherit;font-size:0.85em}
input:focus{border-color:#0ff;outline:none}
button{width:100%;padding:12px;margin-top:15px;background:#0ff;color:#000;
  font-weight:bold;border:none;border-radius:4px;cursor:pointer;
  font-family:inherit;font-size:1em}
button:hover{background:#ff0}
#nets{max-height:150px;overflow-y:auto;margin-bottom:10px}
.net{padding:6px;cursor:pointer;border-bottom:1px solid #222;font-size:0.8em}
.net:hover{background:#1a1a1a}
.footer{text-align:center;color:#333;font-size:0.6em;margin-top:20px}
</style></head><body>

<h1>⚡ PUNKCYBER CLOCK ⚡</h1>

<div class="section">
<h2>📡 WIFI</h2>
<button type="button" onclick="scan()" style="margin:0 0 10px;padding:6px;font-size:0.75em;background:#333;color:#0ff">🔍 SCAN NETWORKS</button>
<div id="nets"></div>
<label>SSID</label>
<input type="text" name="ssid" id="ssid" maxlength="32" required>
<label>PASSWORD</label>
<input type="password" name="pass" id="pass" maxlength="64">
</div>

<div class="section">
<h2>💻 PC CONNECTION</h2>
<label>PC IP ADDRESS</label>
<input type="text" name="pcip" id="pcip" placeholder="192.168.1.50" maxlength="15">
<label>UDP PORT</label>
<input type="number" name="pcport" id="pcport" value="8889" min="1" max="65535">
<p style="color:#555;font-size:0.65em;margin-top:4px">IP of the PC running the stats server. Port default: 8889.</p>
</div>

<form method="POST" action="/save" id="mainForm">
<input type="hidden" name="ssid" id="h_ssid">
<input type="hidden" name="pass" id="h_pass">
<input type="hidden" name="pcip" id="h_pcip">
<input type="hidden" name="pcport" id="h_pcport">
<button type="submit" onclick="prepSave()">💾 SAVE & CONNECT</button>
</form>

<p class="footer">PUNKCYBER Clock — ESP8266 Thin Client</p>

<script>
function scan(){
  var d=document.getElementById('nets');
  d.innerHTML='Scanning...';
  fetch('/scan').then(r=>r.json()).then(function(nets){
    d.innerHTML='';
    nets.forEach(function(n){
      var div=document.createElement('div');
      div.className='net';
      div.textContent=n.ssid+' ('+n.rssi+'dBm)';
      div.onclick=function(){document.getElementById('ssid').value=n.ssid;};
      d.appendChild(div);
    });
    if(!nets.length) d.innerHTML='No networks found';
  }).catch(function(){d.innerHTML='Scan failed';});
}
function prepSave(){
  document.getElementById('h_ssid').value=document.getElementById('ssid').value;
  document.getElementById('h_pass').value=document.getElementById('pass').value;
  document.getElementById('h_pcip').value=document.getElementById('pcip').value;
  document.getElementById('h_pcport').value=document.getElementById('pcport').value;
}
</script>
</body></html>
)rawhtml";

// ── Constructor ─────────────────────────────────────────────────────────────
WebPortal::WebPortal() : _server(80), _cfgMgr(nullptr), _saved(false) {}

// ── Route setup ─────────────────────────────────────────────────────────────
void WebPortal::_setupRoutes() {
  _server.on("/",     [this]() { _handleRoot(); });
  _server.on("/scan", [this]() { _handleScan(); });
  _server.on("/save", HTTP_POST, [this]() { _handleSave(); });
  _server.onNotFound([this]() { _handleNotFound(); });
}

// ── begin (AP mode) ─────────────────────────────────────────────────────────
void WebPortal::begin(ConfigManager& cfgMgr) {
  _cfgMgr = &cfgMgr;
  _saved  = false;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, nullptr, AP_CHANNEL);

  _dns.start(DNS_PORT, "*", WiFi.softAPIP());

  _setupRoutes();
  _server.begin();

  Serial.printf("[PORTAL] AP: %s  IP: %s\n", AP_SSID,
                WiFi.softAPIP().toString().c_str());
}

// ── update (AP mode loop) ───────────────────────────────────────────────────
void WebPortal::update() {
  _dns.processNextRequest();
  _server.handleClient();
}

// ── handleRoot ──────────────────────────────────────────────────────────────
void WebPortal::_handleRoot() {
  _server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
}

// ── handleScan ──────────────────────────────────────────────────────────────
void WebPortal::_handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  json.reserve(n * 60);
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ',';
    String ssid = WiFi.SSID(i);
    ssid.replace("\\", "\\\\");  // Escape backslash
    ssid.replace("\"", "\\\"");  // Escape quotes
    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  _server.send(200, "application/json", json);
}

// ── handleSave ──────────────────────────────────────────────────────────────
void WebPortal::_handleSave() {
  ClockConfig cfg;
  _cfgMgr->setDefaults(cfg);

  String ssid   = _server.arg("ssid");
  String pass   = _server.arg("pass");
  String pcip   = _server.arg("pcip");
  String pcport = _server.arg("pcport");

  strlcpy(cfg.ssid,     ssid.c_str(),  sizeof(cfg.ssid));
  strlcpy(cfg.password, pass.c_str(),  sizeof(cfg.password));
  strlcpy(cfg.pcIP,     pcip.c_str(),  sizeof(cfg.pcIP));
  cfg.pcPort = pcport.length() ? pcport.toInt() : 8889;

  _cfgMgr->save(cfg);

  _server.send(200, "text/html; charset=utf-8",
    F("<html><head><meta charset=\"utf-8\"></head>"
      "<body style='background:#0a0a0a;color:#0ff;font-family:monospace;"
      "text-align:center;padding:50px'>"
      "<h2>✅ SAVED</h2><p>Rebooting...</p></body></html>"));

  Serial.println(F("[PORTAL] Config saved — rebooting..."));
  _saved = true;
}

// ── handleNotFound (captive portal redirect) ────────────────────────────────
void WebPortal::_handleNotFound() {
  _server.sendHeader("Location", "http://192.168.4.1/");
  _server.send(302, "text/plain", "");
}
