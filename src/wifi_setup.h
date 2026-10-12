#pragma once
#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "settings.h"
#include "i18n.h"
#include "theme.h"
#include "improv.h"

// Conexão WiFi + portal de configuração (WiFiManager).
//
// Ordem de tentativa:
//   1. credenciais de compilação (WIFI_SSID no user_config.h), se houver
//   2. credenciais salvas pelo portal
//   3. nada funcionou -> sobe o AP "CYD-Dashboard" com o portal em 192.168.4.1,
//      onde dá pra escolher a rede e editar IP/porta do agente, fuso, idioma,
//      abas (quais e em que ordem), auto-next, brilho, rotação, inversão de cores
//      e modo noturno
//
// O portal também abre se o usuário segurar a tela durante o boot (forcePortal).
// Em todas as esperas (conexão, portal) a serial atende o Improv: o instalador
// web pode mandar a rede direto, sem o portal (ver improv.h).
namespace wifisetup {

static const char* AP_NAME = "CYD-Dashboard";
static const unsigned long PORTAL_TIMEOUT_S = 180;  // com rede já salva, desiste e segue offline

inline void drawPortalScreen(TFT_eSPI& tft) {
    const i18n::Lang& L = i18n::tr();
    tft.fillScreen(THEME_BG);
    theme::iconNet(tft, 160, 34, 14, THEME_PURPLE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(THEME_FG, THEME_BG);
    tft.drawString(L.SETUP_TITLE, 160, 68, 4);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(THEME_COMMENT, THEME_BG);
    tft.drawString(L.SETUP_STEP_1, 24, 104, 2);
    theme::drawCard(tft, 24, 116, 272, 30);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(THEME_CYAN, THEME_CARD);
    tft.drawString(AP_NAME, 160, 131, 4);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(THEME_COMMENT, THEME_BG);
    tft.drawString(L.SETUP_STEP_2, 24, 168, 2);
    tft.drawString(L.SETUP_STEP_3, 24, 190, 2);
}

// pollImprov=false quando quem espera já é um comando do Improv (sem reentrar)
inline bool waitConnected(unsigned long timeoutMs, bool pollImprov = true) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        for (int i = 0; i < 25; i++) {  // 250ms, atendendo a serial no meio
            if (pollImprov) improv::poll();
            delay(10);
        }
    }
    return WiFi.status() == WL_CONNECTED;
}

// Improv depois do boot (painel já rodando): tenta a rede nova e, se não
// conectar, volta pra anterior. As credenciais ficam salvas pelo próprio WiFi
// do ESP32 (WiFi.persistent), as mesmas que o WiFiManager usa no próximo boot.
inline bool improvConnectRunning(const char* ssid, const char* pass) {
    String oldSsid = WiFi.SSID(), oldPass = WiFi.psk();
    WiFi.begin(ssid, pass);
    if (waitConnected(WIFI_CONNECT_TIMEOUT_MS, false)) return true;
    if (oldSsid.length()) WiFi.begin(oldSsid.c_str(), oldPass.c_str());
    return false;
}

