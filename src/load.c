#include "fp.h"
#include <stdarg.h>
Game G;

void logf_(const char *fmt, ...) {
    static FILE *lf; static int n;
    if (n > 20000) return;
    n++;
    va_list ap; va_start(ap, fmt);
#ifdef __vita__
    if (!lf) lf = fopen("ux0:data/fnaf2/log.txt", "w");
    if (lf) { vfprintf(lf, fmt, ap); fputc('\n', lf); fflush(lf); }
#else
    (void)lf; vfprintf(stderr, fmt, ap); fputc('\n', stderr);
#endif
    va_end(ap);
}

typedef struct Blk { struct Blk *next; size_t used, cap; } Blk;
static Blk *fblk, *tblk;
static void *arena(Blk **head, size_t n) {
    n = (n + 7) & ~(size_t)7;
    Blk *b = *head;
    if (!b || b->used + n > b->cap) {
        size_t cap = n > (1u << 20) ? n : (1u << 20);
        b = malloc(sizeof(Blk) + cap); b->next = *head; b->used = 0; b->cap = cap; *head = b;
    }
    void *p = (char *)(b + 1) + b->used; b->used += n; memset(p, 0, n); return p;
}
static void arena_free(Blk **head) { while (*head) { Blk *n = (*head)->next; free(*head); *head = n; } }
void *pa(size_t n) { return calloc(1, n ? n : 1); }
void *fa(size_t n) { return arena(&fblk, n); }
void fa_reset(void) { arena_free(&fblk); }
char *ta(size_t n) { return arena(&tblk, n); }
void ta_reset(void) { if (tblk && !tblk->next) tblk->used = 0; else arena_free(&tblk); }
char *ta_dup(const char *s) { size_t n = strlen(s) + 1; char *p = ta(n); memcpy(p, s, n); return p; }

typedef struct { const uint8_t *p; } Rd;
static int r8(Rd *r) { return *r->p++; }
static int r16(Rd *r) { int16_t v; memcpy(&v, r->p, 2); r->p += 2; return v; }
static int ru16(Rd *r) { uint16_t v; memcpy(&v, r->p, 2); r->p += 2; return v; }
static int r32(Rd *r) { int32_t v; memcpy(&v, r->p, 4); r->p += 4; return v; }
static double rf64(Rd *r) { int64_t v; memcpy(&v, r->p, 8); r->p += 8; return (double)v / 4294967296.0; } // 32.32 fixed point
static int rcol(Rd *r) { int c = (r->p[0] << 16) | (r->p[1] << 8) | r->p[2]; r->p += 4; return c; }
// UTF-16LE -> UTF-8; stops at NUL or maxchars
static char *u16dup(const uint8_t *p, int maxchars, void *(*al)(size_t), const uint8_t **end) {
    int n = 0; while (n < maxchars && (p[2 * n] || p[2 * n + 1])) n++;
    char *out = al((size_t)n * 3 + 1), *o = out;
    for (int i = 0; i < n; i++) {
        unsigned c = p[2 * i] | (p[2 * i + 1] << 8);
        if (c < 0x80) *o++ = (char)c;
        else if (c < 0x800) { *o++ = (char)(0xC0 | (c >> 6)); *o++ = (char)(0x80 | (c & 63)); }
        else { *o++ = (char)(0xE0 | (c >> 12)); *o++ = (char)(0x80 | ((c >> 6) & 63)); *o++ = (char)(0x80 | (c & 63)); }
    }
    *o = 0;
    if (end) *end = p + 2 * n + (n < maxchars ? 2 : 0);
    return out;
}
static char *rstr(Rd *r, void *(*al)(size_t)) { const uint8_t *e; char *s = u16dup(r->p, 1 << 28, al, &e); r->p = e; return s; }

OI *oi_get(int h) { if (h < 0 || h >= G.noi || !G.oi[h].exists) return NULL; return &G.oi[h]; }

static uint8_t *readchunk(long off, uint32_t sz) {
    uint8_t *b = malloc(sz + 16); fseek(G.f, off, SEEK_SET);
    if (fread(b, 1, sz, G.f) != sz) logf_("short read at %ld", off);
    memset(b + sz, 0, 16); return b;
}

