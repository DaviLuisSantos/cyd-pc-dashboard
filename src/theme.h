#pragma once
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"

// ===== Paleta Dracula (convertida pra RGB565) =====
#define DRACULA_BG      0x2946  // #282a36 — fundo
#define DRACULA_CARD    0x31A8  // #313442 — fundo de card (entre BG e CURRENT)
#define DRACULA_CURRENT 0x422B  // #44475a — fundo secundário (tab bar, trilho de barra)
#define DRACULA_FG      0xFFDE  // #f8f8f2 — texto principal
#define DRACULA_COMMENT 0x6394  // #6272a4 — texto apagado/secundário
#define DRACULA_CYAN    0x8F5F  // #8be9fd
#define DRACULA_GREEN   0x57CF  // #50fa7b
#define DRACULA_ORANGE  0xFDCD  // #ffb86c
#define DRACULA_PINK    0xFBD8  // #ff79c6
#define DRACULA_PURPLE  0xBC9F  // #bd93f9
#define DRACULA_RED     0xFAAA  // #ff5555
#define DRACULA_YELLOW  0xF7D1  // #f1fa8c

// ===== Layout =====
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int TAB_BAR_H = 34;
static const int CONTENT_TOP = TAB_BAR_H + 6;

namespace theme {

// ===== Canvas: sprite compartilhado pra desenho sem flicker =====
// Regiões dinâmicas são desenhadas fora da tela (TFT_eSprite) e enviadas de
// uma vez com pushSprite — a atualização vira atômica, sem a piscada do
// desenho direto. create/delete por uso: os tamanhos se repetem a cada ciclo,
// então o heap estabiliza (nada de sprite gigante permanente disputando RAM
// com o WiFi).
class Canvas {
public:
    TFT_eSprite& begin(TFT_eSPI& tft, int w, int h, uint16_t bg) {
        if (!spr_) spr_ = new TFT_eSprite(&tft);
        spr_->setColorDepth(16);
        spr_->createSprite(w, h);
        spr_->fillSprite(bg);
        return *spr_;
    }
    void push(int x, int y) {
        if (!spr_) return;
        spr_->pushSprite(x, y);
        spr_->deleteSprite();
    }
private:
    TFT_eSprite* spr_ = nullptr;
};

inline Canvas& canvas() {
    static Canvas c;
    return c;
}

// ---- Ícones vetoriais (desenhados com primitivas, sem bitmap/filesystem) ----
// Recebem o CENTRO (cx, cy) e um `size` (raio/meia-largura aproximados —
// size=8 é o tamanho clássico usado na tab bar; nas linhas de conteúdo
// usa-se algo maior, tipo 11-12, pra ganhar presença visual).

inline void iconClock(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy, size, color);
    tft.drawLine(cx, cy, cx, cy - size * 5 / 8, color);
    tft.drawLine(cx, cy, cx + size / 2, cy + size / 4, color);
}

inline void iconCpu(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int half = size;
    int pin = size / 2;
    int off = half * 2 / 3;
    tft.drawRect(cx - half, cy - half, half * 2, half * 2, color);
    for (int i = -off; i <= off; i += (off > 0 ? off : 1)) {
        tft.drawFastVLine(cx + i, cy - half - pin, pin, color);
        tft.drawFastVLine(cx + i, cy + half, pin, color);
        tft.drawFastHLine(cx - half - pin, cy + i, pin, color);
        tft.drawFastHLine(cx + half, cy + i, pin, color);
    }
}

inline void iconRam(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int halfW = size;
    int halfH = size * 5 / 8;
    int r = size / 4;
    int off = halfW * 3 / 4;
    int step = halfW / 2;
    tft.drawRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, r, color);
    for (int i = -off; i <= off; i += (step > 0 ? step : 1)) {
        tft.drawFastVLine(cx + i, cy + halfH, size * 3 / 8, color);
    }
}

inline void iconDisk(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int halfW = size;
    int halfH = size * 7 / 8;
    tft.drawRoundRect(cx - halfW, cy - halfH, halfW * 2, halfH * 2, size * 3 / 8, color);
    tft.drawFastHLine(cx - halfW, cy, halfW * 2, color);
    tft.fillCircle(cx + halfW / 2, cy + size * 3 / 8, (size / 8 > 1 ? size / 8 : 1), color);
}

