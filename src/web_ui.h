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
#include <errno.h>
#include "config.h"
#include "settings.h"
#include "backlight.h"
#include "screens.h"
#include "i18n.h"
#include "discovery.h"
#include "alerts.h"
#include "notify.h"
#include "ota_guard.h"
#include "ambient.h"
#include "status_led.h"
#include "theme.h"

// Interface web pós-boot: http://<ip-do-painel>/ ou http://cyd-dashboard.local/
//
//   GET  /                 página (status, screenshot clicável, abas, configurações)
//   GET  /api/status       estado em JSON (versão, IP, aba atual, brilho, modo noturno...)
//   GET  /api/settings     configurações atuais (sem os segredos: só "<chave>_set")
//   GET  /api/schema       descrição dos campos (o formulário da página é gerado disso)
//   POST /api/settings     JSON parcial {"bright": 120, "screens": "pcstats,quotes"} —
//                          valida (settings::setField), salva no NVS e aplica na hora
//   POST /api/tab          ?id=pcstats | ?index=2 | ?next=1
//   POST /api/touch        ?x=..&y=.. (simula um toque, mesmo caminho do touch físico)
//   POST /api/notify       {"text", "title", "level": info|ok|warn|error, "duration": s}
//                          (JSON ou form: curl -d text=Oi http://cyd-dashboard.local/api/notify)
//   POST /api/led          ?color=ff0000&seconds=3 — acende o LED de status (testar a placa)
//   POST /api/reboot
//   POST /api/update       multipart com o .bin do APP (.pio/build/cyd/firmware.bin ou o
//                          *-ota.bin da release) — grava no outro slot OTA e reinicia.
//                          ?size=<bytes> opcional, só pra barra de progresso no display.
//                          curl -F firmware=@firmware.bin http://cyd-dashboard.local/api/update
//   GET  /screenshot.bmp   framebuffer atual (BMP 24 bits)
//   GET  /theme.css        variáveis CSS do tema atual (a página segue o tema do painel)
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
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.fgrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:12px;margin-bottom:6px}.fgrid .wide{grid-column:1/-1}
h3{font-size:13px;color:var(--fg);margin:14px 0 8px;text-transform:uppercase;letter-spacing:.04em}#msg{min-height:20px;font-size:13px}.ok{color:var(--green)}.err{color:var(--red)}
</style><link rel="stylesheet" id="themecss" href="/theme.css"></head><body><main>
<h1>CYD PC Dashboard</h1><div class="sub" id="sub">&nbsp;</div>
<div class="card"><h2>Screen</h2><img id="shot" alt="Panel screenshot (click to tap)">
<div class="row"><button onclick="shot()">Refresh</button><label class="chk"><input type="checkbox" id="live"> auto-refresh</label>
<span style="color:var(--com);font-size:13px">Click the image to tap the panel.</span></div></div>
<div class="card"><h2>Tabs</h2><div class="row" id="tabs"></div></div>
<div class="card"><h2>Status</h2><div class="grid" id="status"></div></div>
<div class="card"><h2>Settings</h2><form id="f"><div id="fields"></div>
<div class="row"><button class="primary">Save</button><span id="msg"></span></div></form></div>
<div class="card"><h2>Firmware update</h2>
<p class="sub" style="margin:0 0 12px">Upload the <b>app</b> binary: <code>.pio/build/cyd/firmware.bin</code> or the <code>*-ota.bin</code> file from a GitHub release (not the merged installer image).</p>
<div class="row"><input type="file" id="fw" accept=".bin" style="flex:1"><button class="primary" id="upd">Update</button></div>
<progress id="prog" max="100" value="0" style="width:100%;margin-top:10px;display:none"></progress><div id="umsg" style="font-size:13px;margin-top:6px"></div></div>
<div class="card"><h2>Send notification</h2><form id="nf">
<div class="two"><label>Title (optional)<input name="title" maxlength="47"></label><label>Level<select name="level"><option>info</option><option>ok</option><option>warn</option><option>error</option></select></label></div>
<label>Text<input name="text" maxlength="239" required></label>
<div class="row"><button class="primary">Send</button><span id="nmsg"></span></div></form></div>
<div class="row"><button class="danger" onclick="if(confirm('Reboot the panel?'))post('/api/reboot')">Reboot</button></div>
</main><script>
const $=s=>document.querySelector(s),f=$('#f');let timer;
const post=(u,b)=>fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:b?JSON.stringify(b):''});
// O servidor do painel atende uma conexão por vez e às vezes reseta uma:
// GETs com retentativa e carregamento em sequência (nada de rajada em paralelo)
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
async function getJson(u){for(let i=0;i<3;i++){try{const r=await fetch(u,{cache:'no-store'});if(r.ok)return await r.json()}catch(e){}await sleep(400)}throw new Error(u)}
async function css(){const l=$('#themecss');for(let i=0;i<3;i++){
 const ok=await new Promise(r=>{l.onload=()=>r(1);l.onerror=()=>r(0);l.href='/theme.css?t='+Date.now()});if(ok)return;await sleep(400)}}
