// event engine: object selection, expressions, conditions, actions (ported from the Fusion runtime's algorithm)
#include "fp.h"
#define CODE(t, n) ((int)((unsigned)(n) << 16) | ((t) & 0xFFFF))
#define NEG(c) ((c)->flags2 & 1)
static int evCount, actCount, actLoop, actLoopCount, actOn, enablePick, repeatFlag, inTrigger;
int evt_nsel;
static Obj *evPos, *evPrev; static OI *evPosOI, *evPrevOI; static Qual *evQ; static int evQi;
static Group *curG; static const char *curLoop = "", *curTimerName = "", *curForEach = "";
static int curClick = -1, curClickOi = -1; static Obj *curClickObj;
static Param *childParam; static int *always, nalways; static int (*trig)[2], ntrig;
typedef struct Loop { char name[80]; int index, stop; } Loop; static Loop loops[128]; static int nloops;
typedef struct TimerEv { int time; char name[80]; int dead; } TimerEv; static TimerEv tev[64]; static int ntev;
typedef struct FE { const char *name; int n; Obj **o; } FE; static FE fes[16]; static int nfe;
typedef struct Saved { OI *oi; int n; Obj **o; int depth; } Saved; static Saved saved[256]; static int nsaved, sdepth;
Tok *ct; Val R[1024]; int pp; static Tok *OP[1024];

static void sel_init(OI *o) {
    if (o->evcount == evCount) return;
    o->evcount = evCount; o->selfirst = o->first; o->nsel = o->nobj;
    for (int i = o->first; i >= 0; i = G.obj[i]->next) G.obj[i]->nextsel = G.obj[i]->next;
}
static Qual *qual(int oil) { int qi = oil & 0x7FFF; return qi < G.nq ? &G.q[qi] : NULL; }
Obj *evt_first(int oil, int oi) {
    evPos = evPrev = NULL; evQ = NULL; evt_nsel = 0; evPosOI = evPrevOI = NULL;
    if (oil >= 0) {
        OI *o = oi_get(oi); if (!o) return NULL;
        sel_init(o); evPosOI = evPrevOI = o; evt_nsel = o->nsel;
        if (o->selfirst < 0) return NULL;
        return evPos = G.obj[o->selfirst];
    }
    if (oil == -1) return NULL;
    Qual *q = qual(oil); if (!q) return NULL;
    evQ = q;
    for (int i = 0; i < q->n; i++) { sel_init(q->oi[i]); evt_nsel += q->oi[i]->nsel; }
    for (evQi = 0; evQi < q->n; evQi++) if (q->oi[evQi]->selfirst >= 0) { evPosOI = evPrevOI = q->oi[evQi]; return evPos = G.obj[evPosOI->selfirst]; }
    return NULL;
}
Obj *evt_next(void) {
    if (!evPos) { if (evPosOI && evPosOI->selfirst >= 0) { evPrev = NULL; evPrevOI = evPosOI; return evPos = G.obj[evPosOI->selfirst]; } }
    else if (evPos->nextsel >= 0) { evPrev = evPos; evPrevOI = NULL; return evPos = G.obj[evPos->nextsel]; }
    if (!evQ) return NULL;
    for (evQi++; evQi < evQ->n; evQi++) if (evQ->oi[evQi]->selfirst >= 0) { evPrev = NULL; evPosOI = evPrevOI = evQ->oi[evQi]; return evPos = G.obj[evPosOI->selfirst]; }
    evPos = NULL; evPosOI = NULL; return NULL;
}
void evt_delete_current(void) {
    evPos->oi->nsel--;
    if (evPrev) { evPrev->nextsel = evPos->nextsel; evPos = evPrev; }
    else { evPrevOI->selfirst = evPos->nextsel; evPosOI = evPrevOI; evPos = NULL; }
}
static void desel(OI *o) { o->evcount = evCount; o->selfirst = -1; o->nsel = 0; }
void evt_force_one(int oil, int oi, Obj *o) {
    if (oil >= 0) { OI *x = oi_get(oi); if (x) desel(x); }
    else if (oil != -1) { Qual *q = qual(oil); if (q) for (int i = 0; i < q->n; i++) desel(q->oi[i]); }
    o->nextsel = -1; o->oi->selfirst = o->idx; o->oi->nsel = 1; o->oi->evcount = evCount;
}
static void evt_add_current(Obj *o) {
    OI *x = o->oi;
    if (x->evcount != evCount) { x->evcount = evCount; x->selfirst = o->idx; x->nsel = 1; o->nextsel = -1; return; }
    for (int i = x->selfirst; i >= 0; i = G.obj[i]->nextsel) if (i == o->idx) return;
    o->nextsel = x->selfirst; x->selfirst = o->idx; x->nsel++;
}
static void lpush(Obj ***l, int *n, int *cap, Obj *o) { if (*n >= *cap) { *cap = *cap ? *cap * 2 : 16; *l = realloc(*l, sizeof(Obj *) * *cap); } (*l)[(*n)++] = o; }
static Obj *cur_objects(int oil, int oi) {
    Obj **l; int n;
    if (oil >= 0) {
        OI *o = oi_get(oi); if (!o) { repeatFlag = 0; return NULL; }
        if (o->actcount != actCount) {
            o->actcount = actCount; o->actn = 0;
            if (o->evcount == evCount && o->selfirst >= 0) { for (int i = o->selfirst; i >= 0; i = G.obj[i]->nextsel) lpush(&o->actlist, &o->actn, &o->actcap, G.obj[i]); }
            else if (enablePick && o->evcount == evCount) {}
            else for (int i = o->first; i >= 0; i = G.obj[i]->next) lpush(&o->actlist, &o->actn, &o->actcap, G.obj[i]);
        }
        l = o->actlist; n = o->actn;
    } else {
        Qual *q = oil == -1 ? NULL : qual(oil); if (!q) { repeatFlag = 0; return NULL; }
        if (q->actcount != actCount) {
            q->actcount = actCount; q->actn = 0; int filt = 0;
            for (int k = 0; k < q->n; k++) { OI *o = q->oi[k]; if (o->evcount == evCount) { filt = 1; for (int i = o->selfirst; i >= 0; i = G.obj[i]->nextsel) lpush(&q->actlist, &q->actn, &q->actcap, G.obj[i]); } }
            if (!q->actn && !(enablePick && filt))
                for (int k = 0; k < q->n; k++) for (int i = q->oi[k]->first; i >= 0; i = G.obj[i]->next) lpush(&q->actlist, &q->actn, &q->actcap, G.obj[i]);
        }
        l = q->actlist; n = q->actn;
    }
    if (!n) { repeatFlag = 0; return NULL; }
    repeatFlag = actLoopCount < n - 1;
    return l[actLoopCount % n];
}
static Obj *act_obj(Evt *a) {
    a->flags &= ~0x10; enablePick = 1;
    Obj *o = cur_objects(a->oil, a->oi);
    if (!o) { a->flags |= 0x10; return NULL; }
    if (repeatFlag) { a->flags |= 1; actLoop = 1; }
    return o;
}
static Obj *exp_obj(int oil, int oi) {
    if (actOn) { enablePick = 0; return cur_objects(oil, oi); }
    if (oil >= 0) {
        OI *o = oi_get(oi); if (!o) return NULL;
        if (o->evcount == evCount && o->selfirst >= 0) return G.obj[o->selfirst];
        return o->first >= 0 ? G.obj[o->first] : NULL;
    }
    Qual *q = oil == -1 ? NULL : qual(oil); if (!q) return NULL;
    for (int k = 0; k < q->n; k++) if (q->oi[k]->evcount == evCount && q->oi[k]->selfirst >= 0) return G.obj[q->oi[k]->selfirst];
    for (int k = 0; k < q->n; k++) if (q->oi[k]->first >= 0) return G.obj[q->oi[k]->first];
    return NULL;
}