inline void iconNet(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy + size / 4, size, color);
    tft.drawFastHLine(cx - size, cy + size / 4, size * 2, color);
    tft.drawFastVLine(cx, cy - size * 3 / 4, size * 2, color);
}

inline void iconCoin(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawCircle(cx, cy, size, color);
    tft.drawCircle(cx, cy, size * 3 / 4, color);
    tft.drawFastVLine(cx, cy - size / 2, size + 1, color);
}

// Lista (processos): três linhas com bullet
inline void iconList(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    int gap = size * 3 / 4;
    for (int i = -1; i <= 1; i++) {
        int y = cy + i * gap;
        tft.fillCircle(cx - size + 1, y, 1, color);
        tft.drawFastHLine(cx - size + 5, y, size * 2 - 5, color);
    }
}

// Play (YouTube): retângulo arredondado com triângulo dentro
inline void iconPlay(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.drawRoundRect(cx - size, cy - size * 3 / 4, size * 2, size * 3 / 2, size / 2, color);
    tft.fillTriangle(cx - size / 3, cy - size / 3,
                     cx - size / 3, cy + size / 3,
                     cx + size / 2, cy, color);
}

inline void iconArrowDown(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.fillTriangle(cx - size, cy - size, cx + size, cy - size, cx, cy + size, color);
}

inline void iconArrowUp(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    tft.fillTriangle(cx - size, cy + size, cx + size, cy + size, cx, cy - size, color);
}

// ---- Widgets (todos via Canvas — push atômico, sem flicker) ----

// Texto dinâmico dentro de uma região fixa. Substitui o padrão antigo de
// setTextPadding + drawString direto na tela: a região inteira é redesenhada
// no sprite e enviada de uma vez. `datum` posiciona dentro da região
// (ML_DATUM = esquerda, MC_DATUM = centro, MR_DATUM = direita).
inline void drawValue(TFT_eSPI& tft, int x, int y, int w, int h,
                      const char* text, int font, uint16_t color, uint16_t bg,
                      uint8_t datum = ML_DATUM) {
    TFT_eSprite& s = canvas().begin(tft, w, h, bg);
    s.setTextColor(color, bg);
    int tx = (datum == MR_DATUM) ? w - 1 : (datum == MC_DATUM) ? w / 2 : 0;
    // Se a fonte é mais alta que a região, ancora o TOPO do texto no topo do
    // sprite em vez de centralizar — centralizado, o topo dos glifos cai fora
    // do sprite e os dígitos saem decapitados. Cortar por baixo é invisível
    // pra números (a sobra inferior da célula é área de descender, g/y/p).
    int fh = s.fontHeight(font);
    if (fh > h) {
        s.setTextDatum(datum == MR_DATUM ? TR_DATUM
                     : datum == MC_DATUM ? TC_DATUM : TL_DATUM);
        s.drawString(text, tx, 0, font);
    } else {
        s.setTextDatum(datum);
        s.drawString(text, tx, h / 2, font);
    }
    canvas().push(x, y);
}

// Barra de progresso com cantos arredondados e bordas suavizadas sobre trilho
// DRACULA_CURRENT. `bg` é a cor por trás da barra (BG ou CARD).
inline void drawBar(TFT_eSPI& tft, int x, int y, int w, int h, float pct,
                    uint16_t fillColor, uint16_t bg = DRACULA_BG) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    TFT_eSprite& s = canvas().begin(tft, w, h, bg);
    s.fillSmoothRoundRect(0, 0, w, h, h / 2, DRACULA_CURRENT, bg);
    int fillW = (int)(pct / 100.0f * (w - 4));
    if (fillW < h - 4) fillW = (pct > 0) ? (h - 4) : 0;
    if (fillW > 0) {
        s.fillSmoothRoundRect(2, 2, fillW, h - 4, (h - 4) / 2, fillColor, DRACULA_CURRENT);
    }
    canvas().push(x, y);
}

