#pragma once
#include "screens.h"
#include "agent_client.h"
#include <string.h>

// Mídia — o que está tocando no PC (GET /media do agente: título, artista, app,
// posição) e controles anterior / tocar-pausar / próxima / volume, que viram as
// teclas de mídia no PC (POST /media/<cmd>). Consulta a cada MEDIA_POLL_MS só
// com a aba aberta; entre consultas a barra de progresso anda sozinha.
class MediaScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_MEDIA; }
    uint16_t accentColor() override { return THEME_CYAN; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconMusic(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        mode_ = NONE;
        flashIdx_ = -1;
        nextPollMs_ = millis();
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
    }

    void update(TFT_eSPI& tft) override {
        if (flashIdx_ >= 0 && (long)(millis() - flashUntil_) >= 0) {
            drawButton(tft, flashIdx_, THEME_CARD);
            flashIdx_ = -1;
        }
        if ((long)(millis() - nextPollMs_) >= 0) poll(tft);
        if (mode_ == PLAYER) drawProgress(tft, false);
    }

    void onTouch(TFT_eSPI& tft, int x, int y) override {
        if (mode_ != PLAYER || y < BTN_Y || y >= BTN_Y + BTN_H) return;
        int i = (x - BTN_X0) / (BTN_W + BTN_GAP);
        if (i < 0 || i >= 5 || x < btnX(i) || x >= btnX(i) + BTN_W) return;

        drawButton(tft, i, accentColor());  // aparece antes do POST (bloqueia até a resposta)
        static const char* const CMDS[] = {"prev", "play_pause", "next", "vol_down", "vol_up"};
        char path[24];
        snprintf(path, sizeof(path), "/media/%s", CMDS[i]);
        JsonDocument doc;
        int code = agent::post(path, doc);
        if (code == 200) {
            if (i == 1) {  // otimista: o botão já mostra o novo estado até a próxima leitura
                playing_ = !playing_;
                fetchPos_ = currentPos();
                fetchMs_ = millis();
            }
            flash(i, 150);
            nextPollMs_ = millis() + 350;  // o agente relê na hora; pega o estado novo
        } else {
            drawButton(tft, i, THEME_RED);
            flash(i, 1000);
            agent::showResult(tft, code == agent::POST_NO_WIFI ? agent::NO_WIFI
                                   : code == agent::POST_SEARCHING ? agent::SEARCHING
                                   : code == 401 ? agent::BAD_TOKEN : agent::DOWN);
        }
    }

private:
    enum Mode { NONE, HINT, PLAYER };
    // Controles: 5 botões na base, acima do status (y=221)
    static const int BTN_X0 = 8, BTN_Y = 166, BTN_W = 56, BTN_H = 48, BTN_GAP = 6;
    static const int BAR_Y = 130;

    int btnX(int i) const { return BTN_X0 + i * (BTN_W + BTN_GAP); }

    void flash(int i, unsigned long ms) {
        flashIdx_ = i;
        flashUntil_ = millis() + ms;
    }

    void poll(TFT_eSPI& tft) {
        nextPollMs_ = millis() + MEDIA_POLL_MS;
        JsonDocument doc;
        agent::Result r = agent::get("/media", doc);
        agent::showResult(tft, r);
        if (r != agent::OK) return;

        if (!(doc["available"] | false)) {
            drawHint(tft, doc["reason"] | "");
            return;
        }
        if (mode_ != PLAYER) {
            mode_ = PLAYER;
            tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
            theme::drawBar(tft, 12, BAR_Y, 296, 8, 0, accentColor());
            drawnSec_ = -2;
            title_[0] = artist_[0] = app_[0] = '\0';
            forceText_ = true;
            btnPlaying_ = labelPlaying_ = -1;  // botões e linha do app de novo
        }

        playing_ = doc["playing"] | false;
        fetchPos_ = doc["position_s"].isNull() ? -1 : doc["position_s"].as<float>();
        duration_ = doc["duration_s"].isNull() ? -1 : doc["duration_s"].as<float>();
        fetchMs_ = millis();

        const char* title = doc["title"] | "";
        const char* artist = doc["artist"] | "";
        const char* app = doc["app"] | "";
        if (forceText_ || strcmp(title, title_)) {
            strlcpy(title_, title, sizeof(title_));
            drawTitle(tft);
            drawnSec_ = -2;  // faixa nova: barra e tempos de novo
        }
        if (forceText_ || strcmp(artist, artist_)) {
            strlcpy(artist_, artist, sizeof(artist_));
            theme::drawValue(tft, 12, 102, 296, 18, artist_, 2, accentColor(), THEME_BG);
        }
        // Estado desenhado x atual (o toque já troca o botão antes da leitura)
        if (forceText_ || labelPlaying_ != playing_ || strcmp(app, app_)) {
            strlcpy(app_, app, sizeof(app_));
            labelPlaying_ = playing_;
            char line[48];
            if (!playing_ && title_[0]) snprintf(line, sizeof(line), "%s  -  %s", app_, i18n::tr().MEDIA_PAUSED);
            else strlcpy(line, app_, sizeof(line));
            theme::drawValue(tft, 12, 40, 296, 16, line, 2, THEME_COMMENT, THEME_BG);
        }
        if (btnPlaying_ < 0) {
            for (int i = 0; i < 5; i++) drawButton(tft, i, THEME_CARD);
        } else if (btnPlaying_ != playing_ && flashIdx_ != 1) {
            drawButton(tft, 1, THEME_CARD);
        }
        forceText_ = false;
        drawProgress(tft, true);
    }

    // Posição agora: a da última leitura + o tempo desde então (se tocando)
    float currentPos() const {
        if (fetchPos_ < 0) return -1;
        float p = fetchPos_ + (playing_ ? (millis() - fetchMs_) / 1000.0f : 0);
        return duration_ > 0 && p > duration_ ? duration_ : p;
    }

    static void fmtTime(char* buf, size_t len, float s) {
        int t = (int)s;
        if (t >= 3600) snprintf(buf, len, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
        else snprintf(buf, len, "%d:%02d", t / 60, t % 60);
    }

    // Barra + tempos, só quando o segundo mostrado muda
    void drawProgress(TFT_eSPI& tft, bool force) {
        float pos = currentPos();
        int sec = pos < 0 ? -1 : (int)pos;
        if (!force && sec == drawnSec_) return;
        drawnSec_ = sec;
        float pct = (pos >= 0 && duration_ > 0) ? pos * 100 / duration_ : 0;
        theme::drawBar(tft, 12, BAR_Y, 296, 8, pct, accentColor());
        char left[12] = "", right[12] = "";
        if (pos >= 0) fmtTime(left, sizeof(left), pos);
        if (duration_ > 0) fmtTime(right, sizeof(right), duration_);
        theme::drawValue(tft, 12, 142, 100, 16, left, 2, THEME_COMMENT, THEME_BG);
        theme::drawValue(tft, 208, 142, 100, 16, right, 2, THEME_COMMENT, THEME_BG, MR_DATUM);
    }

    // Título em até 2 linhas (quebra por palavra, "..." se não couber), smooth
    // font num sprite só. Nada tocando: aviso no lugar.
    void drawTitle(TFT_eSPI& tft) {
        const int w = 296, h = 40;
        TFT_eSprite& s = theme::canvas().begin(tft, w, h, THEME_BG);
        s.loadFont(NotoSansBold15);
        s.setTextDatum(ML_DATUM);
        if (!title_[0]) {
            s.setTextColor(THEME_COMMENT, THEME_BG);
            s.drawString(i18n::tr().MEDIA_IDLE, 0, h / 2);
        } else {
            s.setTextColor(THEME_FG, THEME_BG);
            char line1[sizeof(title_)], line2[sizeof(title_)];
            splitLine(s, title_, w, line1, line2);
            if (s.textWidth(line2) > w) ellipsize(s, line2, w);
            if (line2[0]) {
                s.drawString(line1, 0, 10);
                s.drawString(line2, 0, 30);
            } else {
                s.drawString(line1, 0, h / 2);
            }
        }
        s.unloadFont();
        theme::canvas().push(12, 60);
    }

    // line1 = o máximo de palavras que cabe em w; line2 = o resto
    static void splitLine(TFT_eSprite& s, const char* text, int w, char* line1, char* line2) {
        size_t n = strlen(text), best = 0;
        char buf[96];
        for (size_t i = 0; i <= n; i++) {
            if (text[i] != ' ' && text[i] != '\0') continue;
            size_t len = i < sizeof(buf) - 1 ? i : sizeof(buf) - 1;
            memcpy(buf, text, len);
            buf[len] = '\0';
            if (s.textWidth(buf) > w) break;
            best = i;
        }
        if (best == 0) {  // uma palavra maior que a linha: corta no meio dela
            strlcpy(line1, text, 96);
            ellipsize(s, line1, w);
            line2[0] = '\0';
            return;
        }
        memcpy(line1, text, best);
        line1[best] = '\0';
        const char* rest = text + best;
        while (*rest == ' ') rest++;
        strlcpy(line2, rest, 96);
    }

    static void ellipsize(TFT_eSprite& s, char* text, int w) {
        size_t n = strlen(text);
        while (n > 0 && s.textWidth(text) > w) {
            text[--n] = '\0';
            if (n >= 3) {
                char tmp[96];
                snprintf(tmp, sizeof(tmp), "%.*s...", (int)(n - 3), text);
                if (s.textWidth(tmp) <= w) { strlcpy(text, tmp, 96); return; }
            }
        }
    }

    // Botão de controle: fundo `fill` (CARD normal, destaque ao tocar, vermelho na falha)
    void drawButton(TFT_eSPI& tft, int i, uint16_t fill) {
        const int x = btnX(i);
        tft.fillSmoothRoundRect(x, BTN_Y, BTN_W, BTN_H, 8, fill, THEME_BG);
        const int cx = x + BTN_W / 2, cy = BTN_Y + BTN_H / 2;
        const uint16_t fg = fill == THEME_CARD ? (i == 1 ? accentColor() : THEME_FG) : THEME_BG;
        switch (i) {
            case 0: theme::iconSkip(tft, cx, cy, 10, fg, -1); break;
            case 1:
                theme::iconTransport(tft, cx, cy, 11, fg, playing_);
                btnPlaying_ = playing_;
                break;
            case 2: theme::iconSkip(tft, cx, cy, 10, fg, 1); break;
            case 3: theme::iconSpeaker(tft, cx, cy, 10, fg, 2); break;
            case 4: theme::iconSpeaker(tft, cx, cy, 10, fg, 1); break;
        }
    }

    void drawHint(TFT_eSPI& tft, const char* reason) {
        if (mode_ == HINT && !strcmp(reason, reason_)) return;
        mode_ = HINT;
        strlcpy(reason_, reason, sizeof(reason_));
        const i18n::Lang& L = i18n::tr();
        const char* text = !strcmp(reason, "missing_winrt") ? L.MEDIA_NO_WINRT
                           : !strcmp(reason, "no_playerctl") ? L.MEDIA_NO_PLAYERCTL
                           : !strcmp(reason, "disabled") ? L.MEDIA_DISABLED
                                                         : L.MEDIA_UNSUPPORTED;
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
        theme::iconMusic(tft, 160, 100, 16, THEME_COMMENT);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(THEME_COMMENT, THEME_BG);
        tft.drawString(text, 160, 150, 2);
    }

    Mode mode_ = NONE;
    char title_[96] = "";
    char artist_[64] = "";
    char app_[24] = "";
    char reason_[16] = "";
    bool playing_ = false;
    int8_t btnPlaying_ = -1;    // estado mostrado no botão tocar/pausar (-1 = nada desenhado)
    int8_t labelPlaying_ = -1;  // idem na linha do app ("pausado")
    bool forceText_ = true;
    float fetchPos_ = -1, duration_ = -1;
    unsigned long fetchMs_ = 0;
    unsigned long nextPollMs_ = 0;
    int drawnSec_ = -2;
    int flashIdx_ = -1;
    unsigned long flashUntil_ = 0;
};