// ---------------- values ----------------
int cmp_val(const Val *a, const Val *b, int op) {
    int c;
    if (a->t == 2 || b->t == 2) c = strcmp(vs(a), vs(b));
    else if (a->t == 0 && b->t == 0) c = a->i < b->i ? -1 : a->i > b->i;
    else { double x = vd(a), y = vd(b); c = x < y ? -1 : x > y; }
    switch (op) { case 0: return c == 0; case 1: return c != 0; case 2: return c <= 0; case 3: return c < 0; case 4: return c >= 0; case 5: return c > 0; }
    return 0;
}
static void apply(Tok *op) {
    Val *a = &R[pp], *b = &R[pp + 1];
    switch (op->num) {
    case 2: if (a->t == 2) { if (b->t == 2) { size_t la = strlen(a->s), lb = strlen(b->s); char *s = ta(la + lb + 1); memcpy(s, a->s, la); memcpy(s + la, b->s, lb + 1); a->s = s; } }
            else if (a->t == 0 && b->t == 0) a->i += b->i; else if (b->t != 2) *a = VD(vd(a) + vd(b)); break;
    case 4: if (a->t == 0 && b->t == 0) a->i -= b->i; else if (a->t != 2 && b->t != 2) *a = VD(vd(a) - vd(b)); break;
    case 6: if (a->t == 0 && b->t == 0) a->i *= b->i; else if (a->t != 2 && b->t != 2) *a = VD(vd(a) * vd(b)); break;
    case 8: if (a->t == 0 && b->t == 0) a->i = b->i ? a->i / b->i : 0; else if (a->t != 2 && b->t != 2) { double y = vd(b); *a = VD(y != 0 ? vd(a) / y : 0); } break;
    case 10: if (a->t == 0 && b->t == 0) a->i = b->i ? a->i % b->i : 0; else if (a->t != 2 && b->t != 2) { double y = vd(b); *a = VD(y != 0 ? fmod(vd(a), y) : 0); } break;
    case 12: *a = VD(pow(vd(a), vd(b))); break;
    case 14: *a = VI(vi(a) & vi(b)); break;
    case 16: *a = VI(vi(a) | vi(b)); break;
    case 18: *a = VI(vi(a) ^ vi(b)); break;
    }
}
static void eval_tok(Tok *t);
Val *get_expr(void) {
    int start = pp, i;
    OP[pp] = NULL;
    pp++; eval_tok(ct); ct++;
    for (;;) {
        Tok *e = ct;
        if (e->code <= 0 || e->code >= 0x140000) { i = pp - 1; pp = i; if (i == start) break; apply(OP[i]); }
        else if (e->code > (OP[pp - 1] ? OP[pp - 1]->code : 0)) { OP[pp] = e; ct++; pp++; eval_tok(ct); ct++; }
        else { pp--; apply(OP[pp]); }
    }
    return &R[start + 1];
}
Val eval_param(Param *p) {
    if (!p->tok) return VI(p->a);
    Tok *s = ct; ct = p->tok; Val v = *get_expr(); ct = s; return v;
}
int eval_int(Param *p) { Val v = eval_param(p); return vi(&v); }
const char *eval_str(Param *p) { Val v = eval_param(p); return vs(&v); }
static Val arg(void) { ct++; return *get_expr(); }      // next argument of a function token
Val exp_arg(void) { return arg(); }
static void skip_args(void) { // keep the token stream in sync for functions we do not implement
    Tok *n = ct + 1;
    if (n->type == 0 || (n->type == -1 && (n->num == -2 || n->num == -3))) return;
    int save = pp; do { ct++; get_expr(); } while (ct->type == -1 && ct->num == -3); pp = save;
}
static const char *lower(const char *s) { char *d = ta_dup(s); for (char *p = d; *p; p++) *p = (char)tolower((unsigned char)*p); return d; }
static Loop *find_loop(const char *nm, int add) {
    for (int i = 0; i < nloops; i++) if (!strcmp(loops[i].name, nm)) return &loops[i];
    if (!add || nloops >= 128) return NULL;
    Loop *l = &loops[nloops++]; snprintf(l->name, sizeof l->name, "%s", nm); l->index = 0; l->stop = 0; return l;
}
static Val *oval(Obj *o, int n) {
    static Val zero; if (!o || n < 0) { zero = VI(0); return &zero; }
    if (n >= o->nval) { int nn = n + 10; o->val = realloc(o->val, sizeof(Val) * nn); for (int i = o->nval; i < nn; i++) o->val[i] = VI(0); o->nval = nn; }
    return &o->val[n];
}
static Val *gval(int n) { static Val zero; if (n < 0 || n >= G.ngv + 64) { zero = VI(0); return &zero; } if (n >= G.ngv) G.ngv = n + 1; return &G.gv[n]; }
static const char *fmt_d(double d) { char *b = ta(48); if (d == floor(d) && fabs(d) < 1e15) snprintf(b, 48, "%.0f", d); else snprintf(b, 48, "%g", d); return b; }

