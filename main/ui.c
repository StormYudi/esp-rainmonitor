#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "font.h"

#define UI_MARGIN 12
#define UI_HEADER_INSET 36
#define UI_GAP 8
#define UI_HEADER 54
#define TILE_W ((LCD_H_RES - UI_MARGIN * 2 - UI_GAP) / 2)
#define TILE_H ((LCD_V_RES - UI_HEADER - UI_MARGIN - UI_GAP) / 2)
#define BATT_W 40
#define BATT_H 19
#define BATT_NUB_W 3
#define BATT_NUB_H 8
#define BATT_RADIUS 4
#define BATT_STROKE 2
#define BATT_GAP 2
#define CLOCK_INK_H 18
#define CLOCK_YOFF 8
#define HEADER_BAND_Y 16

enum {
    TILE_NONE = -1,
    TILE_CPU = 0,
    TILE_MEM = 1,
    TILE_NET = 2,
    TILE_GPU = 3,
};

typedef struct {
    int x;
    int y;
    int w;
    int h;
} ui_rect_t;

/* Windows 11 Task Manager: near-black chrome, cyan CPU, lavender GPU */
static const uint16_t COL_BG = RGB(28, 28, 28);
static const uint16_t COL_CHART = RGB(28, 28, 28);
static const uint16_t COL_TEXT = RGB(255, 255, 255);
static const uint16_t COL_MUTED = RGB(180, 180, 180);
static const uint16_t COL_GRID = RGB(52, 52, 52);
static const uint16_t COL_CPU = RGB(61, 206, 240);
static const uint16_t COL_GPU = RGB(212, 184, 248);
static const uint16_t COL_MEM = RGB(122, 144, 232);
static const uint16_t COL_NET = RGB(232, 168, 216);
static const uint16_t COL_CPU_FILL = RGB(28, 122, 148);
static const uint16_t COL_GPU_FILL = RGB(112, 80, 152);
static const uint16_t COL_MEM_FILL = RGB(48, 60, 128);
static const uint16_t COL_NET_FILL = RGB(120, 64, 112);
static const uint16_t COL_OK = RGB(108, 203, 95);
static const uint16_t COL_WAIT = RGB(247, 184, 75);
static const uint16_t COL_BATT_ON = RGB(220, 220, 220);
static const uint16_t COL_RULE = RGB(48, 48, 48);
static const uint16_t COL_CORE_FRAME = RGB(82, 82, 82);

static uint16_t *s_fb;
static float s_cpu[HISTORY_LEN];
static float s_gpu[HISTORY_LEN];
static float s_mem[HISTORY_LEN];
static float s_net[HISTORY_LEN];
static float s_core[MAX_CORES][HISTORY_LEN];
static bool s_gpu_ok[HISTORY_LEN];
static int s_count;
static int s_head;
static int s_core_n;
static usb_sample_t s_now;
static char s_cpu_model[MODEL_NAME_LEN];
static char s_gpu_model[MODEL_NAME_LEN];
static bool s_time_ok;
static int s_tod_sec;
static int64_t s_time_sync_us;
static int s_zoom = TILE_NONE;
static int s_anim_tile = TILE_NONE;
static bool s_anim_active;
static bool s_anim_closing;
static int64_t s_anim_start_us;
static ui_rect_t s_anim_from;
static ui_rect_t s_anim_to;
static int s_core_scroll;
static int s_core_max_scroll;
static bool s_clip;
static int s_clip_x0;
static int s_clip_y0;
static int s_clip_x1;
static int s_clip_y1;

static void clip_begin(int x, int y, int w, int h)
{
    s_clip = true;
    s_clip_x0 = x;
    s_clip_y0 = y;
    s_clip_x1 = x + w;
    s_clip_y1 = y + h;
}

static void clip_end(void)
{
    s_clip = false;
}

