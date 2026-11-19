// headless software backend used for testing on Linux: renders into an RGBA buffer, dumps PNG screenshots
#include "fp.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
struct Tex { int w, h; uint8_t *px; };
static uint8_t *fb; static int FW, FH; static unsigned now_ms;
struct Tex *plat_tex(int w, int h, int fmt, const uint8_t *px, int stride) {
    struct Tex *t = malloc(sizeof *t); t->w = w; t->h = h; t->px = malloc((size_t)w * h * 4);
    if (fmt == 8) { const uint8_t *pal = px; px += 1024;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) memcpy(t->px + ((size_t)y * w + x) * 4, pal + px[(size_t)y * stride + x] * 4, 4);
        return t; }
    for (int y = 0; y < h; y++) { const uint8_t *s = px + (size_t)y * stride; uint8_t *d = t->px + (size_t)y * w * 4;
        for (int x = 0; x < w; x++, d += 4) {
            if (fmt == 0) { memcpy(d, s + x * 4, 4); continue; }
            unsigned v = s[x * 2] | (s[x * 2 + 1] << 8);
            if (fmt == 1) { d[0] = ((v >> 12) & 15) * 17; d[1] = ((v >> 8) & 15) * 17; d[2] = ((v >> 4) & 15) * 17; d[3] = (v & 15) * 17; }
            else if (fmt == 2) { d[0] = ((v >> 11) & 31) * 255 / 31; d[1] = ((v >> 6) & 31) * 255 / 31; d[2] = ((v >> 1) & 31) * 255 / 31; d[3] = (v & 1) * 255; }
            else { d[0] = ((v >> 11) & 31) * 255 / 31; d[1] = ((v >> 5) & 63) * 255 / 63; d[2] = (v & 31) * 255 / 31; d[3] = 255; }
        } }
    return t;
}
void plat_tex_free(struct Tex *t) { if (t) { free(t->px); free(t); } }
void plat_begin(int off) { (void)off; if (!fb) { FW = G.w; FH = G.h; fb = malloc((size_t)FW * FH * 4); } }
void plat_present(void) {}
void plat_clear(unsigned rgb) { for (int i = 0; i < FW * FH; i++) { fb[i * 4] = rgb >> 16; fb[i * 4 + 1] = rgb >> 8; fb[i * 4 + 2] = rgb; fb[i * 4 + 3] = 255; } }
void plat_rect(float x, float y, float w, float h, unsigned rgba) {
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h); if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > FW) x1 = FW; if (y1 > FH) y1 = FH;
    for (int yy = y0; yy < y1; yy++) for (int xx = x0; xx < x1; xx++) { uint8_t *d = fb + (yy * FW + xx) * 4; d[0] = rgba >> 24; d[1] = rgba >> 16; d[2] = rgba >> 8; }
}
void plat_quad(struct Tex *t, float u0, float v0, float u1, float v1, const float *xy, unsigned rgba, int blend) {
    float ax = xy[2] - xy[0], ay = xy[3] - xy[1], bx = xy[6] - xy[0], by = xy[7] - xy[1], det = ax * by - ay * bx;
    if (fabsf(det) < 1e-6f) return;
    float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i < 4; i++) { if (xy[i * 2] < mnx) mnx = xy[i * 2]; if (xy[i * 2] > mxx) mxx = xy[i * 2]; if (xy[i * 2 + 1] < mny) mny = xy[i * 2 + 1]; if (xy[i * 2 + 1] > mxy) mxy = xy[i * 2 + 1]; }
    int x0 = (int)floorf(mnx), x1 = (int)ceilf(mxx), y0 = (int)floorf(mny), y1 = (int)ceilf(mxy);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > FW) x1 = FW; if (y1 > FH) y1 = FH;
    int tr = rgba >> 24, tg = (rgba >> 16) & 255, tb = (rgba >> 8) & 255, ta_ = rgba & 255;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        float px = x + 0.5f - xy[0], py = y + 0.5f - xy[1];
        float s = (px * by - py * bx) / det, tt = (py * ax - px * ay) / det;
        if (s < 0 || s >= 1 || tt < 0 || tt >= 1) continue;
        int sx = (int)((u0 + s * (u1 - u0)) * t->w), sy = (int)((v0 + tt * (v1 - v0)) * t->h); if (sx < 0 || sy < 0 || sx >= t->w || sy >= t->h) continue;
        const uint8_t *p = t->px + (sy * t->w + sx) * 4; int a = p[3] * ta_ / 255; if (!a) continue;
        uint8_t *d = fb + (y * FW + x) * 4; int r = p[0] * tr / 255, g = p[1] * tg / 255, b = p[2] * tb / 255;
        if (blend) { int v; v = d[0] + r * a / 255; d[0] = v > 255 ? 255 : v; v = d[1] + g * a / 255; d[1] = v > 255 ? 255 : v; v = d[2] + b * a / 255; d[2] = v > 255 ? 255 : v; }
        else { d[0] = (r * a + d[0] * (255 - a)) / 255; d[1] = (g * a + d[1] * (255 - a)) / 255; d[2] = (b * a + d[2] * (255 - a)) / 255; }
    }
}
void plat_persp(float x, float y, float w, float h, float fbv) { if (getenv("NOPERSP")) return;
    int x0 = (int)x, y0 = (int)y, ww = (int)w, hh = (int)h; if (x0 < 0 || y0 < 0 || x0 + ww > FW || y0 + hh > FH || ww <= 0 || hh <= 0) return;
    uint8_t *col = malloc((size_t)hh * 4);
    for (int i = 0; i < ww; i++) {
        float s = (i + 0.5f) / ww, fc = 1.0f + sinf(s * 3.1415f) * fbv - fbv;
        for (int j = 0; j < hh; j++) memcpy(col + j * 4, fb + ((y0 + j) * FW + x0 + i) * 4, 4);
        for (int j = 0; j < hh; j++) { float tt = (j + 0.5f) / hh; int sj = (int)((tt * fc + (1 - fc) / 2) * hh); if (sj < 0) sj = 0; if (sj >= hh) sj = hh - 1; memcpy(fb + ((y0 + j) * FW + x0 + i) * 4, col + sj * 4, 4); }
    }
    free(col);
}
unsigned plat_ms(void) { return now_ms; }
static void tick(int n) { static int16_t sink[2048 * 2]; for (int i = 0; i < n; i++) { G.timerDelta = (i + G.loop) % 3 == 0 ? 16 : 17; now_ms += G.timerDelta; frame_tick(); snd_mix(sink, 735);
        for (int k = 0; k < MAXTOUCH; k++) { G.in.t[k].isnew = 0; G.in.t[k].ended = 0; } } }
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: fp2 datadir [cmds: t N | f N | d x y | u | c x y | s file.png | o]\n"); return 1; }
    G.rng = 12345; if (getenv("FP_CACHE_MB")) cache_set_budget(atol(getenv("FP_CACHE_MB")) << 20); { extern int fp_trace; fp_trace = getenv("FPTRACE") != NULL; }
    if (app_load(argv[1])) return 2;
    char p[400]; snprintf(p, sizeof p, "%s/font.ttf", argv[1]); font_init(p); snd_init();
    frame_start(0);
    for (int i = 2; i < argc; i++) {
        const char *c = argv[i];
        if (!strcmp(c, "t") && i + 1 < argc) tick(atoi(argv[++i]));
        else if (!strcmp(c, "f") && i + 1 < argc) frame_start(atoi(argv[++i]));
        else if (!strcmp(c, "d") && i + 2 < argc) { G.in.t[0].down = 1; G.in.t[0].isnew = 1; G.in.t[0].x = atoi(argv[i + 1]); G.in.t[0].y = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(c, "m") && i + 2 < argc) { G.in.t[0].x = atoi(argv[i + 1]); G.in.t[0].y = atoi(argv[i + 2]); i += 2; }
        else if (!strcmp(c, "D") && i + 3 < argc) { int s = atoi(argv[i + 1]); G.in.t[s].down = 1; G.in.t[s].isnew = 1; G.in.t[s].x = atoi(argv[i + 2]); G.in.t[s].y = atoi(argv[i + 3]); i += 3; }
        else if (!strcmp(c, "U") && i + 1 < argc) { int s = atoi(argv[++i]); G.in.t[s].down = 0; G.in.t[s].ended = 1; }
        else if (!strcmp(c, "u")) { G.in.t[0].down = 0; G.in.t[0].ended = 1; }
        else if (!strcmp(c, "c") && i + 2 < argc) { G.in.t[0].down = 1; G.in.t[0].isnew = 1; G.in.t[0].x = atoi(argv[i + 1]); G.in.t[0].y = atoi(argv[i + 2]); i += 2; tick(3); G.in.t[0].down = 0; G.in.t[0].ended = 1; tick(2); }
        else if (!strcmp(c, "s") && i + 1 < argc) { frame_draw(); stbi_write_png(argv[++i], FW, FH, 4, fb, FW * 4); }
        else if (!strcmp(c, "a") && i + 2 < argc) { snd_stop_all(); snd_play(atoi(argv[i + 1]), 0, 1); FILE *f = fopen(argv[i + 2], "wb"); static int16_t b[2048 * 2];
            for (int k = 0; k < 44100 * 6 / 1024; k++) { snd_mix(b, 1024); fwrite(b, 4, 1024, f); } fclose(f); i += 2; }
        else if (!strcmp(c, "o")) {
            fprintf(stderr, "frame %d '%s' loop %d timer %d win %d,%d\n", G.fi, G.fname, G.loop, G.timer, G.winx, G.winy);
            for (int k = 0; k < G.ngv; k++) fprintf(stderr, "gv%d=%g ", k, vd(&G.gv[k])); fprintf(stderr, "\n");
            for (int k = 0; k < G.nobjslots; k++) { Obj *o = G.obj[k]; if (!o) continue; int l, t, r, b; obj_rect(o, &l, &t, &r, &b);
                fprintf(stderr, " #%d oi%d '%s' t%d L%d %s (%d,%d) rect %d,%d-%d,%d img %d a%d f%d\n", k, o->oi->handle, o->oi->name ? o->oi->name : "", o->type, o->layer, o->hidden ? "hid" : "vis", o->x, o->y, l, t, r, b, obj_image(o), o->animForced >= 0 ? o->animForced : o->anim, o->frame); if (o->nval) fprintf(stderr, "     v0=%g v1=%g v2=%g\n", vd(&o->val[0]), vd(&o->val[1]), vd(&o->val[2])); if (o->type == 7) fprintf(stderr, "     cv=%g\n", vd(&o->cv)); }
        }
    }
    return 0;
}