static void parse_common(OI *o, const uint8_t *b) {
    Rd r = {b};
    if (o->type == 1) { r.p += 4; o->bobst = r16(&r); r16(&r); o->bcx = r32(&r); o->bcy = r32(&r); o->bimage = r16(&r); o->bfill = 3; return; }
    if (o->type == 0) {
        r.p += 4; o->bobst = r16(&r); r16(&r); o->bcx = r32(&r); o->bcy = r32(&r); r16(&r); rcol(&r); o->bshape = r16(&r); o->bfill = r16(&r); o->bimage = -1;
        if (o->bshape == 1) r16(&r);
        else if (o->bfill == 1) o->bcolor = rcol(&r);
        else if (o->bfill == 2) o->bcolor = rcol(&r);
        else if (o->bfill == 3) o->bimage = r16(&r);
        return;
    }
    r.p += 4;
    int oMov = r16(&r), oVal = r16(&r); r.p += 2;
    int oCnt = r16(&r), oData = r16(&r), oExt = r16(&r);
    o->oeflags = r32(&r);
    for (int i = 0; i < 8; i++) o->quals[i] = r16(&r);
    int oAnim = r16(&r); r.p += 2;
    int oStr = r16(&r);
    o->flags2 = ru16(&r); o->prefs = ru16(&r); r.p += 4; rcol(&r); r32(&r); r32(&r);
    if (oMov) {
        const uint8_t *mb = b + oMov; Rd v = {mb}; int n = r32(&v); if (n < 0 || n > 16) n = 0;
        o->nmov = n; o->mov = pa(sizeof(MoveDef) * (n + 1));
        for (int i = 0; i < n; i++) {
            Rd e = {mb + 4 + i * 16}; r32(&e); r32(&e); int doff = r32(&e); r32(&e);
            Rd m = {mb + doff}; MoveDef *md = &o->mov[i]; r16(&m); md->type = r16(&m); md->atstart = r8(&m); r8(&m); m.p += 2; md->dirs = r32(&m);
            if (md->type == 4) md->bspeed = r16(&m);
            if (md->type == 5) {
                md->n = r16(&m); md->minsp = r16(&m); md->maxsp = r16(&m); md->loop = r8(&m); md->repos = r8(&m); md->reverse = r8(&m); r8(&m);
                if (md->n < 0 || md->n > 256) md->n = 0;
                md->st = pa(sizeof(PathStep) * (md->n + 1));
                for (int k = 0; k < md->n; k++) {
                    const uint8_t *sp = m.p; r8(&m); int ssz = r8(&m); PathStep *s = &md->st[k];
                    s->speed = (int8_t)r8(&m) & 255; s->dir = (int8_t)r8(&m); s->dx = r16(&m); s->dy = r16(&m); s->cos = r16(&m); s->sin = r16(&m); s->len = ru16(&m); s->pause = ru16(&m);
                    m.p = sp + ssz;
                }
            }
        }
    }
    if (oVal) {
        Rd v = {b + oVal}; o->nval = ru16(&v); o->val = pa(sizeof(int) * (o->nval + 1));
        for (int i = 0; i < o->nval; i++) o->val[i] = r32(&v);
        if (o->flags2 & 128) o->valflags = r32(&v);
    }
    if (oStr) {
        Rd v = {b + oStr}; o->nstr = ru16(&v); o->str = pa(sizeof(char *) * (o->nstr + 1));
        for (int i = 0; i < o->nstr; i++) o->str[i] = rstr(&v, pa);
    }
    if (oAnim) {
        const uint8_t *ab = b + oAnim; Rd v = {ab}; v.p += 2; o->nanim = r16(&v); o->anim = pa(sizeof(Anim *) * (o->nanim + 1));
        for (int i = 0; i < o->nanim; i++) {
            int ao = r16(&v); if (!ao) continue;
            Anim *an = pa(sizeof(Anim)); o->anim[i] = an;
            Rd d = {ab + ao};
            for (int k = 0; k < 32; k++) {
                int doff = r16(&d); if (!doff) continue;
                Rd f = {ab + ao + doff}; AnimDir *ad = pa(sizeof(AnimDir)); an->dir[k] = ad;
                ad->min = r8(&f); ad->max = r8(&f); ad->repeat = r16(&f); ad->repframe = r16(&f); ad->n = r16(&f);
                ad->frames = pa(sizeof(short) * (ad->n + 1));
                for (int j = 0; j < ad->n; j++) ad->frames[j] = r16(&f);
            }
        }
    }
    if (oCnt) { Rd v = {b + oCnt}; v.p += 2; o->has_counter = 1; o->cinit = r32(&v); o->cmin = r32(&v); o->cmax = r32(&v); }
    if (oExt) { Rd v = {b + oExt}; int sz = r32(&v); v.p += 4; o->extver = r32(&v); r32(&v); r32(&v); o->extsize = sz - 20; o->ext = (uint8_t *)v.p; }
    if (oData) {
        const uint8_t *db = b + oData; Rd v = {db};
        if (o->type == 3 || o->type == 4) {
            v.p += 4; o->tcx = r32(&v); o->tcy = r32(&v); o->tn = r32(&v); o->texts = pa(sizeof(TextDef) * (o->tn + 1));
            for (int i = 0; i < o->tn; i++) {
                Rd t = {db + r32(&v)}; o->texts[i].font = r16(&t); o->texts[i].flags = ru16(&t); o->texts[i].color = rcol(&t); o->texts[i].text = rstr(&t, pa);
            }
        } else if (o->type >= 5 && o->type <= 7) {
            v.p += 4; o->has_cdisp = 1; o->ccx = r32(&v); o->ccy = r32(&v); r16(&v); o->cdisp = r16(&v); o->cdflags = ru16(&v); o->cfont = r16(&v);
            if (o->cdisp == 1 || o->cdisp == 4) {
                o->cnframes = r16(&v); o->cframes = pa(sizeof(short) * (o->cnframes + 1));
                for (int i = 0; i < o->cnframes; i++) o->cframes[i] = r16(&v);
            }
        }
    }
}

