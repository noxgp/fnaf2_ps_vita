// text rendering with stb_truetype
#include "fp.h"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
static stbtt_fontinfo fi; static uint8_t *ttf; static int ok;
int font_init(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) { logf_("font %s missing", path); return -1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); ttf = malloc(sz);
    if (fread(ttf, 1, sz, f) != (size_t)sz) { fclose(f); return -1; }
    fclose(f);
    ok = stbtt_InitFont(&fi, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)); return ok ? 0 : -1;
}
static int utf8(const unsigned char **p) {
    const unsigned char *s = *p; int c = *s++;
    if (c >= 0xF0 && s[0] && s[1] && s[2]) { c = ((c & 7) << 18) | ((s[0] & 63) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); s += 3; }
    else if (c >= 0xE0 && s[0] && s[1]) { c = ((c & 15) << 12) | ((s[0] & 63) << 6) | (s[1] & 63); s += 2; }
    else if (c >= 0xC0 && s[0]) { c = ((c & 31) << 6) | (s[0] & 63); s += 1; }
    *p = s; return c;
}
static float adv(int cp, float scale) { int a, l; stbtt_GetCodepointHMetrics(&fi, cp, &a, &l); return a * scale; }
typedef struct Line { const unsigned char *s, *e; float w; } Line;
uint8_t *font_render(int px, const char *text, int w, int h, int flags, int color, int *outw, int *outh, int measure_only, int maxw) {
    *outw = w; *outh = h;
    if (!ok) { if (measure_only) { *outw = (int)(strlen(text) * px * 0.55f); *outh = px; } return NULL; }
    if (px <= 0) px = 12;
    float scale = stbtt_ScaleForMappingEmToPixels(&fi, (float)px); int asc, desc, gap; stbtt_GetFontVMetrics(&fi, &asc, &desc, &gap);
    float lineh = (asc - desc + gap) * scale, base = asc * scale; int wrapw = measure_only ? maxw : w;
    Line lines[128]; int nl = 0; const unsigned char *p = (const unsigned char *)text;
    while (nl < 128) {
        const unsigned char *ls = p, *lastsp = NULL, *q = p; float lw = 0, wsp = 0;
        while (*q && *q != '\n') {
            const unsigned char *cs = q; int cp = utf8(&q); if (cp == '\r') continue;
            float a = adv(cp, scale);
            if (wrapw > 0 && lw + a > wrapw && lastsp && cs > ls) { q = lastsp; lw = wsp; break; }
            if (cp == ' ') { lastsp = q; wsp = lw; }
            lw += a;
        }
        lines[nl].s = ls; lines[nl].e = q; lines[nl].w = lw; nl++;
        if (!*q) break;
        if (*q == '\n') q++;
        p = q;
    }
    if (measure_only) { float mw = 0; for (int i = 0; i < nl; i++) if (lines[i].w > mw) mw = lines[i].w; *outw = (int)ceilf(mw); *outh = (int)ceilf(nl * lineh); return NULL; }
    if (w <= 0 || h <= 0) return NULL;
    uint8_t *buf = calloc(1, (size_t)w * h * 4);
    int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    for (int i = 0; i < w * h; i++) { buf[i * 4] = (uint8_t)r; buf[i * 4 + 1] = (uint8_t)g; buf[i * 4 + 2] = (uint8_t)b; }
    float y0 = (flags & 4) ? (h - nl * lineh) / 2 : (flags & 8) ? h - nl * lineh : 0; if (y0 < 0) y0 = 0;
    static uint8_t gb[160 * 160];
    for (int i = 0; i < nl; i++) {
        float x = (flags & 1) ? (w - lines[i].w) / 2 : (flags & 2) ? w - lines[i].w : 0; const unsigned char *q = lines[i].s;
        while (q < lines[i].e) {
            int cp = utf8(&q); if (cp == '\r') continue;
            int x0, yy0, x1, y1; stbtt_GetCodepointBitmapBox(&fi, cp, scale, scale, &x0, &yy0, &x1, &y1);
            int gw = x1 - x0, gh = y1 - yy0;
            if (gw > 0 && gh > 0 && gw <= 160 && gh <= 160) {
                stbtt_MakeCodepointBitmap(&fi, gb, gw, gh, gw, scale, scale, cp);
                int dx = (int)floorf(x + 0.5f) + x0, dy = (int)floorf(y0 + i * lineh + base + 0.5f) + yy0;
                for (int yy = 0; yy < gh; yy++) { int ty = dy + yy; if (ty < 0 || ty >= h) continue;
                    for (int xx = 0; xx < gw; xx++) { int tx = dx + xx; if (tx < 0 || tx >= w) continue; uint8_t *d = &buf[(ty * w + tx) * 4 + 3]; int v = *d + gb[yy * gw + xx]; *d = (uint8_t)(v > 255 ? 255 : v); } }
            }
            x += adv(cp, scale);
        }
    }
    return buf;
}