function shot(){$('#shot').src='/screenshot.bmp?t='+Date.now()}
$('#shot').onload=()=>{if($('#live').checked){clearTimeout(timer);timer=setTimeout(shot,1500)}};
$('#live').onchange=()=>{if($('#live').checked)shot()};
$('#shot').onclick=e=>{const r=e.target.getBoundingClientRect();
 post('/api/touch?x='+Math.round((e.clientX-r.left)*320/r.width)+'&y='+Math.round((e.clientY-r.top)*240/r.height)).then(()=>setTimeout(()=>{shot();status()},500))};
function fmtUp(s){const d=s/86400|0,h=s/3600%24|0,m=s/60%60|0;return(d?d+'d ':'')+h+'h '+m+'m'}
async function status(){const s=await getJson('/api/status');
 $('#sub').textContent=s.version+' · '+s.ip+' · '+(s.time||'no time yet');
 $('#tabs').innerHTML='';s.tabs.forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;if(i==s.tab_index)b.className='on';
  b.onclick=()=>post('/api/tab?index='+i).then(()=>setTimeout(()=>{shot();status()},400));$('#tabs').appendChild(b)});
 const g={'Firmware':s.version+' ('+s.ota.slot+(s.ota.pending_verify?', verifying':'')+')'+(s.ota.rolled_back_from?' <span class="err">rolled back from '+s.ota.rolled_back_from+'</span>':''),'WiFi signal':s.rssi+' dBm','Uptime':fmtUp(s.uptime_s),'Free heap':(s.heap/1024).toFixed(0)+' KB','Backlight':s.backlight,'Night mode':s.night_active?'active':'inactive','Alerts':Object.entries(s.alerts).filter(([k,a])=>a.active).map(([k,a])=>'<span class="err">'+k.toUpperCase()+' '+Math.round(a.value)+(k=='temp'?'°C':'%')+'</span>').join(', ')||'none','Agent':s.agent+(s.agent_auto?' (auto)':''),'Agents on network':s.agents_seen.map(a=>a.name+' '+a.ip+(a.auth?' (token)':'')).join(', ')||'none'}
 $('#status').innerHTML=Object.entries(g).map(([k,v])=>'<div><b>'+k+'</b>'+v+'</div>').join('')}
// Formulário gerado de /api/schema (= tabela settings::FIELDS do firmware)
let schema=[];
function mk(t,a){const e=document.createElement(t);Object.assign(e,a||{});return e}
async function buildForm(){schema=await getJson('/api/schema');const box=$('#fields');box.innerHTML='';let grid,group;
 for(const fd of schema){if(fd.group!==group){group=fd.group;box.append(mk('h3',{textContent:group}));grid=mk('div',{className:'fgrid'});box.append(grid)}
  let lab,inp;
  if(fd.type=='bool'){lab=mk('label',{className:'chk wide'});inp=mk('input',{type:'checkbox',name:fd.key});lab.append(inp,' '+fd.label)}
  else{lab=mk('label',{textContent:fd.label});
   if(fd.type=='enum'){inp=mk('select',{name:fd.key});for(const o of fd.options)inp.append(mk('option',{value:o.value,textContent:o.label}))}
   else if(fd.type=='number')inp=mk('input',{type:'number',name:fd.key,min:fd.min,max:fd.max});
   else if(fd.type=='time')inp=mk('input',{type:'time',name:fd.key});
   else inp=mk('input',{type:fd.secret?'password':'text',name:fd.key,maxLength:fd.maxlen,autocomplete:fd.secret?'new-password':'off'});
   if(fd.secret)inp.dataset.secret=1;
   lab.append(inp);if(fd.type=='text'&&fd.maxlen>40)lab.classList.add('wide')}
  grid.append(lab)}}
async function load(){if(!schema.length)await buildForm();const s=await getJson('/api/settings');
 for(const fd of schema){const el=f.elements[fd.key];
  if(fd.secret){el.value='';el.placeholder=s[fd.key+'_set']?'set (blank = keep, "-" = remove)':'not set';continue}
  if(fd.type=='bool')el.checked=s[fd.key]=='1';else el.value=s[fd.key]}}
