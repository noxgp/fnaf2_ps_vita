#include <math.h>
// extension objects used by FNaF 2
#include "fp.h"
#include <time.h>
Val exp_arg(void);
typedef struct KV { char *g, *k, *v; } KV;
typedef struct Ini { int n, cap; KV *e; char file[200], group[120], item[120]; int dirty; } Ini;
typedef struct Kyso { int w, h, mode, flags, speed, back, n; short *img; int frame, counter, playing, loop, started; } Kyso;
typedef struct PSlot { int used, x, y, l, t, r, b; float sx, sy; } PSlot;
typedef struct Persp { int effect, dir, zoom; PSlot slot[16]; } Persp;
typedef struct Parser { char *src; char delims[16][16]; int nd; } Parser;
typedef struct CalcR { char *text; int size, maxw, w, h; } CalcR;
static Ini prefs; static int prefs_loaded;

int ext_kind(const char *nm) {
    if (!strncasecmp(nm, "Perspective", 11)) return XK_PERSP;
    if (!strncasecmp(nm, "kcini", 5)) return XK_INI;
    if (!strncasecmp(nm, "kcclock", 7)) return XK_CLOCK;
    if (!strncasecmp(nm, "Kyso", 4)) return XK_KYSO;
    if (!strncasecmp(nm, "CalcRect", 8)) return XK_CALCRECT;
    if (!strncasecmp(nm, "Layer", 5)) return XK_LAYER;
    if (!strncasecmp(nm, "parser", 6)) return XK_PARSER;
    if (!strncasecmp(nm, "MultipleTouch", 13)) return XK_TOUCH;
    if (!strncasecmp(nm, "AndroidPlus", 11)) return XK_ANDROIDPLUS;
    if (!strncasecmp(nm, "Android.", 8) || !strcasecmp(nm, "Android")) return XK_ANDROID;
    return XK_STUB;
}
// ---- ini ----
static const char *base_name(const char *p) { const char *b = p; for (; *p; p++) if (*p == '/' || *p == '\\' || *p == ':') b = p + 1; return b; }
static void ini_clear(Ini *i) { for (int k = 0; k < i->n; k++) { free(i->e[k].g); free(i->e[k].k); free(i->e[k].v); } i->n = 0; }
static KV *ini_find(Ini *i, const char *g, const char *k) { for (int j = 0; j < i->n; j++) if (!strcasecmp(i->e[j].g, g) && !strcasecmp(i->e[j].k, k)) return &i->e[j]; return NULL; }
static void ini_set(Ini *i, const char *g, const char *k, const char *v) {
    KV *e = ini_find(i, g, k);
    if (e) { if (!strcmp(e->v, v)) return; free(e->v); e->v = strdup(v); i->dirty = 1; return; }
    if (i->n >= i->cap) { i->cap = i->cap ? i->cap * 2 : 32; i->e = realloc(i->e, sizeof(KV) * i->cap); }
    i->e[i->n].g = strdup(g); i->e[i->n].k = strdup(k); i->e[i->n].v = strdup(v); i->n++; i->dirty = 1;
}
static void ini_parse(Ini *i, char *txt) {
    char grp[120] = ""; char *save = NULL;
    for (char *ln = strtok_r(txt, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
        size_t l = strlen(ln); while (l && (ln[l - 1] == '\r' || ln[l - 1] == ' ')) ln[--l] = 0;
        while (*ln == ' ' || *ln == '\t') ln++;
        if (*ln == '[') { char *e = strchr(ln, ']'); if (e) { *e = 0; snprintf(grp, sizeof grp, "%s", ln + 1); } continue; }
        char *eq = strchr(ln, '='); if (!eq || *ln == ';') continue;
        *eq = 0; ini_set(i, grp, ln, eq + 1);
    }
    i->dirty = 0;
}
static void ini_load(Ini *i, const char *name) {
    ini_clear(i); snprintf(i->file, sizeof i->file, "%s", base_name(name));
    char path[500]; snprintf(path, sizeof path, "%s/%s", G.savedir, i->file);
    FILE *f = fopen(path, "rb");
    if (f) { fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(sz + 1); sz = (long)fread(b, 1, sz, f); b[sz] = 0; fclose(f); ini_parse(i, b); free(b); return; }
    for (int k = 0; k < G.nef; k++) if (!strcasecmp(base_name(G.ef[k].name), i->file)) { char *b = malloc(G.ef[k].size + 1); memcpy(b, G.ef[k].data, G.ef[k].size); b[G.ef[k].size] = 0; ini_parse(i, b); free(b); return; }
}
static void ini_save(Ini *i) {
    if (!i->dirty || !i->file[0]) return;
    i->dirty = 0;
    char path[500]; snprintf(path, sizeof path, "%s/%s", G.savedir, i->file);
    FILE *f = fopen(path, "wb"); if (!f) return;
    char *done = calloc(1, i->n + 1);
    for (int a = 0; a < i->n; a++) if (!done[a]) {
        fprintf(f, "[%s]\r\n", i->e[a].g);
        for (int b = a; b < i->n; b++) if (!done[b] && !strcasecmp(i->e[b].g, i->e[a].g)) { fprintf(f, "%s=%s\r\n", i->e[b].k, i->e[b].v); done[b] = 1; }
    }
    free(done); fclose(f);
}
static const char *ini_get(Ini *i, const char *g, const char *k) { KV *e = ini_find(i, g, k); return e ? e->v : ""; }
static void prefs_init(void) { if (!prefs_loaded) { prefs_loaded = 1; ini_load(&prefs, "prefs.ini"); } }

void ext_create(Obj *o) {
    OI *oi = o->oi; const uint8_t *d = oi->ext; int sz = oi->extsize;
    switch (oi->ext_kind) {
    case XK_INI: { Ini *i = calloc(1, sizeof(Ini)); o->ext = i; break; }
    case XK_KYSO: { Kyso *k = calloc(1, sizeof(Kyso)); o->ext = k; if (!d || sz < 24) break;
        int16_t s; int32_t v; const uint8_t *p = d;
        memcpy(&s, p, 2); k->w = s; memcpy(&s, p + 2, 2); k->h = s; memcpy(&v, p + 4, 4); k->mode = v; memcpy(&v, p + 8, 4); k->flags = v; memcpy(&v, p + 16, 4); k->speed = v; p += 20;
        if (oi->extver == 2) { memcpy(&s, p, 2); k->back = s; p += 4; }
        memcpy(&s, p, 2); k->n = s; p += 2; if (k->n < 0 || p + 2 * k->n > d + sz) k->n = 0;
        k->img = malloc(sizeof(short) * (k->n + 1)); memcpy(k->img, p, 2 * k->n);
        k->playing = k->flags & 1; k->loop = (k->flags & 2) != 0; k->started = (k->flags & 32) != 0 || k->playing; o->w = k->w; o->h = k->h; break; }
    case XK_PERSP: { Persp *p = calloc(1, sizeof(Persp)); o->ext = p; if (!d || sz < 16) break;
        int16_t s; int32_t v; memcpy(&s, d + 4, 2); o->w = s; memcpy(&s, d + 6, 2); o->h = s; p->effect = d[8]; p->dir = d[9]; memcpy(&v, d + 12, 4); p->zoom = v; break; }
    case XK_PARSER: o->ext = calloc(1, sizeof(Parser)); break;
    case XK_CALCRECT: { CalcR *c = calloc(1, sizeof(CalcR)); c->size = 16; o->ext = c; break; }
    }
}
void ext_destroy(Obj *o) {
    if (!o->ext) return;
    switch (o->oi->ext_kind) {
    case XK_INI: { Ini *i = o->ext; ini_save(i); ini_clear(i); free(i->e); break; }
    case XK_KYSO: free(((Kyso *)o->ext)->img); break;
    case XK_PARSER: free(((Parser *)o->ext)->src); break;
    case XK_CALCRECT: free(((CalcR *)o->ext)->text); break;
    }
    free(o->ext); o->ext = NULL;
}
void ext_handle(Obj *o) {
    if (o->oi->ext_kind != XK_KYSO || !o->ext) return;
    Kyso *k = o->ext; if (!k->playing || k->n <= 0) return;
    k->counter += k->speed;
    while (k->counter > 100) {
        k->counter -= 100; k->frame++;
        if (k->frame >= k->n) { if (k->loop) k->frame = k->back > 0 ? k->back - 1 : 0; else { k->frame = k->n - 1; k->playing = 0; break; } }
    }
}
void ext_tick_end(void) {
    for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (o && o->type >= 32 && o->oi->ext_kind == XK_INI && o->ext) ini_save(o->ext); }
    if (prefs_loaded) ini_save(&prefs);
}
int ext_image(Obj *o) { if (o->oi->ext_kind != XK_KYSO || !o->ext) return -1; Kyso *k = o->ext; if (!k->started || k->n <= 0) return -1; return k->img[k->frame < k->n ? k->frame : k->n - 1]; }
float ext_persp_fb(Obj *o) { Persp *p = o->ext; if (!p) return 0; int d = p->dir ? o->h : o->w; return d ? (float)p->zoom / (float)d : 0; }