int app_load(const char *dir) {
    char path[400];
    snprintf(G.datadir, sizeof G.datadir, "%s", dir);
    snprintf(G.savedir, sizeof G.savedir, "%s/save", dir);
    snprintf(path, sizeof path, "%s/application.ccn", dir);
    snprintf(path, sizeof path, "%s/images.pak", dir);
    G.pak = fopen(path, "rb");
    if (G.pak) { // pre-converted texture pack (see tools/mkpack.py)
        uint8_t ph[12];
        if (fread(ph, 1, 12, G.pak) == 12 && !memcmp(ph, "FP2I", 4) && ph[4] == 3) {
            int n = ph[8] | (ph[9] << 8) | (ph[10] << 16) | (ph[11] << 24);
            G.nimg = n; G.img = pa(sizeof(ImgInfo) * (n + 1));
            uint8_t *tb = malloc((size_t)n * 34); if (fread(tb, 34, n, G.pak) != (size_t)n) logf_("short pak table");
            for (int i = 0; i < n; i++) { Rd r = {tb + i * 34}; ImgInfo *ii = &G.img[i];
                ii->off = r32(&r); ii->size = r32(&r); ii->pw = ru16(&r); ii->ph = ru16(&r); ii->mode = r8(&r); r8(&r);
                ii->w = ru16(&r); ii->h = ru16(&r); ii->xs = r16(&r); ii->ys = r16(&r); ii->xa = r16(&r); ii->ya = r16(&r); ii->moff = r32(&r); ii->msize = r32(&r); }
            free(tb);
        } else { fclose(G.pak); G.pak = NULL; logf_("bad images.pak"); }
    }
    snprintf(path, sizeof path, "%s/application.ccn", dir);
    G.f = fopen(path, "rb");
    if (!G.f) { logf_("cannot open %s", path); return -1; }
    uint8_t h[16];
    if (fread(h, 1, 16, G.f) != 16 || memcmp(h, "PAMU", 4)) { logf_("bad header"); return -2; }
    long off = 16; int nfr = 0;
    G.frdata = pa(sizeof(uint8_t *) * 64); G.frsize = pa(sizeof(int) * 64);
    for (;;) {
        fseek(G.f, off, SEEK_SET);
        if (fread(h, 1, 8, G.f) != 8) break;
        int id = h[0] | (h[1] << 8); uint32_t sz = h[4] | (h[5] << 8) | (h[6] << 16) | ((uint32_t)h[7] << 24);
        long data = off + 8;
        if (id == 0x2223) {
            uint8_t *b = readchunk(data, sz); Rd r = {b + 4};
            G.flags = ru16(&r); G.newflags = ru16(&r); r.p += 4; G.w = r16(&r); G.h = r16(&r); r.p += 8 + 8 + 64 + 4; G.nframes = r32(&r); G.fps = r32(&r); free(b);
        } else if (id == 0x2232) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; G.ngv = ru16(&r); G.gv = pa(sizeof(Val) * (G.ngv + 64));
            for (int i = 0; i < G.ngv; i++) G.gv[i] = VI(r32(&r));
            free(b);
        } else if (id == 0x2234) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; int n = ru16(&r); r.p += 2; G.next_n = 64; G.extname = pa(sizeof(char *) * 64);
            for (int i = 0; i < n; i++) {
                const uint8_t *st = r.p; int s = r16(&r); if (s < 0) s = -s; int hd = r16(&r); r.p += 12;
                if (hd >= 0 && hd < 64) G.extname[hd] = rstr(&r, pa);
                r.p = st + s;
            }
            free(b);
        } else if (id == 0x2229) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; int n = r32(&r);
            G.noi = 1024; G.oi = pa(sizeof(OI) * G.noi);
            for (int i = 0; i < n; i++) {
                OI *o = NULL;
                for (;;) {
                    int cid = ru16(&r); ru16(&r); int cs = r32(&r); const uint8_t *cp = r.p; r.p += cs;
                    if (cid == 0x4444) {
                        Rd c = {cp}; int hd = r16(&c);
                        if (hd < 0 || hd >= G.noi) { logf_("oi handle %d out of range", hd); continue; }
                        o = &G.oi[hd]; o->handle = hd; o->type = r16(&c); o->flags = ru16(&c); c.p += 2; o->ink = r32(&c); o->inkparam = r32(&c); o->exists = 1; o->first = -1;
                    } else if (cid == 0x4445 && o) { Rd c = {cp}; o->name = rstr(&c, pa); }
                    else if (cid == 0x4446 && o) parse_common(o, cp);
                    else if (cid == 0x7F7F) break;
                }
            }
            // chunk buffer kept (ext data points into it)
        } else if (id == 0x222B) {
            uint8_t *b = readchunk(data, sz); G.nfrh = sz / 2; G.frhandle = pa(sz + 2); memcpy(G.frhandle, b, sz); free(b);
        } else if (id == 0x3333) {
            if (nfr < 64) { G.frdata[nfr] = readchunk(data, sz); G.frsize[nfr] = sz; nfr++; }
        } else if (id == 0x6666 && !G.pak) {
            fseek(G.f, data, SEEK_SET); uint8_t t[22]; if (fread(t, 1, 4, G.f) != 4) return -3;
            int nh = t[0] | (t[1] << 8), ni = t[2] | (t[3] << 8);
            G.nimg = nh + 1; G.img = pa(sizeof(ImgInfo) * G.nimg);
            long p = data + 4;
            for (int i = 0; i < ni; i++) {
                fseek(G.f, p, SEEK_SET); if (fread(t, 1, 22, G.f) != 22) break;
                Rd r = {t}; int hd = ru16(&r); int mode = r32(&r);
                ImgInfo ii; memset(&ii, 0, sizeof ii); ii.mode = mode & 0xFF; ii.w = r16(&r); ii.h = r16(&r); ii.xs = r16(&r); ii.ys = r16(&r); ii.xa = r16(&r); ii.ya = r16(&r);
                ii.size = r32(&r); ii.off = p + 22;
                if (hd < G.nimg) G.img[hd] = ii;
                p += 22 + ii.size;
            }
        } else if (id == 0x6667) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; int n = r32(&r);
            for (int i = 0; i < n; i++) {
                int hd = r32(&r); r.p += 12; const uint8_t *p = r.p; int hgt = r32(&r); r32(&r); r32(&r); r32(&r); int wt = r32(&r); int it = r8(&r); r.p += 7;
                char *nm = rstr(&r, pa);
                if (hd >= 0 && hd < 64) { G.font[hd].height = hgt < 0 ? -hgt : hgt; G.font[hd].weight = wt; G.font[hd].italic = it; snprintf(G.font[hd].name, 40, "%s", nm); }
                free(nm); r.p = p + 92;
            }
            free(b);
        } else if (id == 0x6668) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; int nh = ru16(&r), ns = ru16(&r);
            G.nsnd = nh; G.sndname = pa(sizeof(char *) * (nh + 1));
            for (int i = 0; i < ns; i++) {
                int hd = ru16(&r), fl = ru16(&r); r32(&r); r32(&r);
                if (fl & 256) { int l = r16(&r); char *nm = u16dup(r.p, l, pa, NULL); r.p += 2 * l; if (hd < nh) G.sndname[hd] = nm; }
            }
            free(b);
        } else if (id == 0x2238) {
            uint8_t *b = readchunk(data, sz); Rd r = {b}; int n = r32(&r); G.nef = n; G.ef = pa(sizeof(EFile) * (n + 1));
            for (int i = 0; i < n; i++) {
                int l = r16(&r); G.ef[i].name = u16dup(r.p, l, pa, NULL); r.p += 2 * l;
                G.ef[i].size = r32(&r); G.ef[i].data = (uint8_t *)r.p; r.p += G.ef[i].size;
            }
        }
        off = data + sz;
        if (id == 0x7F7F) break;
    }
    G.nframes = nfr;
    for (int i = 0; i < G.noi; i++) if (G.oi[i].exists && G.oi[i].type >= 32) {
        int e = G.oi[i].type - 32; G.oi[i].ext_kind = (e < 64 && G.extname[e]) ? ext_kind(G.extname[e]) : XK_STUB;
    }
    logf_("app %dx%d fps %d frames %d images %d sounds %d", G.w, G.h, G.fps, G.nframes, G.nimg, G.nsnd);
    return 0;
}