static void eval_tok(Tok *t) {
    Val *r = &R[pp]; int ty = t->type, n = t->num;
    if (ty == -1) switch (n) {
        case -1: { Val v = arg(); R[pp] = v; return; }
        case 0: *r = VI(t->i); return;
        case 1: { Val v = arg(); R[pp] = VI(frnd(vi(&v))); return; }
        case 3: *r = VS(t->s); return;
        case 4: { Val v = arg(); if (v.t == 0) { char *b = ta(16); snprintf(b, 16, "%d", v.i); R[pp] = VS(b); } else if (v.t == 1) R[pp] = VS(fmt_d(v.d)); else R[pp] = VS(""); return; }
        case 5: { Val v = arg(); const char *s = vs(&v); if (strchr(s, '.') || strchr(s, 'e') || strchr(s, 'E')) R[pp] = VD(atof(s)); else R[pp] = VI((int)strtol(s, NULL, 10)); return; }
        case 22: { Val v = arg(); int c = 0; for (const unsigned char *s = (const unsigned char *)vs(&v); *s; s++) if ((*s & 0xC0) != 0x80) c++; R[pp] = VI(c); return; }
        case 23: *r = VD(t->d); return;
        case 24: *r = *gval(t->n); return;
        case 40: case 41: {
            Val a = arg(); Val b = arg(); int mx = n == 41;
            if (a.t == 1 || b.t == 1) { double x = vd(&a), y = vd(&b); R[pp] = VD(mx ? (x > y ? x : y) : (x < y ? x : y)); }
            else { int x = vi(&a), y = vi(&b); R[pp] = VI(mx ? (x > y ? x : y) : (x < y ? x : y)); }
            return; }
        case 46: { Val v = arg(); Loop *l = find_loop(lower(vs(&v)), 0); R[pp] = VI(l ? l->index : 0); return; }
        case 47: *r = VS("\r\n"); return;
        case 50: *r = VS(""); return;
        case 59: { Val a = arg(); Val b = arg(); R[pp] = VD(atan2(vd(&a), vd(&b)) * 180.0 / 3.141592653589793); return; }
        case 60: *r = VI(0); return;
        case 61: *r = VS(""); return;
        case 62: { Val a = arg(); Val b = arg(); Val c = arg(); Val d = arg(); double dx = vd(&c) - vd(&a), dy = vd(&d) - vd(&b); R[pp] = VI((int)sqrt(dx * dx + dy * dy)); return; }
    }
    else if (ty == -4 && n == 0) { *r = VI(G.timer); return; }
    else if (ty == -3) switch (n) {
        case 2: *r = VI(G.winx); return; case 3: *r = VI(G.winx + G.w); return;
        case 4: *r = VI(G.winy); return; case 5: *r = VI(G.winy + G.h); return;
        case 8: *r = VI(G.fi + 1); return;
    }
    else if (ty == -6) { *r = VI(n == 0 ? G.mx + G.winx : G.my + G.winy); return; }
    else if (ty == -2) switch (n) {
        case 7: { Val v = arg(); R[pp] = VI(snd_chan_pos(vi(&v) - 1)); return; }
        case 12: { Val v = arg(); R[pp] = VS(ta_dup(snd_chan_name(vi(&v) - 1))); return; }
        default: { int save = pp; skip_args(); pp = save; R[pp] = VI(0); return; }
    }
    else if (ty >= 2) {
        Obj *o = exp_obj(t->oil, t->oi);
        if (n >= 80) {
            if (ty >= 32) { int save = pp; Tok *before = ct; Val v = ext_exp(o, n - 80); if (ct == before) skip_args(); pp = save; R[pp] = v; return; }
            if (!o) { int save = pp; skip_args(); pp = save; R[pp] = VI(0); return; }
            if (o->type == 7) { if (n == 80) *r = o->cv; else if (n == 82) *r = VD(o->cmax); else if (n == 81) *r = VD(o->cmin); else *r = VI(0); if (r->t == 1 && r->d == floor(r->d) && n != 80) *r = VI((int)r->d); return; }
            if (o->type == 2) { *r = VD(n == 81 ? o->sx : n == 82 ? o->sy : o->angle); return; }
            if (o->type == 3) { if (n == 81) *r = VS(o->text ? ta_dup(o->text) : ""); else if (n == 82) { Val v = arg(); int k = vi(&v); OI *x = o->oi; R[pp] = VS(k < 0 || !x->tn ? (o->text ? ta_dup(o->text) : "") : ta_dup(x->texts[k < x->tn ? k : x->tn - 1].text)); } else *r = VI(o->tpara + 1); return; }
            *r = VI(0); return;
        }
        int l, tp, rr, bb;
        switch (n) {
        case 1: *r = VI(o ? o->y : 0); return;
        case 6: *r = VI(o ? o->dir : 0); return;
        case 7: case 8: case 9: case 10: if (!o) { *r = VI(0); return; } obj_rect(o, &l, &tp, &rr, &bb); *r = VI(n == 7 ? l : n == 8 ? rr : n == 9 ? tp : bb); return;
        case 11: *r = VI(o ? o->x : 0); return;
        case 12: *r = VI(o ? (int)o->fixed : 0); return;
        case 13: { Val v = arg(); R[pp] = VI(o ? (o->vflags >> (vi(&v) & 31)) & 1 : 0); return; }
        case 14: *r = VI(o ? (o->animForced >= 0 ? o->animForced : o->anim) : 0); return; // current animation number
        case 15: { int c = 0; if (t->oil >= 0) { OI *x = oi_get(t->oi); c = x ? x->nobj : 0; } else { Qual *q = t->oil == -1 ? NULL : qual(t->oil); if (q) for (int k = 0; k < q->n; k++) c += q->oi[k]->nobj; } *r = VI(c); return; }
        case 16: *r = o ? *oval(o, t->n) : VI(0); return;
        case 19: *r = VS(o && t->n >= 0 && t->n < 10 && o->str[t->n] ? ta_dup(o->str[t->n]) : ""); return;
        case 20: *r = VS("Arial"); return;
        case 21: *r = VI(o && o->oi->tn ? G.font[o->oi->texts[0].font & 63].height : 12); return;
        case 23: *r = VI(o ? o->layer + 1 : 0); return;
        case 25: case 26: { int im = o ? obj_image(o) : -1; if (im < 0 || im >= G.nimg) { *r = VI(o ? (n == 25 ? o->x : o->y) : 0); return; } ImgInfo *ii = &G.img[im]; *r = VI(n == 25 ? o->x + (int)((ii->xa - ii->xs) * o->sx) : o->y + (int)((ii->ya - ii->ys) * o->sy)); return; }
        case 27: *r = VI(o ? 255 - ((o->effect & 0x1000) ? ((unsigned)o->effparam >> 24) : 255) : 0); return;
        case 30: { Val v = arg(); R[pp] = o ? *oval(o, vi(&v)) : VI(0); return; }
        case 31: { Val v = arg(); int k = vi(&v); R[pp] = VS(o && k >= 0 && k < 10 && o->str[k] ? ta_dup(o->str[k]) : ""); return; }
        case 40: case 41: if (!o) { *r = VI(0); return; } obj_rect(o, &l, &tp, &rr, &bb); *r = VI(n == 40 ? rr - l : bb - tp); return;
        case 44: *r = VS(o && o->oi->name ? o->oi->name : ""); return;
        }
    }
    static int warned; if (warned++ < 40) logf_("unhandled expression type %d num %d", ty, n);
    { int save = pp; skip_args(); pp = save; R[pp] = VI(0); }
}

