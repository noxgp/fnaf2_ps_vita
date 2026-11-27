// runtime: objects, animations, frame lifecycle, rendering, image cache
#include "fp.h"
#define STBI_ONLY_JPEG
#define STBI_SUPPORT_ZLIB
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "lz4.h"
int ext_image(Obj *o); float ext_persp_fb(Obj *o);

int frnd(int n) { if (n <= 0) return 0; G.rng = (G.rng * 31415u + 1u) & 0xFFFF; return (int)((G.rng * (unsigned)n) >> 16); }

// ---------------- image cache ----------------
static unsigned drawFrame; static long cacheBytes;
static struct Tex *tex_make(int w, int h, int fmt, const uint8_t *px, int stride);
int img_loads; double img_load_ms; long cache_bytes(void) { return cacheBytes; }
#ifdef __vita__
static long cacheBudget = 80L << 20;
#else
static long cacheBudget = 900L << 20;
#endif
struct Tex *img_tex(int h) {
    if (h < 0 || h >= G.nimg) return NULL;
    ImgInfo *ii = &G.img[h];
    if (!ii->size) return NULL;
    ii->used = drawFrame;
    if (ii->tex) return ii->tex;
    unsigned lt0 = plat_ms(); img_loads++; if (getenv("FP_LATE")) logf_("load img %d", h);
    uint8_t *comp = malloc(ii->size); if (!comp) return NULL;
    FILE *src = G.pak ? G.pak : G.f;
    fseek(src, ii->off, SEEK_SET);
    if (fread(comp, 1, ii->size, src) != (size_t)ii->size) { free(comp); return NULL; }
    if (G.pak) {
        int bpp = ii->mode == 0 ? 4 : ii->mode == 8 ? 1 : 2, len = ii->pw * ii->ph * bpp + (ii->mode == 8 ? 1024 : 0); uint8_t *raw = malloc(len);
        int n = raw ? LZ4_decompress_safe((const char *)comp, (char *)raw, ii->size, len) : -1; free(comp);
        if (n != len) { logf_("image %d: lz4 %d expected %d", h, n, len); free(raw); ii->size = 0; return NULL; }
        ii->tex = tex_make(ii->pw, ii->ph, ii->mode, raw, ii->pw * bpp); free(raw); ii->bytes = len + 256;
        if (!ii->tex) { logf_("out of texture memory for image %d", h); return NULL; }
        cacheBytes += ii->bytes; img_load_ms += plat_ms() - lt0; return ii->tex;
    }
    if (ii->mode == 5) {
        int w, hh, n; uint8_t *rgb = stbi_load_from_memory(comp, ii->size, &w, &hh, &n, 3); free(comp);
        if (!rgb) { logf_("jpeg decode failed img %d", h); ii->size = 0; return NULL; }
        uint16_t *p = malloc((size_t)w * hh * 2);
        for (int i = 0; i < w * hh; i++) p[i] = (uint16_t)(((rgb[i * 3] >> 3) << 11) | ((rgb[i * 3 + 1] >> 2) << 5) | (rgb[i * 3 + 2] >> 3));
        stbi_image_free(rgb);
        ii->tex = tex_make(w, hh, 4, (uint8_t *)p, w * 2); free(p); ii->bytes = w * hh * 2;
    } else {
        int bpp = ii->mode == 0 ? 4 : 2, stride = (ii->w * bpp + 3) & ~3; size_t len = (size_t)stride * ii->h;
        uint8_t *raw = malloc(len + 8);
        int n = stbi_zlib_decode_buffer((char *)raw, (int)len, (const char *)comp, ii->size); free(comp);
        if (n == ii->w * bpp * ii->h) stride = ii->w * bpp;
        else if (n != (int)len) { logf_("image %d: inflate %d expected %d", h, n, (int)len); if (n <= 0) { free(raw); ii->size = 0; return NULL; } }
        ii->tex = tex_make(ii->w, ii->h, ii->mode, raw, stride); free(raw); ii->bytes = ii->w * ii->h * bpp;
    }
    cacheBytes += ii->bytes;
    return ii->tex;
}
static int cache_evict_one(int minAge) {
    int best = -1; unsigned bu = drawFrame;
    for (int i = 0; i < G.nimg; i++) if (G.img[i].tex && G.img[i].used + minAge <= drawFrame && G.img[i].used < bu) { bu = G.img[i].used; best = i; }
    if (best < 0) return 0;
    plat_tex_free(G.img[best].tex); G.img[best].tex = NULL; cacheBytes -= G.img[best].bytes; return 1;
}
static void cache_trim(void) { while (cacheBytes > cacheBudget && cache_evict_one(2)) {} }
void cache_set_budget(long b) { cacheBudget = b; }
// texture creation with eviction when GPU memory runs out
static struct Tex *tex_make(int w, int h, int fmt, const uint8_t *px, int stride) {
    for (;;) {
        struct Tex *t = plat_tex(w, h, fmt, px, stride);
        if (t || !cache_evict_one(1)) return t;
    }
}
// collision mask (1 bit per pixel, rows padded to bytes, original image size); *full = 1 when the image is fully solid
const uint8_t *img_mask(int h, int *full) {
    *full = 1; if (h < 0 || h >= G.nimg || !G.pak) return NULL;
    ImgInfo *ii = &G.img[h]; if (!ii->msize) return NULL;
    *full = 0; if (ii->mask || ii->mtried) return ii->mask;
    ii->mtried = 1; int len = ((ii->w + 7) / 8) * ii->h; uint8_t *c = malloc(ii->msize), *m = malloc(len);
    fseek(G.pak, ii->moff, SEEK_SET);
    if (c && m && fread(c, 1, ii->msize, G.pak) == (size_t)ii->msize && LZ4_decompress_safe((const char *)c, (char *)m, ii->msize, len) == len) ii->mask = m; else { free(m); *full = 1; }
    free(c); return ii->mask;
}
// ---------------- objects ----------------
static AnimDir *cur_dir(Obj *o) {
    OI *oi = o->oi; if (!oi->nanim) return NULL;
    int a = o->animForced >= 0 ? o->animForced : o->anim; Anim *an = (a >= 0 && a < oi->nanim) ? oi->anim[a] : NULL;
    if (!an) { for (int i = 0; i < oi->nanim && !an; i++) an = oi->anim[i]; }
    if (!an) return NULL;
    int d = (o->dirForced >= 0 ? o->dirForced : o->dir) & 31;
    for (int k = 0; k <= 16; k++) { if (an->dir[(d + k) & 31]) return an->dir[(d + k) & 31]; if (an->dir[(d - k) & 31]) return an->dir[(d - k) & 31]; }
    return NULL;
}
int obj_image(Obj *o) {
    if (o->type == 2) {
        AnimDir *ad = cur_dir(o); if (!ad || !ad->n) return -1;
        int f = o->frameForced >= 0 ? o->frameForced : o->frame; if (f >= ad->n) f = ad->n - 1; if (f < 0) f = 0;
        return ad->frames[f];
    }
    if (o->type >= 32) return ext_image(o);
    return -1;
}
void anim_restart(Obj *o) { AnimDir *ad = cur_dir(o); o->frame = 0; o->animCounter = 0; o->repeats = ad ? ad->repeat : 0; o->animDone = 0; }
void anim_force(Obj *o, int a) { if (o->animForced != a || o->animDone) { o->animForced = a; anim_restart(o); } }
static int mover_speed(Obj *o, int *mn, int *mx) {
    if (o->mv) { *mn = o->mv->minsp; *mx = o->mv->maxsp; return o->mtSpeed; }
    if (o->ball) { *mn = 0; *mx = 100; return o->bspeed; }
    *mn = *mx = 0; return 0;
}
// animation speed, as computed by the runtime's animIn()
static int anim_delta(Obj *o, AnimDir *ad) {
    int mn, mx, speed = mover_speed(o, &mn, &mx), ds = ad->max - ad->min, d;
    if (o->speedForced >= 0) speed = o->speedForced;
    if (ds == 0) return o->speedForced >= 0 ? o->speedForced : ad->min;
    if (mx - mn != 0) d = ds * speed / (mx - mn) + ad->min;
    else if (o->speedForced >= 0) d = ds * speed / 100 + ad->min;
    else return ds / 2 + ad->min;
    return d > ad->max ? ad->max : d;
}
static void anim_step(Obj *o) {
    if (o->animForced < 0 && (o->mv || o->ball)) { // moving objects pick stopped / walking / running
        int mn, mx, sp = mover_speed(o, &mn, &mx), a = sp == 0 ? 0 : sp >= 75 ? 2 : 1;
        if (a != o->anim) { o->anim = a; anim_restart(o); }
    }
    AnimDir *ad = cur_dir(o); if (!ad || ad->n <= 0 || o->animDone || o->frameForced >= 0) return;
    int speed = anim_delta(o, ad); if (speed <= 0) return;
    o->animCounter += speed;
    while (o->animCounter > 100) {
        o->animCounter -= 100; o->frame++;
        if (o->frame >= ad->n) {
            if (ad->repeat == 0 || --o->repeats > 0) o->frame = ad->repframe < ad->n ? ad->repframe : 0;
            else {
                o->frame = ad->n - 1; int an = o->animForced >= 0 ? o->animForced : o->anim; o->animDone = an + 1;
                int forced = o->animForced >= 0;
                if (forced) { o->animForced = -1; o->dirForced = -1; o->speedForced = -1; }
                Obj *self = o; int idx = o->idx;
                run_obj_trigger(o, -2, an);
                // a finished forced animation hands back to the default one (unless the trigger forced another)
                if (G.obj[idx] == self && forced && self->animForced < 0 && self->anim != an) anim_restart(self);
                return;
            }
        }
    }
}
static int counter_str(Obj *o, char *buf, int n) { if (o->cv.t == 1) snprintf(buf, n, "%g", o->cv.d); else snprintf(buf, n, "%d", o->cv.i); return (int)strlen(buf); }
static int digit_img(OI *oi, char c) { int k = c >= '0' && c <= '9' ? c - '0' : c == '-' ? 10 : c == '+' ? 11 : c == '.' ? 12 : 13; return k < oi->cnframes ? oi->cframes[k] : -1; }
static int counter_frame(Obj *o) {
    OI *oi = o->oi; if (!oi->cnframes) return -1; double v = vd(&o->cv), rng = o->cmax - o->cmin; int f = 0;
    if (rng > 0) f = (int)((v - o->cmin) * oi->cnframes / rng); if (f >= oi->cnframes) f = oi->cnframes - 1; if (f < 0) f = 0; return oi->cframes[f];
}
void obj_rect(Obj *o, int *l, int *t, int *r, int *b) {
    *l = o->x; *t = o->y; *r = o->x + o->w; *b = o->y + o->h;
    if (o->type == 3) { *r = o->x + o->oi->tcx; *b = o->y + o->oi->tcy; return; }
    if (o->type == 7) {
        OI *oi = o->oi; if (!oi->has_cdisp) return;
        if (oi->cdisp == 1) { char s[40]; int n = counter_str(o, s, sizeof s), w = 0, h = 0;
            for (int i = 0; i < n; i++) { int im = digit_img(oi, s[i]); if (im >= 0 && im < G.nimg) { w += G.img[im].w; if (G.img[im].h > h) h = G.img[im].h; } }
            *l = o->x - w; *t = o->y - h; *r = o->x; *b = o->y; return; }
        if (oi->cdisp == 4) { int im = counter_frame(o); if (im >= 0 && im < G.nimg) { *l = o->x - G.img[im].xs; *t = o->y - G.img[im].ys; *r = *l + G.img[im].w; *b = *t + G.img[im].h; } }
        return;
    }
    int im = obj_image(o); if (im < 0 || im >= G.nimg) return;
    ImgInfo *ii = &G.img[im];
    float x0 = -ii->xs * o->sx, y0 = -ii->ys * o->sy, x1 = (ii->w - ii->xs) * o->sx, y1 = (ii->h - ii->ys) * o->sy;
    if (o->angle == 0) { *l = o->x + (int)floorf(x0 < x1 ? x0 : x1); *r = o->x + (int)ceilf(x0 < x1 ? x1 : x0); *t = o->y + (int)floorf(y0 < y1 ? y0 : y1); *b = o->y + (int)ceilf(y0 < y1 ? y1 : y0); return; }
    float a = o->angle * 3.14159265f / 180.f, c = cosf(a), s = sinf(a), xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1}, mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int i = 0; i < 4; i++) { float px = xs[i] * c + ys[i] * s, py = -xs[i] * s + ys[i] * c; if (px < mnx) mnx = px; if (px > mxx) mxx = px; if (py < mny) mny = py; if (py > mxy) mxy = py; }
    *l = o->x + (int)floorf(mnx); *r = o->x + (int)ceilf(mxx); *t = o->y + (int)floorf(mny); *b = o->y + (int)ceilf(mxy);
}
Obj *obj_create(int oih, int x, int y, int layer, int hidden, int lohandle) {
    (void)lohandle;
    OI *oi = oi_get(oih); if (!oi || oi->type < 2) return NULL;
    int idx = -1; for (int i = 0; i < MAXOBJ; i++) if (!G.obj[i]) { idx = i; break; }
    if (idx < 0) return NULL;
    Obj *o = calloc(1, sizeof(Obj)); G.obj[idx] = o; if (idx >= G.nobjslots) G.nobjslots = idx + 1;
    o->idx = idx; o->fixed = ((++G.createCount) << 16) | (unsigned)idx; o->oi = oi; o->type = oi->type;
    o->x = x; o->y = y; o->layer = layer < 0 ? 0 : layer >= G.nlayer ? (G.nlayer ? G.nlayer - 1 : 0) : layer;
    o->next = -1; o->nextsel = -1; o->hidden = hidden || !(oi->flags2 & 8); o->z = ++G.zmax;
    if (oi->first < 0) oi->first = idx; else { int k = oi->first; while (G.obj[k]->next >= 0) k = G.obj[k]->next; G.obj[k]->next = idx; }
    oi->nobj++;
    o->animForced = o->dirForced = o->frameForced = o->speedForced = -1; o->sx = o->sy = 1;
    o->effect = oi->ink & 0xFFFF; o->effparam = oi->inkparam;
    o->nval = oi->nval > 26 ? oi->nval : 26; o->val = malloc(sizeof(Val) * o->nval);
    for (int i = 0; i < o->nval; i++) o->val[i] = VI(i < oi->nval ? oi->val[i] : 0);
    for (int i = 0; i < 10 && i < oi->nstr; i++) o->str[i] = strdup(oi->str[i]);
    o->vflags = (unsigned)oi->valflags;
    if (o->type == 7) { o->cv = VI(oi->cinit); o->cmin = oi->cmin; o->cmax = oi->cmax; }
    if (o->type == 3 && oi->tn) { o->text = strdup(oi->texts[0].text); o->tcolor = oi->texts[0].color; o->tdirty = 1; }
    if (o->type >= 32) ext_create(o);
    anim_restart(o);
    if (o->type == 2) path_init(o, 0);
    return o;
}
void obj_destroy(Obj *o) { o->dead = 1; }
static void obj_free(Obj *o) {
    OI *oi = o->oi;
    if (oi->first == o->idx) oi->first = o->next;
    else for (int k = oi->first; k >= 0; k = G.obj[k]->next) if (G.obj[k]->next == o->idx) { G.obj[k]->next = o->next; break; }
    oi->nobj--; oi->evcount = -1; oi->actcount = -1;
    if (o->type >= 32) ext_destroy(o);
    if (o->ttex) plat_tex_free(o->ttex);
    for (int i = 0; i < 10; i++) free(o->str[i]);
    free(o->val); free(o->text); G.obj[o->idx] = NULL; free(o);
}

