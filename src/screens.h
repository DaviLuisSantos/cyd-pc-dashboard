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
    // true = tela em alerta: o ícone dela pisca em vermelho na tab bar
    virtual bool alerting() { return false; }
};

// ===== Gerenciador =====
class ScreenManager {
public:
    static const int MAX_SCREENS = 8;

    void add(Screen* s) {
        if (count_ < MAX_SCREENS) screens_[count_++] = s;
    }

    // Entra na aba `start` (ou na primeira, se ela não estiver registrada)
    void begin(TFT_eSPI& tft, Screen* start = nullptr) {
        tft_ = &tft;
        if (count_ == 0) return;
        int idx = 0;
        for (int i = 0; i < count_; i++) {
            if (screens_[i] == start) idx = i;
        }
        enter(idx);
    }

    void update() {
        if (current_ >= 0) screens_[current_]->update(*tft_);
        updateAlerts();

        // Auto-next: troca de aba sozinho depois de autoNextMs_ sem interação
        if (autoNext_ && autoNextMs_ > 0 && count_ > 1 && millis() - lastActivityMs_ >= autoNextMs_) {
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
    void setAutoNextInterval(unsigned long ms) { autoNextMs_ = ms; }
    void setAlertJump(bool on) { alertJump_ = on; }
    int count() const { return count_; }
    int currentIndex() const { return current_; }
    Screen* at(int idx) const { return (idx >= 0 && idx < count_) ? screens_[idx] : nullptr; }

    // Vai direto pra uma aba (interface web)
    void goTo(int idx) {
        lastActivityMs_ = millis();
        if (idx >= 0 && idx < count_ && idx != current_) enter(idx);
    }

    // Esvazia a lista de abas pra registrar de novo (mudou a ordem/idioma/rotação);
    // depois de re-adicionar, chamar begin() redesenha tudo
    void clear() { count_ = 0; current_ = -1; alertMask_ = 0; blinkOn_ = false; }

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
        for (int i = 0; i < count_; i++) drawTab(i);
    }

    // Desenha uma aba (fundo + ícone). Aba em alerta: ícone vermelho no
    // "aceso" do pisca.
    void drawTab(int i) {
        int slotW = SCREEN_W / count_;
        int cx = i * slotW + slotW / 2;
        bool active = (i == current_);
        // Limpa só a área do slot (o redesenho do pisca não toca nas vizinhas)
        tft_->fillRect(i * slotW, 0, slotW, TAB_BAR_H, DRACULA_CURRENT);
        if (active) {
            // "Aba" ativa: mesma cor do conteúdo, criando o efeito de continuidade
            tft_->fillSmoothRoundRect(i * slotW + 3, 2, slotW - 6, TAB_BAR_H, 6, DRACULA_BG, DRACULA_CURRENT);
            tft_->fillRect(cx - 10, TAB_BAR_H - 3, 20, 3, screens_[i]->accentColor());
        }
        uint16_t iconColor = active ? screens_[i]->accentColor() : DRACULA_COMMENT;
        if ((alertMask_ & (1u << i)) && blinkOn_) iconColor = DRACULA_RED;
        screens_[i]->drawIcon(*tft_, cx, TAB_BAR_H / 2 - 1, iconColor);
    }

    // Pisca (500ms) o ícone das abas em alerta. Com alertJump, vai pra aba que
    // acabou de entrar em alerta (só na subida, não prende a navegação).
    void updateAlerts() {
        uint32_t mask = 0;
        for (int i = 0; i < count_; i++) {
            if (screens_[i]->alerting()) mask |= 1u << i;
        }
        uint32_t rising = mask & ~alertMask_;
        uint32_t changed = mask ^ alertMask_;
        alertMask_ = mask;

        if (rising && alertJump_) {
            for (int i = 0; i < count_; i++) {
                if (rising & (1u << i)) { if (i != current_) enter(i); break; }
            }
        }

        bool blink = (millis() / 500) % 2 == 0;
        bool blinkChanged = blink != blinkOn_;
        blinkOn_ = blink;
        for (int i = 0; i < count_; i++) {
            uint32_t bit = 1u << i;
            if ((changed & bit) || (blinkChanged && (mask & bit))) drawTab(i);
        }
    }

    TFT_eSPI* tft_ = nullptr;
    Screen* screens_[MAX_SCREENS];
    int count_ = 0;
    int current_ = -1;
    unsigned long lastActivityMs_ = 0;
    bool autoNext_ = true;
    unsigned long autoNextMs_ = AUTO_NEXT_MS;
    bool alertJump_ = false;
    uint32_t alertMask_ = 0;  // bit i = aba i em alerta
    bool blinkOn_ = false;
};