// ---------------- conditions ----------------
static int cmp_param(const Val *a, Param *p) { Val b = eval_param(p); return cmp_val(a, &b, p->cmp); }
static int multivar(MultiVar *m, Obj *o) {
    if (m->mask && (o->vflags & (unsigned)m->mask) != (unsigned)m->values) return 0;
    for (int i = 0; i < m->n; i++) { Val *v = m->v[i].global ? gval(m->v[i].idx) : oval(o, m->v[i].idx); if (!cmp_val(v, &m->v[i].v, m->v[i].op)) return 0; }
    return 1;
}
static int mouse_over(Obj *o) {
    Layer *L = &G.layer[o->layer < G.nlayer ? o->layer : 0];
    int fx = G.mx + (int)(G.winx * L->xc), fy = G.my + (int)(G.winy * L->yc), l, t, r, b;
    obj_rect(o, &l, &t, &r, &b);
    return fx >= l && fx < r && fy >= t && fy < b;
}
// is pixel (px,py) of the frame solid in object o's current image? (box objects and non-images count as solid)
static int solid_at(Obj *o, int px, int py, int im, const uint8_t *m) {
    if (!m) return 1;
    ImgInfo *ii = &G.img[im]; float fx = (px + 0.5f - o->x) , fy = (py + 0.5f - o->y);
    if (o->angle != 0) { float an = o->angle * 3.14159265f / 180.f, c = cosf(an), s = sinf(an), rx = fx * c - fy * s, ry = fx * s + fy * c; fx = rx; fy = ry; }
    int ix = (int)floorf(fx / (o->sx ? o->sx : 1) + ii->xs), iy = (int)floorf(fy / (o->sy ? o->sy : 1) + ii->ys);
    if (ix < 0 || iy < 0 || ix >= ii->w || iy >= ii->h) return 0;
    return (m[iy * ((ii->w + 7) / 8) + (ix >> 3)] >> (ix & 7)) & 1;
}
static const uint8_t *obj_mask(Obj *o, int *im) {
    *im = -1; if (o->type != 2 || (o->oi->flags2 & 4)) return NULL; // OCFLAGS2_COLBOX: box collisions only
    *im = obj_image(o); int full; return *im >= 0 ? img_mask(*im, &full) : NULL;
}
static int overlap(Obj *a, Obj *b) {
    if (a == b || a->layer != b->layer) return 0;
    int l1, t1, r1, b1, l2, t2, r2, b2; obj_rect(a, &l1, &t1, &r1, &b1); obj_rect(b, &l2, &t2, &r2, &b2);
    if (!(l1 < r2 && l2 < r1 && t1 < b2 && t2 < b1)) return 0;
    int ia, ib; const uint8_t *ma = obj_mask(a, &ia), *mb = obj_mask(b, &ib);
    if (!ma && !mb) return 1;
    int x0 = l1 > l2 ? l1 : l2, x1 = r1 < r2 ? r1 : r2, y0 = t1 > t2 ? t1 : t2, y1 = b1 < b2 ? b1 : b2;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (solid_at(a, x, y, ia, ma) && solid_at(b, x, y, ib, mb)) return 1;
    return 0;
}
enum { F_BACK, F_NEAR, F_FRAMEC, F_ANIM, F_COLL, F_CMPX, F_CMPY, F_FLAG, F_FIXED, F_VAR, F_HIDDEN, F_STR, F_MOUSE, F_COUNTER, F_MV, F_TRUE, F_EXT, F_TOUCH };
static int filter(Evt *c, int kind, Val *v, int k) {
    Obj *o = evt_first(c->oil, c->oi); int n = evt_nsel, neg = NEG(c);
    if (!o) return 0;
    Param *p1 = c->np > 1 ? &c->p[1] : &c->p[0];
    // collision partner iteration disturbs the cursor, so collect first
    Obj *list[MAXOBJ]; int ln = 0;
    for (; o && ln < MAXOBJ; o = evt_next()) list[ln++] = o;
    char keep[MAXOBJ];
    for (int i = 0; i < ln; i++) {
        o = list[i]; int r = 0;
        switch (kind) {
        case F_BACK: { int l, t, rr, bb; obj_rect(o, &l, &t, &rr, &bb);
            for (int j = 0; j < G.nlo && !r; j++) { LO *lo = &G.lo[j]; OI *bo = oi_get(lo->oi); if (!bo || bo->type > 1 || bo->bobst != 1 || lo->layer != o->layer) continue;
                int bw = bo->bcx, bh = bo->bcy; if (bo->type == 1 && bo->bimage >= 0 && bo->bimage < G.nimg && (!bw || !bh)) { bw = G.img[bo->bimage].w; bh = G.img[bo->bimage].h; }
                if (!(l < lo->x + bw && lo->x < rr && t < lo->y + bh && lo->y < bb)) continue;
                int im; const uint8_t *m = obj_mask(o, &im); if (!m) { r = 1; break; }
                int x0 = l > lo->x ? l : lo->x, x1 = rr < lo->x + bw ? rr : lo->x + bw, y0 = t > lo->y ? t : lo->y, y1 = bb < lo->y + bh ? bb : lo->y + bh;
                for (int y = y0; y < y1 && !r; y++) for (int x = x0; x < x1; x++) if (solid_at(o, x, y, im, m)) { r = 1; break; } }
            break; }
        case F_NEAR: { int bd = k, l, t, rr, bb; obj_rect(o, &l, &t, &rr, &bb);
            r = l <= G.winx + bd || rr >= G.winx + G.w - bd || t <= G.winy + bd || bb >= G.winy + G.h - bd; break; }
        case F_FRAMEC: { Val x = VI(o->frameForced >= 0 ? o->frameForced : o->frame); r = cmp_val(&x, v, k); break; }
        case F_ANIM: r = (o->animForced >= 0 ? o->animForced : o->anim) == k; break;
        case F_CMPX: { Val x = VI(o->x); r = cmp_val(&x, v, k); break; }
        case F_CMPY: { Val x = VI(o->y); r = cmp_val(&x, v, k); break; }
        case F_FLAG: r = (o->vflags >> (k & 31)) & 1; break;
        case F_FIXED: { Val x = VI((int)o->fixed); r = cmp_val(&x, v, k); break; }
        case F_VAR: r = cmp_val(oval(o, k), v, p1->cmp); break;
        case F_HIDDEN: r = o->hidden; break;
        case F_STR: { Val x = VS(k >= 0 && k < 10 && o->str[k] ? o->str[k] : ""); r = cmp_val(&x, v, p1->cmp); break; }
        case F_MOUSE: r = mouse_over(o); break;
        case F_COUNTER: r = cmp_val(&o->cv, v, k); break;
        case F_MV: r = multivar((MultiVar *)v, o); break;
        case F_TRUE: r = 1; break;
        case F_EXT: r = ext_cond(o, c, k); break;
        case F_TOUCH: r = touch_over(o, k); break;
        }
        keep[i] = neg ? !r : r;
    }
    // rebuild selection: re-walk and delete failing
    o = evt_first(c->oil, c->oi); int idx = 0;
    while (o) { if (idx < ln && !keep[idx]) { n--; evt_delete_current(); } idx++; o = evt_next(); }
    return n > 0;
}
static int cond_collide(Evt *c) {
    Param *p = &c->p[0]; int neg = NEG(c);
    Obj *a[MAXOBJ], *b[MAXOBJ]; int na = 0, nb = 0;
    for (Obj *o = evt_first(c->oil, c->oi); o && na < MAXOBJ; o = evt_next()) a[na++] = o;
    for (Obj *o = evt_first(p->s[0], (uint16_t)p->s[1]); o && nb < MAXOBJ; o = evt_next()) b[nb++] = o;
    if (!na) return 0;
    char ka[MAXOBJ], kb[MAXOBJ]; memset(ka, 0, na); memset(kb, 0, nb ? nb : 1);
    for (int i = 0; i < na; i++) for (int j = 0; j < nb; j++) if (overlap(a[i], b[j])) { ka[i] = 1; kb[j] = 1; }
    int cnt = 0, idx = 0;
    Obj *o = evt_first(c->oil, c->oi);
    while (o) { int k = neg ? !ka[idx] : ka[idx]; if (!k) evt_delete_current(); else cnt++; idx++; o = evt_next(); }
    if (!neg && cnt) { idx = 0; o = evt_first(p->s[0], (uint16_t)p->s[1]); while (o) { if (idx < nb && !kb[idx]) evt_delete_current(); idx++; o = evt_next(); } }
    return cnt > 0;
}
static const char *cname(Evt *c) { // constant-name cache for loop/event names
    if (c->name) return c->name;
    Param *p = &c->p[0];
    if (p->ntok == 2 && p->tok[0].type == -1 && p->tok[0].num == 3) { char *s = fa(strlen(p->tok[0].s) + 1); strcpy(s, p->tok[0].s); for (char *q = s; *q; q++) *q = (char)tolower((unsigned char)*q); return c->name = s; }
    return lower(eval_str(p));
}
static int oi_match(int oil, int oi, Obj *o) {
    if (oil >= 0) return o->oi->handle == oi;
    Qual *q = oil == -1 ? NULL : qual(oil); if (q) for (int k = 0; k < q->n; k++) if (q->oi[k] == o->oi) return 1;
    return 0;
}
static Saved *saved_find(OI *o) { for (int i = nsaved - 1; i >= 0; i--) if (saved[i].oi == o) return &saved[i]; return NULL; }