f.onsubmit=async e=>{e.preventDefault();const b={};for(const fd of schema){const el=f.elements[fd.key];
 if(fd.type=='bool')b[fd.key]=el.checked?'1':'0';
 else if(fd.secret){if(el.value)b[fd.key]=el.value=='-'?'':el.value}  // segredo: branco = manter, "-" = apagar
 else b[fd.key]=el.value}
 const r=await(await post('/api/settings',b)).json();
 $('#msg').className=r.rejected.length?'err':'ok';$('#msg').textContent=r.rejected.length?'Invalid: '+r.rejected.join(', '):'Saved and applied';

 await css();await load();await status();shot()};
$('#nf').onsubmit=async e=>{e.preventDefault();const n=$('#nf').elements;
 const r=await post('/api/notify',{title:n.title.value,text:n.text.value,level:n.level.value});
 $('#nmsg').className=r.ok?'ok':'err';$('#nmsg').textContent=r.ok?'Sent':'Failed';if(r.ok)n.text.value='';setTimeout(shot,800)};
$('#upd').onclick=()=>{const file=$('#fw').files[0];if(!file)return;const fd=new FormData();fd.append('firmware',file);
 const x=new XMLHttpRequest(),p=$('#prog'),m=$('#umsg');p.style.display='block';m.className='';m.textContent='Uploading...';
 x.upload.onprogress=e=>{if(e.lengthComputable)p.value=e.loaded*100/e.total};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}
  if(x.status==200){m.className='ok';m.textContent='Updated to the new firmware, rebooting...';setTimeout(()=>location.reload(),9000)}
  else{m.className='err';m.textContent='Failed: '+(r.error||x.status)}};
 x.onerror=()=>{m.className='err';m.textContent='Upload error'};
 x.open('POST','/api/update?size='+file.size);x.send(fd)};
(async()=>{await css();await load().catch(()=>{});await status().catch(()=>{});shot();
 setInterval(()=>status().catch(()=>{}),5000)})();
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

// Valores atuais. Segredos (senha da web, token do agente) nunca saem: só
// "<chave>_set" dizendo se estão definidos.
inline void fillSettings(JsonObject obj) {
    char buf[100];
    for (const settings::Field& f : settings::FIELDS) {
        if (f.flags & settings::SECRET) {
            obj[String(f.key) + "_set"] = settings::isSet(f);
        } else if (settings::getField(f, buf, sizeof(buf))) {
            obj[f.key] = buf;
        }
    }
}

