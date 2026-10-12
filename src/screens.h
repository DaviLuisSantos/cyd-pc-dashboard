#pragma once
#include <TFT_eSPI.h>
#include "theme.h"
#include "config.h"

// ===== Contrato de tela =====
// Cada tela implementa essa interface. O ScreenManager cuida da navegação
// por abas (tab bar fixa no topo) e do desenho de cada aba.
class Screen {
public:
    virtual ~Screen() {}
    virtual const char* name() = 0;
    // Cor de destaque da aba (usada na aba ativa e em detalhes da tela)
    virtual uint16_t accentColor() { return DRACULA_PINK; }
    // Desenha o ícone da aba (18x18px aprox.), centrado em (cx, cy)
    virtual void drawIcon(TFT_eSPI& tft, int cx, int cy, uint16_t color) = 0;
    // Chamado uma vez ao entrar na tela (desenha o layout estático da área de conteúdo)
    virtual void onEnter(TFT_eSPI& tft) = 0;
    // Chamado em loop (atualiza só o que mudou — evitar redesenho total)
    virtual void update(TFT_eSPI& tft) = 0;
    // Toque dentro da área de conteúdo (abaixo da tab bar)
    virtual void onTouch(TFT_eSPI& tft, int x, int y) {}
};

// ===== Gerenciador =====
class ScreenManager {
public:
    static const int MAX_SCREENS = 8;

    void add(Screen* s) {
        if (count_ < MAX_SCREENS) screens_[count_++] = s;
    }

    void begin(TFT_eSPI& tft) {
        tft_ = &tft;
        if (count_ > 0) enter(0);
    }

    void update() {
        if (current_ >= 0) screens_[current_]->update(*tft_);

        // Auto-next: troca de aba sozinho depois de AUTO_NEXT_MS sem interação
        if (autoNext_ && AUTO_NEXT_MS > 0 && count_ > 1 && millis() - lastActivityMs_ >= AUTO_NEXT_MS) {
            next();
        }
    }

    // Recebe qualquer toque já em coordenadas de tela (landscape 320x240)
    void handleTouch(int x, int y) {
        lastActivityMs_ = millis();
        if (count_ == 0) return;
        if (y < TAB_BAR_H) {
            int slotW = SCREEN_W / count_;
            int idx = x / slotW;
            if (idx >= 0 && idx < count_ && idx != current_) enter(idx);
            return;
        }
        if (current_ >= 0) screens_[current_]->onTouch(*tft_, x, y);
    }

    void nextScreen() { next(); }
    bool autoNext() const { return autoNext_; }
    void setAutoNext(bool on) { autoNext_ = on; lastActivityMs_ = millis(); }

private:
    void next() { if (count_) enter((current_ + 1) % count_); }

    void enter(int idx) {
        int prev = current_;
        current_ = idx;
        lastActivityMs_ = millis();
        if (prev >= 0 && prev != idx) {
            // Ir pra uma aba à direita (ou dar a volta) varre pra frente;
            // voltar pra uma aba à esquerda varre pra trás.
            bool forward = (idx == (prev + 1) % count_) || idx > prev;
            wipe(screens_[idx]->accentColor(), forward);
        } else {
            tft_->fillRect(0, TAB_BAR_H, SCREEN_W, SCREEN_H - TAB_BAR_H, DRACULA_BG);
        }
        drawTabBar();
        screens_[idx]->onEnter(*tft_);
    }

    // Transição de aba: uma linha vertical na cor de destaque da tela nova
    // varre a área de conteúdo, apagando a tela antiga atrás de si.
    void wipe(uint16_t accent, bool forward) {
        const int stepW = 10;
        const int contentH = SCREEN_H - TAB_BAR_H;
        for (int x = 0; x < SCREEN_W; x += stepW) {
            int px = forward ? x : SCREEN_W - stepW - x;
            tft_->fillRect(px, TAB_BAR_H, stepW, contentH, DRACULA_BG);
            int lx = forward ? px + stepW : px - 1;
            if (lx >= 0 && lx < SCREEN_W) {
                tft_->drawFastVLine(lx, TAB_BAR_H, contentH, accent);
            }
            delay(3);
        }
    }

    void drawTabBar() {
        tft_->fillRect(0, 0, SCREEN_W, TAB_BAR_H, DRACULA_CURRENT);
        int slotW = SCREEN_W / count_;
        for (int i = 0; i < count_; i++) {
            int cx = i * slotW + slotW / 2;
            bool active = (i == current_);
            if (active) {
                // "Aba" ativa: mesma cor do conteúdo, criando o efeito de continuidade
                tft_->fillSmoothRoundRect(i * slotW + 3, 2, slotW - 6, TAB_BAR_H, 6, DRACULA_BG, DRACULA_CURRENT);
                tft_->fillRect(cx - 10, TAB_BAR_H - 3, 20, 3, screens_[i]->accentColor());
            }
            uint16_t iconColor = active ? screens_[i]->accentColor() : DRACULA_COMMENT;
            screens_[i]->drawIcon(*tft_, cx, TAB_BAR_H / 2 - 1, iconColor);
        }
    }

    TFT_eSPI* tft_ = nullptr;
    Screen* screens_[MAX_SCREENS];
    int count_ = 0;
    int current_ = -1;
    unsigned long lastActivityMs_ = 0;
    bool autoNext_ = true;
};