static int eval_cond(Evt *c, Obj *to, int first) {
    int ty = c->type, n = c->num, neg = NEG(c), r;
    if (ty == -1) switch (n) {
        case -1: return 1;
        case -2: case -42: case -10: case -11: case -25: return 0;
        case -3: { Val a = eval_param(&c->p[0]); return cmp_param(&a, &c->p[1]); }
        case -6: if (curG->flags & 1) return 0; curG->flags |= 1; return 1;
        case -7: if (curG->flags & 2) return 1; if (curG->flags & 8) return 0; curG->inhibit = -2; curG->flags |= 2; return 1;
        case -8: { Val *g = gval(c->p[0].tok ? eval_int(&c->p[0]) - 1 : c->p[0].a); return cmp_param(g, &c->p[1]); }
        case -12: r = !(G.g[c->p[0].a < 0 ? 0 : c->p[0].a].flags & 0x4000); return neg ? !r : r;
        case -16: if (!first || strcmp(cname(c), curLoop)) return 0; actOn = 0; return 1;
        case -23: if (curG->flags & 1) return neg; curG->flags |= 1; return !neg;
        case -28: case -29: case -30: case -31: case -32: case -33: case -34: case -35: case -36: case -37: case -38: case -39: {
            static const int ops[6] = {0, 1, 2, 3, 4, 5}; Val b = eval_param(&c->p[1]);
            return cmp_val(gval(c->p[0].a), &b, ops[(-28 - n) % 6]); }
        case -40: r = (c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a) == 3; return neg ? !r : r;
        case -43: { Param *p = &c->p[0]; int *ois = p->ptr;
            int exp[256], ne = 0;
            for (int i = 0; i < p->a; i++) { if (ois[i] & 0x8000) { for (int k = 0; k < G.nq; k++) if (G.q[k].qoi == ois[i]) for (int m = 0; m < G.q[k].n && ne < 256; m++) exp[ne++] = G.q[k].oi[m]->handle; } else if (ne < 256) exp[ne++] = ois[i]; }
            for (int i = 0; i < ne; i++) { OI *o = oi_get(exp[i]); Saved *s = o ? saved_find(o) : NULL; if (!s) continue;
                o->evcount = evCount; o->selfirst = -1; o->nsel = 0;
                for (int k = s->n - 1; k >= 0; k--) { Obj *x = s->o[k]; if (G.obj[x->idx] != x) continue; x->nextsel = o->selfirst; o->selfirst = x->idx; o->nsel++; } }
            return !neg; }
    }
    else if (ty == -4) switch (n) {
        case -8: { Param *p = &c->p[1]; int per = c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a;
            if (p->b == 0) { p->a = per; p->b = -1; return 0; }
            p->a -= G.timerDelta; if (p->a <= 0) { p->a += per; return 1; } return 0; }
        case -7: { int t = c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a; Param *p = &c->p[1];
            if (G.timer < t) return 0;
            if (p->a == G.loop) { p->a = G.loop + 1; return 0; } p->a = G.loop + 1; return 1; }
        case -6: return !strcasecmp(c->p[0].ntok ? eval_str(&c->p[0]) : "", curTimerName);
        case -1: return G.timer > (c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a);
        case -2: return G.timer < (c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a);
    }
    else if (ty == -3) return n == -1 || n == -2;
    else if (ty == -6) switch (n) {
        case -8: r = c->p[0].a == 1 ? G.mdown : 0; return neg ? !r : r;
        case -5: return first && curClick == (c->p[0].a & 0xFF) && !(c->p[0].a >> 8);
        case -7: { if (!first || curClick != (c->p[0].a & 0xFF) || (c->p[0].a >> 8) || !curClickObj) return 0; Param *p = &c->p[1];
            if (p->s[0] >= 0) { if ((uint16_t)p->s[1] != curClickOi) return 0; }
            else { Qual *q = qual(p->s[0]); int f = 0; if (q) for (int k = 0; k < q->n; k++) if (q->oi[k]->handle == curClickOi) f = 1; if (!f) return 0; }
            evt_force_one(p->s[0], (uint16_t)p->s[1], curClickObj); return 1; }
        case -4: { Evt tmp = *c; tmp.oil = c->p[0].s[0]; tmp.oi = (uint16_t)c->p[0].s[1]; return filter(&tmp, F_MOUSE, NULL, 0); }
        case -2: case -1: return neg; // keyboard keys: not available
        default: return 0;
    }
    else if (ty == -2) switch (n) {
        case -9: r = snd_chan_paused(eval_int(&c->p[0]) - 1); return neg ? !r : r;
        case -8: r = !snd_chan_playing(eval_int(&c->p[0]) - 1); return neg ? !r : r;
        case -1: r = !snd_sample_playing(c->p[0].tok ? snd_find(eval_str(&c->p[0])) : c->p[0].a); return neg ? !r : r;
        default: return 0;
    }
    else if (ty >= 2) {
        if (n <= -81) {
            int xn = -n - 81;
            if (ty == 7) { Val v = eval_param(&c->p[0]); return filter(c, F_COUNTER, &v, c->p[0].cmp); }
            if (ty < 32) return 0;
            if (first && to) { // triggered extension condition
                if (!oi_match(c->oil, c->oi, to)) return 0;
                inTrigger = 1; r = ext_cond(to, c, xn); inTrigger = 0; if (neg) r = !r;
                if (r) evt_force_one(c->oil, c->oi, to);
                return r;
            }
            if (!(c->flags & 0x20) && first) return 0; // trigger-only condition outside its trigger
            return filter(c, F_EXT, NULL, xn);
        }
        switch (n) {
        case -2: { if (!first || !to || to->animDone - 1 != c->p[0].a || !oi_match(c->oil, c->oi, to)) return 0; evt_force_one(c->oil, c->oi, to); return 1; }
        case -1: { Val v = eval_param(&c->p[0]); return filter(c, F_FRAMEC, &v, c->p[0].cmp); }
        case -23: return filter(c, F_BACK, NULL, 0);
        case -22: return filter(c, F_NEAR, NULL, c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a);
        case -3: return filter(c, F_ANIM, NULL, c->p[0].a);
        case -4: return cond_collide(c);
        case -7: return filter(c, F_TRUE, NULL, 0);
        case -16: case -17: { Val v = eval_param(&c->p[0]); return filter(c, n == -17 ? F_CMPX : F_CMPY, &v, c->p[0].cmp); }
        case -24: case -25: {
            if (c->p[0].code == 68) return filter(c, F_MV, (Val *)c->p[0].ptr, 0);
            int k = eval_int(&c->p[0]); Evt tmp = *c; if (n == -24) tmp.flags2 ^= 1; return filter(&tmp, F_FLAG, NULL, k); }
        case -26: { Val v = eval_param(&c->p[0]); return filter(c, F_FIXED, &v, c->p[0].cmp); }
        case -27: case -42: case -43: { int k = c->p[0].tok ? eval_int(&c->p[0]) : c->p[0].a; Val v = eval_param(&c->p[1]); return filter(c, F_VAR, &v, k); }
        case -28: return filter(c, F_HIDDEN, NULL, 0);
        case -29: { Evt tmp = *c; tmp.flags2 ^= 1; return filter(&tmp, F_HIDDEN, NULL, 0); }
        case -32: case -33: { int cnt = 0; if (c->oil >= 0) { OI *x = oi_get(c->oi); cnt = x ? x->nobj : 0; } else { Qual *q = c->oil == -1 ? NULL : qual(c->oil); if (q) for (int k = 0; k < q->n; k++) cnt += q->oi[k]->nobj; }
            if (n == -33) return neg ? cnt != 0 : cnt == 0;
            Val v = VI(cnt); return cmp_param(&v, &c->p[0]); }
        case -34: { Obj *o = evt_first(c->oil, c->oi); if (!o || evt_nsel <= 0) return 0; int k = frnd(evt_nsel);
            while (k-- > 0 && o) o = evt_next(); if (!o) return 0;
            if (c->np > 0 && c->p[0].code == 68 && !multivar(c->p[0].ptr, o)) return 0;
            evt_force_one(c->oil, c->oi, o); return 1; }
        case -36: { Val v = eval_param(&c->p[1]); return filter(c, F_STR, &v, c->p[0].a); }
        case -41: { if (!first || !to || strcasecmp(cname(c), curForEach)) return 0;
            if (!oi_match(c->oil, c->oi, to)) return 0;
            if (c->np > 1 && c->p[1].code == 68 && !multivar(c->p[1].ptr, to)) return 0;
            evt_force_one(c->oil, c->oi, to); return 1; }
        }
    }
    static int warned; if (warned++ < 60) logf_("unhandled condition type %d num %d (frame %d)", ty, n, G.fi);
    return 0;
}

