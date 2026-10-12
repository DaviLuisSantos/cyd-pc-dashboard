#pragma once
#include "screens.h"
#include "agent_client.h"
#include <string.h>

// Atalhos — botões que rodam uma ação no PC (as "actions" do agent/config.json:
// bloquear, volume, mídia, abrir app/URL, rodar comando). O painel só conhece
// nome/ícone/cor e manda o NÚMERO do botão (POST /actions/<n>); o que roda é
// decidido no agente. Toque: o botão acende na cor dele enquanto o agente
// responde, e fica vermelho com o motivo se falhar.
class ActionsScreen : public Screen {
public:
    const char* name() override { return i18n::tr().NAME_ACTIONS; }
    uint16_t accentColor() override { return THEME_YELLOW; }

    void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) override {
        theme::iconGrid(tft, cx, cy, 8, color);
    }

    void onEnter(TFT_eSPI& tft) override {
        flashIdx_ = -1;
        lastPollMs_ = 0;
        polled_ = false;
        hintDrawn_ = false;  // a tela foi limpa: a dica (se for o caso) é redesenhada
        // Lista da visita anterior já aparece; a consulta logo em seguida atualiza
        if (count_ > 0) drawLayout(tft);
        else tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
    }

    void update(TFT_eSPI& tft) override {
        if (flashIdx_ >= 0 && (long)(millis() - flashUntil_) >= 0) {
            drawButton(tft, flashIdx_, NORMAL);
            flashIdx_ = -1;
        }
        if (polled_ && millis() - lastPollMs_ < ACTIONS_POLL_MS) return;
        polled_ = true;
        lastPollMs_ = millis();

        JsonDocument doc;
        agent::Result r = agent::get("/actions", doc);
        if (r == agent::OK) {
            char sig[sizeof(sig_)];
            parse(doc, sig, sizeof(sig));
            if (!(doc["configured"] | false)) drawNotConfigured(tft);
            else if (strcmp(sig, sig_) || hintDrawn_) drawLayout(tft);
            strlcpy(sig_, sig, sizeof(sig_));
        }
        agent::showResult(tft, r);
    }

    void onTouch(TFT_eSPI& tft, int x, int y) override {
        int i = hit(x, y);
        if (i < 0 || hintDrawn_) return;
        if (flashIdx_ >= 0 && flashIdx_ != i) drawButton(tft, flashIdx_, NORMAL);

        drawButton(tft, i, PRESSED);  // aparece antes do POST (que bloqueia até a resposta)
        char path[16];
        snprintf(path, sizeof(path), "/actions/%d", i);
        JsonDocument doc;
        int code = agent::post(path, doc);

        if (code == 200) {
            flash(tft, i, DONE, 250);
            agent::showOk(tft);  // apaga um "agente offline" de antes
            return;
        }
        // Erro do agente (curto, cabe no botão: "missing", "busy"...); sem
        // conexão/token o botão diz só "falhou" e o motivo vai pro rodapé
        if (code > 0 && code != 401 && doc["error"].is<const char*>()) {
            strlcpy(error_, doc["error"], sizeof(error_));
        } else {
            strlcpy(error_, i18n::tr().ACT_FAILED, sizeof(error_));
            agent::showResult(tft, code == agent::POST_NO_WIFI ? agent::NO_WIFI
                                   : code == agent::POST_SEARCHING ? agent::SEARCHING
                                   : code == 401 ? agent::BAD_TOKEN : agent::DOWN);
        }
        flash(tft, i, FAILED, 1500);
    }

