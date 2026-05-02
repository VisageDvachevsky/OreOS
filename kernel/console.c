#include "kernel.h"

#define CELL_W 12U
#define CELL_H 18U
#define TOP_BAR_H 32U
#define LEFT_PAD 16U
#define BG 0x000b1014U
#define PANEL_BG 0x00101920U
#define BAR_BG 0x00182026U
#define FG 0x00e6edf3U
#define DIM 0x008aa0adU
#define ACCENT 0x004ed7b7U
#define BLUE 0x0064b5f6U
#define YELLOW 0x00f3cc60U
#define RED 0x00ff6b6bU
#define GREEN 0x0068d391U
#define MAGENTA 0x00c792eaU
#define CYAN 0x005fd7e5U

static OreFramebuffer fb;
static Spinlock console_lock;
static uint32_t cursor_x;
static uint32_t cursor_y;
static uint32_t cols;
static uint32_t rows;
static uint32_t text_origin_y;
static uint32_t text_origin_x;
static uint32_t esc_state;
static uint32_t esc_param;
static uint32_t esc_params[4];
static uint32_t esc_param_count;
static uint32_t current_fg;
static uint32_t current_bg;
static uint8_t inverse;
static uint8_t ps2_shift;
static uint8_t ps2_e0;
static volatile uint32_t input_keys;
static volatile uint32_t input_ascii;
static volatile uint32_t input_frame;
static uint8_t gfx_mode_active;

static const uint32_t game_palette[16] = {
    0x00000000U, 0x0011233dU, 0x00236fc2U, 0x004cc9f0U,
    0x002f7d32U, 0x006fc45aU, 0x007a4a28U, 0x007f8a93U,
    0x00e8eef2U, 0x00f5d76eU, 0x00e89242U, 0x00d64646U,
    0x00ef4fc3U, 0x008746d6U, 0x002073a8U, 0x00fff6a8U
};

static void put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!fb.base || x >= fb.width || y >= fb.height) return;
    uint32_t *p = (uint32_t *)(uintptr_t)fb.base;
    p[y * fb.pixels_per_scanline + x] = color;
}

static void input_set_ascii(uint32_t ascii) {
    input_ascii = ascii;
    input_frame++;
}

static uint32_t input_bit_for_ascii(uint32_t c) {
    if (c == 'a' || c == 'A') return ORE_INPUT_LEFT;
    if (c == 'd' || c == 'D') return ORE_INPUT_RIGHT;
    if (c == 'w' || c == 'W') return ORE_INPUT_UP;
    if (c == 's' || c == 'S') return ORE_INPUT_DOWN;
    if (c == ' ') return ORE_INPUT_FIRE;
    if (c == 'q' || c == 'Q' || c == 27) return ORE_INPUT_QUIT;
    return 0;
}

static void input_set_key(uint32_t bit, uint8_t down) {
    if (!bit) return;
    if (down) input_keys |= bit;
    else input_keys &= ~bit;
    input_frame++;
}

static uint32_t input_bit_for_scancode(uint8_t sc, uint8_t e0) {
    if (e0) {
        if (sc == 0x4b) return ORE_INPUT_LEFT;
        if (sc == 0x4d) return ORE_INPUT_RIGHT;
        if (sc == 0x48) return ORE_INPUT_UP;
        if (sc == 0x50) return ORE_INPUT_DOWN;
        return 0;
    }
    if (sc == 0x1e) return ORE_INPUT_LEFT;
    if (sc == 0x20) return ORE_INPUT_RIGHT;
    if (sc == 0x11) return ORE_INPUT_UP;
    if (sc == 0x1f) return ORE_INPUT_DOWN;
    if (sc == 0x39) return ORE_INPUT_FIRE;
    if (sc == 0x10 || sc == 0x01) return ORE_INPUT_QUIT;
    return 0;
}