// ---------------- actions ----------------
static void grp_set(int gi, int on);
static void run_list(int code, Obj *to);
static void set_gvar(Evt *a, int mode) {
    int k = a->p[0].tok ? eval_int(&a->p[0]) - 1 : a->p[0].a; Val v = eval_param(&a->p[1]); Val *d = gval(k);
    if (mode == 0) *d = v; else if (d->t == 0 && v.t == 0) d->i += mode * v.i; else *d = VD(vd(d) + mode * vd(&v));
}
static int get_dir(int mask) { if (!mask) return 0; int c = 0, k; for (k = 0; k < 32; k++) if ((unsigned)mask >> k & 1) c++; int pick = frnd(c); for (k = 0; k < 32; k++) if ((unsigned)mask >> k & 1) { if (!pick--) return k; } return 0; }
static int read_pos(Pos *ps, int *x, int *y, int *layer, int *dir) {
    *layer = -1; *dir = -1;
    if (ps->parent == 0xFFFF) { *x = ps->x; *y = ps->y; *layer = ps->layer >= G.nlayer ? G.nlayer - 1 : ps->layer; if (!(ps->flags & 8)) *dir = get_dir(ps->dir); return 1; }
    enablePick = 0; Obj *o = cur_objects(ps->oil, ps->parent); if (!o) return 0;
    *x = o->x; *y = o->y; *layer = o->layer;
    if ((ps->flags & 2) && o->type == 2) { int im = obj_image(o); if (im >= 0 && im < G.nimg) { *x += (int)((G.img[im].xa - G.img[im].xs) * o->sx); *y += (int)((G.img[im].ya - G.img[im].ys) * o->sy); } }
    *x += ps->x; *y += ps->y;
    if (ps->flags & 4) *dir = o->dir; else if (!(ps->flags & 8)) *dir = get_dir(ps->dir);
    return 1;
}
static void set_text(Obj *o, const char *s) { if (o->text && !strcmp(o->text, s)) return; free(o->text); o->text = strdup(s); o->tdirty = 1; }
static void set_counter(Obj *o, Val v) {
    double d = vd(&v); if (d < o->cmin) d = o->cmin; if (d > o->cmax) d = o->cmax;
    if (v.t == 1) o->cv = VD(d); else o->cv = VI((int)d);
}
static void exec_act(Evt *a) {
    int ty = a->type, n = a->num; Obj *o;
    if (ty == -1) switch (n) {
        case 0: case 44: return;
        case 3: case 27: case 28: case 29: case 30: set_gvar(a, 0); return;
        case 5: case 31: case 32: case 33: case 34: set_gvar(a, 1); return;
        case 35: case 36: case 37: case 38: set_gvar(a, -1); return;
        case 6: if (a->p[0].a >= 0) grp_set(a->p[0].a, 1); return;
        case 7: if (a->p[0].a >= 0) grp_set(a->p[0].a, 0); return;
        case 14: {
            const char *nm = lower(eval_str(&a->p[0])); int cnt = eval_int(&a->p[1]); if (!*nm) return;
            Loop *l = find_loop(nm, 1); if (!l) return; l->stop = 0; int inf = cnt < 0;
            const char *sl = curLoop; int sal = actLoop, salc = actLoopCount; Group *sg = curG; Tok *sct = ct; int spp = pp; Param *scp = childParam; int li = (int)(l - loops);
            char nmc[80]; snprintf(nmc, sizeof nmc, "%s", nm);
            for (l->index = 0; inf || l->index < cnt; l->index++) {
                curLoop = nmc; actOn = 0; run_list(CODE(-1, -16), NULL); l = &loops[li];
                if (l->stop) break; if (inf && l->index > 100000) break;
            }
            curLoop = sl; actLoop = sal; actLoopCount = salc; curG = sg; ct = sct; pp = spp; childParam = scp; actOn = 1; return; }
        case 15: { Loop *l = find_loop(lower(eval_str(&a->p[0])), 0); if (l) l->stop = 1; return; }
        case 43: childParam = &a->p[0]; return;
    }
    else if (ty == -4 && n == 1) { add_timer_event(a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a, eval_str(&a->p[1])); return; }
    else if (ty == -3) switch (n) {
        case 2: { int f;
            if (a->p[0].code == 26) { int hd = a->p[0].a; f = (hd >= 0 && hd < G.nfrh) ? G.frhandle[hd] : -1; if (f < 0 || f >= G.nframes) return; }
            else { f = eval_int(&a->p[0]) - 1; if (f < 0 || f >= G.nframes) return; }
            logf_("jump to frame %d from frame %d group %d at %d ms", f, G.fi, curG ? (int)(curG - G.g) : -1, G.timer); G.quit = 3; G.quitParam = f; return; }
        case 7: { int x, y, l, d; if (read_pos(a->p[0].ptr, &x, &y, &l, &d)) { G.winx = x - G.w / 2; G.winy = y - G.h / 2; } break; }
        case 8: G.winx = eval_int(&a->p[0]) - G.w / 2; break;
        case 9: G.winy = eval_int(&a->p[0]) - G.h / 2; break;
        default: return;
    }
    else if (ty == -2) switch (n) {
        case 0: snd_play(a->p[0].tok ? snd_find(eval_str(&a->p[0])) : a->p[0].a, -1, 1); return;
        case 1: snd_stop_all(); return;
        case 11: snd_play(a->p[0].tok ? snd_find(eval_str(&a->p[0])) : a->p[0].a, eval_int(&a->p[1]) - 1, 1); return;
        case 12: snd_play(a->p[0].tok ? snd_find(eval_str(&a->p[0])) : a->p[0].a, eval_int(&a->p[1]) - 1, eval_int(&a->p[2])); return;
        case 15: snd_stop_chan(eval_int(&a->p[0]) - 1); return;
        case 17: snd_set_vol(eval_int(&a->p[0]) - 1, eval_int(&a->p[1])); return;
        case 20: snd_set_main(eval_int(&a->p[0])); return;
        default: return;
    }
    else if (ty == -5 && n == 0) {
        int x, y, l, d; Param *p = &a->p[0];
        if (!read_pos(p->ptr, &x, &y, &l, &d)) return;
        if (repeatFlag && ((Pos *)p->ptr)->parent != 0xFFFF) { a->flags |= 1; actLoop = 1; }
        o = obj_create(p->b, x, y, l, 0, -1); if (!o) return;
        if (d >= 0) o->dir = d;
        if (l >= 0 && l < G.nlayer && G.layer[l].hidden) o->hidden = 1;
        evt_add_current(o); return;
    }
    else if (ty >= 2) {
        if (n >= 80 && ty >= 32) { o = act_obj(a); if (o) ext_act(o, a, n - 80); return; }
        o = act_obj(a); if (!o) return;
        switch (n) {
        case 0: return;
        case 6: path_speed(o, a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a); return;
        case 13: path_init(o, a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a); return;
        case 1: { int x, y, l, d; if (read_pos(a->p[0].ptr, &x, &y, &l, &d)) { o->x = x; o->y = y; if (d >= 0) o->dir = d & 31; } return; }
        case 2: o->x = eval_int(&a->p[0]); return;
        case 3: o->y = eval_int(&a->p[0]); return;
        case 17: anim_force(o, a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a); return;
        case 18: o->dirForced = a->p[0].tok ? (eval_int(&a->p[0]) & 31) : get_dir(a->p[0].a); return;
        case 19: o->speedForced = a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a; return;
        case 23: o->dir = a->p[0].tok ? (eval_int(&a->p[0]) & 31) : get_dir(a->p[0].a); return;
        case 24: obj_destroy(o); return;
        case 26: o->hidden = 1; o->flash = 0; return;
        case 27: o->hidden = 0; o->flash = 0; return;
        case 28: o->hidden = 1; o->flash = a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a; o->flashcpt = o->flash; return;
        case 31: {
            int k = a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a; if (k < 0) return; Val v = eval_param(&a->p[1]); Val *d = oval(o, k); *d = v.t == 2 ? VI(0) : v; return; }
        case 32: case 33: { int k = a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a; if (k < 0) return; Val v = eval_param(&a->p[1]); Val *d = oval(o, k); int m = n == 32 ? 1 : -1;
            if (d->t == 0 && v.t == 0) d->i += m * v.i; else *d = VD(vd(d) + m * vd(&v)); return; }
        case 35: o->vflags |= 1u << (eval_int(&a->p[0]) & 31); return;
        case 36: o->vflags &= ~(1u << (eval_int(&a->p[0]) & 31)); return;
        case 37: o->vflags ^= 1u << (eval_int(&a->p[0]) & 31); return;
        case 40: { int k = a->p[0].tok ? eval_int(&a->p[0]) : a->p[0].a; AnimDir *ad = NULL; (void)ad;
            if (k < 0) k = 0; o->frameForced = k; o->frame = k; return; }
        case 41: o->frameForced = -1; return;
        case 49: { int k = a->p[0].a; const char *s = eval_str(&a->p[1]); if (k >= 0 && k < 10) { free(o->str[k]); o->str[k] = strdup(s); } return; }
        case 57: o->z = ++G.zmax; return;
        case 58: o->z = --G.zmin; return;
        case 60: { enablePick = 0; Obj *b = cur_objects(a->p[0].s[0], (uint16_t)a->p[0].s[1]); if (b) o->z = b->z + 0.5; return; }
        case 61: { int l = eval_int(&a->p[0]) - 1; if (l >= 0 && l < G.nlayer) { o->layer = l; o->z = ++G.zmax; } return; }
        case 65: { int v = 255 - eval_int(&a->p[0]); if (v < 0) v = 0; if (v > 255) v = 255;
            int rgb = (o->effect & 0x1000) ? (o->effparam & 0xFFFFFF) : 0xFFFFFF; int bop = o->effect & 0xFFF;
            o->effect = 0x1000 | (bop == 1 ? 0 : bop); o->effparam = rgb | (v << 24); return; }
        case 76: { const char *nm = ta_dup(eval_str(&a->p[0])); FE *f = NULL;
            for (int i = 0; i < nfe; i++) if (!strcasecmp(fes[i].name, nm)) f = &fes[i];
            if (!f) { if (nfe >= 16) return; f = &fes[nfe++]; f->name = nm; f->n = 0; f->o = (Obj **)ta(sizeof(Obj *) * MAXOBJ); }
            if (f->n < MAXOBJ) f->o[f->n++] = o; return; }
        case 80: if (o->type == 7) set_counter(o, eval_param(&a->p[0])); return;
        case 81: case 82: if (o->type == 7) { Val v = eval_param(&a->p[0]); double s = n == 81 ? 1 : -1;
            if (o->cv.t == 0 && v.t == 0) set_counter(o, VI(o->cv.i + (int)s * v.i)); else set_counter(o, VD(vd(&o->cv) + s * vd(&v))); } return;
        case 83: if (o->type == 3) { o->tcolor = a->p[0].a; o->tdirty = 1; } else if (o->type == 7) o->cmin = eval_int(&a->p[0]); return;
        case 84: if (o->type == 7) { o->cmax = eval_int(&a->p[0]); set_counter(o, o->cv); } else if (o->type == 3) { int k = eval_int(&a->p[0]); if (k >= 0 && k < o->oi->tn) { o->tpara = k; set_text(o, o->oi->texts[k].text); } } return;
        case 85: if (o->type == 2) { Val v = eval_param(&a->p[0]); o->sx = o->sy = (float)vd(&v); } return;
        case 86: if (o->type == 2) { Val v = eval_param(&a->p[0]); o->sx = (float)vd(&v); } return;
        case 87: if (o->type == 2) { Val v = eval_param(&a->p[0]); o->sy = (float)vd(&v); } return;
        case 88: if (o->type == 2) { Val v = eval_param(&a->p[0]); o->angle = (float)vd(&v); } else if (o->type == 3) set_text(o, eval_str(&a->p[0])); return;
        }
        if (n >= 80) return;
    }
    if (ty == -3) { // scrolling clamp
        if (G.winx > G.fw - G.w) G.winx = G.fw - G.w; if (G.winx < 0) G.winx = 0;
        if (G.winy > G.fh - G.h) G.winy = G.fh - G.h; if (G.winy < 0) G.winy = 0; return;
    }
    static int warned; if (warned++ < 60) logf_("unhandled action type %d num %d (frame %d)", ty, n, G.fi);
}

