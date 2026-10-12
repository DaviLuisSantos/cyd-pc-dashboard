#pragma once
#include <WiFi.h>
#include <WiFiManager.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "settings.h"
#include "i18n.h"
#include "theme.h"

// Conexão WiFi + portal de configuração (WiFiManager).
//
// Ordem de tentativa:
//   1. credenciais de compilação (WIFI_SSID no user_config.h), se houver
//   2. credenciais salvas pelo portal
//   3. nada funcionou -> sobe o AP "CYD-Dashboard" com o portal em 192.168.4.1,
//      onde dá pra escolher a rede e editar IP/porta do agente, fuso e idioma
//
// O portal também abre se o usuário segurar a tela durante o boot (forcePortal).
namespace wifisetup {

static const char* AP_NAME = "CYD-Dashboard";
static const unsigned long PORTAL_TIMEOUT_S = 180;  // com rede já salva, desiste e segue offline

inline void drawPortalScreen(TFT_eSPI& tft) {
    const i18n::Lang& L = i18n::tr();
    tft.fillScreen(DRACULA_BG);
    theme::iconNet(tft, 160, 34, 14, DRACULA_PURPLE);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_FG, DRACULA_BG);
    tft.drawString(L.SETUP_TITLE, 160, 68, 4);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    tft.drawString(L.SETUP_STEP_1, 24, 104, 2);
    theme::drawCard(tft, 24, 116, 272, 30);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_CYAN, DRACULA_CARD);
    tft.drawString(AP_NAME, 160, 131, 4);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    tft.drawString(L.SETUP_STEP_2, 24, 168, 2);
    tft.drawString(L.SETUP_STEP_3, 24, 190, 2);
}

inline bool waitConnected(unsigned long timeoutMs) {
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
        delay(250);
    }
    return WiFi.status() == WL_CONNECTED;
}

// Retorna true se conectou. Em false o painel segue offline (as telas mostram
// "sem wifi") e o ESP continua tentando reconectar sozinho em background.
inline bool connect(TFT_eSPI& tft, bool forcePortal) {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    if (!forcePortal && strlen(WIFI_SSID) > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        if (waitConnected(WIFI_CONNECT_TIMEOUT_MS)) return true;
    }

    settings::Settings& s = settings::get();
    char portBuf[6];
    snprintf(portBuf, sizeof(portBuf), "%u", s.agentPort);
    const char* langCode = (s.lang == LANG_PT_BR) ? "pt" : "en";

    WiFiManager wm;
    WiFiManagerParameter pHost("host", "Agent IP (PC running pc_stats_agent.py)", s.agentHost, sizeof(s.agentHost) - 1);
    WiFiManagerParameter pPort("port", "Agent port", portBuf, 5);
    WiFiManagerParameter pTz("tz", "Timezone (POSIX TZ, e.g. &lt;-03&gt;3 or EST5EDT,M3.2.0,M11.1.0)", s.tz, sizeof(s.tz) - 1);
    WiFiManagerParameter pLang("lang", "Language: en / pt", langCode, 2);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.addParameter(&pTz);
    wm.addParameter(&pLang);

    bool paramsSaved = false;
    wm.setSaveParamsCallback([&paramsSaved]() { paramsSaved = true; });
    wm.setSaveConfigCallback([&paramsSaved]() { paramsSaved = true; });
    wm.setAPCallback([&tft](WiFiManager*) { drawPortalScreen(tft); });
    wm.setTitle("CYD PC Dashboard");
    wm.setDarkMode(true);
    wm.setShowInfoUpdate(false);  // sem OTA pelo portal
    // Sem rede salva: espera o usuário pra sempre. Com rede salva (ex.: roteador
    // fora do ar), desiste depois de um tempo e segue offline.
    wm.setConfigPortalTimeout(wm.getWiFiIsSaved() ? PORTAL_TIMEOUT_S : 0);

    bool ok = forcePortal ? wm.startConfigPortal(AP_NAME) : wm.autoConnect(AP_NAME);

    if (paramsSaved) {
        strlcpy(s.agentHost, pHost.getValue(), sizeof(s.agentHost));
        int port = atoi(pPort.getValue());
        if (port > 0 && port < 65536) s.agentPort = (uint16_t)port;
        strlcpy(s.tz, pTz.getValue(), sizeof(s.tz));
        s.lang = (strncmp(pLang.getValue(), "pt", 2) == 0) ? LANG_PT_BR : LANG_EN;
        settings::save();
    }

    if (!ok) WiFi.begin();  // segue tentando a rede salva em background
    return ok;
}

} // namespace wifisetup
