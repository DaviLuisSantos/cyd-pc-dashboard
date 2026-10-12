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

unsigned long lastTouchMs = 0;
const int BL_PWM_CH = 0;
const unsigned long SETUP_HOLD_MS = 1500;

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

    // Display em landscape (DISPLAY_ROTATION 1 = USB à direita, 3 = à esquerda)
    tft.init();
    tft.setRotation(DISPLAY_ROTATION);
    // Muitos CYD (ESP32-2432S028R) mostram as cores invertidas com o
    // ILI9341_2_DRIVER — sem isso, o fundo escuro do tema aparece esbranquiçado.
    // Se ficar pior (cores nítidas ao contrário do esperado), DISPLAY_INVERT false.
    tft.invertDisplay(DISPLAY_INVERT);
    tft.fillScreen(DRACULA_BG);

    // Fade-in do backlight com a tela já limpa no fundo do tema
    for (int duty = 0; duty <= BACKLIGHT_LEVEL; duty += 5) {
        ledcWrite(BL_PWM_CH, duty);
        delay(4);
    }
    ledcWrite(BL_PWM_CH, BACKLIGHT_LEVEL);

    // Touch no barramento próprio
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touch.begin(touchSPI);
    touch.setRotation(DISPLAY_ROTATION);

    connectWiFi(bootSplashWantsSetup());

    // NTP (fuso vem do portal ou de TZ_INFO)
    configTzTime(settings::get().tz, NTP_SERVER);

    // Registra telas — pra adicionar uma nova, é só criar a classe e dar add()
    // (habilitar/desabilitar cada uma via ENABLE_SCREEN_* no user_config.h)
#if ENABLE_SCREEN_CLOCK
    manager.add(&screenClock);
#endif
#if ENABLE_SCREEN_PCSTATS
    manager.add(&screenPc);
#endif
#if ENABLE_SCREEN_PROCS
    manager.add(&screenProcs);
#endif
#if ENABLE_SCREEN_SYSINFO
    manager.add(&screenSys);
#endif
#if ENABLE_SCREEN_QUOTES
    manager.add(&screenQuotes);
#endif
#if ENABLE_SCREEN_YOUTUBE
    manager.add(&screenYt);
#endif
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