static int run_group(int gi, Obj *to, int code);
static void exec_children(Param *p, int gi) {
    int base = nsaved; int *ois = p->ptr; sdepth++;
    int exp[256], ne = 0;
    for (int i = 0; i < p->a; i++) { if (ois[i] & 0x8000) { for (int k = 0; k < G.nq; k++) if (G.q[k].qoi == ois[i]) for (int m = 0; m < G.q[k].n && ne < 256; m++) exp[ne++] = G.q[k].oi[m]->handle; } else if (ne < 256) exp[ne++] = ois[i]; }
    for (int i = 0; i < ne && nsaved < 256; i++) {
        OI *o = oi_get(exp[i]); if (!o || o->evcount != evCount) continue;
        Saved *s = &saved[nsaved++]; s->oi = o; s->n = 0; s->o = (Obj **)ta(sizeof(Obj *) * (o->nsel + 1)); s->depth = sdepth;
        for (int k = o->selfirst; k >= 0 && s->n < o->nsel; k = G.obj[k]->nextsel) s->o[s->n++] = G.obj[k];
    }
    int lvl = G.g[gi].level;
    for (int j = gi + 1; j < G.ng; j++) {
        Group *c = &G.g[j];
        if (!(c->flags & 0x8000) || c->level <= lvl) break;
        if (c->level != lvl + 1) continue;
        if (run_group(j, NULL, -1)) break;
    }
    nsaved = base; sdepth--;
}
int fp_trace;
static void call_actions(Group *g, int gi) {
    childParam = NULL;
    if (fp_trace) logf_("  fire G%d loop %d", gi, G.loop);
    if (g->flags & 30) {
        if (g->flags & 2) { int i = G.loop, i2 = g->inhibit; g->inhibit = i; if (i == i2 || i - 1 == i2) return; }
        if (g->flags & 4) { if (g->inhibitcpt == 0) return; g->inhibitcpt--; }
        if (g->flags & 8) { int t = G.timer / 10; if (g->inhibitcpt && t < g->inhibitcpt) return; g->inhibitcpt = t + g->inhibit; }
    }
    actCount++; actLoop = 0; actLoopCount = 0; actOn = 1; nfe = 0;
    for (int i = 0; i < g->na; i++) { Evt *a = &g->a[i]; if (!(a->flags & 0x88)) { a->flags &= ~1; exec_act(a); } }
    while (actLoop) {
        actLoop = 0; actLoopCount++;
        for (int i = 0; i < g->na; i++) { Evt *a = &g->a[i]; if (a->flags & 1) { a->flags &= ~1; exec_act(a); } }
        if (actLoopCount > 5000) break;
    }
    Param *cp = childParam; childParam = NULL; actOn = 0;
    if (nfe) { // for-each loops collected by this event
        FE loc[16]; int n = nfe; memcpy(loc, fes, sizeof(FE) * n); nfe = 0; const char *sf = curForEach; Group *sg = curG;
        for (int i = 0; i < n; i++) for (int k = 0; k < loc[i].n; k++) { Obj *o = loc[i].o[k]; if (G.obj[o->idx] != o) continue; curForEach = loc[i].name; run_list(CODE(o->type, -41), o); }
        curForEach = sf; curG = sg;
    }
    if (cp) { Group *sg = curG; exec_children(cp, gi); curG = sg; }
}
static int cond_in_list(Evt *c, int code) { return code == -1 ? 1 : code == 0 ? (c->flags & 0x20) != 0 : c->code == code; }
static int run_group(int gi, Obj *to, int code) {
    Group *g = &G.g[gi];
    if ((g->flags & 0x4000) || !g->nc) return 0;
    curG = g;
    if (!(g->flags & 0x400)) {
        evCount++;
        static int trace = -2; if (trace == -2) { const char *e = getenv("FP_TRACE"); trace = e ? atoi(e) : -1; }
        int ok = eval_cond(&g->c[0], to, 1);
        if (gi == trace) logf_("g%d c0 -> %d (nsel %d)", gi, ok, evt_nsel);
        for (int i = 1; ok && i < g->nc; i++) { ok = eval_cond(&g->c[i], NULL, 0); if (gi == trace) logf_("g%d c%d -> %d", gi, i, ok); }
        if (ok && gi == trace) for (int k = 0; k < G.noi; k++) { OI *o = &G.oi[k]; if (o->exists && o->evcount == evCount) { char b[200]; int n = 0; for (int j = o->selfirst; j >= 0 && n < 190; j = G.obj[j]->nextsel) n += snprintf(b + n, 200 - n, "#%d ", j); b[n] = 0; logf_("   sel oi%d n%d: %s", k, o->nsel, n ? b : ""); } }
        if (ok) { call_actions(g, gi); curG = g; if (g->flags & 0x80) return 1; }
        return 0;
    }
    int s = 0;
    while (s < g->nc) { // OR sections
        int e = s; while (e < g->nc && g->c[e].code != CODE(-1, -25) && g->c[e].code != CODE(-1, -24)) e++;
        if (e > s && cond_in_list(&g->c[s], code)) {
            evCount++; curG = g;
            int ok = eval_cond(&g->c[s], to, 1);
            for (int i = s + 1; ok && i < e; i++) ok = eval_cond(&g->c[i], NULL, 0);
            if (ok) { call_actions(g, gi); curG = g; return (g->flags & 0x80) != 0; }
        }
        s = e + 1;
    }
    return 0;
}
static int trig_cmp(const void *a, const void *b) { const int *x = a, *y = b; return x[0] != y[0] ? (x[0] < y[0] ? -1 : 1) : x[1] - y[1]; }
static void run_list(int code, Obj *to) {
    if (code == 0) { for (int i = 0; i < nalways; i++) if (run_group(always[i], NULL, 0)) return; return; }
    int lo = 0, hi = ntrig;
    while (lo < hi) { int m = (lo + hi) / 2; if (trig[m][0] < code) lo = m + 1; else hi = m; }
    for (; lo < ntrig && trig[lo][0] == code; lo++) { if (run_group(trig[lo][1], to, code)) return; }
}
// group (de)activation, ported
static int grp_activate(int i) {
    Group *g = &G.g[i]; Param *pg = &g->c[0].p[0]; int on = !(pg->a & 4);
    if (on) g->flags &= ~0x4000;
    int idx = i + 1, depth = 1;
    while (idx < G.ng) {
        Group *e = &G.g[idx]; int code = e->nc ? e->c[0].code : 0;
        if (code == CODE(-1, -11)) { depth--; if (depth == 0) { if (on) e->flags &= ~0x4000; return idx + 1; } idx++; }
        else if (code == CODE(-1, -10)) {
            Param *p = &e->c[0].p[0];
            if (depth == 1 && on) p->a &= ~4;
            if (!on || (p->a & 8) || depth > 1) { depth++; idx++; } else idx = grp_activate(idx);
        }
        else if (code == CODE(-1, -42)) { e->flags |= 0x4000; idx++; }
        else { if (depth == 1 && on) { e->flags &= ~0x4000; if (code == CODE(-1, -23)) e->flags &= ~1; } idx++; }
    }
    return idx;
}
static int grp_deactivate(int i) {
    Group *g = &G.g[i]; g->flags |= 0x4000;
    int idx = i + 1, depth = 1;
    while (idx < G.ng) {
        Group *e = &G.g[idx]; int code = e->nc ? e->c[0].code : 0;
        if (code == CODE(-1, -11)) { depth--; if (depth == 0) { e->flags |= 0x4000; return idx + 1; } idx++; }
        else if (code == CODE(-1, -10)) {
            Param *p = &e->c[0].p[0]; int was = !(p->a & 4);
            if (depth == 1) p->a |= 4;
            if (was && !(p->a & 8) && depth == 1) idx = grp_deactivate(idx); else { depth++; idx++; }
        }
        else { if (depth == 1) e->flags |= 0x4000; idx++; }
    }
    return idx;
}
static void grp_set(int gi, int on) {
    Param *pg = &G.g[gi].c[0].p[0];
    if (on) { int was = pg->a & 8; pg->a &= ~8; if (was) grp_activate(gi); }
    else { int was = !(pg->a & 8); pg->a |= 8; if (was && !(pg->a & 4)) grp_deactivate(gi); }
}