// ---------------- frame ----------------
// global objects (object flag 4): counters, texts and alterable values carry over between frames, keyed by name and type
typedef struct GSave { char *key; int n; struct GItem { Val cv; double cmin, cmax; char *text; int nval; Val *val; char *str[10]; unsigned vflags; } *it; } GSave;
static GSave *gsv; static int ngsv;
static void gkey(OI *oi, char *b, int n) { snprintf(b, n, "%s::%d", oi->name ? oi->name : "", oi->type); }
static void globals_save(void) {
    for (int h = 0; h < G.noi; h++) { OI *oi = &G.oi[h]; if (!oi->exists || !(oi->flags & 4) || oi->first < 0) continue;
        char k[160]; gkey(oi, k, sizeof k); GSave *g = NULL;
        for (int i = 0; i < ngsv; i++) if (!strcmp(gsv[i].key, k)) g = &gsv[i];
        if (!g) { gsv = realloc(gsv, sizeof(GSave) * (ngsv + 1)); g = &gsv[ngsv++]; memset(g, 0, sizeof *g); g->key = strdup(k); }
        for (int i = 0; i < g->n; i++) { struct GItem *t = &g->it[i]; free(t->text); free(t->val); for (int j = 0; j < 10; j++) free(t->str[j]); }
        free(g->it); g->n = 0; g->it = NULL;
        for (int x = oi->first; x >= 0; x = G.obj[x]->next) { Obj *o = G.obj[x]; if (!o) break;
            g->it = realloc(g->it, sizeof(struct GItem) * (g->n + 1)); struct GItem *t = &g->it[g->n++]; memset(t, 0, sizeof *t);
            t->cv = o->cv; if (t->cv.t == 2) t->cv = VS(""); t->cmin = o->cmin; t->cmax = o->cmax; t->text = o->text ? strdup(o->text) : NULL;
            t->nval = o->nval; if (o->nval) { t->val = malloc(sizeof(Val) * o->nval); memcpy(t->val, o->val, sizeof(Val) * o->nval); for (int j = 0; j < o->nval; j++) if (t->val[j].t == 2) t->val[j] = VI(0); }
            for (int j = 0; j < 10; j++) t->str[j] = o->str[j] ? strdup(o->str[j]) : NULL; t->vflags = o->vflags; } }
}
static void globals_load(void) {
    for (int h = 0; h < G.noi; h++) { OI *oi = &G.oi[h]; if (!oi->exists || !(oi->flags & 4) || oi->first < 0) continue;
        char k[160]; gkey(oi, k, sizeof k); GSave *g = NULL;
        for (int i = 0; i < ngsv; i++) if (!strcmp(gsv[i].key, k)) g = &gsv[i];
        if (!g) continue; int i = 0;
        for (int x = oi->first; x >= 0 && i < g->n; x = G.obj[x]->next, i++) { Obj *o = G.obj[x]; if (!o) break; struct GItem *t = &g->it[i];
            if (o->type == 7) { o->cv = t->cv; o->cmin = t->cmin; o->cmax = t->cmax; }
            else if (o->type == 3) { free(o->text); o->text = strdup(t->text ? t->text : ""); o->tdirty = 1; }
            else { for (int j = 0; j < t->nval && j < o->nval; j++) o->val[j] = t->val[j];
                for (int j = 0; j < 10; j++) if (t->str[j]) { free(o->str[j]); o->str[j] = strdup(t->str[j]); } o->vflags = t->vflags; } } }
}
void frame_end(void) {
    if (G.nobjslots) globals_save();
    for (int i = 0; i < G.nobjslots; i++) if (G.obj[i]) obj_free(G.obj[i]);
    G.nobjslots = 0;
    for (int i = 0; i < G.noi; i++) { G.oi[i].first = -1; G.oi[i].nobj = 0; G.oi[i].evcount = -1; G.oi[i].actcount = -1; }
}
// load every image the frame can show up front (like the original runtime does), as far as texture memory allows
static void preload_oi(int h, char *seen) {
    OI *oi = oi_get(h); if (!oi || seen[h]) return; seen[h] = 1;
    if (oi->type <= 1) { if (oi->bimage >= 0) img_tex(oi->bimage); return; }
    for (int a = 0; a < oi->nanim; a++) if (oi->anim[a]) for (int d = 0; d < 32; d++) { AnimDir *ad = oi->anim[a]->dir[d]; if (!ad) continue;
        for (int k = 0; k < ad->n; k++) { if (cacheBytes > cacheBudget * 9 / 10) return; img_tex(ad->frames[k]); } }
    for (int k = 0; k < oi->cnframes; k++) img_tex(oi->cframes[k]);
}
static void frame_preload(void) {
    unsigned t0 = plat_ms(); char *seen = calloc(1, G.noi + 1);
    for (int i = 0; i < G.nlo; i++) preload_oi(G.lo[i].oi, seen);
    for (int i = 0; i < G.ng; i++) for (int k = 0; k < G.g[i].na; k++) { Evt *a = &G.g[i].a[k]; if (a->type == -5 && a->np && a->p[0].ptr) preload_oi(a->p[0].b, seen); }
    free(seen); logf_("preload %u ms", plat_ms() - t0);
}
void frame_start(int idx) {
    frame_end(); fa_reset();
    G.fi = idx; frame_parse(idx);
    G.winx = G.winy = 0; G.lastwinx = G.lastwiny = 0; G.timer = 0; G.loop = 0; G.zmax = 0; G.zmin = 0; G.quit = 0; G.started = 0;
    if (!(G.newflags & 1)) snd_stop_all();
    for (int i = 0; i < G.nlo; i++) {
        LO *l = &G.lo[i]; OI *oi = oi_get(l->oi); if (!oi || oi->type < 2 || l->ptype != 0) continue;
        if (oi->flags2 & 8) { if (oi->oeflags & 0x20000) continue; obj_create(l->oi, l->x, l->y, l->layer, 0, l->handle); }
        else if (oi->type != 4) obj_create(l->oi, l->x, l->y, l->layer, 1, l->handle);
    }
    globals_load();
    evt_frame_init();
    frame_preload();
    logf_("frame %d '%s' %dx%d objects %d groups %d (textures %ld KB)", idx, G.fname, G.fw, G.fh, G.nobjslots, G.ng, cacheBytes >> 10);
}
void frame_tick(void) {
    ta_reset();
    G.loop++; G.timer += G.timerDelta;
    int any = -1; for (int i = 0; i < MAXTOUCH; i++) if (G.in.t[i].down) { any = i; break; }
    G.mclick = 0;
    for (int i = 0; i < MAXTOUCH; i++) if (G.in.t[i].isnew) { G.mclick = 1; G.mx = G.in.t[i].x; G.my = G.in.t[i].y; break; }
    if (any >= 0 && !G.mclick) { G.mx = G.in.t[any].x; G.my = G.in.t[any].y; }
    G.mdown = any >= 0;
    for (int i = 0; i < G.nobjslots; i++) {
        Obj *o = G.obj[i]; if (!o || o->dead) continue;
        if (o->type == 2) { if (o->mv) { path_move(o); o->mtLX = o->x; o->mtLY = o->y; } else if (o->ball && o->bspeed) ball_move(o); anim_step(o); } else if (o->type >= 32) ext_handle(o);
        if (G.obj[i] != o) continue;
        if (o->flash > 0) { o->flashcpt -= G.timerDelta; if (o->flashcpt <= 0) { o->hidden = !o->hidden; o->flashcpt = o->flash; } }
    }
    if (!G.started) { G.started = 1; run_start(); }
    if (!G.quit) { // input events arrive between loops on the real runtime: handle them before the main event pass
        for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (!o || o->dead || o->type < 32 || o->oi->ext_kind != XK_TOUCH) continue;
            for (int k = 0; k < MAXTOUCH; k++) {
                if (G.in.t[k].isnew) { gen_event(o, 0); gen_event(o, 2); gen_event(o, 6); }
                if (G.in.t[k].ended) { gen_event(o, 1); gen_event(o, 3); }
            } }
        if (G.mclick) run_click(0, 0);
    }
    if (!G.quit) run_timers();
    if (!G.quit) run_always();
    if (G.winx != G.lastwinx || G.winy != G.lastwiny) { // like CRun.f_UpdateWindowPos: object coordinates stay in frame space,
        int dx = G.winx - G.lastwinx, dy = G.winy - G.lastwiny;    // objects on slower layers (and scroll-independent ones) move along
        for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (!o || o->dead || o->layer >= G.nlayer) continue;
            if (o->oi->oeflags & 0x800) { o->x += dx; o->y += dy; continue; }
            Layer *L = &G.layer[o->layer];
            if (L->opt & 1) o->x += dx + (int)(L->xc * G.lastwinx) - (int)(L->xc * G.winx);
            if (L->opt & 2) o->y += dy + (int)(L->yc * G.lastwiny) - (int)(L->yc * G.winy); }
        G.lastwinx = G.winx; G.lastwiny = G.winy;
    }
    ext_tick_end();
    for (int i = 0; i < G.nobjslots; i++) if (G.obj[i] && G.obj[i]->dead) obj_free(G.obj[i]);
    if (G.quit == 3) { int f = G.quitParam; run_end(); frame_start(f); }
}