static void fill_rect(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t y = y0; y < y0 + h && y < fb.height; ++y) {
        for (uint32_t x = x0; x < x0 + w && x < fb.width; ++x) put_pixel(x, y, color);
    }
}

static const uint8_t *glyph_rows(char c) {
    static const uint8_t space[7] = {0,0,0,0,0,0,0};
    static const uint8_t qmark[7] = {14,17,1,2,4,0,4};
    static const uint8_t bang[7] = {4,4,4,4,4,0,4};
    static const uint8_t colon[7] = {0,4,4,0,4,4,0};
    static const uint8_t dot[7] = {0,0,0,0,0,12,12};
    static const uint8_t comma[7] = {0,0,0,0,0,4,8};
    static const uint8_t slash[7] = {1,1,2,4,8,16,16};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t underscore[7] = {0,0,0,0,0,0,31};
    static const uint8_t gt[7] = {16,8,4,2,4,8,16};
    static const uint8_t lt[7] = {1,2,4,8,4,2,1};
    static const uint8_t eq[7] = {0,0,31,0,31,0,0};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
    static const uint8_t quote[7] = {10,10,0,0,0,0,0};
    static const uint8_t apos[7] = {4,4,0,0,0,0,0};
    static const uint8_t lpar[7] = {2,4,8,8,8,4,2};
    static const uint8_t rpar[7] = {8,4,2,2,2,4,8};
    static const uint8_t star[7] = {0,21,14,31,14,21,0};
    static const uint8_t hash[7] = {10,31,10,10,31,10,0};
    static const uint8_t zero[7] = {14,17,19,21,25,17,14};
    static const uint8_t one[7] = {4,12,4,4,4,4,14};
    static const uint8_t two[7] = {14,17,1,2,4,8,31};
    static const uint8_t three[7] = {30,1,1,14,1,1,30};
    static const uint8_t four[7] = {2,6,10,18,31,2,2};
    static const uint8_t five[7] = {31,16,30,1,1,17,14};
    static const uint8_t six[7] = {6,8,16,30,17,17,14};
    static const uint8_t seven[7] = {31,1,2,4,8,8,8};
    static const uint8_t eight[7] = {14,17,17,14,17,17,14};
    static const uint8_t nine[7] = {14,17,17,15,1,2,12};
    static const uint8_t letters[26][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c == '0') return zero; if (c == '1') return one; if (c == '2') return two;
    if (c == '3') return three; if (c == '4') return four; if (c == '5') return five;
    if (c == '6') return six; if (c == '7') return seven; if (c == '8') return eight;
    if (c == '9') return nine; if (c == ' ') return space; if (c == '!') return bang;
    if (c == ':') return colon; if (c == '.') return dot; if (c == ',') return comma;
    if (c == '/') return slash; if (c == '-') return dash; if (c == '_') return underscore;
    if (c == '>') return gt; if (c == '<') return lt; if (c == '=') return eq;
    if (c == '+') return plus; if (c == '"') return quote; if (c == '\'') return apos;
    if (c == '(') return lpar; if (c == ')') return rpar; if (c == '*') return star;
    if (c == '#') return hash;
    return qmark;
}

static void draw_glyph(uint32_t px, uint32_t py, char c, uint32_t color) {
    const uint8_t *rows5 = glyph_rows(c);
    for (uint32_t y = 0; y < 7; ++y) {
        for (uint32_t x = 0; x < 5; ++x) {
            if (rows5[y] & (1U << (4 - x))) {
                uint32_t gx = px + 1 + x * 2;
                uint32_t gy = py + 2 + y * 2;
                put_pixel(gx, gy, color);
                put_pixel(gx + 1, gy, color);
                put_pixel(gx, gy + 1, color);
                put_pixel(gx + 1, gy + 1, color);
            }
        }
    }
}

static void draw_text_px(uint32_t x, uint32_t y, const char *s, uint32_t color) {
    while (*s) {
        draw_glyph(x, y, *s++, color);
        x += CELL_W;
    }
}