static void put_pixel(int x, int y, uint16_t color)
{
    if (s_clip && (x < s_clip_x0 || y < s_clip_y0 || x >= s_clip_x1 || y >= s_clip_y1)) {
        return;
    }
    if ((unsigned)x < LCD_H_RES && (unsigned)y < LCD_V_RES) {
        s_fb[y * LCD_H_RES + x] = color;
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (s_clip) {
        if (x < s_clip_x0) {
            w -= s_clip_x0 - x;
            x = s_clip_x0;
        }
        if (y < s_clip_y0) {
            h -= s_clip_y0 - y;
            y = s_clip_y0;
        }
        if (x + w > s_clip_x1) {
            w = s_clip_x1 - x;
        }
        if (y + h > s_clip_y1) {
            h = s_clip_y1 - y;
        }
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_H_RES) {
        w = LCD_H_RES - x;
    }
    if (y + h > LCD_V_RES) {
        h = LCD_V_RES - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int yy = y; yy < y + h; ++yy) {
        uint16_t *row = s_fb + yy * LCD_H_RES + x;
        for (int xx = 0; xx < w; ++xx) {
            row[xx] = color;
        }
    }
}

static void fill_circle(int cx, int cy, int r, uint16_t color)
{
    const int r2 = r * r;
    for (int yy = -r; yy <= r; ++yy) {
        for (int xx = -r; xx <= r; ++xx) {
            if (xx * xx + yy * yy <= r2) {
                put_pixel(cx + xx, cy + yy, color);
            }
        }
    }
}

static void stroke_rect(int x, int y, int w, int h, uint16_t color)
{
    fill_rect(x, y, w, 1, color);
    fill_rect(x, y + h - 1, w, 1, color);
    fill_rect(x, y, 1, h, color);
    fill_rect(x + w - 1, y, 1, h, color);
}

static int clamp_radius(int w, int h, int r)
{
    if (r < 1) {
        return 0;
    }
    if (r * 2 > w) {
        r = w / 2;
    }
    if (r * 2 > h) {
        r = h / 2;
    }
    return r;
}

static void fill_quarter(int cx, int cy, int r, int sx, int sy, uint16_t color)
{
    const int r2 = r * r;
    for (int yy = 0; yy <= r; ++yy) {
        for (int xx = 0; xx <= r; ++xx) {
            if (xx * xx + yy * yy <= r2) {
                put_pixel(cx + sx * xx, cy + sy * yy, color);
            }
        }
    }
}

static void fill_round_rect(int x, int y, int w, int h, int r, uint16_t color)
{
    r = clamp_radius(w, h, r);
    if (r < 1) {
        fill_rect(x, y, w, h, color);
        return;
    }
    fill_rect(x + r, y, w - 2 * r, h, color);
    fill_rect(x, y + r, r, h - 2 * r, color);
    fill_rect(x + w - r, y + r, r, h - 2 * r, color);
    fill_quarter(x + r, y + r, r, -1, -1, color);
    fill_quarter(x + w - 1 - r, y + r, r, 1, -1, color);
    fill_quarter(x + r, y + h - 1 - r, r, -1, 1, color);
    fill_quarter(x + w - 1 - r, y + h - 1 - r, r, 1, 1, color);
}

static bool in_round_rect(int px, int py, int x, int y, int w, int h, int r)
{
    if (px < x || py < y || px >= x + w || py >= y + h) {
        return false;
    }
    r = clamp_radius(w, h, r);
    if (r < 1) {
        return true;
    }
    int cx = px;
    int cy = py;
    if (px < x + r && py < y + r) {
        cx = x + r;
        cy = y + r;
    } else if (px >= x + w - r && py < y + r) {
        cx = x + w - 1 - r;
        cy = y + r;
    } else if (px < x + r && py >= y + h - r) {
        cx = x + r;
        cy = y + h - 1 - r;
    } else if (px >= x + w - r && py >= y + h - r) {
        cx = x + w - 1 - r;
        cy = y + h - 1 - r;
    } else {
        return true;
    }
    const int dx = px - cx;
    const int dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

static void fill_round_rect_left(int x, int y, int w, int h, int r, int fill_w, uint16_t color)
{
    if (fill_w <= 0) {
        return;
    }
    if (fill_w >= w) {
        fill_round_rect(x, y, w, h, r, color);
        return;
    }
    r = clamp_radius(w, h, r);
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < fill_w; ++xx) {
            if (in_round_rect(x + xx, y + yy, x, y, w, h, r)) {
                put_pixel(x + xx, y + yy, color);
            }
        }
    }
}


static float hist_at(const float *hist, int i)
{
    return hist[(s_head + i) % HISTORY_LEN];
}

static int value_to_y(int y, int h, float v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 100) {
        v = 100;
    }
    int py = y + h - 2 - (int)((v / 100.0f) * (h - 3));
    if (py < y) {
        py = y;
    }
    return py;
}

static float hist_scale(const float *hist, float floor)
{
    float max_v = floor;
    for (int i = 0; i < s_count; ++i) {
        float v = hist_at(hist, i);
        if (v > max_v) {
            max_v = v;
        }
    }
    return max_v;
}