// Anel de progresso (gauge) com anti-aliasing via drawSmoothArc.
// Arco de 30° a 330° (abertura embaixo, cara de velocímetro), valor inteiro
// grande no centro e label pequeno abaixo. Redesenhado inteiro num sprite.
inline void drawRingGauge(TFT_eSPI& tft, int cx, int cy, int r, int thick,
                          float pct, uint16_t color, const char* label) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int box = r * 2 + 4;
    int c = box / 2;
    TFT_eSprite& s = canvas().begin(tft, box, box, DRACULA_BG);
    s.drawSmoothArc(c, c, r, r - thick, 30, 330, DRACULA_CURRENT, DRACULA_BG, true);
    int endA = 30 + (int)(pct * 3.0f);
    if (endA > 33) {
        s.drawSmoothArc(c, c, r, r - thick, 30, endA, color, DRACULA_CURRENT, true);
    }
    char buf[5];
    snprintf(buf, sizeof(buf), "%d", (int)(pct + 0.5f));
    s.setTextDatum(MC_DATUM);
    s.setTextColor(DRACULA_FG, DRACULA_BG);
    s.drawString(buf, c, c - 5, 4);
    s.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    s.drawString(label, c, c + 15, 2);
    canvas().push(cx - c, cy - c);
}

// Mini-gráfico de linha (sparkline) com linha de base. `v[0..n-1]` em ordem
// cronológica (mais antigo primeiro), valores 0-100.
inline void drawSparkline(TFT_eSPI& tft, int x, int y, int w, int h,
                          const float* v, int n, uint16_t color) {
    TFT_eSprite& s = canvas().begin(tft, w, h, DRACULA_BG);
    s.drawFastHLine(0, h - 1, w, DRACULA_CURRENT);
    if (n >= 2) {
        float step = (float)(w - 1) / (n - 1);
        for (int i = 1; i < n; i++) {
            int x0 = (int)((i - 1) * step);
            int x1 = (int)(i * step);
            int y0 = h - 2 - (int)(v[i - 1] / 100.0f * (h - 3));
            int y1 = h - 2 - (int)(v[i] / 100.0f * (h - 3));
            s.drawLine(x0, y0, x1, y1, color);
        }
    }
    canvas().push(x, y);
}

// Card de fundo (desenhado direto — é estático, vai uma vez no onEnter).
inline void drawCard(TFT_eSPI& tft, int x, int y, int w, int h) {
    tft.fillSmoothRoundRect(x, y, w, h, 8, DRACULA_CARD, DRACULA_BG);
}

// "Pill" colorido com a variação percentual (verde/vermelho), alinhado à
// direita de boxRightX. `bg` é a cor por trás (BG ou CARD).
inline void drawPctBadge(TFT_eSPI& tft, int boxRightX, int cy, float pct,
                         uint16_t bg = DRACULA_BG) {
    const int boxW = 74, h = 20;
    TFT_eSprite& s = canvas().begin(tft, boxW, h + 2, bg);
    char buf[10];
    snprintf(buf, sizeof(buf), "%+.1f%%", pct);
    s.setTextFont(2);
    int textW = s.textWidth(buf);
    int w = textW + 16;
    if (w > boxW - 4) w = boxW - 4;
    uint16_t pill = (pct >= 0) ? DRACULA_GREEN : DRACULA_RED;
    s.fillSmoothRoundRect(boxW - w, 1, w, h, h / 2, pill, bg);
    s.setTextDatum(MC_DATUM);
    s.setTextColor(DRACULA_BG, pill);
    s.drawString(buf, boxW - w / 2, h / 2 + 2, 2);
    canvas().push(boxRightX - boxW, cy - h / 2 - 1);
}

// Indicador de status discreto (dot suavizado + label pequeno), alinhado à
// direita de rightX.
inline void drawStatusDot(TFT_eSPI& tft, int rightX, int y, uint16_t color, const char* label) {
    const int w = 140, h = 20;
    TFT_eSprite& s = canvas().begin(tft, w, h, DRACULA_BG);
    s.setTextDatum(MR_DATUM);
    s.setTextColor(DRACULA_COMMENT, DRACULA_BG);
    s.drawString(label, w - 12, h / 2, 2);
    s.fillSmoothCircle(w - 6, h / 2, 4, color, DRACULA_BG);
    canvas().push(rightX - w, y - h / 2);
}

// Carrega/descarrega a smooth font (anti-aliased) usada nos labels estáticos das
// linhas de conteúdo. Enquanto carregada, ela SOBREPÕE qualquer número de fonte
// clássica passado a drawString — por isso só fica ativa ao redor do onEnter()
// (labels são desenhados uma vez só), nunca durante o update() dos valores.
inline void beginLabelFont(TFT_eSPI& tft) {
    tft.loadFont(NotoSansBold15);
}

inline void endLabelFont(TFT_eSPI& tft) {
    tft.unloadFont();
}

} // namespace theme