static void draw_status_bar(void) {
    if (!fb.base) return;
    fill_rect(0, 0, fb.width, TOP_BAR_H, BAR_BG);
    fill_rect(0, TOP_BAR_H - 3, fb.width, 1, BLUE);
    fill_rect(0, TOP_BAR_H - 2, fb.width, 2, ACCENT);
    fill_rect(8, 7, 24, 18, ACCENT);
    draw_text_px(40, 9, "ORE", FG);
    draw_text_px(80, 9, "interactive system", ACCENT);
    draw_text_px(fb.width > 224 ? fb.width - 224 : 8, 9, "disk / smp / ring3", DIM);
}

static void clear_screen(void) {
    if (!fb.base) return;
    fill_rect(0, 0, fb.width, fb.height, BG);
    draw_status_bar();
    fill_rect(8, text_origin_y - 8, fb.width > 16 ? fb.width - 16 : fb.width, fb.height - text_origin_y, PANEL_BG);
    cursor_x = 0;
    cursor_y = 0;
}

static void clear_cell(uint32_t cx, uint32_t cy) {
    fill_rect(text_origin_x + cx * CELL_W, text_origin_y + cy * CELL_H, CELL_W, CELL_H, current_bg);
}

static void scroll_up(void) {
    if (!fb.base || rows == 0) return;
    uint32_t *p = (uint32_t *)(uintptr_t)fb.base;
    uint32_t src_y = text_origin_y + CELL_H;
    uint32_t dst_y = text_origin_y;
    uint32_t copy_h = rows > 1 ? (rows - 1) * CELL_H : 0;
    for (uint32_t y = 0; y < copy_h; ++y) {
        for (uint32_t x = 0; x < fb.width; ++x) {
            p[(dst_y + y) * fb.pixels_per_scanline + x] = p[(src_y + y) * fb.pixels_per_scanline + x];
        }
    }
    fill_rect(8, text_origin_y + (rows - 1) * CELL_H, fb.width > 16 ? fb.width - 16 : fb.width, CELL_H, PANEL_BG);
}

static void newline(void) {
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= rows) {
        scroll_up();
        cursor_y = rows - 1;
    }
}

static uint32_t ansi_color(uint32_t code, uint32_t fallback) {
    switch (code) {
        case 30: return 0x001b232bU;
        case 31: return RED;
        case 32: return GREEN;
        case 33: return YELLOW;
        case 34: return BLUE;
        case 35: return MAGENTA;
        case 36: return CYAN;
        case 37: return FG;
        case 90: return DIM;
        case 91: return RED;
        case 92: return GREEN;
        case 93: return YELLOW;
        case 94: return BLUE;
        case 95: return MAGENTA;
        case 96: return CYAN;
        case 97: return 0x00ffffffU;
        default: return fallback;
    }
}

static uint32_t ansi_bg(uint32_t code, uint32_t fallback) {
    if (code >= 40 && code <= 47) return ansi_color(code - 10, fallback);
    if (code >= 100 && code <= 107) return ansi_color(code - 60, fallback);
    return fallback;
}

static void sgr_apply(void) {
    if (esc_param_count == 0) {
        current_fg = FG;
        current_bg = PANEL_BG;
        inverse = 0;
        return;
    }
    for (uint32_t i = 0; i < esc_param_count; ++i) {
        uint32_t p = esc_params[i];
        if (p == 0) {
            current_fg = FG;
            current_bg = PANEL_BG;
            inverse = 0;
        } else if (p == 1) {
            current_fg = 0x00ffffffU;
        } else if (p == 2) {
            current_fg = DIM;
        } else if (p == 7) {
            inverse = 1;
        } else if (p == 22 || p == 27) {
            inverse = 0;
        } else if ((p >= 30 && p <= 37) || (p >= 90 && p <= 97)) {
            current_fg = ansi_color(p, current_fg);
        } else if ((p >= 40 && p <= 47) || (p >= 100 && p <= 107)) {
            current_bg = ansi_bg(p, current_bg);
        }
    }
}