private:
    enum State { NORMAL, PRESSED, DONE, FAILED };
    static const int MAX = 8;
    // Área dos botões: abaixo da tab bar até o status (y=221)
    static const int AREA_X = 8, AREA_Y = 38, AREA_W = 304, AREA_H = 178, GAP = 6;

    struct Act { char name[15]; char icon[12]; char color[8]; };

    void parse(JsonDocument& doc, char* sig, size_t sigLen) {
        count_ = 0;
        sig[0] = '\0';
        for (JsonObject a : doc["actions"].as<JsonArray>()) {
            if (count_ >= MAX) break;
            Act& v = acts_[count_++];
            strlcpy(v.name, a["name"] | "?", sizeof(v.name));
            strlcpy(v.icon, a["icon"] | "bolt", sizeof(v.icon));
            strlcpy(v.color, a["color"] | "yellow", sizeof(v.color));
            char one[40];
            snprintf(one, sizeof(one), "%s|%s|%s;", v.name, v.icon, v.color);
            strlcat(sig, one, sigLen);
        }
    }

    // 1-2 ações: uma linha; 3-4: 2x2; 5-8: 4x2
    int cols() const { return count_ <= 2 ? (count_ < 1 ? 1 : count_) : count_ <= 4 ? 2 : 4; }
    int rows() const { return (count_ + cols() - 1) / cols(); }
    int btnW() const { return (AREA_W - (cols() - 1) * GAP) / cols(); }
    int btnH() const { return (AREA_H - (rows() - 1) * GAP) / rows(); }
    int btnX(int i) const { return AREA_X + (i % cols()) * (btnW() + GAP); }
    int btnY(int i) const { return AREA_Y + (i / cols()) * (btnH() + GAP); }

    int hit(int x, int y) const {
        for (int i = 0; i < count_; i++) {
            if (x >= btnX(i) && x < btnX(i) + btnW() && y >= btnY(i) && y < btnY(i) + btnH()) return i;
        }
        return -1;
    }

    void flash(TFT_eSPI& tft, int i, State s, unsigned long ms) {
        drawButton(tft, i, s);
        flashIdx_ = i;
        flashUntil_ = millis() + ms;
    }

    void drawLayout(TFT_eSPI& tft) {
        hintDrawn_ = false;
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
        for (int i = 0; i < count_; i++) drawButton(tft, i, NORMAL);
    }

    void drawNotConfigured(TFT_eSPI& tft) {
        if (hintDrawn_) return;
        hintDrawn_ = true;
        sig_[0] = '\0';
        tft.fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H - 20, THEME_BG);
        theme::iconGrid(tft, 160, 100, 16, THEME_COMMENT);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(THEME_COMMENT, THEME_BG);
        tft.drawString(i18n::tr().ACT_HINT_1, 160, 145, 2);
        tft.drawString(i18n::tr().ACT_HINT_2, 160, 165, 2);
    }

    // Fundo do botão direto na tela (uma chamada só — muda de cor inteiro ao
    // tocar) e ícone + nome num sprite pequeno no centro, sem flicker. Sprite
    // limitado a ~140x62: um botão grande (1-2 ações) teria 50-100 KB de sprite.
    void drawButton(TFT_eSPI& tft, int i, State s) {
        const Act& a = acts_[i];
        const int x = btnX(i), y = btnY(i), w = btnW(), h = btnH();
        const uint16_t accent = theme::accentByName(a.color);
        uint16_t fill = THEME_CARD, fg = accent, text = THEME_FG;
        if (s == PRESSED || s == DONE) { fill = accent; fg = text = THEME_BG; }
        else if (s == FAILED) { fill = THEME_RED; fg = text = THEME_BG; }
        tft.fillSmoothRoundRect(x, y, w, h, 8, fill, THEME_BG);

        const int cw = w - 8 < 140 ? w - 8 : 140, ch = 62;
        TFT_eSprite& spr = theme::canvas().begin(tft, cw, ch, fill);
        const int size = h >= 80 ? 13 : 11;
        theme::actionIcon(spr, a.icon, cw / 2, 18, size, fg);

        char label[sizeof(error_)];
        strlcpy(label, s == FAILED ? error_ : a.name, sizeof(label));
        // Fonte suavizada se couber; senão a font 2, mais estreita (~10 letras
        // num botão de 4 colunas), e só então corta
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(text, fill);
        spr.loadFont(NotoSansBold15);
        if (spr.textWidth(label) <= cw - 4) {
            spr.drawString(label, cw / 2, 50);
            spr.unloadFont();
        } else {
            spr.unloadFont();
            spr.setTextFont(2);
            for (size_t n = strlen(label); n > 1 && spr.textWidth(label) > cw - 4; n--) label[n - 1] = '\0';
            spr.drawString(label, cw / 2, 50);
        }
        theme::canvas().push(x + (w - cw) / 2, y + (h - ch) / 2);
    }

    Act acts_[MAX];
    int count_ = 0;
    char sig_[MAX * 40] = "";
    char error_[16] = "";
    bool hintDrawn_ = false;
    bool polled_ = false;
    unsigned long lastPollMs_ = 0;
    int flashIdx_ = -1;
    unsigned long flashUntil_ = 0;
};
