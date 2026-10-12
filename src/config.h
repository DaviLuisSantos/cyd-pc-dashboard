#pragma once

// ============================================================================
//  Configuração padrão do firmware — NÃO edite este arquivo.
//
//  Copie `src/user_config.example.h` para `src/user_config.h` e sobrescreva
//  só o que precisar (WiFi, IP do agente, idioma, telas...). O user_config.h
//  está no .gitignore, então suas credenciais nunca vão pro repositório.
//
//  Todo valor abaixo está protegido por #ifndef: o que você definir no
//  user_config.h (ou via build_flags no platformio.ini) tem prioridade.
// ============================================================================

#if __has_include("user_config.h")
#include "user_config.h"
#else
#warning "src/user_config.h nao encontrado - usando defaults. Copie src/user_config.example.h para src/user_config.h"
#endif

// ===== Versão (o workflow de release injeta a tag via build flag) =====
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// ===== Idioma da interface (default; dá pra trocar no portal de configuração) =====
#define LANG_EN    0
#define LANG_PT_BR 1
#ifndef PANEL_LANG
#define PANEL_LANG LANG_EN
#endif

// ===== WiFi =====
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000
#endif

// ===== Agente do PC (agent/pc_stats_agent.py) =====
#ifndef AGENT_HOST
#define AGENT_HOST "192.168.0.100"   // IP da máquina que roda o agente (editável no portal)
#endif
#ifndef AGENT_PORT
#define AGENT_PORT 8377
#endif
#ifndef AGENT_HTTP_TIMEOUT_MS
#define AGENT_HTTP_TIMEOUT_MS 1500
#endif

// Intervalos de consulta de cada endpoint (ms)
#ifndef AGENT_POLL_MS
#define AGENT_POLL_MS 2000     // /stats
#endif
#ifndef QUOTES_POLL_MS
#define QUOTES_POLL_MS 30000   // /quotes (o agente atualiza a cada 60s)
#endif
#ifndef PROCS_POLL_MS
#define PROCS_POLL_MS 3000     // /procs
#endif
#ifndef SYS_POLL_MS
#define SYS_POLL_MS 5000       // /sysinfo
#endif
#ifndef YT_POLL_MS
#define YT_POLL_MS 300000      // /youtube (o agente atualiza a cada 30min)
#endif

// ===== Telas habilitadas (1 = mostra, 0 = esconde) =====
// A ordem das abas segue a ordem de registro em main.cpp.
#ifndef ENABLE_SCREEN_CLOCK
#define ENABLE_SCREEN_CLOCK 1
#endif
#ifndef ENABLE_SCREEN_PCSTATS
#define ENABLE_SCREEN_PCSTATS 1
#endif
#ifndef ENABLE_SCREEN_PROCS
#define ENABLE_SCREEN_PROCS 1
#endif
#ifndef ENABLE_SCREEN_SYSINFO
#define ENABLE_SCREEN_SYSINFO 1
#endif
#ifndef ENABLE_SCREEN_QUOTES
#define ENABLE_SCREEN_QUOTES 1
#endif
#ifndef ENABLE_SCREEN_YOUTUBE
#define ENABLE_SCREEN_YOUTUBE 1
#endif

// ===== Navegação =====
#ifndef AUTO_NEXT_MS
#define AUTO_NEXT_MS 10000  // troca de aba automática (0 = desativa). Reinicia a cada toque.
#endif

// ===== Relógio (NTP) =====
#ifndef NTP_SERVER
#define NTP_SERVER "pool.ntp.org"
#endif
// String POSIX TZ — ex.: "<-03>3" (Brasília), "EST5EDT,M3.2.0,M11.1.0" (Nova York),
// "CET-1CEST,M3.5.0,M10.5.0/3" (Europa Central), "UTC0". Lista completa:
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#ifndef TZ_INFO
#define TZ_INFO "UTC0"
#endif

// ===== Display =====
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 1     // 1 = landscape com USB à direita, 3 = USB à esquerda
#endif
// Muitas CYD mostram cores invertidas com o ILI9341_2_DRIVER (fundo escuro
// aparece branco). Se as cores ficarem estranhas na sua placa, troque.
#ifndef DISPLAY_INVERT
#define DISPLAY_INVERT true
#endif
#ifndef BACKLIGHT_LEVEL
#define BACKLIGHT_LEVEL 255    // brilho 0-255
#endif

// ===== Touch (XPT2046) — pinos fixos da CYD =====
#ifndef XPT2046_IRQ
#define XPT2046_IRQ  36
#endif
#ifndef XPT2046_MOSI
#define XPT2046_MOSI 32
#endif
#ifndef XPT2046_MISO
#define XPT2046_MISO 39
#endif
#ifndef XPT2046_CLK
#define XPT2046_CLK  25
#endif
#ifndef XPT2046_CS
#define XPT2046_CS   33
#endif

// Calibração do touch (ajuste fino se o toque estiver deslocado).
// Ligue TOUCH_DEBUG para logar as leituras cruas no serial.
#ifndef TOUCH_MIN_X
#define TOUCH_MIN_X 200
#endif
#ifndef TOUCH_MAX_X
#define TOUCH_MAX_X 3700
#endif
#ifndef TOUCH_MIN_Y
#define TOUCH_MIN_Y 240
#endif
#ifndef TOUCH_MAX_Y
#define TOUCH_MAX_Y 3800
#endif
#ifndef TOUCH_DEBOUNCE_MS
#define TOUCH_DEBOUNCE_MS 300
#endif
#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG 0
#endif
