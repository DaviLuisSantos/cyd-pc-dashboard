// CYD PC Dashboard — ESP32-2432S028R (Cheap Yellow Display)
// Painel auxiliar de mesa: stats do PC, processos, rede, cotações, YouTube
// Navegação: tab bar touch no topo (+ auto-next configurável)
// Configuração: portal WiFi no primeiro boot (ou segurando a tela no boot),
// com defaults de compilação em src/user_config.h (veja user_config.example.h)

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include <time.h>

#include "config.h"
#include "settings.h"
#include "i18n.h"
#include "wifi_setup.h"
#include "serial_cmds.h"
#include "screens.h"
#include "screen_clock.h"
#include "screen_pcstats.h"
#include "screen_procs.h"
#include "screen_sysinfo.h"
#include "screen_quotes.h"
#include "screen_youtube.h"

TFT_eSPI tft;

// Touch usa um barramento SPI próprio (VSPI) — pinos diferentes do display
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touch(XPT2046_CS, XPT2046_IRQ);

ScreenManager manager;
#if ENABLE_SCREEN_CLOCK
ClockScreen screenClock;
#endif
#if ENABLE_SCREEN_PCSTATS
PcStatsScreen screenPc;
#endif
#if ENABLE_SCREEN_PROCS
ProcsScreen screenProcs;
#endif
#if ENABLE_SCREEN_SYSINFO
SysInfoScreen screenSys;
#endif
#if ENABLE_SCREEN_QUOTES
QuotesScreen screenQuotes;
#endif
#if ENABLE_SCREEN_YOUTUBE
YouTubeScreen screenYt;
#endif

// Id de cada tela compilada — é o que vai na lista de abas do portal (SCREEN_ORDER).
// O sentinela no fim evita array vazio se todas as ENABLE_SCREEN_* forem 0.
struct ScreenEntry { const char* id; Screen* screen; };
ScreenEntry allScreens[] = {
#if ENABLE_SCREEN_CLOCK
    {"clock", &screenClock},
#endif
#if ENABLE_SCREEN_PCSTATS
    {"pcstats", &screenPc},
#endif
#if ENABLE_SCREEN_PROCS
    {"procs", &screenProcs},
#endif
#if ENABLE_SCREEN_SYSINFO
    {"sysinfo", &screenSys},
#endif
#if ENABLE_SCREEN_QUOTES
    {"quotes", &screenQuotes},
#endif
#if ENABLE_SCREEN_YOUTUBE
    {"youtube", &screenYt},
#endif
    {nullptr, nullptr},
};

unsigned long lastTouchMs = 0;
const int BL_PWM_CH = 0;
const unsigned long SETUP_HOLD_MS = 1500;

// Rotação/inversão vêm do NVS (portal) ou do config.h — reaplicado depois do
// portal, que pode ter mudado os valores
void applyDisplaySettings() {
    const settings::Settings& s = settings::get();
    tft.setRotation(s.rotation);
    // Muitos CYD (ESP32-2432S028R) mostram as cores invertidas com o
    // ILI9341_2_DRIVER — sem isso, o fundo escuro do tema aparece esbranquiçado.
    // Se ficar pior (cores nítidas ao contrário do esperado), desmarcar no portal.
    tft.invertDisplay(s.invert);
    touch.setRotation(s.rotation);
}

// Registra as abas na ordem da lista do portal ("pcstats, procs, quotes").
// Id desconhecido ou repetido é ignorado; lista sem nenhum id válido mostra todas.
void registerScreens() {
    char list[sizeof(settings::get().screens)];
    strlcpy(list, settings::get().screens, sizeof(list));
    bool added[sizeof(allScreens) / sizeof(allScreens[0])] = {};

    for (char* tok = strtok(list, ", ;"); tok; tok = strtok(nullptr, ", ;")) {
        bool found = false;
        for (int i = 0; allScreens[i].id; i++) {
            if (strcasecmp(tok, allScreens[i].id) == 0) {
                found = true;
                if (!added[i]) { manager.add(allScreens[i].screen); added[i] = true; }
                break;
            }
        }
        if (!found) Serial.printf("aba desconhecida ou desabilitada na compilacao: %s\n", tok);
    }
    if (manager.count() == 0) {
        for (int i = 0; allScreens[i].id; i++) manager.add(allScreens[i].screen);
    }
}