int touch_over(Obj *o, int ti) {
    if (ti < 0 || ti >= MAXTOUCH || (!G.in.t[ti].down && !G.in.t[ti].isnew)) return 0;
    Layer *L = &G.layer[o->layer < G.nlayer ? o->layer : 0];
    int fx = G.in.t[ti].x + (int)(G.winx * L->xc), fy = G.in.t[ti].y + (int)(G.winy * L->yc), l, t, r, b;
    obj_rect(o, &l, &t, &r, &b);
    return fx >= l && fx < r && fy >= t && fy < b;
}
static int last_new(void) { for (int i = 0; i < MAXTOUCH; i++) if (G.in.t[i].isnew) return i; return -1; }
static int last_end(void) { for (int i = 0; i < MAXTOUCH; i++) if (G.in.t[i].ended) return i; return -1; }
static int touch_objs(Param *p, int ti) { // keep objects of param under touch ti
    if (ti < 0) return 0;
    Obj *l[MAXOBJ]; int n = 0, cnt = 0, idx = 0;
    for (Obj *o = evt_first(p->s[0], (uint16_t)p->s[1]); o && n < MAXOBJ; o = evt_next()) l[n++] = o;
    Obj *o = evt_first(p->s[0], (uint16_t)p->s[1]);
    while (o) { if (idx < n && !touch_over(l[idx], ti)) evt_delete_current(); else cnt++; idx++; o = evt_next(); }
    return cnt > 0;
}
int ext_cond(Obj *o, Evt *c, int num) {
    (void)o;
    switch (o->oi->ext_kind) {
    case XK_TOUCH:
        switch (num) {
        case 0: case 2: return last_new() >= 0;
        case 1: { int p = c->np ? eval_int(&c->p[0]) : -1; int e = last_end(); return e >= 0 && (p < 0 || G.in.t[p < MAXTOUCH ? p : 0].ended); }
        case 3: return last_end() >= 0;
        case 5: { int p = eval_int(&c->p[0]); return p >= 0 && p < MAXTOUCH && G.in.t[p].down; }
        case 6: return touch_objs(&c->p[0], last_new());
        case 7: { int p = eval_int(&c->p[0]); if (p < 0 || p >= MAXTOUCH || !G.in.t[p].down) return 0; return touch_objs(&c->p[1], p); }
        }
        return 0;
    case XK_KYSO: { Kyso *k = o->ext; if (!k) return 0; if (num == 4) return k->playing; if (num == 2) return !k->playing && k->started && k->frame >= k->n - 1; return 0; }
    }
    return 0;
}
void ext_act(Obj *o, Evt *a, int num) {
    switch (o->oi->ext_kind) {
    case XK_INI: { Ini *i = o->ext; if (!i) return;
        switch (num) {
        case 0: snprintf(i->group, sizeof i->group, "%s", eval_str(&a->p[0])); return;
        case 1: snprintf(i->item, sizeof i->item, "%s", eval_str(&a->p[0])); return;
        case 2: { char b[24]; snprintf(b, sizeof b, "%d", eval_int(&a->p[0])); ini_set(i, i->group, i->item, b); return; }
        case 5: ini_set(i, i->group, i->item, eval_str(&a->p[0])); return;
        case 6: ini_save(i); ini_load(i, eval_str(&a->p[0])); return;
        case 7: { char k[120], b[24]; snprintf(k, sizeof k, "%s", eval_str(&a->p[0])); snprintf(b, sizeof b, "%d", eval_int(&a->p[1])); ini_set(i, i->group, k, b); return; }
        case 8: { char g[120], k[120], b[24]; snprintf(g, sizeof g, "%s", eval_str(&a->p[0])); snprintf(k, sizeof k, "%s", eval_str(&a->p[1])); snprintf(b, sizeof b, "%d", eval_int(&a->p[2])); ini_set(i, g, k, b); return; }
        case 9: { char k[120]; snprintf(k, sizeof k, "%s", eval_str(&a->p[0])); ini_set(i, i->group, k, eval_str(&a->p[1])); return; }
        case 10: { char g[120], k[120]; snprintf(g, sizeof g, "%s", eval_str(&a->p[0])); snprintf(k, sizeof k, "%s", eval_str(&a->p[1])); ini_set(i, g, k, eval_str(&a->p[2])); return; }
        case 13: { const char *g = eval_str(&a->p[0]); for (int k = 0; k < i->n;) if (!strcasecmp(i->e[k].g, g)) { free(i->e[k].g); free(i->e[k].k); free(i->e[k].v); i->e[k] = i->e[--i->n]; i->dirty = 1; } else k++; return; }
        }
        return; }
    case XK_KYSO: { Kyso *k = o->ext; if (!k) return;
        if (num == 1 || num == 2) { k->frame = 0; k->counter = 0; k->playing = 1; k->started = 1; k->loop = num == 2; }
        else if (num == 3) { k->frame = 0; k->playing = 0; }
        return; }
    case XK_LAYER: { int n = a->np ? eval_int(&a->p[0]) : 0; if (n < 1 || n > G.nlayer) return; if (num == 31) G.layer[n - 1].hidden = 0; else if (num == 32) G.layer[n - 1].hidden = 1; return; }
    case XK_PARSER: { Parser *p = o->ext; if (!p) return;
        if (num == 0) { free(p->src); p->src = strdup(eval_str(&a->p[0])); }
        else if (num == 5) p->nd = 0;
        else if (num == 6 && p->nd < 16) snprintf(p->delims[p->nd++], 16, "%s", eval_str(&a->p[0]));
        return; }
    case XK_CALCRECT: { CalcR *c = o->ext; if (!c) return;
        if (num == 0) { c->size = eval_int(&a->p[1]); if (c->size <= 0) c->size = 16; }
        else if (num == 1) { free(c->text); c->text = strdup(eval_str(&a->p[0])); }
        else if (num == 2) c->maxw = eval_int(&a->p[0]);
        else if (num == 3) font_render(c->size, c->text ? c->text : "", 0, 0, 0, 0, &c->w, &c->h, 1, c->maxw);
        return; }
    case XK_PERSP: { Persp *p = o->ext; if (!p) return;
        if (num == 22 && a->np >= 9) { // add coordinates: slot, x, y, left, top, right, bottom, scale x, scale y
            int sl = eval_int(&a->p[0]); if (sl < 0 || sl > 15) return; PSlot *q = &p->slot[sl];
            q->x = eval_int(&a->p[1]); q->y = eval_int(&a->p[2]); q->l = eval_int(&a->p[3]); q->t = eval_int(&a->p[4]); q->r = eval_int(&a->p[5]); q->b = eval_int(&a->p[6]);
            Val v = eval_param(&a->p[7]); q->sx = (float)vd(&v); v = eval_param(&a->p[8]); q->sy = (float)vd(&v); q->used = 1; return; }
        if (num == 26 && a->np >= 3) { // apply to object: moves/scales objects placed on the panorama so they match the warped picture
            int sl = eval_int(&a->p[0]), mode = eval_int(&a->p[2]); if (sl < 0 || sl > 15 || !p->slot[sl].used) return; PSlot *q = &p->slot[sl];
            int px = q->x, py = q->y; double sy = 1.0;
            if (p->effect == 0 && p->dir == 0 && o->w > 0) {
                double W = o->w, H = o->h, Z = p->zoom, d2 = W / 3.1415;
                #define PF(xx) (H / fmax(1.0, (sin((float)((double)(xx) / d2 + 1.57075)) * Z + H) - Z))
                double fl = PF((q->l - o->x) - (int)(W / 2)), fr = PF((q->r - o->x) - (int)(W / 2));
                sy = mode == 1 ? fmax(fl, fr) : mode == 2 ? (fl + fr) / 2 : fmin(fl, fr);
                int hh = (int)H / 2; py = o->y + (int)(PF((double)(q->x - o->x) - W / 2.0) * (double)((q->y - o->y) - hh)) + hh;
                #undef PF
            }
            for (Obj *t = evt_first(a->p[1].s[0], (uint16_t)a->p[1].s[1]); t; t = evt_next()) { t->x = px; t->y = py; t->sx = q->sx; t->sy = (float)(sy * q->sy); }
            return; }
        return; }
    case XK_ANDROID:
        if (num == 12 && a->np >= 1) plat_vibrate(eval_int(&a->p[0])); // Options > Vibrations (the game checks the setting itself)
        return;
    case XK_ANDROIDPLUS:
        if (num == 10 && a->np >= 2) { prefs_init(); char k[120]; snprintf(k, sizeof k, "%s", eval_str(&a->p[0])); ini_set(&prefs, "prefs", k, eval_str(&a->p[1])); }
        return;
    }
}
Val ext_exp(Obj *o, int num) {
    int kind = o ? o->oi->ext_kind : XK_STUB;
    switch (kind) {
    case XK_INI: { Ini *i = o->ext;
        if (num == 0) return VI(atoi(ini_get(i, i->group, i->item)));
        if (num == 1) return VS(ta_dup(ini_get(i, i->group, i->item)));
        if (num == 2) { Val k = exp_arg(); return VI(atoi(ini_get(i, i->group, vs(&k)))); }
        if (num == 3) { Val g = exp_arg(); Val k = exp_arg(); return VI(atoi(ini_get(i, vs(&g), vs(&k)))); }
        if (num == 4) { Val k = exp_arg(); return VS(ta_dup(ini_get(i, i->group, vs(&k)))); }
        if (num == 5) { Val g = exp_arg(); Val k = exp_arg(); return VS(ta_dup(ini_get(i, vs(&g), vs(&k)))); }
        return VI(0); }
    case XK_KYSO: { Kyso *k = o->ext; if (num == 1) return VI(k->n); if (num == 2) return VI(k->frame); return VD(1.0); }
    case XK_CALCRECT: { CalcR *c = o->ext; return VI(num == 0 ? c->w : c->h); }
    case XK_PARSER: { Parser *p = o->ext;
        if (num == 29) { Val v = exp_arg(); int want = vi(&v), idx = 1; const char *s = p->src ? p->src : "";
            for (;;) { const char *best = NULL; int bl = 0;
                for (int d = 0; d < p->nd; d++) { if (!p->delims[d][0]) continue; const char *f = strstr(s, p->delims[d]); if (f && (!best || f < best)) { best = f; bl = (int)strlen(p->delims[d]); } }
                size_t len = best ? (size_t)(best - s) : strlen(s);
                if (idx == want) { char *r = ta(len + 1); memcpy(r, s, len); r[len] = 0; return VS(r); }
                if (!best) break; s = best + bl; idx++; }
            return VS(""); }
        return VS(""); }
    case XK_CLOCK: { time_t t = time(NULL); struct tm *tm = localtime(&t); if (!tm) return VI(0);
        switch (num) { case 0: return VI(0); case 1: return VI(tm->tm_sec); case 2: return VI(tm->tm_min); case 3: return VI(tm->tm_hour); case 4: return VI(tm->tm_wday); case 5: return VI(tm->tm_mday); case 6: return VI(tm->tm_mon + 1); case 7: return VI(tm->tm_year + 1900); }
        return VI(0); }
    case XK_TOUCH:
        if (num == 0) { int c = 0; for (int i = 0; i < MAXTOUCH; i++) c += G.in.t[i].down; return VI(c); }
        if (num == 1 || num == 4) { int n = last_new(); if (n < 0) for (int i = 0; i < MAXTOUCH; i++) if (G.in.t[i].down) n = i; return VI(n < 0 ? 0 : n); }
        if (num == 5) return VI(last_end() < 0 ? 0 : last_end());
        if (num == 2 || num == 3 || num == 6 || num == 7) { Val v = exp_arg(); int i = vi(&v); if (i < 0 || i >= MAXTOUCH) return VI(-1); return VI((num & 1) ? G.in.t[i].y + G.winy : G.in.t[i].x + G.winx); }
        return VI(-1);
    case XK_ANDROIDPLUS:
        if (num == 4 || num == 37) return VI(G.w);
        if (num == 5 || num == 38) return VI(G.h);
        if (num == 10) { Val k = exp_arg(); prefs_init(); return VS(ta_dup(ini_get(&prefs, "prefs", vs(&k)))); }
        if (num == 11) { Val k = exp_arg(); prefs_init(); return VI(atoi(ini_get(&prefs, "prefs", vs(&k)))); }
        return VI(0);
    }
    return VI(0);
}
