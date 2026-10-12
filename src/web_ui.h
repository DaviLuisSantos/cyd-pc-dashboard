#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <functional>
#include <time.h>
#include "config.h"
#include "settings.h"
#include "backlight.h"
#include "screens.h"
#include "i18n.h"
#include "discovery.h"
#include "alerts.h"
#include "theme.h"

// Interface web pós-boot: http://<ip-do-painel>/ ou http://cyd-dashboard.local/
//
//   GET  /                 página (status, screenshot clicável, abas, configurações)
//   GET  /api/status       estado em JSON (versão, IP, aba atual, brilho, modo noturno...)
//   GET  /api/settings     configurações atuais (sem a senha)
//   POST /api/settings     JSON parcial {"bright": 120, "screens": "pcstats,quotes"} —
//                          valida (settings::setField), salva no NVS e aplica na hora
//   POST /api/tab          ?id=pcstats | ?index=2 | ?next=1
//   POST /api/touch        ?x=..&y=.. (simula um toque, mesmo caminho do touch físico)
//   POST /api/reboot
//   POST /api/update       multipart com o .bin do APP (.pio/build/cyd/firmware.bin ou o
//                          *-ota.bin da release) — grava no outro slot OTA e reinicia.
//                          ?size=<bytes> opcional, só pra barra de progresso no display.
//                          curl -F firmware=@firmware.bin http://cyd-dashboard.local/api/update
//   GET  /screenshot.bmp   framebuffer atual (BMP 24 bits)
//
// Com settings.webPass preenchida, tudo exige HTTP Basic (usuário "admin").
namespace webui {

struct Hooks {
    std::function<void()> applySettings;         // reaplica tudo depois de salvar
    std::function<void(int, int)> touch;         // toque simulado (x, y)
    std::function<const char*(Screen*)> screenId; // id da tela (clock, pcstats...)
    std::function<void()> redraw;                // recria/redesenha as abas (OTA falhou)
};

static WebServer server(WEB_PORT);
static TFT_eSPI* tft_ = nullptr;
static ScreenManager* manager_ = nullptr;
static Hooks hooks_;
static bool started_ = false;
static bool mdnsStarted_ = false;

static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD PC Dashboard</title>
<style>
:root{--bg:#282a36;--card:#313442;--cur:#44475a;--fg:#f8f8f2;--com:#6272a4;--cyan:#8be9fd;--green:#50fa7b;--orange:#ffb86c;--pink:#ff79c6;--purple:#bd93f9;--red:#ff5555}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}h1{font-size:20px;margin:0 0 4px}h2{font-size:15px;color:var(--purple);margin:0 0 12px}
.sub{color:var(--com);font-size:13px;margin-bottom:16px}.card{background:var(--card);border-radius:10px;padding:16px;margin-bottom:16px}
#shot{width:100%;max-width:640px;aspect-ratio:4/3;display:block;margin:0 auto 10px;border-radius:6px;background:#000;cursor:crosshair;image-rendering:pixelated}
.row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}button{background:var(--cur);color:var(--fg);border:0;border-radius:6px;padding:8px 12px;font:inherit;cursor:pointer}
button:hover{background:var(--com)}button.on{background:var(--purple);color:var(--bg)}button.primary{background:var(--green);color:var(--bg);font-weight:600}
button.danger{background:transparent;color:var(--red);border:1px solid var(--red)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(140px,1fr));gap:6px 16px;font-size:13px}.grid b{display:block;color:var(--com);font-weight:400}
form{display:grid;gap:12px}label{display:grid;gap:4px;font-size:13px;color:var(--com)}label.chk{display:flex;align-items:center;gap:8px;color:var(--fg)}
input,select{background:var(--bg);color:var(--fg);border:1px solid var(--cur);border-radius:6px;padding:8px;font:inherit;width:100%}input[type=checkbox]{width:auto}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}#msg{min-height:20px;font-size:13px}.ok{color:var(--green)}.err{color:var(--red)}
</style></head><body><main>
<h1>CYD PC Dashboard</h1><div class="sub" id="sub">&nbsp;</div>
<div class="card"><h2>Screen</h2><img id="shot" alt="Panel screenshot (click to tap)">
<div class="row"><button onclick="shot()">Refresh</button><label class="chk"><input type="checkbox" id="live"> auto-refresh</label>
<span style="color:var(--com);font-size:13px">Click the image to tap the panel.</span></div></div>
<div class="card"><h2>Tabs</h2><div class="row" id="tabs"></div></div>
<div class="card"><h2>Status</h2><div class="grid" id="status"></div></div>
<div class="card"><h2>Settings</h2><form id="f">
<div class="two"><label>Agent IP (auto = find it on the network)<input name="host"></label><label>Agent port<input name="port" type="number" min="1" max="65535"></label></div>
<label>Agent token (only if agent/config.json sets one; blank = keep current, "-" = remove)<input name="token" type="password" autocomplete="off"></label>
<label>Timezone (POSIX TZ, e.g. &lt;-03&gt;3)<input name="tz"></label>
<div class="two"><label>Language (en / pt)<input name="lang"></label><label>Auto-rotate tabs, seconds (0 = off)<input name="autonext" type="number" min="0" max="3600"></label></div>
<label>Tabs in order, first = start tab (clock, pcstats, procs, sysinfo, quotes, youtube)<input name="screens"></label>
<label>Theme<select name="theme"><option value="dracula">Dracula</option><option value="nord">Nord</option><option value="gruvbox">Gruvbox</option><option value="mocha">Catppuccin Mocha</option><option value="tokyonight">Tokyo Night</option><option value="latte">Catppuccin Latte (light)</option></select></label>
<div class="two"><label>Clock (24 or 12 = AM/PM)<input name="clock" type="number" min="12" max="24" step="12"></label><label>Date format (auto, long, dmy, mdy, ymd)<input name="date"></label></div>
<div class="two"><label>Brightness (10-255)<input name="bright" type="number" min="10" max="255"></label><label>Rotation (1 = USB right, 3 = USB left)<input name="rot" type="number" min="1" max="3" step="2"></label></div>
<label class="chk"><input type="checkbox" name="invert"> Invert colors (toggle if the background looks white)</label>
<div class="two"><label>Night mode from<input name="nstart" type="time"></label><label>Night mode until (same = off)<input name="nend" type="time"></label></div>
<div class="two"><label>Alert when CPU % is above (0 = off)<input name="acpu" type="number" min="0" max="100"></label><label>Alert when RAM % is above (0 = off)<input name="aram" type="number" min="0" max="100"></label></div>
<div class="two"><label>Alert when disk % is above (0 = off)<input name="adisk" type="number" min="0" max="100"></label><label class="chk" style="align-self:end"><input type="checkbox" name="ajump"> Jump to the PC tab when an alert starts</label></div>
<label>Night brightness (0 = screen off, a tap wakes it)<input name="nbright" type="number" min="0" max="255"></label>
<label>New web password (blank = keep current; user: admin)<input name="webpass" type="password" autocomplete="new-password"></label>
<div class="row"><button class="primary">Save</button><span id="msg"></span></div></form></div>
<div class="card"><h2>Firmware update</h2>
<p class="sub" style="margin:0 0 12px">Upload the <b>app</b> binary: <code>.pio/build/cyd/firmware.bin</code> or the <code>*-ota.bin</code> file from a GitHub release (not the merged installer image).</p>
<div class="row"><input type="file" id="fw" accept=".bin" style="flex:1"><button class="primary" id="upd">Update</button></div>
<progress id="prog" max="100" value="0" style="width:100%;margin-top:10px;display:none"></progress><div id="umsg" style="font-size:13px;margin-top:6px"></div></div>
<div class="row"><button class="danger" onclick="if(confirm('Reboot the panel?'))post('/api/reboot')">Reboot</button></div>
</main><script>
const $=s=>document.querySelector(s),f=$('#f');let timer;
const post=(u,b)=>fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''});
function shot(){$('#shot').src='/screenshot.bmp?t='+Date.now()}
$('#shot').onload=()=>{if($('#live').checked){clearTimeout(timer);timer=setTimeout(shot,1500)}};
$('#live').onchange=()=>{if($('#live').checked)shot()};
$('#shot').onclick=e=>{const r=e.target.getBoundingClientRect();
 post('/api/touch?x='+Math.round((e.clientX-r.left)*320/r.width)+'&y='+Math.round((e.clientY-r.top)*240/r.height)).then(()=>setTimeout(()=>{shot();status()},500))};
