#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>
#include "config.h"

// Configurações de runtime — editáveis pelo portal de configuração (WiFiManager)
// ou pela interface web (web_ui.h) e salvas no NVS da placa. O que nunca foi
// salvo cai no default de compilação (config.h / user_config.h), então quem
// compila com user_config.h continua funcionando sem passar pelo portal.
//
// Cada configuração é UMA linha na tabela FIELDS (mais abaixo): chave, tipo,
// campo do struct, faixa válida, default e rótulo. Carregar/salvar no NVS,
// validar (setField), formatar (getField), os campos do portal e o formulário
// da interface web saem todos dela. Opção nova = campo no struct + linha na
// tabela (+ default em config.h e linha no README).
namespace settings {

struct Settings {
    char agentHost[64];
    uint16_t agentPort;
    char tz[64];
    uint8_t lang;  // LANG_EN / LANG_PT_BR
    char screens[96];     // ids das abas, na ordem (ex.: "pcstats,procs,quotes")
    uint16_t autoNextS;   // troca automática de aba, em segundos (0 = desativa)
    uint8_t brightness;   // backlight 0-255
    uint8_t rotation;     // 1 = USB à direita, 3 = USB à esquerda
    bool invert;          // inverte as cores do display (varia por unidade do CYD)
    uint16_t nightStart;  // modo noturno: minutos desde 00:00 (início == fim desativa)
    uint16_t nightEnd;
    uint8_t nightBright;  // brilho à noite (0 = tela apagada)
    char webPass[33];     // senha da interface web (vazio = sem senha)
    uint8_t clockFmt;     // 24 ou 12
    uint8_t dateFmt;      // DATE_AUTO, DATE_LONG...
    char agentToken[65];  // token do agente (vazio = agente sem token)
    uint8_t alertCpu;     // limites de alerta em % (0 = desligado)
    uint8_t alertRam;
    uint8_t alertDisk;
    uint8_t alertTemp;    // °C da CPU (0 = desligado)
    bool alertJump;       // pula pra aba PC quando um alerta começa
    uint8_t theme;        // índice em THEME_NAMES (= theme::PALETTES)
    bool ledOn;           // LED RGB de status
    uint8_t ledBright;    // intensidade do LED, %
    bool autoBright;      // brilho acompanha o sensor de luz
    uint8_t autoMin;      // brilho no escuro com o automático ligado
};

static const char* NVS_NAMESPACE = "cydpc";

// Mesma ordem de theme::PALETTES (theme.h)
static const char* const THEME_NAMES[] = {"dracula", "nord", "gruvbox", "mocha", "tokyonight", "latte"};
static const int N_THEME_NAMES = sizeof(THEME_NAMES) / sizeof(THEME_NAMES[0]);

enum DateFormat : uint8_t { DATE_AUTO, DATE_LONG, DATE_DMY, DATE_MDY, DATE_YMD };
static const char* const DATE_FORMAT_NAMES[] = {"auto", "long", "dmy", "mdy", "ymd"};

// "HH:MM" -> minutos desde a meia-noite; -1 se inválido
inline int parseHHMM(const char* str) {
    int h, m;
    if (sscanf(str, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) return -1;
    return h * 60 + m;
}

inline void formatHHMM(uint16_t minutes, char* buf, size_t len) {
    snprintf(buf, len, "%02u:%02u", minutes / 60, minutes % 60);
}

inline Settings& get() {
    static Settings s;
    return s;
}

// ===== Tabela de campos =====

// Tipo do campo. O tipo também define como ele fica no NVS — tem que bater com
// o que versões anteriores gravaram, senão a configuração salva não é lida:
//   STR -> putString, U8/ENUM -> putUChar, U16/TIME -> putUShort, BOOL -> putBool
enum FieldType : uint8_t { STR, U8, U16, BOOL, ENUM, TIME };

enum FieldFlags : uint8_t {
    NOT_EMPTY = 1,  // STR: não aceita vazio
    SECRET = 2,     // nunca sai pela API; na web, campo em branco = manter
};

struct Field {
    const char* key;          // chave no NVS, no portal e na API (≤15 chars)
    FieldType type;
    uint16_t offset;          // offsetof(Settings, campo)
    uint8_t size;             // STR: tamanho do buffer
    int32_t min, max;         // U8/U16
    const char* const* names; // ENUM: nomes aceitos (e mostrados)
    const uint8_t* values;    // ENUM: valor guardado pra cada nome (nullptr = índice)
    const char* const* optionLabels;  // ENUM: texto de cada opção na web (opcional)
    uint8_t nNames;
    const char* defStr;       // default como texto (passa pela mesma validação)...
    int32_t defNum;           // ...ou numérico, quando defStr == nullptr
    uint8_t flags;
    const char* group;        // seção do formulário da web
    const char* label;        // portal e interface web
};

#define SETTINGS_OFF(m) ((uint16_t)offsetof(Settings, m))
#define SETTINGS_SIZE(m) ((uint8_t)sizeof(((Settings*)0)->m))

constexpr Field Str(const char* key, uint16_t off, uint8_t size, const char* def, uint8_t flags,
                    const char* group, const char* label) {
    return {key, STR, off, size, 0, 0, nullptr, nullptr, nullptr, 0, def, 0, flags, group, label};
}
constexpr Field Num(const char* key, FieldType t, uint16_t off, int32_t min, int32_t max, int32_t def,
                    const char* group, const char* label) {
    return {key, t, off, 0, min, max, nullptr, nullptr, nullptr, 0, nullptr, def, 0, group, label};
}
constexpr Field Bool(const char* key, uint16_t off, bool def, const char* group, const char* label) {
    return {key, BOOL, off, 0, 0, 1, nullptr, nullptr, nullptr, 0, nullptr, def, 0, group, label};
}
constexpr Field Enum(const char* key, uint16_t off, const char* const* names, const uint8_t* values,
                     const char* const* optionLabels, uint8_t n, const char* defStr, int32_t defNum,
                     const char* group, const char* label) {
    return {key, ENUM, off, 0, 0, 0, names, values, optionLabels, n, defStr, defNum, 0, group, label};
}
constexpr Field Time(const char* key, uint16_t off, const char* def, const char* group, const char* label) {
    return {key, TIME, off, 0, 0, 0, nullptr, nullptr, nullptr, 0, def, 0, 0, group, label};
}

static const char* const LANG_NAMES[] = {"en", "pt"};
static const uint8_t LANG_VALUES[] = {LANG_EN, LANG_PT_BR};
static const char* const LANG_LABELS[] = {"English", "Portugues (BR)"};
static const char* const CLOCK_NAMES[] = {"24", "12"};
static const uint8_t CLOCK_VALUES[] = {24, 12};
static const char* const CLOCK_LABELS[] = {"24h", "12h (AM/PM)"};
static const char* const DATE_LABELS[] = {"auto (by language)", "long: Mon, Oct 12 2026", "dmy: 12/10/2026",
                                          "mdy: 10/12/2026", "ymd: 2026-10-12"};
static const char* const ROT_NAMES[] = {"1", "3"};
static const uint8_t ROT_VALUES[] = {1, 3};
static const char* const ROT_LABELS[] = {"USB on the right", "USB on the left"};
static const char* const THEME_LABELS[] = {"Dracula", "Nord", "Gruvbox", "Catppuccin Mocha", "Tokyo Night",
                                           "Catppuccin Latte (light)"};

// Ordem = ordem no portal e no formulário da web
static const Field FIELDS[] = {
    Str("host", SETTINGS_OFF(agentHost), SETTINGS_SIZE(agentHost), AGENT_HOST, NOT_EMPTY, "Agent",
        "Agent IP (PC running pc_stats_agent.py), or auto to find it on the network"),
    Num("port", U16, SETTINGS_OFF(agentPort), 1, 65535, AGENT_PORT, "Agent", "Agent port"),
    Str("token", SETTINGS_OFF(agentToken), SETTINGS_SIZE(agentToken), AGENT_TOKEN, SECRET, "Agent",
        "Agent token (only if agent/config.json sets one)"),

    Str("tz", SETTINGS_OFF(tz), SETTINGS_SIZE(tz), TZ_INFO, NOT_EMPTY, "Region",
        "Timezone (POSIX TZ, e.g. <-03>3 or EST5EDT,M3.2.0,M11.1.0)"),
    Enum("lang", SETTINGS_OFF(lang), LANG_NAMES, LANG_VALUES, LANG_LABELS, 2, nullptr, PANEL_LANG, "Region",
         "Language"),
    Enum("clock", SETTINGS_OFF(clockFmt), CLOCK_NAMES, CLOCK_VALUES, CLOCK_LABELS, 2, nullptr, CLOCK_FORMAT,
         "Region", "Clock"),
    Enum("date", SETTINGS_OFF(dateFmt), DATE_FORMAT_NAMES, nullptr, DATE_LABELS, 5, DATE_FORMAT, 0, "Region",
         "Date format"),

    Str("screens", SETTINGS_OFF(screens), SETTINGS_SIZE(screens), SCREEN_ORDER, 0, "Tabs",
        "Tabs, in order (first = start tab): clock, pcstats, procs, sysinfo, quotes, youtube, services, actions"),
    Num("autonext", U16, SETTINGS_OFF(autoNextS), 0, 3600, AUTO_NEXT_MS / 1000, "Tabs",
        "Auto-rotate tabs every N seconds (0 = off)"),

    Enum("theme", SETTINGS_OFF(theme), THEME_NAMES, nullptr, THEME_LABELS, N_THEME_NAMES, THEME, 0, "Display",
         "Theme"),
    // Mínimo 10: brilho 0 deixaria a tela apagada sem jeito óbvio de voltar
    Num("bright", U8, SETTINGS_OFF(brightness), 10, 255, BACKLIGHT_LEVEL, "Display", "Brightness (10-255)"),
    Enum("rot", SETTINGS_OFF(rotation), ROT_NAMES, ROT_VALUES, ROT_LABELS, 2, nullptr, DISPLAY_ROTATION,
         "Display", "Rotation"),
    Bool("invert", SETTINGS_OFF(invert), DISPLAY_INVERT, "Display",
         "Invert colors (try toggling if the background looks white)"),
    Bool("autobright", SETTINGS_OFF(autoBright), AUTO_BRIGHT, "Display",
         "Auto brightness: follow the room light (light sensor)"),
    Num("automin", U8, SETTINGS_OFF(autoMin), 5, 255, AUTO_BRIGHT_MIN, "Display",
        "Auto brightness: level in the dark (5-255)"),
    Bool("led", SETTINGS_OFF(ledOn), LED_ENABLED, "Display",
         "Status LED: pulses red on alerts, blinks on notifications"),
    Num("ledbright", U8, SETTINGS_OFF(ledBright), 1, 100, LED_BRIGHTNESS, "Display", "Status LED brightness (%)"),

    Time("nstart", SETTINGS_OFF(nightStart), NIGHT_START, "Night mode", "Night mode from (same as 'until' = off)"),
    Time("nend", SETTINGS_OFF(nightEnd), NIGHT_END, "Night mode", "Night mode until"),
    Num("nbright", U8, SETTINGS_OFF(nightBright), 0, 255, NIGHT_BRIGHTNESS, "Night mode",
        "Night brightness (0 = screen off, a tap wakes it)"),

    Num("acpu", U8, SETTINGS_OFF(alertCpu), 0, 100, ALERT_CPU, "Alerts", "Alert when CPU % is above (0 = off)"),
    Num("aram", U8, SETTINGS_OFF(alertRam), 0, 100, ALERT_RAM, "Alerts", "Alert when RAM % is above (0 = off)"),
    Num("adisk", U8, SETTINGS_OFF(alertDisk), 0, 100, ALERT_DISK, "Alerts", "Alert when disk % is above (0 = off)"),
    Num("atemp", U8, SETTINGS_OFF(alertTemp), 0, 120, ALERT_TEMP, "Alerts",
        "Alert when CPU temperature (C) is above (0 = off)"),
    Bool("ajump", SETTINGS_OFF(alertJump), ALERT_JUMP, "Alerts", "Jump to the PC tab when an alert starts"),

    Str("webpass", SETTINGS_OFF(webPass), SETTINGS_SIZE(webPass), WEB_PASSWORD, SECRET, "Web interface",
        "Web interface password (optional; user: admin)"),
};
static const int N_FIELDS = sizeof(FIELDS) / sizeof(FIELDS[0]);

inline const Field* findField(const char* key) {
    for (const Field& f : FIELDS) {
        if (!strcmp(f.key, key)) return &f;
    }
    return nullptr;
}

inline uint8_t* ptr(const Field& f) { return (uint8_t*)&get() + f.offset; }

// Valor guardado de um ENUM pro nome i
inline uint8_t enumValue(const Field& f, int i) { return f.values ? f.values[i] : (uint8_t)i; }

// Índice do nome de um ENUM cujo valor guardado é v (-1 se nenhum)
inline int enumIndexOf(const Field& f, uint8_t v) {
    for (int i = 0; i < f.nNames; i++) {
        if (enumValue(f, i) == v) return i;
    }
    return -1;
}

// "pt" aceita "pt", "PT" e "pt-br"; "12" aceita "12h" mas não "120"
inline int enumMatch(const Field& f, const char* v) {
    for (int i = 0; i < f.nNames; i++) {
        size_t n = strlen(f.names[i]);
        if (strncasecmp(v, f.names[i], n)) continue;
        unsigned char next = v[n];
        bool numericName = isdigit((unsigned char)f.names[i][n - 1]);
        // fim; separador ("pt-br"); ou nome numérico seguido de letra ("12h")
        if (!next || !isalnum(next) || (numericName && isalpha(next))) return i;
    }
    return -1;
}

// Valida e grava (só em memória; chamar save() depois). false = valor inválido,
// campo intacto.
inline bool setField(const Field& f, const char* v) {
    uint8_t* p = ptr(f);
    switch (f.type) {
        case STR:
            if ((f.flags & NOT_EMPTY) && !*v) return false;
            strlcpy((char*)p, v, f.size);
            return true;
        case U8:
        case U16: {
            char* end;
            long n = strtol(v, &end, 10);
            if (end == v || *end || n < f.min || n > f.max) return false;
            if (f.type == U8) *p = (uint8_t)n;
            else *(uint16_t*)p = (uint16_t)n;
            return true;
        }
        case BOOL:
            *(bool*)p = !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on") || !strcasecmp(v, "yes");
            return true;
        case ENUM: {
            int i = enumMatch(f, v);
            if (i < 0) return false;
            *p = enumValue(f, i);
            return true;
        }
        case TIME: {
            int m = parseHHMM(v);
            if (m < 0) return false;
            *(uint16_t*)p = (uint16_t)m;
            return true;
        }
    }
    return false;
}

inline bool setField(const char* key, const char* v) {
    const Field* f = findField(key);
    return f && setField(*f, v);
}

// Valor atual como texto (o mesmo formato que setField aceita). Segredos só com
// includeSecrets (o portal mostra; a API nunca).
inline bool getField(const Field& f, char* buf, size_t len, bool includeSecrets = false) {
    if ((f.flags & SECRET) && !includeSecrets) return false;
    const uint8_t* p = ptr(f);
    switch (f.type) {
        case STR: strlcpy(buf, (const char*)p, len); break;
        case U8: snprintf(buf, len, "%u", *p); break;
        case U16: snprintf(buf, len, "%u", *(const uint16_t*)p); break;
        case BOOL: strlcpy(buf, *(const bool*)p ? "1" : "0", len); break;
        case ENUM: {
            int i = enumIndexOf(f, *p);
            strlcpy(buf, f.names[i < 0 ? 0 : i], len);
            break;
        }
        case TIME: formatHHMM(*(const uint16_t*)p, buf, len); break;
    }
    return true;
}

inline bool getField(const char* key, char* buf, size_t len, bool includeSecrets = false) {
    const Field* f = findField(key);
    return f && getField(*f, buf, len, includeSecrets);
}

inline bool isSet(const Field& f) {
    return f.type == STR ? ((const char*)ptr(f))[0] != '\0' : true;
}

// Default de compilação: texto passa pelo setField; número é gravado direto
// (ENUM numérico = o valor guardado, ex.: PANEL_LANG, DISPLAY_ROTATION)
inline void applyDefault(const Field& f) {
    if (f.defStr) {
        if (setField(f, f.defStr)) return;
        // default de texto inválido (ex.: THEME com erro de digitação): primeira opção / vazio
        if (f.type == ENUM) *ptr(f) = enumValue(f, 0);
        else if (f.type == STR) ((char*)ptr(f))[0] = '\0';
        else if (f.type == TIME) *(uint16_t*)ptr(f) = 0;
        return;
    }
    uint8_t* p = ptr(f);
    switch (f.type) {
        case U8: *p = (uint8_t)constrain(f.defNum, f.min, f.max); break;
        case U16: *(uint16_t*)p = (uint16_t)constrain(f.defNum, f.min, f.max); break;
        case BOOL: *(bool*)p = f.defNum != 0; break;
        case ENUM: *p = enumIndexOf(f, (uint8_t)f.defNum) >= 0 ? (uint8_t)f.defNum : enumValue(f, 0); break;
        default: break;
    }
}

inline void load() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);  // RW: cria o namespace na primeira vez
    for (const Field& f : FIELDS) {
        applyDefault(f);  // base: default de compilação
        if (!p.isKey(f.key)) continue;
        uint8_t* dst = ptr(f);
        switch (f.type) {
            case STR: strlcpy((char*)dst, p.getString(f.key, (const char*)dst).c_str(), f.size); break;
            case U8: *dst = (uint8_t)constrain((int32_t)p.getUChar(f.key, *dst), f.min, f.max); break;
            case U16: *(uint16_t*)dst = (uint16_t)constrain((int32_t)p.getUShort(f.key, *(uint16_t*)dst), f.min, f.max); break;
            case BOOL: *(bool*)dst = p.getBool(f.key, *(bool*)dst); break;
            case ENUM: {
                uint8_t v = p.getUChar(f.key, *dst);
                if (enumIndexOf(f, v) >= 0) *dst = v;  // valor salvo fora da lista: fica o default
                break;
            }
            case TIME: {
                uint16_t v = p.getUShort(f.key, *(uint16_t*)dst);
                if (v < 24 * 60) *(uint16_t*)dst = v;
                break;
            }
        }
    }
    p.end();
}

inline void save() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    for (const Field& f : FIELDS) {
        const uint8_t* src = ptr(f);
        switch (f.type) {
            case STR: p.putString(f.key, (const char*)src); break;
            case U8:
            case ENUM: p.putUChar(f.key, *src); break;
            case U16:
            case TIME: p.putUShort(f.key, *(const uint16_t*)src); break;
            case BOOL: p.putBool(f.key, *(const bool*)src); break;
        }
    }
    p.end();
}

// Apaga o que foi salvo pelo portal (volta pros defaults de compilação)
inline void reset() {
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.clear();
    p.end();
}

} // namespace settings