static void draw_chart(int x, int y, int w, int h, const float *hist, const bool *valid, uint16_t line, uint16_t fill,
                       float scale)
{
    fill_rect(x, y, w, h, COL_CHART);
    if (h >= 24) {
        for (int g = 1; g < 4; ++g) {
            fill_rect(x, y + h - (g * h) / 4, w, 1, COL_GRID);
        }
    }
    fill_rect(x, y + h - 1, w, 1, COL_GRID);

    if (s_count < 2) {
        return;
    }
    if (scale < 1.0f) {
        scale = 1.0f;
    }

    const int n = s_count;
    int prev_px = 0;
    int prev_py = 0;
    bool have_prev = false;
    for (int i = 0; i < n; ++i) {
        if (valid && !valid[(s_head + i) % HISTORY_LEN]) {
            have_prev = false;
            continue;
        }
        int px = x + ((HISTORY_LEN - n + i) * (w - 1)) / (HISTORY_LEN - 1);
        int py = value_to_y(y, h, hist_at(hist, i) * 100.0f / scale);
        if (have_prev) {
            const int dx = px - prev_px;
            const int dy = py - prev_py;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int steps = adx > ady ? adx : ady;
            if (steps < 1) {
                steps = 1;
            }
            for (int s = 0; s <= steps; ++s) {
                int xx = prev_px + (dx * s) / steps;
                int yy = prev_py + (dy * s) / steps;
                if (yy < y + h - 1) {
                    fill_rect(xx, yy + 1, 1, y + h - 1 - (yy + 1), fill);
                }
                put_pixel(xx, yy, line);
                put_pixel(xx, yy - 1, line);
            }
        }
        prev_px = px;
        prev_py = py;
        have_prev = true;
    }
}

static void format_pct(char *buf, size_t n, float value, bool ok)
{
    if (!ok) {
        snprintf(buf, n, "--");
        return;
    }
    int shown = (int)lroundf(value);
    if (shown < 0) {
        shown = 0;
    }
    if (shown > 100) {
        shown = 100;
    }
    snprintf(buf, n, "%d%%", shown);
}

static void format_freq(char *buf, size_t n, float mhz, bool ok)
{
    if (!ok || mhz <= 0) {
        snprintf(buf, n, "--");
        return;
    }
    if (mhz >= 1000.0f) {
        snprintf(buf, n, "%.2f GHz", mhz / 1000.0f);
    } else {
        snprintf(buf, n, "%.0f MHz", mhz);
    }
}

static void format_rate(char *buf, size_t n, float kbs, bool ok)
{
    if (!ok) {
        snprintf(buf, n, "--");
        return;
    }
    if (kbs >= 1024.0f) {
        snprintf(buf, n, "%.2f MB/s", kbs / 1024.0f);
    } else if (kbs >= 10.0f) {
        snprintf(buf, n, "%.0f KB/s", kbs);
    } else {
        snprintf(buf, n, "%.1f KB/s", kbs);
    }
}

static void format_mem(char *buf, size_t n, float used, float total, bool ok)
{
    if (!ok) {
        snprintf(buf, n, "--");
        return;
    }
    snprintf(buf, n, "%.1f/%.1f GB", used, total);
}

static void draw_battery(int x, int y, battery_status_t battery)
{
    const bool charging = battery.valid && battery.charging;
    const uint16_t frame = COL_BATT_ON;
    uint16_t on = COL_BATT_ON;
    if (charging) {
        on = COL_OK;
    } else if (battery.valid && battery.percent <= 15) {
        on = COL_WAIT;
    }

    /* iOS-style: flat rounded rect, 2px outline, then a clear gap, then fill. */
    fill_round_rect(x, y, BATT_W, BATT_H, BATT_RADIUS, frame);
    fill_round_rect(x + BATT_STROKE, y + BATT_STROKE, BATT_W - BATT_STROKE * 2, BATT_H - BATT_STROKE * 2,
                    BATT_RADIUS - 1, COL_BG);
    fill_round_rect(x + BATT_W, y + (BATT_H - BATT_NUB_H) / 2, BATT_NUB_W, BATT_NUB_H, 1, frame);

    const int ix = x + BATT_STROKE + BATT_GAP;
    const int iy = y + BATT_STROKE + BATT_GAP;
    const int iw = BATT_W - (BATT_STROKE + BATT_GAP) * 2;
    const int ih = BATT_H - (BATT_STROKE + BATT_GAP) * 2;
    const int ir = 2;
    const int pct = battery.valid ? battery.percent : 0;
    int fill_w = (iw * pct + 50) / 100;
    if (pct > 0 && fill_w < 2) {
        fill_w = 2;
    }
    if (fill_w > iw) {
        fill_w = iw;
    }
    fill_round_rect_left(ix, iy, iw, ih, ir, fill_w, on);
}

static void format_clock(char *buf, size_t n)
{
    if (!s_time_ok) {
        snprintf(buf, n, "--:--");
        return;
    }
    int elapsed = (int)((esp_timer_get_time() - s_time_sync_us) / 1000000);
    int tod = (s_tod_sec + elapsed) % (24 * 3600);
    if (tod < 0) {
        tod += 24 * 3600;
    }
    snprintf(buf, n, "%02d:%02d", tod / 3600, (tod % 3600) / 60);
}