function fmtUp(s){const d=s/86400|0,h=s/3600%24|0,m=s/60%60|0;return(d?d+'d ':'')+h+'h '+m+'m'}
async function status(){const s=await(await fetch('/api/status')).json();
 $('#sub').textContent=s.version+' · '+s.ip+' · '+(s.time||'no time yet');
 $('#tabs').innerHTML='';s.tabs.forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;if(i==s.tab_index)b.className='on';
  b.onclick=()=>post('/api/tab?index='+i).then(()=>setTimeout(()=>{shot();status()},400));$('#tabs').appendChild(b)});
 const g={'WiFi signal':s.rssi+' dBm','Uptime':fmtUp(s.uptime_s),'Free heap':(s.heap/1024).toFixed(0)+' KB','Backlight':s.backlight,'Night mode':s.night_active?'active':'inactive','Alerts':Object.entries(s.alerts).filter(([k,a])=>a.active).map(([k,a])=>'<span class="err">'+k.toUpperCase()+' '+Math.round(a.value)+'%</span>').join(', ')||'none','Agent':s.agent+(s.agent_auto?' (auto)':''),'Agents on network':s.agents_seen.map(a=>a.name+' '+a.ip+(a.auth?' (token)':'')).join(', ')||'none'}
 $('#status').innerHTML=Object.entries(g).map(([k,v])=>'<div><b>'+k+'</b>'+v+'</div>').join('')}