// ---------------- frame / events ----------------
static short norm_oil(int oi) { // the file leaves oiList unresolved: qualifiers are oi values with the high bit set
    if (oi == 0xFFFF) return -1;
    if (oi & 0x8000) { for (int i = 0; i < G.nq; i++) if (G.q[i].qoi == oi) return (short)(0x8000 | i); return -1; }
    return 0;
}
static int is_expr_param(int c) { return c == 15 || c == 22 || c == 23 || c == 27 || c == 28 || c == 45 || c == 46 || c == 52 || c == 53 || c == 54 || c == 59 || c == 62; }
static void parse_expr(Param *p, const uint8_t *b) {
    Rd r = {b}; int n = 0;
    for (;;) { Rd q = r; int t = r16(&q), nm = r16(&q); n++; if (!t && !nm) break; int sz = ru16(&q); r.p += sz; }
    p->ntok = n; p->tok = fa(sizeof(Tok) * (n + 1)); r.p = b;
    for (int i = 0; i < n; i++) {
        const uint8_t *st = r.p; Tok *k = &p->tok[i];
        k->type = r16(&r); k->num = r16(&r); k->code = ((int)k->num << 16) | (k->type & 0xFFFF);
        if (!k->type && !k->num) break;
        int sz = ru16(&r);
        if (k->type == -1 && k->num == 0) k->i = r32(&r);
        else if (k->type == -1 && k->num == 3) k->s = rstr(&r, fa);
        else if (k->type == -1 && k->num == 23) { int64_t v; memcpy(&v, r.p, 8); r.p += 8; k->d = (double)v / 4294967296.0; }
        else if (k->type == -1 && (k->num == 24 || k->num == 50)) { r.p += 4; k->n = r16(&r); }
        else if (k->type >= 2 || k->type == -7) { k->oi = ru16(&r); k->oil = r16(&r); k->oil = norm_oil(k->oi); if (k->num == 16 || k->num == 19) k->n = r16(&r); }
        r.p = st + sz;
    }
}
static const uint8_t *parse_param(Param *p, const uint8_t *b) {
    Rd r = {b}; int size = r16(&r); int c = p->code = r16(&r);
    if (is_expr_param(c)) { p->cmp = r16(&r); parse_expr(p, r.p); }
    else if (c == 1) { p->s[0] = r16(&r); p->s[1] = r16(&r); p->s[2] = r16(&r); p->s[0] = norm_oil((uint16_t)p->s[1]); }
    else if (c == 2) { p->a = r32(&r); p->b = r32(&r); }
    else if (c == 42) { p->a = r32(&r); p->b = r32(&r); p->cmp = r16(&r); }
    else if (c == 5 || c == 25 || c == 29 || c == 34 || c == 48 || c == 56) { p->a = r32(&r); p->b = 0; }
    else if (c == 6 || c == 7 || c == 35 || c == 36) { p->a = r16(&r); p->b = ru16(&r); p->str = rstr(&r, fa); }
    else if (c == 9 || c == 21 || c == 16) {
        Pos *ps = fa(sizeof(Pos)); p->ptr = ps;
        ps->parent = ru16(&r); ps->flags = ru16(&r); ps->x = r16(&r); ps->y = r16(&r); ps->slope = r16(&r); ps->angle = r16(&r); ps->dir = r32(&r);
        ps->typeParent = r16(&r); ps->oil = r16(&r); ps->layer = r16(&r); ps->oil = norm_oil(ps->parent);
        if (c != 16) { p->a = ru16(&r); p->b = ru16(&r); }
    }
    else if (c == 13) { p->a = r32(&r); p->b = r32(&r); }
    else if (c == 14 || c == 44) p->a = ru16(&r);
    else if (c == 19 || c == 72) { for (int i = 0; i < 4; i++) p->s[i] = r16(&r); }
    else if (c == 24) p->a = rcol(&r);
    else if (c == 38) { p->a = ru16(&r); p->b = ru16(&r); }
    else if (c == 39) { r.p += 4; p->b = r16(&r); p->a = -1; }
    else if (c == 40 || c == 41 || c == 63 || c == 64) p->str = rstr(&r, fa);
    else if (c == 47 || c == 51) { p->s[0] = r16(&r); p->s[1] = r16(&r); }
    else if (c == 68) {
        MultiVar *m = fa(sizeof(MultiVar)); p->ptr = m; m->flags = r32(&r); m->mask = r32(&r); m->values = r32(&r);
        for (int i = 0; i < 4 && (m->flags >> (4 * i) & 1); i++) {
            int nib = m->flags >> (4 * i); m->v[i].idx = r32(&r); m->v[i].op = r32(&r); m->v[i].global = (nib & 2) != 0;
            if (nib & 4) m->v[i].v = VD(rf64(&r)); else { m->v[i].v = VI(r32(&r)); r.p += 4; }
            m->n = i + 1;
        }
    }
    else if (c == 69) { int n = r32(&r); int *o = fa(sizeof(int) * (n + 1)); for (int i = 0; i < n; i++) { o[i] = ru16(&r); r.p += 2; } p->a = n; p->ptr = o; }
    else p->a = r16(&r); // shorts and anything else
    return b + size;
}
static const uint8_t *parse_evt(Evt *e, const uint8_t *b, int cond) {
    Rd r = {b}; int size = ru16(&r);
    e->type = r16(&r); e->num = r16(&r); e->code = ((int)e->num << 16) | (e->type & 0xFFFF);
    e->oi = ru16(&r); e->oil = r16(&r); e->oil = e->type >= 2 ? norm_oil(e->oi) : 0; e->flags = r8(&r); e->flags2 = r8(&r); e->np = r8(&r); r8(&r);
    if (cond) r16(&r);
    e->p = fa(sizeof(Param) * (e->np + 1));
    const uint8_t *q = r.p;
    for (int i = 0; i < e->np; i++) q = parse_param(&e->p[i], q);
    return b + size;
}