// ---------------- rendering ----------------
static void draw_img(int im, float x, float y, float sx, float sy, float angle, unsigned rgba, int blend, int usehot) {
    struct Tex *t = img_tex(im); if (!t) return;
    ImgInfo *ii = &G.img[im];
    float hx = usehot ? ii->xs : 0, hy = usehot ? ii->ys : 0;
    float lx[4] = {-hx * sx, (ii->w - hx) * sx, (ii->w - hx) * sx, -hx * sx}, ly[4] = {-hy * sy, -hy * sy, (ii->h - hy) * sy, (ii->h - hy) * sy}, xy[8];
    if (angle != 0) { float a = angle * 3.14159265f / 180.f, c = cosf(a), s = sinf(a); for (int i = 0; i < 4; i++) { xy[i * 2] = x + lx[i] * c + ly[i] * s; xy[i * 2 + 1] = y - lx[i] * s + ly[i] * c; } }
    else for (int i = 0; i < 4; i++) { xy[i * 2] = x + lx[i]; xy[i * 2 + 1] = y + ly[i]; }
    plat_quad(t, 0, 0, 1, 1, xy, rgba, blend);
}
static unsigned obj_tint(Obj *o, int *blend) {
    int e = o->effect, bop = e & 0xFFF; unsigned a = 255, rgb = 0xFFFFFF; *blend = bop == 9;
    if (e & 0x1000) { a = (unsigned)o->effparam >> 24; rgb = o->effparam & 0xFFFFFF; }
    else if (bop == 1) { int p = o->effparam; if (p < 0) p = 0; if (p > 128) p = 128; a = 255 - p * 255 / 128; }
    return (rgb << 8) | a;
}
static int zcmp(const void *a, const void *b) {
    const Obj *x = *(Obj *const *)a, *y = *(Obj *const *)b;
    if (x->layer != y->layer) return x->layer - y->layer;
    return x->z < y->z ? -1 : x->z > y->z;
}
// Credit on the title screen, top-right corner: "Ported to PS Vita by Nox" and the update below it,
// same font, size and colour as the copyright line
#define CREDIT_UPDATE "Update 2 (v1.0.2)"
static unsigned obj_tint(Obj *o, int *blend);
static void draw_credit(void) {
    static const char *lines[2] = {"Ported to PS Vita by Nox", CREDIT_UPDATE};
    static struct Tex *ct[2]; static int cw[2], ch[2]; Obj *cp = NULL;
    for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (o && !o->dead && o->type == 3 && o->oi->name && !strncmp(o->oi->name, "copyright", 9)) cp = o; }
    if (!cp || cp->hidden || !cp->oi->tn) return;
    TextDef *td = &cp->oi->texts[0]; int fh = G.font[td->font & 63].height;
    for (int k = 0; k < 2; k++) if (!ct[k]) { int w, h;
        font_render(fh, lines[k], 0, 0, 0, 0, &w, &h, 1, 0);
        uint8_t *px = font_render(fh, lines[k], w + 4, h, 0, cp->tcolor, &cw[k], &ch[k], 0, 0);
        if (!px) return; ct[k] = tex_make(cw[k], ch[k], 0, px, cw[k] * 4); free(px); }
    int b; unsigned tint = obj_tint(cp, &b); const float margin = 12; float y = margin;
    for (int k = 0; k < 2; k++) {
        float x = G.w - margin - cw[k];
        float xy[8] = {x, y, x + cw[k], y, x + cw[k], y + ch[k], x, y + ch[k]}; plat_quad(ct[k], 0, 0, 1, 1, xy, tint, 0);
        y += ch[k] + 2;
    }
}
void frame_draw(void) {
    drawFrame++; cache_trim();
    static Obj *list[MAXOBJ]; int n = 0, persp = 0;
    for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (!o || o->dead || o->hidden) continue; if (o->type >= 32 && o->oi->ext_kind != XK_KYSO && o->oi->ext_kind != XK_PERSP) continue; if (o->type >= 32 && o->oi->ext_kind == XK_PERSP) persp = 1; list[n++] = o; }
    qsort(list, n, sizeof(Obj *), zcmp);
    plat_begin(persp); plat_clear(G.fbg);
    int k = 0;
    for (int L = 0; L < G.nlayer; L++) {
        Layer *ly = &G.layer[L]; float ox = G.winx * ly->xc, oy = G.winy * ly->yc;
        if (!ly->hidden) for (int i = ly->first; i < ly->first + ly->nbkd && i < G.nlo; i++) {
            LO *lo = &G.lo[i]; OI *oi = oi_get(lo->oi); if (!oi || oi->type > 1) continue;
            if (oi->bimage >= 0 && oi->bfill == 3) draw_img(oi->bimage, lo->x - ox, lo->y - oy, 1, 1, 0, 0xFFFFFFFF, 0, 0);
            else if (oi->type == 0 && oi->bfill == 1) plat_rect(lo->x - ox, lo->y - oy, oi->bcx, oi->bcy, ((unsigned)oi->bcolor << 8) | 255);
        }
        for (; k < n && list[k]->layer <= L; k++) {
            Obj *o = list[k]; if (o->layer != L || ly->hidden) continue;
            int blend; unsigned tint = obj_tint(o, &blend); float x = o->x - G.winx, y = o->y - G.winy; // objects are in frame space
            if (o->type == 2 || (o->type >= 32 && o->oi->ext_kind == XK_KYSO)) { int im = obj_image(o); if (im >= 0) draw_img(im, x, y, o->sx, o->sy, o->angle, tint, blend, 1); }
            else if (o->type >= 32) plat_persp(x, y, o->w, o->h, ext_persp_fb(o));
            else if (o->type == 7 && o->oi->has_cdisp) {
                OI *oi = o->oi;
                if (oi->cdisp == 1) { char s[40]; int len = counter_str(o, s, sizeof s); float cx = x;
                    for (int i = len - 1; i >= 0; i--) { int im = digit_img(oi, s[i]); if (im < 0 || im >= G.nimg) continue; cx -= G.img[im].w; draw_img(im, cx, y - G.img[im].h, 1, 1, 0, tint, blend, 0); } }
                else if (oi->cdisp == 4) { int im = counter_frame(o); if (im >= 0) draw_img(im, x, y, 1, 1, 0, tint, blend, 1); }
            }
            else if (o->type == 3 && o->oi->tn) {
                OI *oi = o->oi; TextDef *td = &oi->texts[o->tpara < oi->tn ? o->tpara : 0];
                if (o->tdirty || !o->ttex) {
                    if (o->ttex) { plat_tex_free(o->ttex); o->ttex = NULL; }
                    int w, h; uint8_t *px = font_render(G.font[td->font & 63].height, o->text ? o->text : "", oi->tcx, oi->tcy, td->flags, o->tcolor, &w, &h, 0, 0);
                    if (px) { o->ttex = tex_make(w, h, 0, px, w * 4); o->tw = w; o->th = h; free(px); }
                    o->tdirty = 0;
                }
                if (o->ttex) { float xy[8] = {x, y, x + o->tw, y, x + o->tw, y + o->th, x, y + o->th}; plat_quad(o->ttex, 0, 0, 1, 1, xy, tint, 0); }
            }
        }
    }
    draw_credit();
    plat_present();
}

