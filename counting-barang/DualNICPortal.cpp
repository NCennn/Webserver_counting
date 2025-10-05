#include "DualNICPortal.h"


// ---- MQTT probe state (non-blocking terhadap AsyncWebServer) ----
static volatile bool s_mqttProbeRequested = false;
static volatile bool s_mqttProbeRunning   = false;
static volatile bool s_mqttLastOK         = false;
static volatile int  s_mqttLastRC         = 0;
bool isEthernetConnected;
#define LED_PIN 3


// ================== HTML UI ==================
extern const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Konfigurasi Jaringan</title>
<style>
  :root{--bg:#0b1020;--card:#121831;--muted:#8da2c0;--ok:#2ecc71;--bad:#e74c3c;--pri:#4c84ff;--txt:#e9eefb}
  *{box-sizing:border-box} body{margin:0;background:linear-gradient(180deg,#0b1020,#0b0f1a);color:var(--txt);font:14px/1.4 system-ui,Segoe UI,Roboto,Ubuntu}
  header{padding:16px 20px;border-bottom:1px solid #1d2440;background:#0b1020;position:sticky;top:0;z-index:5}
  h1{font-size:18px;margin:0} main{max-width:980px;margin:0 auto;padding:20px;display:grid;gap:16px}
  .card{background:var(--card);border:1px solid #1d2440;border-radius:16px;padding:16px;box-shadow:0 10px 20px rgba(0,0,0,.25)}
  .row{display:flex;gap:12px;flex-wrap:wrap}
  .pill{padding:6px 10px;border-radius:999px;background:#0c1330;border:1px solid #1f2a4d;color:var(--muted)}
  button,.btn{border:0;border-radius:12px;padding:10px 14px;background:var(--pri);color:#fff;cursor:pointer}
  button:disabled{opacity:.5;cursor:not-allowed}
  input,select{background:#0d1329;border:1px solid #1f2a4d;border-radius:10px;padding:10px;color:#e9eefb;min-width:0}
  label{display:block;margin:6px 0 6px;color:#c9d6ef}
  table{width:100%;border-collapse:collapse;margin-top:10px}
  th,td{padding:10px;border-bottom:1px solid #1d2440}
  .ok{color:var(--ok)} .bad{color:var(--bad)} .muted{color:var(--muted)}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:12px}
  .hint{font-size:12px;color:var(--muted)}
  .hide{display:none} 
</style>
</head>
<body>
<header><h1>Konfigurasi Jaringan (Wi-Fi & Ethernet)</h1></header>
<main>
  <section class="card">
    <div class="row" id="statusRow">
      <div class="pill" id="wifiStat">Wi-Fi: —</div>
      <div class="pill" id="ethStat">Ethernet: —</div>
      <div class="pill" id="ipStat">IP: —</div>
      <div class="pill" id="apStat">AP: —</div>
      <div class="pill" id="mdnsStat">mDNS: —</div>
      <div class="pill muted" id="apTimer">AP Auto-Off: —</div>
      <div class="pill" id="queueStat">Queue: —</div>
    </div>
    <div class="hint">Akses cepat: <b>http://Counter.local/</b> (mDNS). Wi-Fi diprioritaskan untuk koneksi keluar (contoh: MQTT). Halaman ini dapat diakses via Wi-Fi & Ethernet.</div>
  </section>

  <section class="card" id="wifiCard">
    <h3>Wi-Fi</h3>
    <div class="row">
      <button id="scanBtn">Pindai Jaringan</button>
      <button id="discBtn">Putuskan Wi-Fi</button>
    </div>
    <table>
      <thead><tr><th>SSID</th><th>RSSI</th><th>Keamanan</th><th>Aksi</th></tr></thead>
      <tbody id="scanBody"><tr><td colspan="4" class="muted">Belum dipindai.</td></tr></tbody>
    </table>
    <div class="grid" style="margin-top:12px">
      <div>
        <label>SSID Tersimpan</label>
        <input id="ssidSaved" placeholder="Masukkan SSID">
      </div>
      <div>
        <label>Password</label>
        <input id="passInput" type="password" placeholder="••••••">
      </div>
      <div style="align-self:end">
        <button id="connectBtn">Sambungkan</button>
      </div>
    </div>
  </section>

  <section class="card" id="ethCard">
    <h3>Ethernet (Static)</h3>
    <div class="grid">
      <div><label>IP Address</label><input id="ethIP" placeholder="192.168.1.50"></div>
      <div><label>Gateway</label><input id="ethGW" placeholder="192.168.1.1"></div>
      <div><label>Subnet</label><input id="ethSN" placeholder="255.255.255.0"></div>
      <div style="align-self:end"><button id="saveEth">Simpan & Terapkan</button></div>
    </div>
  </section>

  <section class="card">
    <h3>Server Configuration</h3>
    <div class="grid">
      <div><label>Host</label><input id="mqHost" placeholder="broker.local"></div>
      <div><label>Port</label><input id="mqPort" type="number" value="1883"></div>
      <div><label>Username</label><input id="mqUser" placeholder="(opsional)"></div>
      <div><label>Password</label><input id="mqPass" type="password" placeholder="(opsional)"></div>
      <div><label>Topic</label><input id="mqTopic" placeholder="device/telemetry"></div>
      <div style="align-self:end" class="row">
        <button id="saveMQTT">Simpan & Sambungkan</button>
      </div>
    </div>
  </section>

  <section class="card">
    <h3>Antrian Offline</h3>
    <div class="row">
      <button id="flushQueue">Flush ke MQTT</button>
      <span class="hint">Akan mengirim item antrian jika MQTT sedang terhubung.</span>
    </div>
  </section>

  <section class="card">
    <h3>AP & Reset</h3>
    <div class="row">
      <button id="apEnable">Aktifkan AP 10 menit</button>
      <button id="apDisable">Matikan AP</button>
      <button id="factoryBtn" style="background:#e74c3c">Factory Reset</button>
      <span class="hint">AP sementara untuk provisioning; default auto-off setelah 10 menit.</span>
    </div>
  </section>
</main>
<script>
const $ = sel => document.querySelector(sel);
const api = (p, opt={}) => fetch(p, Object.assign({headers:{'Content-Type':'application/json'}}, opt));
const setIfIdle = (el, v) => { if (!el) return; if (document.activeElement === el) return; el.value = v ?? ''; };
let uiMode = 'wifi';
function fmtMs(ms){ if(ms<=0) return '—'; const s=Math.ceil(ms/1000); const m=Math.floor(s/60); const r=s%60; return `${m}:${String(r).padStart(2,'0')}`; }

async function refreshStatus(){
  try{
    const r = await api('/api/status'); const j = await r.json(); uiMode = j.ui?.mode || 'wifi';
    $('#wifiStat').textContent = j.wifi.connected ? `Wi-Fi: Terhubung (${j.wifi.ssid})` : 'Wi-Fi: Tidak tersambung';
    $('#wifiStat').className = 'pill ' + (j.wifi.connected? 'ok':'bad');
    $('#ethStat').textContent = j.eth.link ? 'Ethernet: Link UP' : 'Ethernet: Link DOWN';
    $('#ethStat').className = 'pill ' + (j.eth.link? 'ok':'bad');
    const ip = j.wifi.connected ? j.wifi.ip : (j.eth.ip||'—');
    $('#ipStat').textContent = 'IP: ' + (ip || '—');
    $('#apStat').textContent = j.ap.active ? ('AP: ' + j.ap.ssid) : 'AP: nonaktif';
    $('#mdnsStat').textContent = 'mDNS: ' + (j.mdns?.host ? (j.mdns.host + '.local') : '—');
    $('#apTimer').textContent = 'AP Auto-Off: ' + fmtMs(j.ap.remaining_ms||0);
    $('#queueStat').textContent = `Queue: ${j.queue?.count||0} (${j.queue?.bytes||0}B)`;

    const ssidInput = $('#ssidSaved');
    if (ssidInput && ssidInput !== document.activeElement){
      if (ssidInput.value === '' || ssidInput.value === (j.wifi.ssid || '')) ssidInput.value = j.wifi.ssid || '';
    }
    $('#ethIP').value = j.cfg.eth_ip||'';
    $('#ethGW').value = j.cfg.eth_gateway||'';
    $('#ethSN').value = j.cfg.eth_subnet||'';

    const mqttHost = $('#mqHost');
    if (mqttHost && mqttHost !== document.activeElement){
      if (mqttHost.value === '' || mqttHost.value === (j.mqtt.host || '')) mqttHost.value = j.mqtt.host || '';
    }
    $('#mqPort').value = j.mqtt.port||1883;
    const mqUser = $('#mqUser');
    if (mqUser && mqUser !== document.activeElement){
      if (mqUser.value === '' || mqUser.value === (j.mqtt.user || '')) mqUser.value = j.mqtt.user || '';
    }
    const mqPass = $('#mqPass');
    if (mqPass && mqPass !== document.activeElement){
      mqPass.value = '';
      mqPass.placeholder = (j.mqtt?.pass_set ? '(tersimpan)' : '(opsional)');
    }
    const mqTopic = $('#mqTopic');
    if (mqTopic && mqTopic !== document.activeElement){
      if (mqTopic.value === '' || mqTopic.value === (j.mqtt.topic || '')) mqTopic.value = j.mqtt.topic || '';
    }

    $('#wifiCard').classList.toggle('hide', uiMode !== 'wifi');
    $('#ethCard').classList.toggle('hide', uiMode !== 'ethernet');
  }catch(e){console.warn(e)}
}

$('#scanBtn')?.addEventListener('click', async ()=>{
  const tb = $('#scanBody'); tb.innerHTML = '<tr><td colspan=4 class="muted">Memindai...</td></tr>';
  const r = await api('/api/scan'); const j = await r.json();
  if(!j.aps || !j.aps.length){ tb.innerHTML = '<tr><td colspan=4 class="muted">Tidak ada jaringan ditemukan.</td></tr>'; return; }
  tb.innerHTML = '';
  j.aps.sort((a,b)=>b.rssi-a.rssi).forEach(ap=>{
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${ap.ssid}</td><td>${ap.rssi}</td><td>${ap.sec}</td><td><button class="btn btn-sm">Pilih</button></td>`;
    tr.querySelector('button').onclick = ()=>{ $('#ssidSaved').value = ap.ssid; };
    tb.appendChild(tr);
  });
});

$('#connectBtn')?.addEventListener('click', async ()=>{
  const ssid = $('#ssidSaved').value.trim(); const pass = $('#passInput').value;
  if(!ssid){ alert('Isi SSID terlebih dahulu.'); return; }
  const r = await api('/api/wifi/connect',{method:'POST',body:JSON.stringify({ssid,pass})});
  const ok = r.ok; const j = await r.json();
  alert(ok? 'Wi-Fi tersambung.':'Gagal: '+(j.error||r.status));
  refreshStatus();
});
$('#discBtn')?.addEventListener('click', async ()=>{ await api('/api/wifi/disconnect',{method:'POST'}); refreshStatus(); });

$('#saveEth')?.addEventListener('click', async ()=>{
  const ip=$('#ethIP').value.trim(), gw=$('#ethGW').value.trim(), sn=$('#ethSN').value.trim();
  if(!ip||!sn){ alert('IP dan Subnet wajib diisi.'); return; }
  const r = await api('/api/eth/set',{method:'POST',body:JSON.stringify({ip,gateway,subnet})});
  const ok = r.ok; const j = await r.json();
  alert(ok? 'Ethernet diterapkan.':'Gagal: '+(j.error||r.status));
  setTimeout(refreshStatus, 1000);
});

$('#saveMQTT').addEventListener('click', async ()=>{
  const host=$('#mqHost').value.trim(), port=Number($('#mqPort').value||1883),
        user=$('#mqUser').value.trim(), pass=$('#mqPass').value.trim(),
        topic=$('#mqTopic').value.trim();
  if(!host){ alert("Masukkan Host Server Terlebih dahulu"); return; }
  const payload = {host, port, user, topic};
  if (pass) payload.pass = pass; // hanya kirim jika diisi
  const r = await api('/api/mqtt/set',{method:'POST',body:JSON.stringify(payload)});
  const j = await r.json();
  if (!r.ok) { alert('Gagal menyimpan MQTT.'); return; }
  // Tampilkan status testing, hasil akan muncul via /api/status yang dipoll tiap 2s
  alert('Berhasil Mengubah Server. Device akan di restart otomatis');
  refreshStatus();
});


// offline queue UI akan memanggil endpoint ekstra yang kamu tambahkan di sketch
$('#flushQueue')?.addEventListener('click', async ()=>{
  const r = await api('/api/queue/flush',{method:'POST'});
  const j = await r.json();
  alert('Flushed: ' + (j.flushed||0));
  refreshStatus();
});

$('#apEnable').addEventListener('click', async ()=>{
  await api('/api/ap/enable',{method:'POST',body:JSON.stringify({minutes:10})});
  setTimeout(refreshStatus, 300);
});
$('#apDisable').addEventListener('click', async ()=>{
  await api('/api/ap/disable',{method:'POST'});
  setTimeout(refreshStatus, 300);
});

$('#factoryBtn').addEventListener('click', async ()=>{
  if(!confirm('Hapus semua konfigurasi dan restart?')) return;
  await api('/api/reset',{method:'POST'});
});

setInterval(refreshStatus, 2000);
refreshStatus();
</script>
</body>
</html>)HTML";

// ================== ctor ==================
DualNICPortal::DualNICPortal(const Pins& pins, const char* mdnsHost, const char* configPath)
: _pins(pins), _mdnsHost(mdnsHost), _configPath(configPath), _mqttTest() {
  // nothing
}

// ================== public ==================
void DualNICPortal::begin(){
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  delay(100);
  loadConfig();
  setupAPIfNoCred();
  if (!_cfg.wifi_ssid.isEmpty()) {
    if (connectWiFiBlocking(30000)) {
      startMDNSIfNeeded();
      if (_apSSID.length() > 0) _apOffAt = millis() + _AP_DEFAULT_MINUTES * 60000UL;
    }else if (ethernetBeginStatic()) {
      startMDNSIfNeeded();
      Serial.println("[ETH] Ethernet Connected");
      // serveEthernet();
    }
    else apEnableForMinutes(_AP_DEFAULT_MINUTES);
  }
  setupAsyncRoutes();
}

void DualNICPortal::loop(){

  serveEthernet();      // <-- ini harus dipanggil di setiap loop
  if (_pendingRestart) { delay(200); ESP.restart(); }

  // AP auto-off
  if (_apSSID.length() > 0 && _apOffAt && (int32_t)(millis() - _apOffAt) >= 0) {
    Serial.println(F("[AP] Auto-off trigger"));
    apDisable();
  }

  // Wi-Fi watchdog
  wifiWatchdogLoop();


  // ---- MQTT probe dijalankan di sini, bukan di handler HTTP ----
  if (s_mqttProbeRequested && !s_mqttProbeRunning) {
    s_mqttProbeRunning = true;
    s_mqttProbeRequested = false;

    // Lakukan tes koneksi dengan timeout singkat supaya loop tidak lama berhenti
    // Gunakan klien test internal (_mqttTest) yang sudah ada
    // (opsional) kecilkan socket timeout supaya cepat
    _mqttTest.setSocketTimeout(2); // detik

    bool ok = mqttTestConnectivity(6000); // ~6s max
    s_mqttLastRC = _mqttTest.state();
    s_mqttLastOK = ok;
    if (ok) _mqttTest.disconnect();

    s_mqttProbeRunning = false;
  }
}


bool DualNICPortal::selectClient(PubSubClient& mqtt, WiFiClient& wifiClientRef, EthernetClient& ethClientRef){
  if (WiFi.status() == WL_CONNECTED) { mqtt.setClient(wifiClientRef); return true; }
  else if (ethernetLinkUp())         { mqtt.setClient(ethClientRef); return true; }
  else return false;
}

DualNICPortal::NetInfo DualNICPortal::current() const{
  NetInfo n;
  n.wifi = (WiFi.status() == WL_CONNECTED);
  n.eth = ethernetLinkUp();
  n.ap = (_apSSID.length() > 0);
  if (n.wifi) n.ip = WiFi.localIP();
  else if (n.eth) n.ip = Ethernet.localIP();
  else n.ip = WiFi.softAPIP();
  n.ipStr = n.ip.toString();
  return n;
}

String DualNICPortal::activeIP() const{
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  IPAddress eip = Ethernet.localIP(); if (eip != IPAddress(0,0,0,0)) return eip.toString();
  return WiFi.softAPIP().toString();
}

bool DualNICPortal::ethernetLinkUp() const{
  return Ethernet.linkStatus() == LinkON;
}

AppConfig& DualNICPortal::config(){ return _cfg; }

void DualNICPortal::setExtraApiHandler(ExtraApiHandler fn){ _extraHandler = fn; }
void DualNICPortal::setStatusAugmenter(StatusAugmenter fn){ _statusAugmenter = fn; }

// ================== internals ==================
void DualNICPortal::loadConfig(){
  if (!LittleFS.begin(true)) {
    Serial.println(F("[FS] LittleFS mount failed (formatted)."));
  }
  if (!LittleFS.exists(_configPath)) {
    Serial.println(F("[CFG] No config file, using defaults."));
    return;
  }
  File f = LittleFS.open(_configPath, "r");
  if (!f) { Serial.println(F("[CFG] Open config failed.")); return; }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("[CFG] JSON parse error: %s\n", err.c_str());
    return;
  }
  _cfg.wifi_ssid   = doc["wifi_ssid"].as<String>();
  _cfg.wifi_pass   = doc["wifi_pass"].as<String>();
  _cfg.eth_ip      = doc["eth_ip"].as<String>();
  _cfg.eth_gateway = doc["eth_gateway"].as<String>();
  _cfg.eth_subnet  = doc["eth_subnet"].as<String>();
  _cfg.mqtt_host   = doc["mqtt_host"].as<String>();
  _cfg.mqtt_port   = doc["mqtt_port"] | 1883;
  _cfg.mqtt_user   = doc["mqtt_user"].as<String>();
  _cfg.mqtt_pass   = doc["mqtt_pass"].as<String>();
  _cfg.mqtt_topic  = doc["mqtt_topic"].as<String>();
  Serial.println(F("[CFG] Loaded."));
}

bool DualNICPortal::saveConfig(){
  JsonDocument doc;
  doc["wifi_ssid"] = _cfg.wifi_ssid;
  doc["wifi_pass"] = _cfg.wifi_pass;
  doc["eth_ip"] = _cfg.eth_ip;
  doc["eth_gateway"] = _cfg.eth_gateway;
  doc["eth_subnet"] = _cfg.eth_subnet;
  doc["mqtt_host"] = _cfg.mqtt_host;
  doc["mqtt_port"] = _cfg.mqtt_port;
  doc["mqtt_user"] = _cfg.mqtt_user;
  doc["mqtt_pass"] = _cfg.mqtt_pass;
  doc["mqtt_topic"] = _cfg.mqtt_topic;
  File f = LittleFS.open(_configPath, "w");
  if (!f) { Serial.println(F("[CFG] Save open failed.")); return false; }
  size_t n = serializeJson(doc, f);
  f.close();
  Serial.printf("[CFG] Saved %u bytes.\n", (unsigned)n);
  return n > 0;
}

void DualNICPortal::setupAPIfNoCred(){
  if (_cfg.wifi_ssid.length() == 0) {
    _apSSID = "Device-Counter";
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_apSSID.c_str(), "12345678");
    Serial.printf("[AP] SoftAP '%s' started, IP: %s\n", _apSSID.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    _apSSID = "";
  }
}

bool DualNICPortal::connectWiFiBlocking(uint32_t timeoutMs){
  if (_cfg.wifi_ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(_mdnsHost.c_str());
  WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
  Serial.printf("[WiFi] Connecting to %s ...", _cfg.wifi_ssid.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(300); Serial.print('.');
  }
  bool ok = WiFi.status() == WL_CONNECTED;
  Serial.println(""); 
  Serial.println(ok ? "[WiFi] Connected ": " [WiFi] Failed.");
  // if (ok) {
  //   startMDNSIfNeeded();
  //   if (_apSSID.length() > 0) {
  //     _apOffAt = millis() + _AP_DEFAULT_MINUTES * 60000UL;
  //     Serial.printf("[AP] Scheduled auto-off in %u minutes", (unsigned)_AP_DEFAULT_MINUTES);
  //     ESP.restart();
  //   }
  //   _wifiWatchActive = false; _wifiWatchStart = 0; _wifiScanAt = 0;
  // } 
  // // else {
  // //   if (_apSSID.length() == 0) apEnableForMinutes(_AP_DEFAULT_MINUTES);
  // // }
  return ok;
}

void DualNICPortal::disconnectWiFi(){
  WiFi.disconnect(true, false);
  delay(100);
}

void DualNICPortal::startMDNSIfNeeded(){
  if (_mdnsStarted) return;
  // if (WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin(_mdnsHost.c_str())) {
    MDNS.addService("http", "tcp", 80);
    _mdnsStarted = true;
    Serial.printf("[mDNS] http://%s.local/ registered", _mdnsHost.c_str());
  } else {
    Serial.println(F("[mDNS] start failed"));
  }
}

void DualNICPortal::apEnableForMinutes(uint32_t minutes){
  _apSSID = "Device-Counter";
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(_apSSID.c_str(), "12345678");
  Serial.printf("[AP] SoftAP '%s' started, IP: %s", _apSSID.c_str(), WiFi.softAPIP().toString().c_str());
  _apOffAt = minutes ? millis() + minutes * 60000UL : 0;
}

void DualNICPortal::apDisable(){
  if (_apSSID.length() > 0) {
    WiFi.softAPdisconnect(true);
    Serial.println(F("[AP] Disabled"));
    _apSSID = "";
    _apOffAt = 0;
    if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
  }
}

void DualNICPortal::wifiWatchdogLoop() {
  if (_cfg.wifi_ssid.length() == 0) return;

  const uint32_t now = millis();

  // --- Jika sudah tersambung Wi-Fi: bereskan state & housekeeping ---
  if (WiFi.status() == WL_CONNECTED) {
    if (_wifiWatchActive) Serial.println(F("[WiFi] Reconnected; stop watchdog"));
    _wifiWatchActive = false;
    _wifiWatchStart  = 0;
    _wifiScanAt      = 0;

    // Pastikan mDNS aktif dan jadwalkan AP auto-off
    startMDNSIfNeeded();
    if (_apSSID.length() > 0 && _apOffAt == 0) {
      _apOffAt = now + _AP_DEFAULT_MINUTES * 60000UL;
      Serial.print(F("[AP] Scheduled auto-off in "));
      Serial.print(_AP_DEFAULT_MINUTES);
      Serial.println(F(" minutes"));
    }
    digitalWrite(LED_PIN, HIGH);

    // (Opsional) kalau punya fungsi untuk memilih jalur data, panggil di sini
    // selectClientPreferWiFi();
    // mqttTestConnectivity(10000);

    return;
  }

  // --- Tidak tersambung: aktifkan watchdog & fallback Ethernet sekali ---
  if (!_wifiWatchActive) {
    _wifiWatchActive = true;
    _wifiWatchStart  = now;
    _wifiScanAt      = 0;
    Serial.println(F("[WiFi] Lost; watchdog started"));

    // Fallback: hidupkan Ethernet statik saat Wi-Fi drop
    ethernetBeginStatic();
  }

  // Setelah tenggat, aktifkan AP untuk provisioning (tetap lanjut scan)
  if ((now - _wifiWatchStart) > _WIFI_LOST_AP_DELAY_MS && !isEthernetConnected) {
    if (_apSSID.length() == 0) {
      Serial.println(F("[WiFi] Enabling AP for provisioning (scan tetap berjalan)"));
      apEnableForMinutes(_AP_DEFAULT_MINUTES);
    }
  }

  // --- Scan periodik: SELALU jalan meskipun AP/Ethernet aktif ---
  const uint32_t SCAN_INTERVAL_MS  = 5000;  // sesuai poin #2
  const uint32_t RETRY_CONNECT_GAP = 8000;  // sesuai poin #3
  static uint32_t s_lastConnectTry = 0;

  if (now - _wifiScanAt >= SCAN_INTERVAL_MS) {
    _wifiScanAt = now;

    // show_hidden=true; SSID target selalu broadcast (poin #5), aman
    int n = WiFi.scanNetworks(false, true);
    bool found = false;
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == _cfg.wifi_ssid) { found = true; break; }
    }

    if (found && (now - s_lastConnectTry >= RETRY_CONNECT_GAP)) {
      Serial.println(F("[WiFi] Saved SSID detected; forcing reconnect (preempt AP/Ethernet)"));

      // Preempt: kalau AP aktif, gunakan mode AP+STA; kalau tidak, STA saja
      if (_apSSID.length() > 0) WiFi.mode(WIFI_AP_STA);
      else                      WiFi.mode(WIFI_STA);

      // Hostname mDNS (jika dipakai)
      if (_mdnsHost.length() > 0) WiFi.setHostname(_mdnsHost.c_str());

      WiFi.begin(_cfg.wifi_ssid.c_str(), _cfg.wifi_pass.c_str());
      s_lastConnectTry = now;
    }
  }
}


void DualNICPortal::ethernetResetPulse(){
  if (_pins.w5500_rst < 0) return;
  pinMode(_pins.w5500_rst, OUTPUT);
  digitalWrite(_pins.w5500_rst, LOW); delay(10);
  digitalWrite(_pins.w5500_rst, HIGH); delay(50);
}

bool DualNICPortal::ethernetBeginStatic(){
  SPI.begin();
  Ethernet.init(_pins.w5500_cs);
  ethernetResetPulse();

  // MAC bisa reuse MAC Wi-Fi agar unik
  uint8_t mac[6]; WiFi.macAddress(mac);

  // --- Ambil dari _cfg lebih dulu ---
  IPAddress ip, gw , sn;
  if (_cfg.eth_ip.isEmpty())      _cfg.eth_ip      = "10.26.101.197";
  if (_cfg.eth_gateway.isEmpty()) _cfg.eth_gateway = "10.26.101.1";
  if (_cfg.eth_subnet.isEmpty())  _cfg.eth_subnet  = "255.255.255.0";

  Serial.printf("[ETH] cfg: ip='%s' gw='%s' sn='%s'\n",
                _cfg.eth_ip.c_str(), _cfg.eth_gateway.c_str(), _cfg.eth_subnet.c_str());
  bool ipOk = ip.fromString(_cfg.eth_ip);
  bool snOk = sn.fromString(_cfg.eth_subnet);
  if (!gw.fromString(_cfg.eth_gateway)) gw = IPAddress(0,0,0,0);
  IPAddress dns = (gw == IPAddress(0,0,0,0)) ? IPAddress(8,8,8,8) : gw;

  Ethernet.begin(mac, ip, dns, gw, sn);

  if (!ipOk || !snOk) {
    Serial.println(F("[ETH] Static IP/Subnet invalid; skip Ethernet."));
    isEthernetConnected = false;
    return false;
  }

  // --- Mulai Ethernet dengan konfigurasi terbaru ---

  // Cek hardware & link
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println(F("[ETH] W5500 not found."));
    isEthernetConnected = false;
    return false;
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println(F("[ETH] Link OFF (cable?)."));
    isEthernetConnected = false;
    return false;
  }

  Serial.print(F("[ETH] Started with static IP: "));
  Serial.println(Ethernet.localIP());

  _ethServer.begin();
  Serial.println(F("[ETH] EthernetServer listening on :80"));

  isEthernetConnected = true;
  return true;
}

static String HTTP_200(const String& ct, const String& body) {
  String h = "HTTP/1.1 200 OK\r\nContent-Type: "; h += ct; h += "\r\nConnection: close\r\nContent-Length: ";
  h += String(body.length()); h += "\r\n\r\n"; return h + body;
}
static String HTTP_XXX(int code, const String& ct, const String& body) {
  String h = "HTTP/1.1 "; h += String(code); h += (code==404?" Not Found":" Error");
  h += "\r\nContent-Type: "; h += ct; h += "\r\nConnection: close\r\nContent-Length: ";
  h += String(body.length()); h += "\r\n\r\n"; return h + body;
}

void DualNICPortal::serveEthernet(){
  EthernetClient c = _ethServer.available();
  if (!c) return;
  c.setTimeout(2000);

  String reqLine = c.readStringUntil('\n');
  if (reqLine.length() == 0) { c.stop(); return; }
  reqLine.trim();
  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  String method = reqLine.substring(0, sp1);
  String url = reqLine.substring(sp1 + 1, sp2);
  String path = url;

  int contentLength = 0;
  while (true) {
    String h = c.readStringUntil('\n');
    if (h == "\r" || h.length() == 0) break;
    h.trim();
    if (h.startsWith("Content-Length:")) contentLength = h.substring(15).toInt();
  }

  String body;
  if (method == "POST" && contentLength > 0) {
    while ((int)body.length() < contentLength) {
      int ch = c.read(); if (ch < 0) break; body += (char)ch;
    }
  }

  if ((path == "/" || path == "/index.html") && method == "GET") {
    size_t len = strlen_P(INDEX_HTML);               // panjang dari PROGMEM
    String hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n"
                "Connection: close\r\nContent-Length: " + String(len) + "\r\n\r\n";
    c.print(hdr);
    // Print dari PROGMEM
    c.print(FPSTR(INDEX_HTML));   // Print::print(__FlashStringHelper*) akan baca dari flash
    c.stop();
    return;
  }

  String ct; int code=200; String payload = apiHandler(path, method, body, "ethernet", ct, code);
  if (code == 200) c.print(HTTP_200(ct, payload)); else c.print(HTTP_XXX(code, ct, payload));
  c.stop();
}

void DualNICPortal::setupAsyncRoutes(){
  _server.on("/", HTTP_GET, [this](AsyncWebServerRequest* req){
    req->send(200, "text/html", INDEX_HTML);
  });

  _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req){
    String ct; int code; String out = apiHandler("/api/status","GET","", "wifi", ct, code);
    req->send(code, ct, out);
  });
  _server.on("/api/scan", HTTP_GET, [this](AsyncWebServerRequest* req){
    String ct; int code; String out = apiHandler("/api/scan","GET","", "wifi", ct, code);
    req->send(code, ct, out);
  });

  auto handlePost = [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
    String body((const char*)data, len);
    String path = req->url();
    String ct; int code; String out = apiHandler(path, "POST", body, "wifi", ct, code);
    req->send(code, ct, out);
  };

  _server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/wifi/disconnect", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/eth/set", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/mqtt/set", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/ap/enable", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/ap/disable", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);
  _server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest* req){}, NULL, handlePost);

  // forward all unknown POSTs to extra handler via generic entry
  _server.onNotFound([this](AsyncWebServerRequest* req){
    if (req->method() == HTTP_GET || req->method() == HTTP_POST) {
      String body;
      if (req->method() == HTTP_POST) {
        // empty body: extra handler will decide
      }
      String ct; int code; String out = apiHandler(req->url(), (req->method()==HTTP_GET?"GET":"POST"),
                                                  body, "wifi", ct, code);
      req->send(code, ct, out);
    } else {
      req->send(404, "application/json", jsonErr("not found"));
    }
  });

  _server.begin();
  Serial.println(F("[HTTP] AsyncWebServer started on :80 (Wi-Fi)"));
}

// ===== API core =====
String DualNICPortal::jsonStatus(const String& ctx){
  JsonDocument doc;
  doc["wifi"]["connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi"]["ssid"] = WiFi.SSID();
  doc["wifi"]["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["wifi"]["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";

  doc["ap"]["active"] = _apSSID.length() > 0;
  doc["ap"]["ssid"] = _apSSID;
  doc["ap"]["remaining_ms"] = (_apOffAt && _apSSID.length()>0) ? (int32_t)(_apOffAt - millis()) : 0;

  bool elink = ethernetLinkUp();
  doc["eth"]["link"] = elink;
  doc["eth"]["ip"] = Ethernet.localIP().toString();
  doc["eth"]["gw"] = Ethernet.gatewayIP().toString();
  doc["eth"]["sn"] = Ethernet.subnetMask().toString();

  doc["cfg"]["eth_ip"] = _cfg.eth_ip;
  doc["cfg"]["eth_gateway"] = _cfg.eth_gateway;
  doc["cfg"]["eth_subnet"] = _cfg.eth_subnet;

  doc["mqtt"]["host"] = _cfg.mqtt_host;
  doc["mqtt"]["port"] = _cfg.mqtt_port;
  doc["mqtt"]["topic"] = _cfg.mqtt_topic;
  doc["mqtt"]["user"]        = _cfg.mqtt_user;
  doc["mqtt"]["pass_set"]    = _cfg.mqtt_pass.length() > 0;
  doc["mqtt"]["probe_running"]= s_mqttProbeRunning;
  doc["mqtt"]["last_rc"]     = s_mqttLastRC;     // PubSubClient::state()
  doc["mqtt"]["last_ok"]     = s_mqttLastOK;
  

  doc["mdns"]["host"] = _mdnsHost;
  doc["ui"]["mode"] = ctx;

  // augmentor (queue stats, dsb.)
  if (_statusAugmenter) _statusAugmenter(doc);

  String out; serializeJson(doc, out); return out;
}

String DualNICPortal::jsonOk(const String& msg){ JsonDocument d; d["status"]=msg; String s; serializeJson(d,s); return s; }
String DualNICPortal::jsonErr(const String& msg){ JsonDocument d; d["error"]=msg; String s; serializeJson(d,s); return s; }

String DualNICPortal::apiHandler(const String& path, const String& method, const String& body,
                                 const String& ctx, String& contentType, int& code){
  contentType = "application/json"; code = 200;

  if (path == "/api/status" && method == "GET") return jsonStatus(ctx);

  if (path == "/api/scan" && method == "GET") {
    int n = WiFi.scanNetworks(false, true);
    JsonDocument doc;
    JsonArray arr = doc["aps"].to<JsonArray>();
    for (int i=0;i<n;i++){
      JsonObject o = arr.add<JsonObject>();
      o["ssid"]=WiFi.SSID(i); o["rssi"]=WiFi.RSSI(i); o["sec"]=WiFi.encryptionType(i);
      o["bssid"]=WiFi.BSSIDstr(i); o["ch"]=WiFi.channel(i);
    }
    String s; serializeJson(doc,s); return s;
  }

  if (path == "/api/wifi/connect" && method == "POST") {
    JsonDocument d; if (deserializeJson(d, body)) { code=400; return jsonErr("JSON invalid"); }
    String ssid=d["ssid"].as<String>(), pass=d["pass"].as<String>();
    if (ssid.isEmpty()) { code=400; return jsonErr("SSID kosong"); }
    _cfg.wifi_ssid=ssid; _cfg.wifi_pass=pass; saveConfig();
    bool ok = connectWiFiBlocking(30000);
    if (!ok) { code=504; return jsonErr("Gagal konek Wi-Fi (timeout)"); }
    return jsonOk("connected");
    ESP.restart();
  }

  if (path == "/api/wifi/disconnect" && method == "POST") { disconnectWiFi(); return jsonOk("disconnected"); }

  if (path == "/api/eth/set" && method == "POST") {
    JsonDocument d; if (deserializeJson(d, body)) { code=400; return jsonErr("JSON invalid"); }

    String ip=d["ip"].as<String>(), gw=d["gateway"].as<String>(), sn=d["subnet"].as<String>();
    IPAddress tip, tgw, tsn; if (!tip.fromString(ip) || !tsn.fromString(sn)) { code=400; return jsonErr("IP/Subnet tidak valid"); }
    if (!tgw.fromString(gw)) tgw = IPAddress(0,0,0,0);
    _cfg.eth_ip=ip; _cfg.eth_gateway=gw; _cfg.eth_subnet=sn; saveConfig();
    ethernetBeginStatic(); 
    return jsonOk("eth_set");
  }

  if (path == "/api/mqtt/set" && method == "POST") {
    JsonDocument d;
    if (deserializeJson(d, body)) { code=400; return jsonErr("JSON invalid"); }

    _cfg.mqtt_host  = d["host"].as<String>();
    _cfg.mqtt_port  = d["port"] | _cfg.mqtt_port;
    _cfg.mqtt_user  = d["user"].as<String>();
    // hanya overwrite jika field 'pass' dikirim
    if (!d["pass"].isNull()) {
      String p = d["pass"].as<String>();
      if (p.length()) _cfg.mqtt_pass = p; // atau langsung = p; jika ingin bisa clear
    }
    _cfg.mqtt_topic = d["topic"].as<String>();
    saveConfig();

    // Jangan tes koneksi di thread async_tcp (hindari WDT). Jadwalkan saja.
    s_mqttProbeRequested = true;

    JsonDocument resp;
    resp["status"]  = "mqtt_saved";
    resp["testing"] = true;
    String s; serializeJson(resp, s);
    _pendingRestart = true;
    return s;
  }

  if (path == "/api/reset" && method == "POST") {
    LittleFS.remove(_configPath.c_str()); ESP.restart(); return jsonOk("restarting");
  }

  if (path == "/api/ap/enable" && method == "POST") {
    JsonDocument d; if (deserializeJson(d, body)) { code=400; return jsonErr("JSON invalid"); }
    uint32_t m = d["minutes"] | _AP_DEFAULT_MINUTES; apEnableForMinutes(m); return jsonOk("ap_enabled");
  }
  if (path == "/api/ap/disable" && method == "POST") { apDisable(); return jsonOk("ap_disabled"); }

  // → extra handler (user app)
  if (_extraHandler) {
    String out;
    bool handled = _extraHandler(path, method, body, ctx, contentType, code, out);
    if (handled) return out;
  }

  code = 404; return jsonErr("not found");
}

bool DualNICPortal::mqttTestConnectivity(uint32_t timeoutMs){
  if (_cfg.mqtt_host.isEmpty()) return false;

  if (WiFi.status() == WL_CONNECTED) _mqttTest.setClient(_wifiClient);
  else if (ethernetLinkUp())         _mqttTest.setClient(_ethClient);
  else return false;

  _mqttTest.setServer( _cfg.mqtt_host.c_str(), _cfg.mqtt_port);
  Serial.print("Menghubungkan ke MQTT ");
  Serial.print(_cfg.mqtt_host);
  Serial.print(":");
  Serial.println(_cfg.mqtt_port);
  
  uint32_t t0 = millis();
  bool ok = false;

  while (!ok && (millis() - t0) < timeoutMs) {
    if (_cfg.mqtt_user.length() > 0) ok = _mqttTest.connect("Counter_Device", _cfg.mqtt_user.c_str(), _cfg.mqtt_pass.c_str());
    else ok = _mqttTest.connect("Counter_Device");
  }

  if(ok) Serial.println("[MQTT] Connected");
  else Serial.printf("[MQTT] Connection Failed, rc= %d", _mqttTest.state());
  return ok;
}