// Descrição dos campos pro formulário da página (gerado da tabela settings::FIELDS)
inline void handleSchema() {
    if (!authed()) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const settings::Field& f : settings::FIELDS) {
        JsonObject o = arr.add<JsonObject>();
        o["key"] = f.key;
        o["label"] = f.label;
        o["group"] = f.group;
        switch (f.type) {
            case settings::STR:
                o["type"] = "text";
                o["maxlen"] = f.size - 1;
                if (f.flags & settings::SECRET) o["secret"] = true;
                break;
            case settings::U8:
            case settings::U16:
                o["type"] = "number";
                o["min"] = f.min;
                o["max"] = f.max;
                break;
            case settings::BOOL: o["type"] = "bool"; break;
            case settings::TIME: o["type"] = "time"; break;
            case settings::ENUM: {
                o["type"] = "enum";
                JsonArray opts = o["options"].to<JsonArray>();
                for (int k = 0; k < f.nNames; k++) {
                    JsonObject op = opts.add<JsonObject>();
                    op["value"] = f.names[k];
                    op["label"] = f.optionLabels ? f.optionLabels[k] : f.names[k];
                }
                break;
            }
        }
    }
    sendJson(doc);
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
    JsonObject ota = doc["ota"].to<JsonObject>();
    ota["slot"] = otaguard::runningSlot();
    ota["pending_verify"] = otaguard::pending();
    if (otaguard::failedVersion()[0]) ota["rolled_back_from"] = otaguard::failedVersion();
    JsonObject amb = doc["ambient"].to<JsonObject>();
    amb["raw"] = ambient::raw();             // leitura crua do sensor (calibração)
    amb["level"] = ambient::level();         // 0 = escuro, 1 = claro
    JsonObject nt = doc["notifications"].to<JsonObject>();
    nt["showing"] = notify::showing();
    nt["pending"] = notify::pending();
    nt["total"] = notify::total();
    // Alertas: valor mais recente, limite e se está em alerta (sustentado)
    JsonObject al = doc["alerts"].to<JsonObject>();
    static const char* const NAMES[] = {"cpu", "ram", "disk", "temp"};
    for (int m = 0; m < alerts::N_METRICS; m++) {
        JsonObject o = al[NAMES[m]].to<JsonObject>();
        float v = alerts::value((alerts::Metric)m);
        if (isnan(v)) o["value"] = nullptr; else o["value"] = v;
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

// :root com as cores do tema atual, sobrescrevendo os defaults (Dracula) do
// <style> da página. color-scheme light/dark pelo brilho do fundo, pros campos
// de formulário do navegador acompanharem.
inline void handleThemeCss() {
    if (!authed()) return;
    static const char* const VARS[] = {"bg", "card", "cur", "fg", "com", "cyan",
                                       "green", "orange", "pink", "purple", "red", "yellow"};
    const uint32_t* h = theme::paletteHex();
    uint32_t bg = h[0];
    int luma = ((bg >> 16 & 0xFF) * 299 + (bg >> 8 & 0xFF) * 587 + (bg & 0xFF) * 114) / 1000;
    String css = ":root{color-scheme:";
    css += luma > 128 ? "light" : "dark";
    char buf[24];
    for (int i = 0; i < 12; i++) {
        snprintf(buf, sizeof(buf), ";--%s:#%06x", VARS[i], (unsigned)h[i]);
        css += buf;
    }
    css += "}";
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/css", css);
}

// Aceita JSON {"text", "title", "level", "duration"} ou campos de form/query
// (text=...&level=ok) — o curl mais curto possível também funciona.
inline void handleNotify() {
    if (!authed()) return;
    JsonDocument in;
    String body = server.arg("plain");
    bool json = body.length() && body[0] == '{' && !deserializeJson(in, body);
    auto field = [&](const char* k) -> String {
        if (json) return in[k].is<const char*>() ? String(in[k].as<const char*>()) : in[k].isNull() ? String() : in[k].as<String>();
        return server.arg(k);
    };
    String text = field("text"), title = field("title");
    if (!text.length()) text = field("message");
    if (!text.length() && !title.length()) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"text is required\"}");
        return;
    }
    String dur = field("duration");
    long seconds = dur.length() ? dur.toInt() : NOTIFY_DEFAULT_S;
    notify::push(title.c_str(), text.c_str(), notify::parseLevel(field("level").c_str()),
                 (uint16_t)constrain(seconds, 0L, 3600L));
    JsonDocument out;
    out["ok"] = true;
    out["pending"] = notify::pending() + (notify::showing() ? 1 : 0);
    sendJson(out);
}

// Acende o LED de status numa cor por alguns segundos — pra conferir se o LED
// da placa está nos pinos/polaridade esperados (LED_*_PIN, LED_ACTIVE_LOW)
inline void handleLed() {
    if (!authed()) return;
    String c = server.arg("color");
    if (c.startsWith("#")) c = c.substring(1);
    uint32_t rgb = c.length() ? strtoul(c.c_str(), nullptr, 16) : 0xFFFFFF;
    long seconds = server.hasArg("seconds") ? server.arg("seconds").toInt() : 3;
    statusled::setOverride(rgb, (unsigned long)constrain(seconds, 1L, 60L) * 1000UL);
    server.send(200, "application/json", "{\"ok\":true}");
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
        statusled::setOverride(0x8000FF, 0);  // roxo até reiniciar (ou falhar)
        statusled::update();
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
        statusled::clearOverride();
        delay(1500);
        if (hooks_.redraw) hooks_.redraw();
        return;
    }
    otaguard::onUpdated();
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
    server.on("/api/schema", HTTP_GET, handleSchema);
    server.on("/api/settings", HTTP_POST, handlePostSettings);
    server.on("/api/tab", HTTP_POST, handleTab);
    server.on("/api/led", HTTP_POST, handleLed);
    server.on("/api/notify", HTTP_POST, handleNotify);
    server.on("/api/touch", HTTP_POST, handleTouch);
    server.on("/api/reboot", HTTP_POST, []() {
        if (!authed()) return;
        server.send(200, "application/json", "{\"ok\":true}");
        delay(200);
        ESP.restart();
    });
    server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
    server.on("/screenshot.bmp", HTTP_GET, handleScreenshot);
    server.on("/theme.css", HTTP_GET, handleThemeCss);
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
    // Bug do arduino-esp32 2.0.x: WiFiClient::connected() faz recv() de 0 bytes
    // (que não define errno) e decide pelo errno GLOBAL. Um ENOTCONN/ECONNRESET
    // que sobrou do HTTPClient (fetch do agente) faz o WebServer achar que o
    // cliente novo caiu e fechar o socket com o pedido não lido -> RST no
    // navegador. Zerar antes de atender evita (era ~4% dos requests na aba PC).
    errno = 0;
    server.handleClient();
}

} // namespace webui