void add_timer_event(int ms, const char *name) {
    for (int i = 0; i < 64; i++) if (i >= ntev || tev[i].dead) { tev[i].time = G.timer + ms; snprintf(tev[i].name, 80, "%s", name); tev[i].dead = 0; if (i >= ntev) ntev = i + 1; return; }
}
void evt_frame_init(void) {
    nloops = 0; ntev = 0; nsaved = 0; sdepth = 0; curLoop = ""; curTimerName = ""; curForEach = ""; childParam = NULL; nfe = 0;
    always = fa(sizeof(int) * (G.ng + 1)); nalways = 0; trig = fa(sizeof(int[2]) * (G.ng * 3 + 4)); ntrig = 0;
    for (int i = 0; i < G.ng; i++) {
        Group *g = &G.g[i]; g->inhibit = 0; g->inhibitcpt = 0;
        if (!g->nc || (g->flags & 0x8000)) continue;
        int c0 = g->c[0].code;
        if (c0 == CODE(-1, -10) || c0 == CODE(-1, -11)) continue;
        int al = 0, s = 0, added = 0;
        while (s < g->nc) {
            Evt *c = &g->c[s];
            if (c->flags & 0x20) al = 1; else if (added < 3) { trig[ntrig][0] = c->code; trig[ntrig][1] = i; ntrig++; added++; }
            if (!(g->flags & 0x400)) break;
            while (s < g->nc && g->c[s].code != CODE(-1, -25) && g->c[s].code != CODE(-1, -24)) s++;
            s++;
        }
        if (al) always[nalways++] = i;
    }
    qsort(trig, ntrig, sizeof(int[2]), trig_cmp);
    for (int i = 0; i < G.ng; i++) { // initial group states
        Group *g = &G.g[i];
        if (g->nc && g->c[0].code == CODE(-1, -10)) { Param *p = &g->c[0].p[0]; if ((p->a & 1) && !(p->a & 8)) { p->a |= 8; if (!(p->a & 4)) grp_deactivate(i); } }
    }
}
void run_start(void) { run_list(CODE(-3, -1), NULL); }
void run_end(void) { run_list(CODE(-3, -2), NULL); }
void run_timers(void) {
    run_list(CODE(-4, -3), NULL); run_list(CODE(-4, -2), NULL); run_list(CODE(-4, -1), NULL);
    for (int i = 0; i < ntev; i++) if (!tev[i].dead && G.timer >= tev[i].time) { tev[i].dead = 1; char nm[80]; strcpy(nm, tev[i].name); curTimerName = nm; run_list(CODE(-4, -6), NULL); curTimerName = ""; }
}
void run_always(void) { run_list(0, NULL); }
void run_obj_trigger(Obj *o, int num, int p0) { (void)p0; run_list(CODE(o->type, num), o); }
void gen_event(Obj *o, int extcond) { run_list(CODE(o->type, -(extcond + 81)), o); }
void run_click(int button, int dbl) {
    (void)dbl; curClick = button; curClickObj = NULL; curClickOi = -1;
    run_list(CODE(-6, -5), NULL);
    Obj *hit[64]; int nh = 0;
    for (int i = 0; i < G.nobjslots && nh < 64; i++) { Obj *o = G.obj[i]; if (o && !o->dead && (o->type == 2 || o->type == 3 || o->type == 7) && mouse_over(o)) hit[nh++] = o; }
    for (int i = 0; i < nh; i++) { curClickObj = hit[i]; curClickOi = hit[i]->oi->handle; run_list(CODE(-6, -7), NULL); if (G.quit) break; }
    curClick = -1; curClickObj = NULL;
}
