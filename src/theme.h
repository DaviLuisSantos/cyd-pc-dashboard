#pragma once
#include <TFT_eSPI.h>
#include "NotoSansBold15.h"

// ===== Temas =====
// Paleta em tempo de execução: os macros THEME_* leem o tema atual
// (settings.theme, trocado por theme::setTheme). Toda tela usa THEME_*, nunca
// TFT_* nem RGB565 cru — trocar de tema é só redesenhar.
//   BG      fundo                     CARD    fundo de card (entre BG e CURRENT)
//   CURRENT fundo secundário (tab bar, trilho de barra)
//   FG      texto principal           COMMENT texto apagado/secundário
//   CYAN/GREEN/ORANGE/PINK/PURPLE/RED/YELLOW  acentos
namespace theme {

struct Palette {
    uint16_t bg, card, current, fg, comment;
    uint16_t cyan, green, orange, pink, purple, red, yellow;
};

// 0xRRGGBB -> RGB565
constexpr uint16_t rgb(uint32_t c) {
    return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F);
}

// Cores em 0xRRGGBB, na ordem dos campos de Palette. Mesma ordem de
// settings::THEME_NAMES. Guardadas em hex (e não já em RGB565) pra interface
// web servir as cores exatas em /theme.css.
static const uint32_t PALETTES_HEX[][12] = {
    // dracula (default)
    {0x282a36, 0x313442, 0x44475a, 0xf8f8f2, 0x6272a4,
     0x8be9fd, 0x50fa7b, 0xffb86c, 0xff79c6, 0xbd93f9, 0xff5555, 0xf1fa8c},
    // nord
    {0x2e3440, 0x3b4252, 0x434c5e, 0xeceff4, 0x7b88a1,
     0x88c0d0, 0xa3be8c, 0xd08770, 0xb48ead, 0x81a1c1, 0xbf616a, 0xebcb8b},
    // gruvbox (dark)
    {0x282828, 0x32302f, 0x3c3836, 0xebdbb2, 0x928374,
     0x8ec07c, 0xb8bb26, 0xfe8019, 0xd3869b, 0x83a598, 0xfb4934, 0xfabd2f},
    // mocha (Catppuccin)
    {0x1e1e2e, 0x313244, 0x45475a, 0xcdd6f4, 0x7f849c,
     0x89dceb, 0xa6e3a1, 0xfab387, 0xf5c2e7, 0xcba6f7, 0xf38ba8, 0xf9e2af},
    // tokyonight
    {0x1a1b26, 0x24283b, 0x2f344d, 0xc0caf5, 0x565f89,
     0x7dcfff, 0x9ece6a, 0xff9e64, 0xbb9af7, 0x7aa2f7, 0xf7768e, 0xe0af68},
    // latte (Catppuccin, claro)
    {0xeff1f5, 0xe6e9ef, 0xccd0da, 0x4c4f69, 0x8c8fa1,
     0x04a5e5, 0x40a02b, 0xfe640b, 0xea76cb, 0x8839ef, 0xd20f39, 0xdf8e1d},
};
static const int N_THEMES = sizeof(PALETTES_HEX) / sizeof(PALETTES_HEX[0]);
static_assert(N_THEMES == 6, "PALETTES_HEX e settings::THEME_NAMES precisam ter a mesma ordem/tamanho");
static_assert(rgb(0x282a36) == 0x2946, "conversao RGB565");

// Paleta atual já convertida pra RGB565 (convertida uma vez por troca de tema)
inline Palette& currentPalette() {
    static Palette p;
    return p;
}
inline int& currentThemeIndex() {
    static int idx = -1;
    return idx;
}
inline void setTheme(int idx) {
    if (idx < 0 || idx >= N_THEMES) idx = 0;
    currentThemeIndex() = idx;
    const uint32_t* h = PALETTES_HEX[idx];
    currentPalette() = {rgb(h[0]), rgb(h[1]), rgb(h[2]), rgb(h[3]), rgb(h[4]), rgb(h[5]),
                        rgb(h[6]), rgb(h[7]), rgb(h[8]), rgb(h[9]), rgb(h[10]), rgb(h[11])};
}

// Sem setTheme() ainda (ex.: desenho antes do settings::load): Dracula
inline const Palette& pal() {
    if (currentThemeIndex() < 0) setTheme(0);
    return currentPalette();
}
inline const uint32_t* paletteHex() {
    if (currentThemeIndex() < 0) setTheme(0);
    return PALETTES_HEX[currentThemeIndex()];
}

} // namespace theme

#define THEME_BG      (theme::pal().bg)
#define THEME_CARD    (theme::pal().card)
#define THEME_CURRENT (theme::pal().current)
#define THEME_FG      (theme::pal().fg)
#define THEME_COMMENT (theme::pal().comment)
#define THEME_CYAN    (theme::pal().cyan)
#define THEME_GREEN   (theme::pal().green)
#define THEME_ORANGE  (theme::pal().orange)
#define THEME_PINK    (theme::pal().pink)
#define THEME_PURPLE  (theme::pal().purple)
#define THEME_RED     (theme::pal().red)
#define THEME_YELLOW  (theme::pal().yellow)

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

