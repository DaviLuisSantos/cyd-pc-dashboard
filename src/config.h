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
#define AGENT_HOST "auto"   // IP da máquina que roda o agente, ou "auto" (descoberta na rede)
#endif
#ifndef AGENT_PORT
#define AGENT_PORT 8377
#endif
// Token do agente ("token" no agent/config.json). Vazio = agente sem token.
// Editável no portal/web.
#ifndef AGENT_TOKEN
#define AGENT_TOKEN ""
#endif
// Porta UDP em que o agente anuncia a si mesmo (discovery.port no config.json)
#ifndef DISCOVERY_PORT
#define DISCOVERY_PORT 8377
#endif
#ifndef AGENT_HTTP_TIMEOUT_MS
#define AGENT_HTTP_TIMEOUT_MS 1500
#endif

// Intervalos de consulta de cada endpoint (ms)
#ifndef AGENT_POLL_MS
#define AGENT_POLL_MS 2000     // /stats
#endif
// Com vários discos ("disks" no config do agente), o card do disco alterna
// entre eles nesse intervalo (tocar no card pula pro próximo)
#ifndef DISK_CYCLE_MS
#define DISK_CYCLE_MS 5000
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
#ifndef MEDIA_POLL_MS
#define MEDIA_POLL_MS 2000     // /media (só com a aba aberta; a barra anda sozinha entre consultas)
#endif
#ifndef ACTIONS_POLL_MS
#define ACTIONS_POLL_MS 60000  // /actions (lista de atalhos; só muda quando o config do agente muda)
#endif
#ifndef SERVICES_POLL_MS
#define SERVICES_POLL_MS 15000 // /services (o agente verifica a cada 30s)
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
#ifndef ENABLE_SCREEN_SERVICES
#define ENABLE_SCREEN_SERVICES 1
#endif
#ifndef ENABLE_SCREEN_ACTIONS
#define ENABLE_SCREEN_ACTIONS 1
#endif
#ifndef ENABLE_SCREEN_MEDIA
#define ENABLE_SCREEN_MEDIA 1
#endif

// Ordem das abas (e quais aparecem) — ids separados por vírgula. A primeira é a
// aba inicial. Editável no portal. Ids: clock, pcstats, procs, sysinfo, quotes, youtube,
// services, actions, media (uma tela com ENABLE_SCREEN_* 0 nem é compilada, então o id dela é ignorado).
#ifndef SCREEN_ORDER
#define SCREEN_ORDER "clock,pcstats,procs,sysinfo,quotes,youtube,services,actions"
// (media fica fora da lista padrão: a tab bar comporta 8 abas — troque por uma que não use)
#endif

// ===== Navegação (default; editável no portal, em segundos) =====
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
// Relógio 24h ou 12h (com AM/PM). Editável no portal/web.
#ifndef CLOCK_FORMAT
#define CLOCK_FORMAT 24
#endif
// Formato da data: "auto" (pelo idioma), "long" (Mon, Oct 12 2026),
// "dmy" (12/10/2026), "mdy" (10/12/2026), "ymd" (2026-10-12). Editável no portal/web.
#ifndef DATE_FORMAT
#define DATE_FORMAT "auto"
#endif

// ===== Tema de cores (editável no portal/web) =====
// dracula, nord, gruvbox, mocha, tokyonight, latte (claro)
#ifndef THEME
#define THEME "dracula"
#endif

// ===== Display (rotação, inversão e brilho são editáveis no portal) =====
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