static void draw_thick_line(int x0, int y0, int x1, int y1, int thick, uint16_t color);
static void draw_centered(int y, const char *text, uint16_t color, const font_t *font);

static void draw_bt_icon(int x, int y, uint16_t color)
{
    /* Classic Bluetooth rune, 16x16. */
    draw_thick_line(x + 8, y + 1, x + 8, y + 14, 2, color);
    draw_thick_line(x + 8, y + 1, x + 13, y + 5, 2, color);
    draw_thick_line(x + 13, y + 5, x + 4, y + 11, 2, color);
    draw_thick_line(x + 4, y + 4, x + 13, y + 10, 2, color);
    draw_thick_line(x + 13, y + 10, x + 8, y + 14, 2, color);
}

static void fill_arrow_right(int tip_x, int cy, uint16_t color)
{
    fill_rect(tip_x, cy, 1, 1, color);
    fill_rect(tip_x - 1, cy - 1, 1, 3, color);
    fill_rect(tip_x - 2, cy - 2, 1, 5, color);
    fill_rect(tip_x - 3, cy - 2, 1, 5, color);
}

static void draw_usb_icon(int x, int y, uint16_t color)
{
    /* Official USB trident, landscape: hub left, arrow right, circle up, square down. */
    const int cy = y + 9;
    fill_circle(x + 3, cy, 2, color);
    fill_rect(x + 5, cy - 1, 16, 2, color);
    fill_arrow_right(x + 24, cy, color);

    draw_thick_line(x + 11, cy - 1, x + 16, y + 3, 1, color);
    fill_circle(x + 18, y + 2, 2, color);

    draw_thick_line(x + 11, cy + 1, x + 16, y + 16, 1, color);
    fill_rect(x + 15, y + 14, 4, 4, color);
}

static void draw_header(bool connected, link_transport_t transport, battery_status_t battery)
{
    char clock[8];
    format_clock(clock, sizeof(clock));

    const int band_h = CLOCK_INK_H > BATT_H ? CLOCK_INK_H : BATT_H;
    const int clock_y = HEADER_BAND_Y + (band_h - CLOCK_INK_H) / 2 - CLOCK_YOFF;
    const int title_y = clock_y + 4;
    const int batt_y = HEADER_BAND_Y + (band_h - BATT_H) / 2;
    font_draw(s_fb, UI_HEADER_INSET, title_y, "RainMonitor", COL_TEXT, &FONT_20);
    draw_centered(clock_y, clock, COL_TEXT, &FONT_26);

    const int batt_x = LCD_H_RES - UI_HEADER_INSET - BATT_W - BATT_NUB_W;
    draw_battery(batt_x, batt_y, battery);
    const int icon_x = batt_x - 34;
    const int icon_y = batt_y + (BATT_H - 19) / 2;
    if (connected && transport == LINK_TRANSPORT_BLE) {
        draw_bt_icon(icon_x, icon_y, COL_TEXT);
    } else if (connected && transport == LINK_TRANSPORT_USB) {
        draw_usb_icon(icon_x, icon_y, COL_TEXT);
    } else {
        fill_circle(batt_x - 16, batt_y + BATT_H / 2, 4, COL_WAIT);
    }
}

static void draw_fitted_text(int x, int y, const char *text, int max_w, uint16_t color, const font_t *font)
{
    if (!text || !text[0]) {
        return;
    }
    if (font_text_width(text, font) <= max_w) {
        font_draw(s_fb, x, y, text, color, font);
        return;
    }
    char buf[MODEL_NAME_LEN + 4];
    int n = (int)strlen(text);
    while (n > 0) {
        snprintf(buf, sizeof(buf), "%.*s...", n, text);
        if (font_text_width(buf, font) <= max_w) {
            font_draw(s_fb, x, y, buf, color, font);
            return;
        }
        n--;
    }
}

