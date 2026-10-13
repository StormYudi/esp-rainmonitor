#include "font.h"

#include "config.h"

static uint16_t swap16(uint16_t color)
{
    return (uint16_t)((color >> 8) | (color << 8));
}

static uint16_t blend_aa(uint16_t dst, uint16_t src, int alpha)
{
    if (alpha <= 0) {
        return dst;
    }
    if (alpha >= 15) {
        return src;
    }
    const uint16_t d = swap16(dst);
    const uint16_t s = swap16(src);
    const int dr = (d >> 11) & 0x1F, dg = (d >> 5) & 0x3F, db = d & 0x1F;
    const int sr = (s >> 11) & 0x1F, sg = (s >> 5) & 0x3F, sb = s & 0x1F;
    const int inv = 15 - alpha;
    const int r = (sr * alpha + dr * inv) / 15;
    const int g = (sg * alpha + dg * inv) / 15;
    const int b = (sb * alpha + db * inv) / 15;
    return swap16((uint16_t)((r << 11) | (g << 5) | b));
}

static const font_glyph_t *glyph_at(const font_t *font, unsigned char ch)
{
    if (ch < font->first || ch > font->last) {
        ch = '?';
        if (ch < font->first || ch > font->last) {
            ch = font->first;
        }
    }
    return &font->glyphs[ch - font->first];
}

int font_line_height(const font_t *font)
{
    return font->line_height;
}

int font_text_width(const char *text, const font_t *font)
{
    int w = 0;
    for (const char *p = text; *p; ++p) {
        w += glyph_at(font, (unsigned char)*p)->advance;
    }
    return w;
}

int font_draw(uint16_t *fb, int x, int y, const char *text, uint16_t color, const font_t *font)
{
    int cursor = x;
    for (const char *p = text; *p; ++p) {
        const font_glyph_t *g = glyph_at(font, (unsigned char)*p);
        int nibble = 0;
        int packed = 0;
        const uint8_t *src = g->bitmap;
        for (int row = 0; row < g->height; ++row) {
            for (int col = 0; col < g->width; ++col) {
                int a;
                if ((nibble++ & 1) == 0) {
                    packed = *src++;
                    a = packed >> 4;
                } else {
                    a = packed & 0x0F;
                }
                if (!a) {
                    continue;
                }
                const int px = cursor + g->xoff + col;
                const int py = y + g->yoff + row;
                if ((unsigned)px >= LCD_H_RES || (unsigned)py >= LCD_V_RES) {
                    continue;
                }
                uint16_t *pix = &fb[py * LCD_H_RES + px];
                *pix = blend_aa(*pix, color, a);
            }
            if (g->width & 1) {
                nibble = 0;
            }
        }
        cursor += g->advance;
    }
    return cursor - x;
}