// ===== LED RGB de status e sensor de luz (hardware da CYD) =====
// LED: vermelho pulsando com alerta, pisca com notificação, roxo na OTA.
// Na CYD ele é ligado ao contrário (acende com nível baixo).
#ifndef LED_R_PIN
#define LED_R_PIN 4
#endif
#ifndef LED_G_PIN
#define LED_G_PIN 16
#endif
#ifndef LED_B_PIN
#define LED_B_PIN 17
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW true
#endif
#ifndef LED_ENABLED
#define LED_ENABLED true      // editável no portal/web
#endif
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 15     // % (o LED é forte; editável no portal/web)
#endif
// Sensor de luz (LDR): leitura crua no "escuro" e no "claro" (ver ambient.h;
// /api/status mostra a leitura atual pra calibrar). Na CYD a leitura SOBE no
// escuro. Medido numa placa (atenuação 0 dB): mão cobrindo ~2900, sala
// iluminada ~1550, lanterna do celular ~1050.
#ifndef LDR_PIN
#define LDR_PIN 34
#endif
#ifndef LDR_DARK_RAW
#define LDR_DARK_RAW 2800
#endif
#ifndef LDR_BRIGHT_RAW
#define LDR_BRIGHT_RAW 1000
#endif
#ifndef AMBIENT_SAMPLE_MS
#define AMBIENT_SAMPLE_MS 250
#endif
#ifndef AUTO_BRIGHT
#define AUTO_BRIGHT false     // brilho acompanha a luz ambiente (editável no portal/web)
#endif
#ifndef AUTO_BRIGHT_MIN
#define AUTO_BRIGHT_MIN 30    // brilho no escuro com o automático ligado
#endif

// ===== Modo noturno (default; editável no portal) =====
// Entre NIGHT_START e NIGHT_END (hora local, "HH:MM") o brilho cai para
// NIGHT_BRIGHTNESS (0 = tela apagada; um toque acende por NIGHT_WAKE_MS).
// Início igual ao fim = desativado.
#ifndef NIGHT_START
#define NIGHT_START "00:00"
#endif
#ifndef NIGHT_END
#define NIGHT_END "00:00"
#endif
#ifndef NIGHT_BRIGHTNESS
#define NIGHT_BRIGHTNESS 0
#endif
#ifndef NIGHT_WAKE_MS
#define NIGHT_WAKE_MS 30000
#endif

// ===== Alertas (default; editáveis no portal/web) =====
// Limite em % de CPU/RAM/disco (0 = sem alerta). A cor do anel/barra fica
// vermelha a partir do limite e laranja 20 pontos abaixo. Acima do limite por
// ALERT_HOLD_MS seguidos, o ícone da aba PC pisca em vermelho em qualquer aba
// (e, com ALERT_JUMP, o painel pula pra aba PC).
#ifndef ALERT_CPU
#define ALERT_CPU 90
#endif
#ifndef ALERT_RAM
#define ALERT_RAM 90
#endif
#ifndef ALERT_DISK
#define ALERT_DISK 95
#endif
#ifndef ALERT_TEMP
#define ALERT_TEMP 85      // °C da CPU (precisa de fonte de temperatura no agente)
#endif
#ifndef ALERT_JUMP
#define ALERT_JUMP false
#endif
#ifndef ALERT_HOLD_MS
#define ALERT_HOLD_MS 10000
#endif
#ifndef ALERT_POLL_MS
#define ALERT_POLL_MS 5000     // /stats em segundo plano quando a aba PC não está aberta
#endif

// ===== Notificações (POST /api/notify, agent/notify.py) =====
#ifndef NOTIFY_DEFAULT_S
#define NOTIFY_DEFAULT_S 8   // quanto tempo fica na tela se o request não disser
#endif

// ===== Rollback de OTA (ota_guard.h) =====
// Firmware novo é confirmado depois de WiFi conectado + OTA_VERIFY_MIN_MS de
// loop(); sem isso em OTA_VERIFY_TIMEOUT_MS, volta pra versão anterior.
#ifndef OTA_VERIFY_MIN_MS
#define OTA_VERIFY_MIN_MS 15000
#endif
#ifndef OTA_VERIFY_TIMEOUT_MS
#define OTA_VERIFY_TIMEOUT_MS 120000
#endif

// ===== Interface web (http://<ip-do-painel>/ ou http://cyd-dashboard.local/) =====
#ifndef WEB_PORT
#define WEB_PORT 80
#endif
#ifndef MDNS_NAME
#define MDNS_NAME "cyd-dashboard"
#endif
#ifndef WEB_PASSWORD
#define WEB_PASSWORD ""   // vazio = sem senha (usuário: admin). Editável no portal/web
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
