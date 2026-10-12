#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "theme.h"
#include "backlight.h"
#include "screens.h"

// Notificações vindas do PC (POST /api/notify na interface web, ou
// agent/notify.py). Aparecem como um card sobre a área de conteúdo, uma de
// cada vez (fila de até NOTIFY_QUEUE), por `duration` segundos (0 = até tocar).
// Enquanto uma está na tela, o ScreenManager fica em overlay: a tela de baixo
// não redesenha e o auto-next pausa. Tocar dispensa. À noite, acende a tela.
namespace notify {

enum Level : uint8_t { INFO, OK, WARN, ERROR };

struct Note {
    char title[48];
    char text[240];
    Level level;
    uint16_t durationS;
};

static const int NOTIFY_QUEUE = 4;
static Note queue_[NOTIFY_QUEUE];
static int count_ = 0;            // na fila, sem contar a que está na tela
static Note current_;
static bool showing_ = false;
static unsigned long shownAtMs_ = 0;
static int lastBarPct_ = -1;
static int lastDrawnPending_ = -1;
static uint32_t total_ = 0;       // recebidas desde o boot (interface web)

// Card (dentro da área de conteúdo, abaixo da tab bar)
static const int CX = 10, CY = TAB_BAR_H + 10, CW = 300, CH = 240 - TAB_BAR_H - 20;
static const int TEXT_X = CX + 18, TEXT_W = CW - 36, LINE_H = 18;

inline Level parseLevel(const char* v) {
    if (!strcasecmp(v, "ok") || !strcasecmp(v, "success")) return OK;
    if (!strcasecmp(v, "warn") || !strcasecmp(v, "warning")) return WARN;
    if (!strcasecmp(v, "error") || !strcasecmp(v, "fail")) return ERROR;
    return INFO;
}

inline uint16_t levelColor(Level l) {
    switch (l) {
        case OK:   return THEME_GREEN;
        case WARN: return THEME_ORANGE;
        case ERROR: return THEME_RED;
        default:   return THEME_CYAN;
    }
}

// UTF-8 -> ASCII (as fontes do display não têm acento): "Compilação" ->
// "Compilacao". Latin-1 comum vira a letra sem acento; o resto vira '?'.
// Mantém '\n' (quebra de linha explícita); outros controles viram espaço.
inline void toAscii(const char* in, char* out, size_t len) {
    static const char* const LATIN1 =  // U+00C0..U+00FF
        "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYPs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuypy";
    size_t o = 0;
    for (const uint8_t* p = (const uint8_t*)in; *p && o + 1 < len; p++) {
        uint8_t c = *p;
        if (c < 0x80) {
            out[o++] = (c == '\n' || c >= 0x20) ? (char)c : ' ';
        } else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) {
            out[o++] = LATIN1[p[1] - 0x80];  // 0xC3 0x80..0xBF = U+00C0..U+00FF
            p++;
        } else if (c >= 0xC0) {
            out[o++] = '?';
            while ((p[1] & 0xC0) == 0x80) p++;  // pula os bytes de continuação
        }
    }
    out[o] = '\0';
}

// Enfileira. Fila cheia: descarta a mais antiga (a mais nova é a mais útil).
inline void push(const char* title, const char* text, Level level, uint16_t durationS) {
    if (count_ == NOTIFY_QUEUE) {
        memmove(queue_, queue_ + 1, sizeof(Note) * (NOTIFY_QUEUE - 1));
        count_--;
    }
    Note& n = queue_[count_++];
    toAscii(title ? title : "", n.title, sizeof(n.title));
    toAscii(text ? text : "", n.text, sizeof(n.text));
    n.level = level;
    n.durationS = durationS;
    total_++;
}

inline bool showing() { return showing_; }
inline int pending() { return count_; }
inline uint32_t total() { return total_; }