// Servidor (aba Serviços): duas gavetas empilhadas com um LED em cada
inline void iconServer(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    const int w = size * 2, h = size * 3 / 4, gap = size / 4;
    for (int k = 0; k < 2; k++) {
        int y = cy - h - gap / 2 + k * (h + gap);
        tft.drawRoundRect(cx - size, y, w, h, 2, color);
        tft.fillCircle(cx + size - size / 3 - 1, y + h / 2, 1, color);
        tft.drawFastHLine(cx - size + 3, y + h / 2, size / 2, color);
    }
}

// Sino (notificações): cúpula + laterais abrindo + base + badalo
inline void iconBell(TFT_eSPI& tft, int cx, int cy, int size, uint16_t color) {
    const int r = size / 2, top = cy - size / 4, base = cy + size / 2, flare = size * 3 / 4;
    tft.drawCircleHelper(cx, top, r, 0x1 | 0x2, color);
    tft.drawLine(cx - r, top, cx - flare, base, color);
    tft.drawLine(cx + r, top, cx + flare, base, color);
    tft.drawFastHLine(cx - flare, base, flare * 2 + 1, color);
    tft.fillCircle(cx, base + 3, 2, color);
    tft.drawFastVLine(cx, top - r - 2, 2, color);
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
// THEME_CURRENT. `bg` é a cor por trás da barra (BG ou CARD).
inline void drawBar(TFT_eSPI& tft, int x, int y, int w, int h, float pct,
                    uint16_t fillColor, uint16_t bg = THEME_BG) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    TFT_eSprite& s = canvas().begin(tft, w, h, bg);
    s.fillSmoothRoundRect(0, 0, w, h, h / 2, THEME_CURRENT, bg);
    int fillW = (int)(pct / 100.0f * (w - 4));
    if (fillW < h - 4) fillW = (pct > 0) ? (h - 4) : 0;
    if (fillW > 0) {
        s.fillSmoothRoundRect(2, 2, fillW, h - 4, (h - 4) / 2, fillColor, THEME_CURRENT);
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
    TFT_eSprite& s = canvas().begin(tft, box, box, THEME_BG);
    s.drawSmoothArc(c, c, r, r - thick, 30, 330, THEME_CURRENT, THEME_BG, true);
    int endA = 30 + (int)(pct * 3.0f);
    if (endA > 33) {
        s.drawSmoothArc(c, c, r, r - thick, 30, endA, color, THEME_CURRENT, true);
    }
    char buf[5];
    snprintf(buf, sizeof(buf), "%d", (int)(pct + 0.5f));
    s.setTextDatum(MC_DATUM);
    s.setTextColor(THEME_FG, THEME_BG);
    s.drawString(buf, c, c - 5, 4);
    s.setTextColor(THEME_COMMENT, THEME_BG);
    s.drawString(label, c, c + 15, 2);
    canvas().push(cx - c, cy - c);
}

// Mini-gráfico de linha (sparkline) com linha de base. `v[0..n-1]` em ordem
// cronológica (mais antigo primeiro), valores 0-100.
inline void drawSparkline(TFT_eSPI& tft, int x, int y, int w, int h,
                          const float* v, int n, uint16_t color) {
    TFT_eSprite& s = canvas().begin(tft, w, h, THEME_BG);
    s.drawFastHLine(0, h - 1, w, THEME_CURRENT);
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
    tft.fillSmoothRoundRect(x, y, w, h, 8, THEME_CARD, THEME_BG);
}

// "Pill" colorido com a variação percentual (verde/vermelho), alinhado à
// direita de boxRightX. `bg` é a cor por trás (BG ou CARD).
inline void drawPctBadge(TFT_eSPI& tft, int boxRightX, int cy, float pct,
                         uint16_t bg = THEME_BG) {
    const int boxW = 74, h = 20;
    TFT_eSprite& s = canvas().begin(tft, boxW, h + 2, bg);
    char buf[10];
    snprintf(buf, sizeof(buf), "%+.1f%%", pct);
    s.setTextFont(2);
    int textW = s.textWidth(buf);
    int w = textW + 16;
    if (w > boxW - 4) w = boxW - 4;
    uint16_t pill = (pct >= 0) ? THEME_GREEN : THEME_RED;
    s.fillSmoothRoundRect(boxW - w, 1, w, h, h / 2, pill, bg);
    s.setTextDatum(MC_DATUM);
    s.setTextColor(THEME_BG, pill);
    s.drawString(buf, boxW - w / 2, h / 2 + 2, 2);
    canvas().push(boxRightX - boxW, cy - h / 2 - 1);
}

// Indicador de status discreto (dot suavizado + label pequeno), alinhado à
// direita de rightX.
inline void drawStatusDot(TFT_eSPI& tft, int rightX, int y, uint16_t color, const char* label) {
    const int w = 140, h = 20;
    TFT_eSprite& s = canvas().begin(tft, w, h, THEME_BG);
    s.setTextDatum(MR_DATUM);
    s.setTextColor(THEME_COMMENT, THEME_BG);
    s.drawString(label, w - 12, h / 2, 2);
    s.fillSmoothCircle(w - 6, h / 2, 4, color, THEME_BG);
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
