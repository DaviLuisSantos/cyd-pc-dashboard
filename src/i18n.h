#pragma once
#include <stdio.h>
#include <time.h>
#include "config.h"
#include "settings.h"

// Textos da interface. O idioma é escolhido em runtime (settings::get().lang,
// editável no portal de configuração; default = PANEL_LANG). As fontes
// clássicas do TFT_eSPI não têm acentos — mantenha tudo em ASCII puro.
namespace i18n {

struct Lang {
    const char* CONNECTING_WIFI;
    const char* NO_WIFI_OFFLINE;
    const char* HOLD_FOR_SETUP;
    const char* SETUP_TITLE;
    const char* SETUP_STEP_1;
    const char* SETUP_STEP_2;
    const char* SETUP_STEP_3;
    const char* OTA_UPDATING;
    const char* OTA_DONE;
    const char* OTA_FAILED;

    const char* ST_OK;
    const char* ST_NO_WIFI;
    const char* ST_AGENT_DOWN;
    const char* ST_BAD_JSON;
    const char* ST_NO_DATA;
    const char* ST_NO_API_KEY;

    const char* LBL_DISK;
    const char* LBL_PING;
    const char* LBL_UPTIME;
    const char* LBL_IP;
    const char* LBL_DL_HISTORY;
    const char* LBL_SUBS;
    const char* FMT_VIEWS;
    const char* FMT_VIDEOS;
    const char* YT_HINT_1;
    const char* YT_HINT_2;

    const char* NAME_CLOCK;
    const char* NAME_PC;
    const char* NAME_PROCS;
    const char* NAME_SYS;
    const char* NAME_QUOTES;
    const char* NAME_YOUTUBE;
};

static const Lang EN = {
    "Connecting WiFi...",
    "No WiFi (running offline)",
    "Hold the screen to open setup",
    "Setup",
    "1. On your phone, join the WiFi:",
    "2. Open http://192.168.4.1",
    "3. Pick your network + agent IP",
    "Updating firmware...", "Done! Restarting", "Update failed",

    "ok", "no wifi", "agent offline", "bad json", "no data", "no api key",

    "DISK", "PING", "UPTIME", "IP", "DOWNLOAD (last min)",
    "subscribers", "%s views", "%ld videos",
    "Set your YouTube API key", "in agent/config.json",

    "Clock", "PC", "Processes", "Network", "Quotes", "YouTube",
};

static const Lang PT_BR = {
    "Conectando WiFi...",
    "Sem WiFi (segue offline)",
    "Segure a tela para configurar",
    "Configuracao",
    "1. No celular, conecte no WiFi:",
    "2. Abra http://192.168.4.1",
    "3. Escolha a rede + IP do agente",
    "Atualizando firmware...", "Pronto! Reiniciando", "Falha na atualizacao",

    "ok", "sem wifi", "agente offline", "json invalido", "sem dados", "sem api key",

    "DISCO", "PING", "UPTIME", "IP", "DOWNLOAD (ultimos min)",
    "inscritos", "%s views", "%ld videos",
    "Configure a API key do YouTube", "em agent/config.json",

    "Relogio", "PC", "Processos", "Rede", "Cotacoes", "YouTube",
};

inline const Lang& tr() {
    return settings::get().lang == LANG_PT_BR ? PT_BR : EN;
}

// EN: "Sun, Oct 11 2026" — PT-BR: "dom, 11/10/2026"
inline void formatDate(char* buf, size_t len, const struct tm& t) {
    if (settings::get().lang == LANG_PT_BR) {
        static const char* const dias[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};
        snprintf(buf, len, "%s, %02d/%02d/%04d",
                 dias[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    } else {
        static const char* const days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(buf, len, "%s, %s %d %04d",
                 days[t.tm_wday], months[t.tm_mon], t.tm_mday, t.tm_year + 1900);
    }
}

} // namespace i18n