static void esc_reset(void) {
    esc_state = 0;
    esc_param = 0;
    esc_param_count = 0;
}

static void esc_push_param(void) {
    if (esc_param_count < 4) esc_params[esc_param_count++] = esc_param;
    esc_param = 0;
}

static int handle_escape(char c) {
    if (!esc_state) return 0;
    if (esc_state == 1) {
        if (c == '[') {
            esc_state = 2;
            esc_param = 0;
            esc_param_count = 0;
            return 1;
        }
        esc_reset();
        return 1;
    }
    if (c >= '0' && c <= '9') {
        esc_param = esc_param * 10 + (uint32_t)(c - '0');
        return 1;
    }
    if (c == ';') {
        esc_push_param();
        return 1;
    }
    if (c == 'J') {
        clear_screen();
        esc_reset();
        return 1;
    }
    if (c == 'H') {
        cursor_x = cursor_y = 0;
        esc_reset();
        return 1;
    }
    if (c == 'm') {
        esc_push_param();
        sgr_apply();
        esc_reset();
        return 1;
    }
    esc_reset();
    return 1;
}

static void put_char(char c) {
    if (!fb.base || !cols || !rows) return;
    if (handle_escape(c)) return;
    if (c == 27) {
        esc_state = 1;
        return;
    }
    if (c == '\r') return;
    if (c == '\n') {
        newline();
        return;
    }
    if (c == '\b') {
        if (cursor_x) cursor_x--;
        clear_cell(cursor_x, cursor_y);
        return;
    }
    uint32_t fg = inverse ? current_bg : current_fg;
    uint32_t bg = inverse ? current_fg : current_bg;
    fill_rect(text_origin_x + cursor_x * CELL_W, text_origin_y + cursor_y * CELL_H, CELL_W, CELL_H, bg);
    draw_glyph(text_origin_x + cursor_x * CELL_W, text_origin_y + cursor_y * CELL_H, c, fg);
    cursor_x++;
    if (cursor_x >= cols) newline();
}

void console_init(const BootInfo *boot_info) {
    fb = boot_info->framebuffer;
    cursor_x = cursor_y = 0;
    text_origin_x = LEFT_PAD;
    text_origin_y = TOP_BAR_H + 16;
    current_fg = FG;
    current_bg = PANEL_BG;
    inverse = 0;
    cols = fb.width > (LEFT_PAD * 2) ? (fb.width - LEFT_PAD * 2) / CELL_W : 0;
    rows = fb.height > text_origin_y ? (fb.height - text_origin_y) / CELL_H : 0;
    if (fb.base) {
        clear_screen();
        console_write("Ore framebuffer console online\n");
    }
    kprintf("console: framebuffer %ux%u base 0x%lx\n", fb.width, fb.height, fb.base);
}

void console_write(const char *s) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    spinlock_lock(&console_lock);
    if (gfx_mode_active) {
        gfx_mode_active = 0;
        clear_screen();
    }
    while (*s) put_char(*s++);
    spinlock_unlock(&console_lock);
    if (flags & (1ULL << 9)) __asm__ volatile("sti" : : : "memory");
}

void console_putc(char c) {
    char s[2] = { c, 0 };
    console_write(s);
}