async function load(){const s=await(await fetch('/api/settings')).json();
 for(const[k,v]of Object.entries(s)){const el=f.elements[k];if(!el)continue;if(el.type=='checkbox')el.checked=v=='1';else el.value=v}}
f.onsubmit=async e=>{e.preventDefault();const b={};for(const el of f.elements){if(!el.name)continue;
 if(el.type=='checkbox')b[el.name]=el.checked?'1':'0';else if((el.name!='webpass'&&el.name!='token')||el.value)b[el.name]=el.value=='-'&&el.name=='token'?'':el.value}
 const r=await(await post('/api/settings',b)).json();
 $('#msg').className=r.rejected.length?'err':'ok';$('#msg').textContent=r.rejected.length?'Invalid: '+r.rejected.join(', '):'Saved and applied';
 f.elements.webpass.value='';f.elements.token.value='';load();status();setTimeout(shot,600)};
$('#upd').onclick=()=>{const file=$('#fw').files[0];if(!file)return;const fd=new FormData();fd.append('firmware',file);
 const x=new XMLHttpRequest(),p=$('#prog'),m=$('#umsg');p.style.display='block';m.className='';m.textContent='Uploading...';
 x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded*100/e.total};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
  if(x.status==200){m.className='ok';m.textContent='Updated to the new firmware, rebooting...';setTimeout(()=>location.reload(),9000)}
  else{m.className='err';m.textContent='Failed: '+(r.error||x.status)}};
 x.onerror=()=>{m.className='err';m.textContent='Upload error'};
 x.open('POST','/api/update?size='+file.size);x.send(fd)};
status();load();shot();setInterval(status,5000);
</script></body></html>)HTML";

// Exige senha se houver uma configurada. Retorna false (e já respondeu 401) se não autenticou.
inline bool authed() {
    const char* pass = settings::get().webPass;
    if (!*pass || server.authenticate("admin", pass)) return true;
    server.requestAuthentication(BASIC_AUTH, "CYD PC Dashboard");
    return false;
}

inline void sendJson(JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    server.send(code, "application/json", out);
}

inline void fillSettings(JsonObject obj) {
    char buf[100];
    for (const char* key : settings::KEYS) {
        if (settings::getField(key, buf, sizeof(buf))) obj[key] = buf;
    }
    obj["webpass_set"] = settings::get().webPass[0] != '\0';
    obj["token_set"] = settings::get().agentToken[0] != '\0';
}

inline void handleStatus() {
    if (!authed()) return;
    JsonDocument doc;
    doc["version"] = FW_VERSION;
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime_s"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    struct tm t;
    if (getLocalTime(&t, 0)) {
        char buf[20];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
        doc["time"] = buf;
    } else {
        doc["time"] = nullptr;
    }
    JsonArray tabs = doc["tabs"].to<JsonArray>();
    for (int i = 0; i < manager_->count(); i++) tabs.add(hooks_.screenId(manager_->at(i)));
    doc["tab_index"] = manager_->currentIndex();
    doc["auto_next"] = manager_->autoNext();
    doc["backlight"] = backlight::level();
    doc["night_active"] = backlight::inNightWindow();
    // Alertas: valor mais recente, limite e se está em alerta (sustentado)
    JsonObject al = doc["alerts"].to<JsonObject>();
    static const char* const NAMES[] = {"cpu", "ram", "disk"};
    for (int m = 0; m < alerts::N_METRICS; m++) {
        JsonObject o = al[NAMES[m]].to<JsonObject>();
        o["value"] = alerts::value((alerts::Metric)m);
        o["limit"] = alerts::limit((alerts::Metric)m);
        o["active"] = alerts::active((alerts::Metric)m);
    }
    char host[64], agent[80];
    uint16_t port;
    if (discovery::resolve(host, sizeof(host), port)) snprintf(agent, sizeof(agent), "%s:%u", host, port);
    else strlcpy(agent, "searching...", sizeof(agent));
    doc["agent"] = agent;
    doc["agent_auto"] = discovery::isAuto();
    // Agentes que anunciaram na rede (pra ajudar a escolher um IP fixo)
    JsonArray seen = doc["agents_seen"].to<JsonArray>();
    for (int i = 0; i < discovery::count(); i++) {
        const discovery::Agent& a = discovery::at(i);
        JsonObject o = seen.add<JsonObject>();
        o["ip"] = a.ip.toString();
        o["port"] = a.port;
        o["name"] = a.name;
        o["auth"] = a.auth;
        o["age_s"] = (millis() - a.lastSeenMs) / 1000;
    }
    sendJson(doc);
}