// Quebra `text` em linhas de até `maxW` px na font 2 (por palavra; palavra
// maior que a linha é cortada). Desenha no máximo `maxLines`, com "..." se sobrar.
inline void drawWrapped(TFT_eSPI& tft, const char* text, int x, int y, int maxW, int maxLines) {
    char line[96];
    int lines = 0;
    const char* p = text;
    while (*p && lines < maxLines) {
        size_t n = 0, lastSpace = 0;
        const char* start = p;
        while (*p && *p != '\n' && n + 1 < sizeof(line)) {
            line[n] = *p;
            line[n + 1] = '\0';
            if (tft.textWidth(line, 2) > maxW) break;
            if (*p == ' ') lastSpace = n;
            n++;
            p++;
        }
        if (*p && *p != '\n' && lastSpace > 0) {  // volta pro último espaço
            n = lastSpace;
            p = start + lastSpace + 1;
        }
        line[n] = '\0';
        if (*p == '\n') p++;
        lines++;
        if (lines == maxLines && *p) {  // última linha e ainda tem texto
            while (n > 0 && tft.textWidth(line, 2) + tft.textWidth("...", 2) > maxW) line[--n] = '\0';
            strlcat(line, "...", sizeof(line));
        }
        tft.drawString(line, x, y + (lines - 1) * LINE_H, 2);
    }
}

inline void drawCurrent(TFT_eSPI& tft) {
    const uint16_t color = levelColor(current_.level);
    tft.fillRect(0, TAB_BAR_H, 320, 240 - TAB_BAR_H, THEME_BG);
    tft.fillSmoothRoundRect(CX, CY, CW, CH, 10, THEME_CARD, THEME_BG);
    tft.fillRect(CX + 2, CY + 10, 4, CH - 20, color);  // faixa do nível

    theme::iconBell(tft, CX + 30, CY + 22, 12, color);
    const char* title = current_.title[0] ? current_.title
                      : current_.level == OK ? "OK" : current_.level == WARN ? "Warning"
                      : current_.level == ERROR ? "Error" : "Notification";
    theme::beginLabelFont(tft);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(THEME_FG, THEME_CARD);
    char t[48];
    strlcpy(t, title, sizeof(t));
    while (strlen(t) > 1 && tft.textWidth(t) > CW - 70) t[strlen(t) - 1] = '\0';
    tft.drawString(t, CX + 52, CY + 24);
    theme::endLabelFont(tft);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(THEME_FG, THEME_CARD);
    drawWrapped(tft, current_.text, TEXT_X, CY + 50, TEXT_W, 6);

    lastBarPct_ = -1;
    lastDrawnPending_ = -1;
}

// "+N" (quantas esperam na fila) no canto do card; atualiza quando chega mais
inline void drawPending(TFT_eSPI& tft) {
    if (count_ == lastDrawnPending_) return;
    lastDrawnPending_ = count_;
    char more[8] = "";
    if (count_ > 0) snprintf(more, sizeof(more), "+%d", count_);
    theme::drawValue(tft, CX + CW - 50, CY + 13, 40, 18, more, 2, THEME_COMMENT, THEME_CARD, MR_DATUM);
}

// Mostra a próxima da fila (true) ou nada (false)
inline bool showNext(TFT_eSPI& tft) {
    if (count_ == 0) return false;
    current_ = queue_[0];
    memmove(queue_, queue_ + 1, sizeof(Note) * (count_ - 1));
    count_--;
    showing_ = true;
    shownAtMs_ = millis();
    backlight::wake();
    drawCurrent(tft);
    return true;
}

inline void dismiss(TFT_eSPI& tft, ScreenManager& manager) {
    if (showNext(tft)) return;
    showing_ = false;
    manager.setOverlay(false);
    manager.redraw();
}

// Chamado no loop()
inline void poll(TFT_eSPI& tft, ScreenManager& manager) {
    if (!showing_) {
        if (count_ && manager.count()) {
            manager.setOverlay(true);
            showNext(tft);
        }
        return;
    }
    drawPending(tft);
    if (current_.durationS == 0) return;  // fica até tocar
    unsigned long elapsed = millis() - shownAtMs_;
    unsigned long total = current_.durationS * 1000UL;
    if (elapsed >= total) {
        dismiss(tft, manager);
        return;
    }
    // Barrinha do tempo restante no pé do card (só redesenha quando o % muda)
    int pct = 100 - (int)(elapsed * 100 / total);
    if (pct != lastBarPct_) {
        lastBarPct_ = pct;
        // h=8: o drawBar preenche com 2px de margem (h=4 daria preenchimento de 0px)
        theme::drawBar(tft, CX + 18, CY + CH - 16, CW - 36, 8, pct, levelColor(current_.level), THEME_CARD);
    }
}

// Toque com notificação na tela: dispensa (true = toque consumido)
inline bool onTouch(TFT_eSPI& tft, ScreenManager& manager) {
    if (!showing_) return false;
    dismiss(tft, manager);
    return true;
}

} // namespace notify