// Splash do boot com a dica "segure a tela para configurar". Retorna true se
// o usuário ainda está segurando a tela no fim da barrinha — aí o portal abre direto.
bool bootSplashWantsSetup() {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_FG, DRACULA_BG);
    tft.drawString("CYD PC Dashboard", 160, 100, 4);
    tft.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    tft.drawString(i18n::tr().HOLD_FOR_SETUP, 160, 140, 2);
    tft.drawString(FW_VERSION, 160, 225, 1);

    unsigned long start = millis();
    bool held = false;
    while (millis() - start < SETUP_HOLD_MS) {
        held = touch.touched();
        int w = (int)((millis() - start) * 160 / SETUP_HOLD_MS);
        tft.fillRect(80, 170, w, 3, held ? DRACULA_PURPLE : DRACULA_CURRENT);
        delay(20);
    }
    return held;
}

void connectWiFi(bool forcePortal) {
    tft.fillScreen(DRACULA_BG);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(DRACULA_FG, DRACULA_BG);
    tft.drawString(i18n::tr().CONNECTING_WIFI, 160, 110, 4);

    bool ok = wifisetup::connect(tft, forcePortal);
    applyDisplaySettings();
    ledcWrite(BL_PWM_CH, settings::get().brightness);

    // O portal pode ter mudado o idioma — redesenha a tela de status inteira
    tft.fillScreen(DRACULA_BG);
    tft.setTextDatum(MC_DATUM);
    if (ok) {
        tft.setTextColor(DRACULA_FG, DRACULA_BG);
        tft.drawString(WiFi.SSID(), 160, 110, 4);
        tft.setTextColor(DRACULA_CYAN, DRACULA_BG);
        tft.drawString(WiFi.localIP().toString(), 160, 150, 2);
        delay(800);
    } else {
        tft.setTextColor(DRACULA_ORANGE, DRACULA_BG);
        tft.drawString(i18n::tr().NO_WIFI_OFFLINE, 160, 130, 2);
        delay(1200);
    }
}

void setup() {
    Serial.begin(115200);
    settings::load();

    // Backlight via PWM (permite fade-in suave no boot em vez de ligar seco)
    ledcSetup(BL_PWM_CH, 5000, 8);
    ledcAttachPin(TFT_BL, BL_PWM_CH);
    ledcWrite(BL_PWM_CH, 0);

    // Touch no barramento próprio
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSPI);

    // Display em landscape (rotação 1 = USB à direita, 3 = à esquerda)
    tft.init();
    applyDisplaySettings();
    tft.fillScreen(DRACULA_BG);

    // Fade-in do backlight com a tela já limpa no fundo do tema
    const uint8_t bright = settings::get().brightness;
    for (int duty = 0; duty <= bright; duty += 5) {
        ledcWrite(BL_PWM_CH, duty);
        delay(4);
    }
    ledcWrite(BL_PWM_CH, bright);

    connectWiFi(bootSplashWantsSetup());

    // NTP (fuso vem do portal ou de TZ_INFO)
    configTzTime(settings::get().tz, NTP_SERVER);

    // Abas na ordem configurada no portal (SCREEN_ORDER por default) — pra
    // adicionar uma tela nova, é só criar a classe e pôr em allScreens[]
    registerScreens();
    manager.setAutoNextInterval(settings::get().autoNextS * 1000UL);
    manager.begin(tft);
}

void loop() {
    // Touch com debounce
    if (touch.tirqTouched() && touch.touched()) {
        unsigned long now = millis();
        if (now - lastTouchMs > TOUCH_DEBOUNCE_MS) {
            lastTouchMs = now;
            TS_Point p = touch.getPoint();
#if TOUCH_DEBUG
            Serial.printf("touch raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
#endif
            // Mapeia coordenadas cruas -> tela 320x240 (landscape)
            int x = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, 320);
            int y = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, 240);
            x = constrain(x, 0, 319);
            y = constrain(y, 0, 239);
            manager.handleTouch(x, y);
        }
    }

    serialcmds::poll(tft, manager);
    manager.update();
    delay(20);
}