int console_read_key(void) {
    int serial = serial_read_byte();
    if (serial >= 0) {
        input_set_ascii((uint32_t)serial);
        return serial;
    }
    uint8_t status = inb(0x64);
    if ((status & 1) == 0) return -1;
    uint8_t sc = inb(0x60);
    if (sc == 0xE0) {
        ps2_e0 = 1;
        return -1;
    }
    uint8_t released = sc & 0x80;
    sc &= 0x7F;
    uint8_t was_e0 = ps2_e0;
    if (sc == 0x2A || sc == 0x36) {
        ps2_shift = released ? 0 : 1;
        ps2_e0 = 0;
        return -1;
    }
    input_set_key(input_bit_for_scancode(sc, was_e0), released ? 0 : 1);
    if (released) {
        ps2_e0 = 0;
        return -1;
    }
    if (ps2_e0) {
        ps2_e0 = 0;
        return -1;
    }

    static const char normal[128] = {
        [0x01] = 27, [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4',
        [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9',
        [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b',
        [0x0F] = '\t', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
        [0x13] = 'r', [0x14] = 't', [0x15] = 'y', [0x16] = 'u',
        [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[',
        [0x1B] = ']', [0x1C] = '\n', [0x1E] = 'a', [0x1F] = 's',
        [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
        [0x24] = 'j', [0x25] = 'k', [0x26] = 'l', [0x27] = ';',
        [0x28] = '\'', [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z',
        [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
        [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.',
        [0x35] = '/', [0x39] = ' '
    };
    static const char shifted[128] = {
        [0x01] = 27, [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$',
        [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(',
        [0x0B] = ')', [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b',
        [0x0F] = '\t', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
        [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U',
        [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = '{',
        [0x1B] = '}', [0x1C] = '\n', [0x1E] = 'A', [0x1F] = 'S',
        [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
        [0x24] = 'J', [0x25] = 'K', [0x26] = 'L', [0x27] = ':',
        [0x28] = '"', [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z',
        [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
        [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>',
        [0x35] = '?', [0x39] = ' '
    };
    char c = ps2_shift ? shifted[sc] : normal[sc];
    if (c) {
        input_set_ascii((uint32_t)c);
        return c;
    }
    return -1;
}

int console_gfx_info(OreGfxInfo *info) {
    if (!info || !fb.base) return -ORE_EINVAL;
    info->width = fb.width;
    info->height = fb.height;
    info->logical_width = ORE_GFX_LOGICAL_WIDTH;
    info->logical_height = ORE_GFX_LOGICAL_HEIGHT;
    info->format = ORE_GFX_FORMAT_INDEXED16;
    info->pitch = ORE_GFX_LOGICAL_WIDTH;
    info->palette_count = 16;
    info->reserved = 0;
    return 0;
}

int console_gfx_present_indexed16(const uint8_t *buffer, uint32_t width, uint32_t height) {
    if (!fb.base || !buffer) return -ORE_EINVAL;
    if (width != ORE_GFX_LOGICAL_WIDTH || height != ORE_GFX_LOGICAL_HEIGHT) return -ORE_EINVAL;
    if (fb.width < width || fb.height < height) return -ORE_EINVAL;
    uint32_t x0 = (fb.width - width) / 2;
    uint32_t y0 = (fb.height - height) / 2;
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    spinlock_lock(&console_lock);
    if (!gfx_mode_active) {
        fill_rect(0, 0, fb.width, fb.height, 0x00000000U);
        gfx_mode_active = 1;
    }
    uint32_t *dst = (uint32_t *)(uintptr_t)fb.base;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t *row = dst + (y0 + y) * fb.pixels_per_scanline + x0;
        const uint8_t *src = buffer + y * width;
        for (uint32_t x = 0; x < width; ++x) row[x] = game_palette[src[x] & 15U];
    }
    spinlock_unlock(&console_lock);
    if (flags & (1ULL << 9)) __asm__ volatile("sti" : : : "memory");
    return 0;
}

void input_state_poll(OreInputState *state) {
    int key = console_read_key();
    uint32_t transient = 0;
    if (key >= 0) transient = input_bit_for_ascii((uint32_t)key);
    if (state) {
        state->keys = input_keys | transient;
        state->ascii = input_ascii;
        state->mouse_x = 0;
        state->mouse_y = 0;
        state->buttons = 0;
        state->frame = input_frame;
    }
    input_ascii = 0;
}

void input_init(void) {
    kprintf("input: PS/2 polling ready\n");
}