static void draw_thick_line(int x0, int y0, int x1, int y1, int thick, uint16_t color)
{
    const int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    const int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;
    const int r = thick / 2;
    while (1) {
        fill_circle(x, y, r, color);
        if (x == x1 && y == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

static void draw_centered(int y, const char *text, uint16_t color, const font_t *font)
{
    const int w = font_text_width(text, font);
    font_draw(s_fb, (LCD_H_RES - w) / 2, y, text, color, font);
}

static void draw_no_data_overlay(void)
{
    const int icon_w = 148;
    const int icon_h = 104;
    const int icon_x = (LCD_H_RES - icon_w) / 2;
    const int icon_y = 118;
    const int stand_w = 22;
    const int base_w = 56;

    fill_round_rect(icon_x, icon_y, icon_w, icon_h, 12, COL_MUTED);
    fill_round_rect(icon_x + 4, icon_y + 4, icon_w - 8, icon_h - 8, 8, COL_CHART);
    for (int g = 1; g < 4; ++g) {
        fill_rect(icon_x + 18, icon_y + 16 + g * 18, icon_w - 36, 1, COL_GRID);
    }
    draw_thick_line(icon_x + 22, icon_y + icon_h - 28, icon_x + icon_w / 2 - 8, icon_y + 44, 3, COL_RULE);
    draw_thick_line(icon_x + icon_w / 2 - 8, icon_y + 44, icon_x + icon_w - 24, icon_y + 62, 3, COL_RULE);
    draw_thick_line(icon_x + 28, icon_y + icon_h - 22, icon_x + icon_w - 28, icon_y + 22, 5, COL_WAIT);
    fill_rect(icon_x + (icon_w - stand_w) / 2, icon_y + icon_h, stand_w, 12, COL_MUTED);
    fill_round_rect(icon_x + (icon_w - base_w) / 2, icon_y + icon_h + 12, base_w, 8, 3, COL_MUTED);

    draw_centered(icon_y + icon_h + 40, "No data", COL_TEXT, &FONT_26);
    draw_centered(icon_y + icon_h + 78, "Start the collector", COL_MUTED, &FONT_20);
    draw_centered(icon_y + icon_h + 106, "on your computer", COL_MUTED, &FONT_20);
}

static bool hit_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && py >= y && px < x + w && py < y + h;
}

static ui_rect_t tile_rect(int tile)
{
    const int x0 = UI_MARGIN;
    const int x1 = UI_MARGIN + TILE_W + UI_GAP;
    const int y0 = UI_HEADER;
    const int y1 = UI_HEADER + TILE_H + UI_GAP;
    switch (tile) {
    case TILE_MEM:
        return (ui_rect_t){x1, y0, TILE_W, TILE_H};
    case TILE_NET:
        return (ui_rect_t){x0, y1, TILE_W, TILE_H};
    case TILE_GPU:
        return (ui_rect_t){x1, y1, TILE_W, TILE_H};
    default:
        return (ui_rect_t){x0, y0, TILE_W, TILE_H};
    }
}

static ui_rect_t zoom_rect(void)
{
    return (ui_rect_t){UI_MARGIN, UI_HEADER, LCD_H_RES - UI_MARGIN * 2, LCD_V_RES - UI_HEADER - UI_MARGIN};
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float ease_out_quad(float t)
{
    t = 1.0f - clampf(t, 0.0f, 1.0f);
    return 1.0f - t * t;
}

static int lerp_i(int a, int b, float t)
{
    return a + (int)lroundf((float)(b - a) * t);
}

static ui_rect_t lerp_rect(ui_rect_t a, ui_rect_t b, float t)
{
    return (ui_rect_t){
        lerp_i(a.x, b.x, t),
        lerp_i(a.y, b.y, t),
        lerp_i(a.w, b.w, t),
        lerp_i(a.h, b.h, t),
    };
}

static ui_rect_t anim_rect_now(void)
{
    if (!s_anim_active) {
        return s_zoom == TILE_NONE ? tile_rect(s_anim_tile) : zoom_rect();
    }
    float t = (float)(esp_timer_get_time() - s_anim_start_us) / (float)(ZOOM_ANIM_MS * 1000);
    return lerp_rect(s_anim_from, s_anim_to, ease_out_quad(t));
}

static void start_zoom_anim(ui_rect_t from, ui_rect_t to, bool closing)
{
    s_anim_from = from;
    s_anim_to = to;
    s_anim_closing = closing;
    s_anim_start_us = esp_timer_get_time();
    s_anim_active = true;
}

static void cancel_zoom(void)
{
    s_zoom = TILE_NONE;
    s_anim_tile = TILE_NONE;
    s_anim_active = false;
    s_anim_closing = false;
    s_core_scroll = 0;
}

static int core_cols(int n)
{
    if (n <= 1) {
        return 1;
    }
    if (n <= 4) {
        return 2;
    }
    if (n <= 9) {
        return 3;
    }
    return 4;
}

static void draw_core_charts(int x, int y, int w, int h)
{
    int n = s_core_n;
    if (n < 1) {
        n = 1;
    }
    if (n > MAX_CORES) {
        n = MAX_CORES;
    }
    const int cols = core_cols(n);
    const int rows = (n + cols - 1) / cols;
    const int gap = CORE_GAP;
    const int cw = (w - gap * (cols - 1)) / cols;
    int ch = (h - gap * (rows - 1)) / rows;
    if (ch < CORE_MIN_H) {
        ch = CORE_MIN_H;
    }
    const int content_h = rows * ch + (rows > 0 ? (rows - 1) * gap : 0);
    s_core_max_scroll = content_h > h ? content_h - h : 0;
    if (s_core_scroll > s_core_max_scroll) {
        s_core_scroll = s_core_max_scroll;
    }
    if (s_core_scroll < 0 || s_core_max_scroll <= 0) {
        s_core_scroll = 0;
    }
    if (cw < 8 || ch < 8) {
        return;
    }

    clip_begin(x, y, w, h);
    for (int i = 0; i < n; ++i) {
        const int c = i % cols;
        const int r = i / cols;
        const int cx = x + c * (cw + gap);
        const int cy = y + r * (ch + gap) - s_core_scroll;
        if (cy + ch <= y || cy >= y + h) {
            continue;
        }
        draw_chart(cx, cy, cw, ch, s_core[i], NULL, COL_CPU, COL_CPU_FILL, 100.0f);
        stroke_rect(cx, cy, cw, ch, COL_CORE_FRAME);
    }
    clip_end();
}

static void draw_tile(int x, int y, int w, int h, const char *title, const char *line1, const char *line2,
                      const char *value, uint16_t value_color, const float *hist, const bool *valid, uint16_t accent,
                      uint16_t fill, float scale, bool cpu_cores)
{
    const int pad = 10;
    const int inner_w = w - pad * 2;
    if (inner_w < 8 || h < 20) {
        return;
    }
    fill_rect(x, y, w, h, COL_BG);
    stroke_rect(x, y, w, h, COL_RULE);
    font_draw(s_fb, x + pad, y + 8, title, COL_TEXT, &FONT_20);
    if (value && value[0]) {
        const int vw = font_text_width(value, &FONT_26);
        font_draw(s_fb, x + w - pad - vw, y + 4, value, value_color, &FONT_26);
    }
    if (line1 && line1[0]) {
        draw_fitted_text(x + pad, y + 34, line1, inner_w, COL_MUTED, &FONT_16);
    }
    if (line2 && line2[0]) {
        draw_fitted_text(x + pad, y + 52, line2, inner_w, accent, &FONT_16);
    }
    const int chart_h = h - 84;
    if (chart_h < 8) {
        return;
    }
    if (cpu_cores && s_core_n > 0) {
        draw_core_charts(x + pad, y + 74, inner_w, chart_h);
        return;
    }
    draw_chart(x + pad, y + 74, inner_w, chart_h, hist, valid, accent, fill, scale);
}

esp_err_t ui_init(void)
{
    s_fb = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb) {
        return ESP_ERR_NO_MEM;
    }
    ui_clear_history();
    return ESP_OK;
}

void ui_clear_history(void)
{
    memset(s_cpu, 0, sizeof(s_cpu));
    memset(s_gpu, 0, sizeof(s_gpu));
    memset(s_mem, 0, sizeof(s_mem));
    memset(s_net, 0, sizeof(s_net));
    memset(s_gpu_ok, 0, sizeof(s_gpu_ok));
    memset(&s_now, 0, sizeof(s_now));
    s_count = 0;
    s_head = 0;
    s_cpu_model[0] = 0;
    s_gpu_model[0] = 0;
    s_core_n = 0;
    memset(s_core, 0, sizeof(s_core));
    cancel_zoom();
}

bool ui_animation_active(void)
{
    return s_anim_active;
}

int ui_poll_ms(bool connected)
{
    if (s_anim_active) {
        return UI_ANIM_MS;
    }
    if (s_zoom != TILE_NONE) {
        return UI_ZOOM_POLL_MS;
    }
    return connected ? UI_POLL_MS : UI_IDLE_POLL_MS;
}

bool ui_handle_scroll(int dy)
{
    if (s_anim_active || s_zoom != TILE_CPU || s_core_max_scroll <= 0 || dy == 0) {
        return false;
    }
    int next = s_core_scroll - dy;
    if (next < 0) {
        next = 0;
    }
    if (next > s_core_max_scroll) {
        next = s_core_max_scroll;
    }
    if (next == s_core_scroll) {
        return false;
    }
    s_core_scroll = next;
    return true;
}

bool ui_handle_tap(int x, int y, bool connected)
{
    if (!connected) {
        if (s_zoom != TILE_NONE || s_anim_active) {
            cancel_zoom();
            return true;
        }
        return false;
    }
    if (s_anim_active) {
        return false;
    }
    if (s_zoom != TILE_NONE) {
        start_zoom_anim(anim_rect_now(), tile_rect(s_zoom), true);
        s_anim_tile = s_zoom;
        return true;
    }

    const ui_rect_t cpu = tile_rect(TILE_CPU);
    const ui_rect_t mem = tile_rect(TILE_MEM);
    const ui_rect_t net = tile_rect(TILE_NET);
    const ui_rect_t gpu = tile_rect(TILE_GPU);
    int tile = TILE_NONE;
    if (hit_rect(x, y, cpu.x, cpu.y, cpu.w, cpu.h)) {
        tile = TILE_CPU;
    } else if (hit_rect(x, y, mem.x, mem.y, mem.w, mem.h)) {
        tile = TILE_MEM;
    } else if (hit_rect(x, y, net.x, net.y, net.w, net.h)) {
        tile = TILE_NET;
    } else if (hit_rect(x, y, gpu.x, gpu.y, gpu.w, gpu.h)) {
        tile = TILE_GPU;
    } else {
        return false;
    }
    s_anim_tile = tile;
    s_zoom = tile;
    start_zoom_anim(tile_rect(tile), zoom_rect(), false);
    return true;
}

void ui_push_sample(const usb_sample_t *sample)
{
    if (!sample) {
        return;
    }
    s_now = *sample;
    if (sample->cpu_model[0]) {
        strncpy(s_cpu_model, sample->cpu_model, sizeof(s_cpu_model) - 1);
        s_cpu_model[sizeof(s_cpu_model) - 1] = 0;
    }
    if (sample->gpu_model[0]) {
        strncpy(s_gpu_model, sample->gpu_model, sizeof(s_gpu_model) - 1);
        s_gpu_model[sizeof(s_gpu_model) - 1] = 0;
    }

    if (sample->time_valid) {
        s_tod_sec = (int)sample->hour * 3600 + (int)sample->minute * 60 + (int)sample->second;
        s_time_sync_us = esp_timer_get_time();
        s_time_ok = true;
    }

    float cpu = sample->cpu;
    float gpu = sample->gpu;
    float mem = sample->mem_pct;
    float net = sample->net_down_kbs;
    if (sample->net_up_kbs > net) {
        net = sample->net_up_kbs;
    }
    if (cpu < 0) {
        cpu = 0;
    }
    if (cpu > 100) {
        cpu = 100;
    }
    if (gpu < 0) {
        gpu = 0;
    }
    if (gpu > 100) {
        gpu = 100;
    }
    if (mem < 0) {
        mem = 0;
    }
    if (mem > 100) {
        mem = 100;
    }
    if (net < 0) {
        net = 0;
    }

    if (s_count < HISTORY_LEN) {
        s_cpu[s_count] = cpu;
        s_gpu[s_count] = gpu;
        s_mem[s_count] = mem;
        s_net[s_count] = net;
        s_gpu_ok[s_count] = sample->gpu_valid;
        s_core_n = sample->core_n > MAX_CORES ? MAX_CORES : sample->core_n;
        for (int i = 0; i < MAX_CORES; ++i) {
            float core = (i < s_core_n) ? sample->cores[i] : 0;
            if (core < 0) {
                core = 0;
            }
            if (core > 100) {
                core = 100;
            }
            s_core[i][s_count] = core;
        }
        s_count++;
    } else {
        s_cpu[s_head] = cpu;
        s_gpu[s_head] = gpu;
        s_mem[s_head] = mem;
        s_net[s_head] = net;
        s_gpu_ok[s_head] = sample->gpu_valid;
        s_core_n = sample->core_n > MAX_CORES ? MAX_CORES : sample->core_n;
        for (int i = 0; i < MAX_CORES; ++i) {
            float core = (i < s_core_n) ? sample->cores[i] : 0;
            if (core < 0) {
                core = 0;
            }
            if (core > 100) {
                core = 100;
            }
            s_core[i][s_head] = core;
        }
        s_head = (s_head + 1) % HISTORY_LEN;
    }
}

void ui_render(bool connected, link_transport_t transport, battery_status_t battery)
{
    fill_rect(0, 0, LCD_H_RES, LCD_V_RES, COL_BG);
    draw_header(connected, transport, battery);
    if (!connected) {
        cancel_zoom();
        draw_no_data_overlay();
        return;
    }

    char cpu_pct[12];
    char gpu_pct[12];
    char mem_pct[12];
    char freq[24];
    char gpu_freq[24];
    char mem_amt[24];
    char net_s[28];
    char net_r[28];
    char s_raw[24];
    char r_raw[24];

    format_pct(cpu_pct, sizeof(cpu_pct), s_now.cpu, connected && s_count > 0);
    format_pct(gpu_pct, sizeof(gpu_pct), s_now.gpu, connected && s_now.gpu_valid);
    format_pct(mem_pct, sizeof(mem_pct), s_now.mem_pct, connected && s_now.mem_valid);
    format_freq(freq, sizeof(freq), s_now.cpu_freq_mhz, connected && s_now.freq_valid);
    format_freq(gpu_freq, sizeof(gpu_freq), s_now.gpu_freq_mhz, connected && s_now.gpu_freq_valid);
    format_mem(mem_amt, sizeof(mem_amt), s_now.mem_used_gb, s_now.mem_total_gb, connected && s_now.mem_valid);
    format_rate(s_raw, sizeof(s_raw), s_now.net_up_kbs, connected && s_now.net_valid);
    format_rate(r_raw, sizeof(r_raw), s_now.net_down_kbs, connected && s_now.net_valid);
    snprintf(net_s, sizeof(net_s), "S: %s", s_raw);
    snprintf(net_r, sizeof(net_r), "R: %s", r_raw);

    bool anim_done = false;
    ui_rect_t overlay = zoom_rect();
    if (s_anim_active) {
        float t = (float)(esp_timer_get_time() - s_anim_start_us) / (float)(ZOOM_ANIM_MS * 1000);
        if (t >= 1.0f) {
            t = 1.0f;
            anim_done = true;
        }
        overlay = lerp_rect(s_anim_from, s_anim_to, ease_out_quad(t));
        if (anim_done) {
            s_anim_active = false;
            if (s_anim_closing) {
                s_zoom = TILE_NONE;
            }
        }
    } else if (s_zoom != TILE_NONE) {
        overlay = zoom_rect();
    }

    const bool show_overlay = (s_zoom != TILE_NONE || s_anim_active || (anim_done && !s_anim_closing)) &&
                              !(anim_done && s_anim_closing);
    const bool fully_zoomed = show_overlay && !s_anim_active && s_zoom != TILE_NONE;
    if (!fully_zoomed) {
        const ui_rect_t cpu = tile_rect(TILE_CPU);
        const ui_rect_t mem = tile_rect(TILE_MEM);
        const ui_rect_t net = tile_rect(TILE_NET);
        const ui_rect_t gpu = tile_rect(TILE_GPU);
        draw_tile(cpu.x, cpu.y, cpu.w, cpu.h, "CPU", s_cpu_model[0] ? s_cpu_model : "Waiting for host",
                  s_now.freq_valid ? freq : "", cpu_pct, COL_CPU, s_cpu, NULL, COL_CPU, COL_CPU_FILL, 100.0f, false);
        draw_tile(mem.x, mem.y, mem.w, mem.h, "MEMORY", mem_amt, "", mem_pct, COL_MEM, s_mem, NULL, COL_MEM,
                  COL_MEM_FILL, 100.0f, false);
        draw_tile(net.x, net.y, net.w, net.h, "NETWORK", net_s, net_r, "", COL_NET, s_net, NULL, COL_NET, COL_NET_FILL,
                  hist_scale(s_net, 64.0f), false);
        draw_tile(gpu.x, gpu.y, gpu.w, gpu.h, "GPU", s_gpu_model[0] ? s_gpu_model : "Waiting for host",
                  s_now.gpu_freq_valid ? gpu_freq : "", gpu_pct, COL_GPU, s_gpu, s_gpu_ok, COL_GPU, COL_GPU_FILL, 100.0f,
                  false);
    }

    if (!show_overlay) {
        return;
    }

    const int tile = s_zoom != TILE_NONE ? s_zoom : s_anim_tile;
    if (tile == TILE_CPU) {
        draw_tile(overlay.x, overlay.y, overlay.w, overlay.h, "CPU", s_cpu_model[0] ? s_cpu_model : "Waiting for host",
                  s_now.freq_valid ? freq : "", cpu_pct, COL_CPU, s_cpu, NULL, COL_CPU, COL_CPU_FILL, 100.0f, true);
    } else if (tile == TILE_MEM) {
        draw_tile(overlay.x, overlay.y, overlay.w, overlay.h, "MEMORY", mem_amt, "", mem_pct, COL_MEM, s_mem, NULL,
                  COL_MEM, COL_MEM_FILL, 100.0f, false);
    } else if (tile == TILE_NET) {
        draw_tile(overlay.x, overlay.y, overlay.w, overlay.h, "NETWORK", net_s, net_r, "", COL_NET, s_net, NULL, COL_NET,
                  COL_NET_FILL, hist_scale(s_net, 64.0f), false);
    } else if (tile == TILE_GPU) {
        draw_tile(overlay.x, overlay.y, overlay.w, overlay.h, "GPU", s_gpu_model[0] ? s_gpu_model : "Waiting for host",
                  s_now.gpu_freq_valid ? gpu_freq : "", gpu_pct, COL_GPU, s_gpu, s_gpu_ok, COL_GPU, COL_GPU_FILL, 100.0f,
                  false);
    }
}

void ui_present(void)
{
    for (int b = 0; b < BAND_COUNT; ++b) {
        display_stage_band(b, 0, LCD_H_RES, s_fb + b * BAND_ROWS * LCD_H_RES);
    }
    display_present();
}
