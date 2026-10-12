#pragma once
#include "config.h"
#include <stdio.h>
#include <time.h>

// Textos da interface. As fontes clássicas do TFT_eSPI não têm acentos —
// mantenha tudo em ASCII puro.
namespace i18n {

#if PANEL_LANG == LANG_PT_BR
constexpr const char* CONNECTING_WIFI = "Conectando WiFi...";
constexpr const char* NO_WIFI_OFFLINE = "Sem WiFi (segue offline)";
constexpr const char* NO_WIFI_CONFIG  = "Configure o WiFi em user_config.h";

constexpr const char* ST_OK          = "ok";
constexpr const char* ST_NO_WIFI     = "sem wifi";
constexpr const char* ST_AGENT_DOWN  = "agente offline";
constexpr const char* ST_BAD_JSON    = "json invalido";
constexpr const char* ST_NO_DATA     = "sem dados";
constexpr const char* ST_NO_API_KEY  = "sem api key";

constexpr const char* LBL_DISK       = "DISCO";
constexpr const char* LBL_PING       = "PING";
constexpr const char* LBL_UPTIME     = "UPTIME";
constexpr const char* LBL_IP         = "IP";
constexpr const char* LBL_DL_HISTORY = "DOWNLOAD (ultimos min)";
constexpr const char* LBL_SUBS       = "inscritos";
constexpr const char* FMT_VIEWS      = "%s views";
constexpr const char* FMT_VIDEOS     = "%ld videos";
constexpr const char* YT_HINT_1      = "Configure a API key do YouTube";
constexpr const char* YT_HINT_2      = "em agent/config.json";

constexpr const char* NAME_CLOCK   = "Relogio";
constexpr const char* NAME_PC      = "PC";
constexpr const char* NAME_PROCS   = "Processos";
constexpr const char* NAME_SYS     = "Rede";
constexpr const char* NAME_QUOTES  = "Cotacoes";
constexpr const char* NAME_YOUTUBE = "YouTube";

static const char* const WEEKDAYS[] = {"dom", "seg", "ter", "qua", "qui", "sex", "sab"};
// dom, 11/10/2026
inline void formatDate(char* buf, size_t len, const struct tm& t) {
    snprintf(buf, len, "%s, %02d/%02d/%04d",
             WEEKDAYS[t.tm_wday], t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
}

#else  // LANG_EN
constexpr const char* CONNECTING_WIFI = "Connecting WiFi...";
constexpr const char* NO_WIFI_OFFLINE = "No WiFi (running offline)";
constexpr const char* NO_WIFI_CONFIG  = "Set WiFi in user_config.h";

constexpr const char* ST_OK          = "ok";
constexpr const char* ST_NO_WIFI     = "no wifi";
constexpr const char* ST_AGENT_DOWN  = "agent offline";
constexpr const char* ST_BAD_JSON    = "bad json";
constexpr const char* ST_NO_DATA     = "no data";
constexpr const char* ST_NO_API_KEY  = "no api key";

constexpr const char* LBL_DISK       = "DISK";
constexpr const char* LBL_PING       = "PING";
constexpr const char* LBL_UPTIME     = "UPTIME";
constexpr const char* LBL_IP         = "IP";
constexpr const char* LBL_DL_HISTORY = "DOWNLOAD (last min)";
constexpr const char* LBL_SUBS       = "subscribers";
constexpr const char* FMT_VIEWS      = "%s views";
constexpr const char* FMT_VIDEOS     = "%ld videos";
constexpr const char* YT_HINT_1      = "Set your YouTube API key";
constexpr const char* YT_HINT_2      = "in agent/config.json";

constexpr const char* NAME_CLOCK   = "Clock";
constexpr const char* NAME_PC      = "PC";
constexpr const char* NAME_PROCS   = "Processes";
constexpr const char* NAME_SYS     = "Network";
constexpr const char* NAME_QUOTES  = "Quotes";
constexpr const char* NAME_YOUTUBE = "YouTube";

static const char* const WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* const MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
// Sun, Oct 11 2026
inline void formatDate(char* buf, size_t len, const struct tm& t) {
    snprintf(buf, len, "%s, %s %d %04d",
             WEEKDAYS[t.tm_wday], MONTHS[t.tm_mon], t.tm_mday, t.tm_year + 1900);
}
#endif

} // namespace i18n
