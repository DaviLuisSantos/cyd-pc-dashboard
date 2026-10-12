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

// Rótulo do campo no portal: texto da tabela com escape de HTML (o fuso tem
// "<-03>3") e, pra ENUM, as opções aceitas ("1 = USB on the right, 3 = ...").
inline String portalLabel(const settings::Field& f) {
    String l = f.label;
    if (f.type == settings::ENUM) {
        l += ": ";
        for (int k = 0; k < f.nNames; k++) {
            if (k) l += ", ";
            l += f.names[k];
            const char* nice = f.optionLabels ? f.optionLabels[k] : nullptr;
            // "Dracula" já começa com "dracula": não repete
            if (nice && strncasecmp(nice, f.names[k], strlen(f.names[k]))) {
                l += " = ";
                l += nice;
            }
        }
    }
    l.replace("&", "&amp;");
    l.replace("<", "&lt;");
    l.replace(">", "&gt;");
    return l;
}

// Campo do portal pra uma linha da tabela. `label`/`attr` têm que viver até o
// fim do portal (o WiFiManager guarda só o ponteiro).
inline WiFiManagerParameter* portalParam(const settings::Field& f, String& label, String& attr) {
    label = portalLabel(f);
    char val[100] = "";
    settings::getField(f, val, sizeof(val), true);  // o portal é local: mostra os segredos
    int len = 5;
    switch (f.type) {
        case settings::STR: len = f.size - 1; attr = (f.flags & settings::SECRET) ? "type='password'" : ""; break;
        case settings::U8:
        case settings::U16:
            len = f.type == settings::U8 ? 3 : 5;
            attr = String("type='number' min='") + f.min + "' max='" + f.max + "'";
            break;
        case settings::TIME: attr = "type='time'"; break;
        case settings::ENUM:
            len = 1;
            for (int k = 0; k < f.nNames; k++) len = max(len, (int)strlen(f.names[k]) + 3);  // folga pra "pt-br"
            break;
        case settings::BOOL:
            // Checkbox: marcado envia "1"; desmarcado não envia nada (valor
            // vazio, que o setField lê como desligado)
            attr = !strcmp(val, "1") ? "type='checkbox' checked" : "type='checkbox'";
            return new WiFiManagerParameter(f.key, label.c_str(), "1", 2, attr.c_str(), WFM_LABEL_AFTER);
    }
    return new WiFiManagerParameter(f.key, label.c_str(), val, len, attr.c_str());
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

    // Campos do portal, gerados da tabela settings::FIELDS (mesma validação
    // da interface web). O WiFiManager só guarda o PONTEIRO do rótulo e dos
    // atributos — por isso os textos ficam em `labels`/`attrs` até o fim do portal.
    const int N = settings::N_FIELDS;
    String labels[N], attrs[N];
    WiFiManager wm;
    WiFiManagerParameter* params[N];
    for (int i = 0; i < N; i++) {
        params[i] = portalParam(settings::FIELDS[i], labels[i], attrs[i]);
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
        for (int i = 0; i < N; i++) settings::setField(settings::FIELDS[i], params[i]->getValue());
        settings::save();
    }
    for (int i = 0; i < N; i++) delete params[i];

    if (!ok) WiFi.begin();  // segue tentando a rede salva em background
    return ok;
}

} // namespace wifisetup
