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
    const char* ST_SEARCHING;
    const char* ST_BAD_TOKEN;

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
    const char* NAME_SERVICES;
    const char* SVC_HINT_1;
    const char* SVC_HINT_2;
    const char* SVC_DOWN;
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

    "ok", "no wifi", "agent offline", "bad json", "no data", "no api key", "finding agent", "bad token",

    "DISK", "PING", "UPTIME", "IP", "DOWNLOAD (last min)",
    "subscribers", "%s views", "%ld videos",
    "Set your YouTube API key", "in agent/config.json",

    "Clock", "PC", "Processes", "Network", "Quotes", "YouTube",
    "Services", "Add services to check in", "agent/config.json (\"services\")", "DOWN",
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

    "ok", "sem wifi", "agente offline", "json invalido", "sem dados", "sem api key", "buscando agente", "token invalido",

    "DISCO", "PING", "UPTIME", "IP", "DOWNLOAD (ultimos min)",
    "inscritos", "%s views", "%ld videos",
    "Configure a API key do YouTube", "em agent/config.json",

    "Relogio", "PC", "Processos", "Rede", "Cotacoes", "YouTube",
    "Servicos", "Adicione servicos pra verificar em", "agent/config.json (\"services\")", "FORA",
};

inline const Lang& tr() {
    return settings::get().lang == LANG_PT_BR ? PT_BR : EN;
}

// Data no formato de settings.dateFmt, sempre com o dia da semana no idioma atual:
//   long: "Sun, Oct 11 2026" / "dom, 11 out 2026"
//   dmy:  "Sun, 11/10/2026"   mdy: "Sun, 10/11/2026"   ymd: "Sun, 2026-10-11"
//   auto: long em EN, dmy em PT-BR
inline void formatDate(char* buf, size_t len, const struct tm& t) {
    static const char* const daysEn[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* const daysPt[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};
    static const char* const monthsEn[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char* const monthsPt[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                           "jul", "ago", "set", "out", "nov", "dez"};
    const bool pt = settings::get().lang == LANG_PT_BR;
    const char* day = (pt ? daysPt : daysEn)[t.tm_wday];
    const int y = t.tm_year + 1900, m = t.tm_mon + 1, d = t.tm_mday;

    uint8_t fmt = settings::get().dateFmt;
    if (fmt == settings::DATE_AUTO) fmt = pt ? settings::DATE_DMY : settings::DATE_LONG;
    switch (fmt) {
        case settings::DATE_DMY: snprintf(buf, len, "%s, %02d/%02d/%04d", day, d, m, y); break;
        case settings::DATE_MDY: snprintf(buf, len, "%s, %02d/%02d/%04d", day, m, d, y); break;
        case settings::DATE_YMD: snprintf(buf, len, "%s, %04d-%02d-%02d", day, y, m, d); break;
        default:
            if (pt) snprintf(buf, len, "%s, %d %s %04d", day, d, monthsPt[t.tm_mon], y);
            else snprintf(buf, len, "%s, %s %d %04d", day, monthsEn[t.tm_mon], d, y);
    }
}

} // namespace i18n