inline void handleGetSettings() {
    if (!authed()) return;
    JsonDocument doc;
    fillSettings(doc.to<JsonObject>());
    sendJson(doc);
}

inline void handlePostSettings() {
    if (!authed()) return;
    JsonDocument in;
    if (deserializeJson(in, server.arg("plain")) || !in.is<JsonObject>()) {
        server.send(400, "application/json", "{\"error\":\"expected a JSON object\"}");
        return;
    }
    JsonDocument out;
    JsonArray rejected = out["rejected"].to<JsonArray>();
    for (JsonPair kv : in.as<JsonObject>()) {
        // Aceita string, número ou bool — tudo vira texto pro setField
        char val[100];
        if (kv.value().is<const char*>()) strlcpy(val, kv.value().as<const char*>(), sizeof(val));
        else serializeJson(kv.value(), val, sizeof(val));
        if (!settings::setField(kv.key().c_str(), val)) rejected.add(kv.key().c_str());
    }
    settings::save();
    if (hooks_.applySettings) hooks_.applySettings();
    out["ok"] = rejected.size() == 0;
    fillSettings(out["settings"].to<JsonObject>());
    sendJson(out, rejected.size() ? 400 : 200);
}

inline void handleTab() {
    if (!authed()) return;
    if (server.hasArg("next")) {
        manager_->nextScreen();
    } else if (server.hasArg("index")) {
        manager_->goTo(server.arg("index").toInt());
    } else if (server.hasArg("id")) {
        for (int i = 0; i < manager_->count(); i++) {
            if (server.arg("id").equalsIgnoreCase(hooks_.screenId(manager_->at(i)))) manager_->goTo(i);
        }
    }
    server.send(200, "application/json", "{\"ok\":true}");
}

inline void handleTouch() {
    if (!authed()) return;
    int x = constrain(server.arg("x").toInt(), 0, 319);
    int y = constrain(server.arg("y").toInt(), 0, 239);
    hooks_.touch(x, y);
    server.send(200, "application/json", "{\"ok\":true}");
}

// BMP 24 bits, linhas de baixo pra cima. O readRect devolve RGB565 com os
// bytes trocados (mesmo formato do pushImage), daí o swap.
inline void handleScreenshot() {
    if (!authed()) return;
    const int w = tft_->width(), h = tft_->height();
    const int rowBytes = w * 3;  // 960, já múltiplo de 4
    const uint32_t dataSize = rowBytes * h, fileSize = 54 + dataSize;
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; i++) hdr[off + i] = v >> (8 * i); };
    put32(2, fileSize);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    hdr[26] = 1;
    hdr[28] = 24;
    put32(34, dataSize);

    server.setContentLength(fileSize);
    server.send(200, "image/bmp", "");
    server.sendContent((const char*)hdr, sizeof(hdr));
    static uint16_t line[320];
    static uint8_t row[320 * 3];
    for (int y = h - 1; y >= 0; y--) {
        tft_->readRect(0, y, w, 1, line);
        for (int x = 0; x < w; x++) {
            uint16_t c = (line[x] >> 8) | (line[x] << 8);
            uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
            row[x * 3 + 0] = (b << 3) | (b >> 2);
            row[x * 3 + 1] = (g << 2) | (g >> 4);
            row[x * 3 + 2] = (r << 3) | (r >> 2);
        }
        server.sendContent((const char*)row, rowBytes);
    }
}