int frame_parse(int idx) {
    const uint8_t *d = G.frdata[idx]; const uint8_t *end = d + G.frsize[idx]; Rd r = {d};
    G.nlo = 0; G.nlayer = 0; G.ng = 0; G.nq = 0; G.fname = ""; int gcap = 0;
    while (r.p + 8 <= end) {
        int id = ru16(&r); ru16(&r); int sz = r32(&r); const uint8_t *cp = r.p; r.p += sz; Rd c = {cp};
        if (id == 0x3334) { G.fw = r32(&c); G.fh = r32(&c); G.fbg = rcol(&c); G.fflags = r32(&c); }
        else if (id == 0x3335) G.fname = rstr(&c, fa);
        else if (id == 0x3338) {
            G.nlo = r32(&c); G.lo = fa(sizeof(LO) * (G.nlo + 1));
            for (int i = 0; i < G.nlo; i++) { LO *l = &G.lo[i]; l->handle = r16(&c); l->oi = r16(&c); l->x = r32(&c); l->y = r32(&c); l->ptype = r16(&c); l->value = r16(&c); l->layer = r16(&c); c.p += 2; }
        } else if (id == 0x3341) {
            G.nlayer = r32(&c); G.layer = fa(sizeof(Layer) * (G.nlayer + 1));
            for (int i = 0; i < G.nlayer; i++) { Layer *l = &G.layer[i]; l->opt = r32(&c); l->xc = r32(&c) / 65536.0f; l->yc = r32(&c) / 65536.0f; l->nbkd = r32(&c); l->first = r32(&c); l->name = rstr(&c, fa); l->hidden = !(l->opt & 0x10); }
        } else if (id == 0x333D) {
            const uint8_t *ce = cp + sz;
            while (c.p + 4 <= ce) {
                const uint8_t *tag = c.p; c.p += 4;
                if (!memcmp(tag, "ER>>", 4)) {
                    c.p += 6 + 34; int nq = r16(&c); G.nq = nq; G.q = fa(sizeof(Qual) * (nq + 1));
                    for (int i = 0; i < nq; i++) {
                        int qoi = ru16(&c), qt = r16(&c); int qn = qoi & 0x7FFF; Qual *q = &G.q[i]; q->qoi = qoi; q->oi = fa(sizeof(OI *) * 64);
                        for (int k = 0; k < G.noi && q->n < 64; k++) if (G.oi[k].exists && G.oi[k].type == qt)
                            for (int m = 0; m < 8; m++) if (G.oi[k].quals[m] == qn) { q->oi[q->n++] = &G.oi[k]; break; }
                    }
                } else if (!memcmp(tag, "ERes", 4)) { r32(&c); int tot = r32(&c); G.g = fa(sizeof(Group) * (tot + 1)); gcap = tot; }
                else if (!memcmp(tag, "ERev", 4)) {
                    r32(&c); int n = r32(&c);
                    for (int i = 0; i < n && G.ng < gcap; i++) {
                        const uint8_t *gp = c.p; Group *g = &G.g[G.ng++]; int gs = r16(&c);
                        g->nc = r8(&c); g->na = r8(&c); g->flags = ru16(&c); g->line = r16(&c); r32(&c); g->level = r32(&c);
                        g->c = fa(sizeof(Evt) * (g->nc + 1)); g->a = fa(sizeof(Evt) * (g->na + 1));
                        const uint8_t *q = c.p;
                        for (int k = 0; k < g->nc; k++) q = parse_evt(&g->c[k], q, 1);
                        for (int k = 0; k < g->na; k++) q = parse_evt(&g->a[k], q, 0);
                        for (int k = 0; k < g->nc; k++) if (g->c[k].type == -1 && (g->c[k].num == -25 || g->c[k].num == -24)) g->flags |= 0x400 | (g->c[k].num == -25 ? 0x1000 : 0); // OR groups
                        c.p = gp - gs;
                    }
                } else if (!memcmp(tag, "ERop", 4)) r32(&c);
                else if (!memcmp(tag, "<<ER", 4)) break;
                else { logf_("bad event tag"); break; }
            }
        } else if (id == 0x7F7F) break;
    }
    // resolve group pointers
    for (int i = 0; i < G.ng; i++) {
        Group *g = &G.g[i];
        for (int k = 0; k < g->nc + g->na; k++) {
            Evt *e = k < g->nc ? &g->c[k] : &g->a[k - g->nc];
            for (int m = 0; m < e->np; m++) if (e->p[m].code == 39) {
                for (int j = 0; j < G.ng; j++) if (G.g[j].nc && G.g[j].c[0].code == ((-10 << 16) | 0xFFFF) && G.g[j].c[0].np && G.g[j].c[0].p[0].b == e->p[m].b) { e->p[m].a = j; break; }
            }
        }
    }
    return 0;
}
