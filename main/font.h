#pragma once

#include <stdint.h>

typedef struct {
    uint8_t width;
    uint8_t height;
    int8_t xoff;
    int8_t yoff;
    uint8_t advance;
    const uint8_t *bitmap;
} font_glyph_t;

typedef struct {
    uint8_t first;
    uint8_t last;
    uint8_t line_height;
    const font_glyph_t *glyphs;
} font_t;

extern const font_t FONT_16;
extern const font_t FONT_20;
extern const font_t FONT_26;
extern const font_t FONT_32;
extern const font_t FONT_56;

int font_draw(uint16_t *fb, int x, int y, const char *text, uint16_t color, const font_t *font);
int font_text_width(const char *text, const font_t *font);
int font_line_height(const font_t *font);
