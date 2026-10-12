#pragma once

// ============================================================================
//  Copie este arquivo para `src/user_config.h` e edite.
//  Só precisa definir o que quer mudar — o resto vem de `config.h`.
// ============================================================================

// ===== WiFi =====
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"

// ===== Agente do PC =====
// #define AGENT_HOST "192.168.0.100"   // IP fixo do PC; o default "auto" acha o agente na rede
// #define AGENT_PORT 8377
// #define AGENT_TOKEN "..."             // se o agent/config.json tiver "token"

// ===== Idioma (LANG_EN ou LANG_PT_BR) =====
// #define PANEL_LANG LANG_PT_BR

// ===== Fuso horário (string POSIX TZ) =====
// #define TZ_INFO "<-03>3"                      // Brasília
// #define TZ_INFO "EST5EDT,M3.2.0,M11.1.0"      // Nova York

// ===== Relógio =====
// #define CLOCK_FORMAT 12          // 12h com AM/PM (default 24)
// #define DATE_FORMAT  "mdy"       // auto, long, dmy, mdy, ymd

// ===== Telas (1 = mostra, 0 = esconde) =====
// #define ENABLE_SCREEN_CLOCK   1
// #define ENABLE_SCREEN_PCSTATS 1
// #define ENABLE_SCREEN_PROCS   1
// #define ENABLE_SCREEN_SYSINFO 1
// #define ENABLE_SCREEN_QUOTES  1
// #define ENABLE_SCREEN_YOUTUBE 0
// Ordem das abas / quais aparecem (a primeira é a inicial; editável no portal)
// #define SCREEN_ORDER "pcstats,procs,sysinfo,quotes,clock"

// ===== Tema: dracula, nord, gruvbox, mocha, tokyonight, latte (claro) =====
// #define THEME "nord"

// ===== Navegação / display =====
// #define AUTO_NEXT_MS     10000   // 0 = desativa a troca automática de aba
// #define DISPLAY_ROTATION 1       // 3 = USB à esquerda
// #define DISPLAY_INVERT   true    // troque se as cores saírem invertidas
// #define BACKLIGHT_LEVEL  255

// ===== Modo noturno (início == fim desativa) =====
// #define NIGHT_START      "23:00"
// #define NIGHT_END        "07:00"
// #define NIGHT_BRIGHTNESS 0       // 0 = tela apagada (um toque acende)

// ===== Alertas (% ; 0 = sem alerta) =====
// #define ALERT_CPU  90
// #define ALERT_RAM  90
// #define ALERT_DISK 95
// #define ALERT_JUMP true          // pula pra aba PC quando um alerta começa

// ===== Interface web =====
// #define WEB_PASSWORD "change-me"   // usuário "admin"; vazio = sem senha
// #define MDNS_NAME    "cyd-dashboard" // http://cyd-dashboard.local/

// ===== Touch =====
// #define TOUCH_DEBUG 1            // loga leituras cruas no serial pra calibrar
// #define TOUCH_MIN_X 200
// #define TOUCH_MAX_X 3700
// #define TOUCH_MIN_Y 240
// #define TOUCH_MAX_Y 3800