// Retorna true se conectou. Em false o painel segue offline (as telas mostram
// "sem wifi") e o ESP continua tentando reconectar sozinho em background.
inline bool connect(TFT_eSPI& tft, bool forcePortal) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    // Sem modem sleep: com ele o rádio dorme entre os beacons do roteador e
    // conexões de entrada (interface web, OTA) se perdem, ainda mais com sinal
    // fraco. O painel vive na USB, o consumo extra não importa.
    WiFi.setSleep(false);

    improv::setConnectHandler(improvConnectRunning);
    if (!forcePortal && strlen(WIFI_SSID) > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        if (waitConnected(WIFI_CONNECT_TIMEOUT_MS)) return true;
    }

    // Campos do portal — as chaves são as de settings::setField (mesma
    // validação da interface web). Labels/custom precisam viver até o fim do
    // portal, por isso são literais.
    struct Field { const char* key; const char* label; int len; const char* custom; };
    static const Field FIELDS[] = {
        {"host", "Agent IP (PC running pc_stats_agent.py), or auto to find it on the network", 63, ""},
        {"port", "Agent port", 5, "type='number' min='1' max='65535'"},
        {"tz", "Timezone (POSIX TZ, e.g. &lt;-03&gt;3 or EST5EDT,M3.2.0,M11.1.0)", 63, ""},
        {"lang", "Language: en / pt", 2, ""},
        {"screens", "Tabs, in order (first = start tab): clock, pcstats, procs, sysinfo, quotes, youtube", 95, ""},
        {"autonext", "Auto-rotate tabs every N seconds (0 = off)", 5, "type='number' min='0' max='3600'"},
        {"bright", "Brightness (10-255)", 3, "type='number' min='10' max='255'"},
        {"rot", "Rotation: 1 = USB on the right, 3 = USB on the left", 1, "type='number' min='1' max='3' step='2'"},
        {"invert", "Invert colors (try toggling if the background looks white)", 2, nullptr},  // checkbox
        {"nstart", "Night mode from (HH:MM; same as 'until' = off)", 5, "type='time'"},
        {"nend", "Night mode until (HH:MM)", 5, "type='time'"},
        {"nbright", "Night brightness (0 = screen off, a tap wakes it)", 3, "type='number' min='0' max='255'"},
        {"token", "Agent token (only if agent/config.json sets one)", 64, ""},
        {"acpu", "Alert when CPU % is above (0 = off)", 3, "type='number' min='0' max='100'"},
        {"aram", "Alert when RAM % is above (0 = off)", 3, "type='number' min='0' max='100'"},
        {"adisk", "Alert when disk % is above (0 = off)", 3, "type='number' min='0' max='100'"},
        {"atemp", "Alert when CPU temperature (C) is above (0 = off)", 3, "type='number' min='0' max='120'"},
        {"ajump", "Jump to the PC tab when an alert starts", 2, nullptr},  // checkbox
        {"theme", "Theme: dracula, nord, gruvbox, mocha, tokyonight, latte (light)", 10, ""},
        {"clock", "Clock: 24 or 12 (AM/PM)", 2, "type='number' min='12' max='24' step='12'"},
        {"date", "Date format: auto, long, dmy, mdy, ymd", 4, ""},
        {"webpass", "Web interface password (optional; user: admin)", 32, ""},
    };
    static const int N_FIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);

    WiFiManager wm;
    WiFiManagerParameter* params[N_FIELDS];
    for (int i = 0; i < N_FIELDS; i++) {
        const Field& f = FIELDS[i];
        char val[100] = "";
        if (!f.custom) {
            // Checkbox (custom nullptr): marcado envia "1"; desmarcado não envia
            // nada (valor fica vazio, que o setField lê como desligado)
            settings::getField(f.key, val, sizeof(val));
            params[i] = new WiFiManagerParameter(f.key, f.label, "1", f.len,
                                                 !strcmp(val, "1") ? "type='checkbox' checked" : "type='checkbox'", WFM_LABEL_AFTER);
        } else {
            if (!strcmp(f.key, "webpass")) strlcpy(val, settings::get().webPass, sizeof(val));
            else if (!strcmp(f.key, "token")) strlcpy(val, settings::get().agentToken, sizeof(val));
            else settings::getField(f.key, val, sizeof(val));
            params[i] = new WiFiManagerParameter(f.key, f.label, val, f.len, f.custom);
        }
        wm.addParameter(params[i]);
    }

    bool paramsSaved = false;
    wm.setSaveParamsCallback([&paramsSaved]() { paramsSaved = true; });
    wm.setSaveConfigCallback([&paramsSaved]() { paramsSaved = true; });
    wm.setAPCallback([&tft](WiFiManager*) { drawPortalScreen(tft); });
    wm.setTitle("CYD PC Dashboard");
    wm.setDarkMode(true);
    wm.setShowInfoUpdate(false);  // sem OTA pelo portal
    // Não bloqueante: o laço abaixo atende o portal E a serial (Improv)
    wm.setConfigPortalBlocking(false);

    // Improv durante o portal: derruba o AP, tenta a rede recebida e, se não
    // conectar, sobe o portal de novo
    bool improvOk = false;
    improv::setConnectHandler([&wm, &improvOk](const char* ssid, const char* pass) {
        wm.stopConfigPortal();
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, pass);
        improvOk = waitConnected(WIFI_CONNECT_TIMEOUT_MS, false);
        if (!improvOk) wm.startConfigPortal(AP_NAME);
        return improvOk;
    });

    bool ok = forcePortal ? wm.startConfigPortal(AP_NAME) : wm.autoConnect(AP_NAME);
    // Sem rede salva: espera o usuário pra sempre. Com rede salva (ex.: roteador
    // fora do ar), desiste depois de PORTAL_TIMEOUT_S e segue offline.
    const bool hasSaved = wm.getWiFiIsSaved();
    const unsigned long portalStart = millis();
    while (!ok) {
        if (wm.process()) { ok = true; break; }   // rede escolhida no portal conectou
        improv::poll();
        if (improvOk) { ok = true; break; }       // rede veio pelo instalador web
        if (!wm.getConfigPortalActive()) break;   // portal encerrado (timeout interno)
        if (hasSaved && millis() - portalStart > PORTAL_TIMEOUT_S * 1000UL) {
            wm.stopConfigPortal();
            break;
        }
        delay(5);
    }
    improv::setConnectHandler(improvConnectRunning);  // o lambda acima referencia `wm`, que vai sair de escopo

    if (paramsSaved) {
        for (int i = 0; i < N_FIELDS; i++) settings::setField(params[i]->getID(), params[i]->getValue());
        settings::save();
    }
    for (int i = 0; i < N_FIELDS; i++) delete params[i];

    if (!ok) WiFi.begin();  // segue tentando a rede salva em background
    return ok;
}

} // namespace wifisetup