// ---------------- path movement (ported from the runtime's CMovePath) ----------------
static void mt_branch(Obj *o) {
    PathStep *s = &o->mv->st[o->mtNum]; o->mtLen = s->len; int sp = s->speed;
    if (o->mtPause) { o->mtPause *= 20; sp |= 0x8000; o->mtStop = sp; }
    if (o->mtStop) sp = 0;
    o->mtSpeed = sp & 255;
}
static void mt_ahead(Obj *o, int i) {
    if (i >= o->mv->n) { o->mtStop = o->mtStop ? o->mtStop : (o->mtSpeed | 0x8000); o->mtSpeed = 0; return; }
    PathStep *s = &o->mv->st[i]; o->mtDir = 0; o->mtNum = i; o->mtPause = s->pause; o->mtCos = s->cos; o->mtSin = s->sin;
    o->mtX0 = o->x; o->mtY0 = o->y; o->mtX1 = o->x + s->dx; o->mtY1 = o->y + s->dy; o->dir = s->dir & 31; mt_branch(o);
}
static void mt_back(Obj *o, int i) {
    if (i >= o->mv->n || i < 0) { o->mtSpeed = 0; return; }
    PathStep *s = &o->mv->st[i]; o->mtDir = 1; o->mtNum = i; o->mtCos = -s->cos; o->mtSin = -s->sin;
    o->mtX0 = o->x; o->mtY0 = o->y; o->mtX1 = o->x - s->dx; o->mtY1 = o->y - s->dy; o->dir = (s->dir + 16) & 31; mt_branch(o);
}
static void mt_end(Obj *o) { o->mtSpeed = 0; o->mtStop = 0; o->mtDone = 1; }
static void mt_repos(Obj *o) { if (o->mv->repos) { o->x = o->mtStartX; o->y = o->mtStartY; } }
void ball_move(Obj *o);
void path_init(Obj *o, int sel) {
    OI *oi = o->oi; o->mv = NULL; o->ball = 0;
    if (sel < 0 || sel >= oi->nmov) return;
    MoveDef *md = &oi->mov[sel];
    if (md->dirs) { unsigned m = (unsigned)md->dirs; int c = 0, k; for (k = 0; k < 32; k++) if (m >> k & 1) c++; int pick = frnd(c); for (k = 0; k < 32; k++) if ((m >> k & 1) && !pick--) { o->dir = k; break; } }
    if (md->type == 4) { o->ball = 1; o->bspeed = md->atstart ? md->bspeed : 0; o->bx = o->x; o->by = o->y; return; }
    if (md->type != 5 || md->n <= 0) return;
    o->mv = md; o->mtStartX = o->x; o->mtStartY = o->y; o->mtLX = o->x; o->mtLY = o->y; o->mtCalc = 0; o->mtStop = 0; o->mtDone = 0;
    mt_ahead(o, 0);
    if (!md->atstart) { o->mtStop = o->mtSpeed | 0x8000; o->mtSpeed = 0; }
}
void path_speed(Obj *o, int s) { if (o->ball) { o->bspeed = s < 0 ? 0 : s > 100 ? 100 : s; return; } if (!o->mv) return; if (s < 0) s = 0; if (s > 250) s = 250; o->mtSpeed = s; }
// advance by one step of `step` (16.16 fixed); returns 1 when the step was consumed fully
static int mt_move(Obj *o, int step) {
    int c = step + o->mtCalc, d = (c >> 16) & 65535;
    if (d < o->mtLen) { o->mtCalc = c; o->x = o->mtCos * d / 16384 + o->mtX0; o->y = d * o->mtSin / 16384 + o->mtY0; return 1; }
    o->x = o->mtX1; o->y = o->mtY1; o->mtCalc = 0;
    int i = o->mtNum;
    if (o->mtDir) {
        o->mtPause = o->mv->st[i].pause;
        if (i - 1 >= 0) { mt_back(o, i - 1); return 0; }
        mt_repos(o);
        if (!o->mv->loop) { mt_end(o); return 1; }
        o->mtDir = 0; mt_ahead(o, 0); return 0;
    }
    if (i + 1 < o->mv->n) { mt_ahead(o, i + 1); return 0; }
    o->mtNum = i + 1;
    if (o->mv->reverse) { o->mtDir = 1; mt_back(o, i); return 0; }
    mt_repos(o);
    if (!o->mv->loop) { mt_end(o); return 1; }
    mt_ahead(o, 0); return 0;
}
void ball_move(Obj *o) {
    if ((int)o->bx != o->x || (int)o->by != o->y) { o->bx = o->x; o->by = o->y; } // moved by events
    double a = (o->dir & 31) * 3.14159265358979 / 16.0, d = o->bspeed / 8.0;
    o->bx += cos(a) * d; o->by -= sin(a) * d; o->x = (int)o->bx; o->y = (int)o->by;
}
void path_move(Obj *o) {
    if (o->x != o->mtLX || o->y != o->mtLY) { // moved by events: shift the path like setX/YPosition does
        int dx = o->x - o->mtLX, dy = o->y - o->mtLY;
        o->mtX0 += dx; o->mtX1 += dx; o->mtStartX += dx; o->mtY0 += dy; o->mtY1 += dy; o->mtStartY += dy;
    }
    if (o->mtSpeed == 0) {
        if (!o->mtPause) return;
        o->mtPause -= G.timerDelta;
        if (o->mtPause > 0) return;
        o->mtPause = 0; o->mtSpeed = o->mtStop & 255; o->mtStop = 0;
        if (!o->mtSpeed) return;
    }
    int budget = o->mtSpeed * 8192, guard = 0; // speed/8 pixels per frame, 16.16 fixed point
    while (budget > 0 && o->mtSpeed && guard++ < 64) {
        int before = o->mtCalc, len = o->mtLen, num = o->mtNum, dirb = o->mtDir;
        int remain = ((len << 16) - before); if (remain < 0) remain = 0;
        if (budget < remain) { mt_move(o, budget); break; }
        budget -= remain; mt_move(o, remain);
        if (o->mtNum == num && o->mtDir == dirb && o->mtCalc) break;
    }
}