// ===== OTA =====
static bool otaRejected_ = false;  // sem senha válida: ignora o corpo do upload
static String otaError_;
static size_t otaExpected_ = 0;
static int otaLastPct_ = -1;

inline void otaScreen(const char* title, uint16_t color) {
    tft_->fillScreen(THEME_BG);
    tft_->setTextDatum(MC_DATUM);
    tft_->setTextColor(color, THEME_BG);
    tft_->drawString(title, 160, 100, 4);
}

inline void otaProgress(size_t written) {
    int pct = otaExpected_ ? (int)(written * 100 / otaExpected_) : -1;
    if (otaExpected_ && pct == otaLastPct_) return;
    otaLastPct_ = pct;
    char buf[24];
    if (pct >= 0) {
        theme::drawBar(*tft_, 60, 130, 200, 10, pct, THEME_PURPLE, THEME_BG);
        snprintf(buf, sizeof(buf), "%d%%", pct);
    } else {
        snprintf(buf, sizeof(buf), "%u KB", (unsigned)(written / 1024));
    }
    theme::drawValue(*tft_, 110, 150, 100, 18, buf, 2, THEME_COMMENT, THEME_BG, MC_DATUM);
}

inline void handleUpdateUpload() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        const char* pass = settings::get().webPass;
        otaRejected_ = *pass && !server.authenticate("admin", pass);
        otaError_ = "";
        otaLastPct_ = -1;
        otaExpected_ = server.hasArg("size") ? server.arg("size").toInt() : 0;
        if (otaRejected_) return;
        Serial.printf("ota: recebendo %s\n", up.filename.c_str());
        otaScreen(i18n::tr().OTA_UPDATING, THEME_FG);
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) otaError_ = Update.errorString();
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (otaRejected_ || otaError_.length()) return;
        // Imagem de app começa com o magic 0xE9; o binário "merged" do instalador
        // começa com 0xFF (padding antes do bootloader) e quebraria o boot
        if (up.totalSize == 0 && up.buf[0] != 0xE9) {
            otaError_ = "not an app image (use firmware.bin / *-ota.bin, not the merged installer .bin)";
            Update.abort();
            return;
        }
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            otaError_ = Update.errorString();
            Update.abort();
            return;
        }
        otaProgress(up.totalSize + up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        if (otaRejected_ || otaError_.length()) return;
        if (!Update.end(true)) otaError_ = Update.errorString();
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        otaError_ = "upload aborted";
    }
}

inline void handleUpdateDone() {
    if (otaRejected_) {
        server.requestAuthentication(BASIC_AUTH, "CYD PC Dashboard");
        return;
    }
    if (otaError_.length() || !Update.isFinished()) {
        if (!otaError_.length()) otaError_ = "no firmware received";
        Serial.printf("ota: falhou: %s\n", otaError_.c_str());
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = otaError_;
        sendJson(doc, 400);
        otaScreen(i18n::tr().OTA_FAILED, THEME_RED);
        delay(1500);
        if (hooks_.redraw) hooks_.redraw();
        return;
    }
    server.send(200, "application/json", "{\"ok\":true}");
    otaScreen(i18n::tr().OTA_DONE, THEME_GREEN);
    Serial.println("ota: ok, reiniciando");
    delay(800);
    ESP.restart();
}

inline void begin(TFT_eSPI& tft, ScreenManager& manager, const Hooks& hooks) {
    tft_ = &tft;
    manager_ = &manager;
    hooks_ = hooks;

    server.on("/", HTTP_GET, []() {
        if (!authed()) return;
        server.send_P(200, "text/html", PAGE);
    });
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/settings", HTTP_GET, handleGetSettings);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/tab", HTTP_POST, handleTab);
    server.on("/api/touch", HTTP_POST, handleTouch);
    server.on("/api/reboot", HTTP_POST, []() {
        if (!authed()) return;
        server.send(200, "application/json", "{\"ok\":true}");
        delay(200);
        ESP.restart();
    });
    server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    server.begin();
    started_ = true;
}

// Chamado no loop(). O mDNS só sobe com WiFi conectado (pode conectar depois do boot).
inline void poll() {
    if (!started_) return;
    if (!mdnsStarted_ && WiFi.status() == WL_CONNECTED) {
        if (MDNS.begin(MDNS_NAME)) {
            MDNS.addService("http", "tcp", WEB_PORT);
            Serial.printf("web: http://%s/ ou http://%s.local/\n", WiFi.localIP().toString().c_str(), MDNS_NAME);
        }
        mdnsStarted_ = true;
    }
    server.handleClient();
}

} // namespace webui
