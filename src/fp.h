// fp2 - native player for Clickteam Fusion 2.5 Android data (FNaF 2), for PS Vita + headless test build
#ifndef FP_H
#define FP_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>

typedef struct Val { int t; int i; double d; const char *s; } Val; // t: 0 int, 1 double, 2 string
static inline Val VI(int i) { Val v = {0, i, 0, 0}; return v; }
static inline Val VD(double d) { Val v = {1, 0, d, 0}; return v; }
static inline Val VS(const char *s) { Val v = {2, 0, 0, s ? s : ""}; return v; }
static inline double vd(const Val *v) { return v->t == 1 ? v->d : v->t == 0 ? (double)v->i : 0.0; }
static inline int vi(const Val *v) { return v->t == 0 ? v->i : v->t == 1 ? (int)v->d : 0; }
static inline const char *vs(const Val *v) { return v->t == 2 ? v->s : ""; }

typedef struct Tok { int code; short type, num; uint16_t oi; short oil, n; int i; double d; char *s; void *cache; } Tok;
typedef struct MultiVar { int flags, mask, values, n; struct { int idx, op, global; Val v; } v[4]; } MultiVar;
typedef struct Pos { uint16_t parent, flags; short x, y, slope, angle; int dir; short typeParent, oil, layer; } Pos;
typedef struct Param { short code, cmp; int ntok; Tok *tok; int a, b; short s[4]; char *str; void *ptr; } Param;
typedef struct Evt { int code; short type, num; uint16_t oi; short oil; uint8_t flags, flags2, np; Param *p; char *name; } Evt;
typedef struct Group { uint16_t flags; int level, nc, na, line; Evt *c, *a; int inhibit, inhibitcpt; } Group;

typedef struct AnimDir { uint8_t min, max; short repeat, repframe, n; short *frames; } AnimDir;
typedef struct Anim { AnimDir *dir[32]; } Anim;
typedef struct TextDef { short font; uint16_t flags; int color; char *text; } TextDef;
struct Obj;
typedef struct PathStep { int speed, dir, dx, dy, cos, sin, len, pause; } PathStep;
typedef struct MoveDef { int type, atstart, n, minsp, maxsp, loop, repos, reverse, bspeed, dirs; PathStep *st; } MoveDef;
typedef struct OI {
    short handle, type; uint16_t flags; int ink, inkparam; char *name; int exists, ext_kind;
    int oeflags; short quals[8]; uint16_t flags2, prefs; int nmov; MoveDef *mov;
    int nval, *val, valflags, nstr; char **str;
    int nanim; Anim **anim;
    int has_counter, cinit, cmin, cmax;
    int has_cdisp, ccx, ccy; short cdisp, cdflags, cfont; int cnframes; short *cframes;
    int tcx, tcy, tn; TextDef *texts;
    short bimage; int bcx, bcy, bobst; short bshape, bfill; int bcolor;
    uint8_t *ext; int extsize, extver;
    // runtime selection state
    int first, nobj, evcount, selfirst, nsel;
    int actcount, actn, actcap; struct Obj **actlist;
} OI;

struct Tex;
typedef struct Obj {
    int idx; unsigned fixed; OI *oi; int type;
    int x, y, layer, next, nextsel, hidden, dead; double z;
    int anim, animForced, dir, dirForced, frame, frameForced, speedForced, animCounter, repeats, animDone, image;
    float sx, sy, angle; int effect, effparam;
    int nval; Val *val; char *str[10]; unsigned vflags;
    int tpara, tcolor, tdirty, tw, th; char *text; struct Tex *ttex;
    Val cv; double cmin, cmax;
    int flash, flashcpt, w, h;
    void *ext;
    MoveDef *mv; int mtNum, mtDir, mtSpeed, mtStop, mtPause, mtCalc, mtLen, mtCos, mtSin, mtX0, mtY0, mtX1, mtY1, mtStartX, mtStartY, mtDone, ball, bspeed, mtLX, mtLY; double bx, by;
} Obj;

typedef struct ImgInfo { long off; int size; short mode, w, h, xs, ys, xa, ya; short pw, ph; long moff; int msize, mtried; uint8_t *mask; struct Tex *tex; unsigned used; int bytes; } ImgInfo;
typedef struct LO { short handle, oi; int x, y; short ptype, value, layer; } LO;
typedef struct Layer { int opt; float xc, yc; int nbkd, first; char *name; int hidden; } Layer;
typedef struct Qual { int qoi; int n; OI **oi; int actcount, actn, actcap; struct Obj **actlist; } Qual;
typedef struct EFile { char *name; uint8_t *data; int size; } EFile;
typedef struct FontDef { int height, weight, italic; char name[40]; } FontDef;

#define MAXOBJ 1200
#define MAXTOUCH 8
typedef struct Touch { int down, x, y, isnew, ended, owner; } Touch;
typedef struct Input { Touch t[MAXTOUCH]; unsigned buttons, pressed; int quit; } Input;

typedef struct Game {
    FILE *f, *pak; char datadir[256], savedir[256];
    int w, h, fps, nframes, flags, newflags;
    int ngv; Val *gv;
    int noi; OI *oi;             // indexed by handle
    int nimg; ImgInfo *img;      // indexed by handle
    int nsnd; char **sndname;
    int nef; EFile *ef;
    int next_n; char **extname;
    uint8_t **frdata; int *frsize; short *frhandle; int nfrh;
    FontDef font[64];
    // frame
    int fi, fw, fh, fbg, fflags; char *fname;
    int nlo; LO *lo; int nlayer; Layer *layer; int nq; Qual *q;
    int ng; Group *g;
    Obj *obj[MAXOBJ]; int nobjslots; unsigned createCount; double zmax, zmin;
    int winx, winy, lastwinx, lastwiny;
    int timer, timerDelta, loop, quit, quitParam, started;
    int mx, my, mdown, mclick; // mouse in screen(game) coords
    Input in;
    unsigned rng;
} Game;
extern Game G;

// mem
void *pa(size_t n); void *fa(size_t n); void fa_reset(void); char *ta(size_t n); void ta_reset(void); char *ta_dup(const char *s);
void logf_(const char *fmt, ...);
// load.c
int app_load(const char *datadir);
int frame_parse(int idx);
OI *oi_get(int handle);
// run.c
void frame_start(int idx); void frame_end(void); void frame_tick(void); void frame_draw(void);
Obj *obj_create(int oihandle, int x, int y, int layer, int hidden, int lohandle);
void obj_destroy(Obj *o); const uint8_t *img_mask(int h, int *full);
struct Tex *img_tex(int handle); void cache_set_budget(long b); long cache_bytes(void); extern int img_loads; extern double img_load_ms;
int obj_image(Obj *o); void obj_rect(Obj *o, int *l, int *t, int *r, int *b);
void path_init(Obj *o, int sel); void path_move(Obj *o); void ball_move(Obj *o); void path_speed(Obj *o, int s);
void anim_force(Obj *o, int anim); void anim_restart(Obj *o);
int frnd(int n);
// evt.c
void evt_frame_init(void);
void run_start(void); void run_end(void); void run_always(void); void run_timers(void);
void run_obj_trigger(Obj *o, int num, int p0); void run_click(int button, int dbl);
void add_timer_event(int ms, const char *name);
Val eval_param(Param *p); int eval_int(Param *p); const char *eval_str(Param *p);
Val *get_expr(void); extern Tok *ct; extern Val R[]; extern int pp;
Obj *evt_first(int oil, int oi); Obj *evt_next(void); void evt_delete_current(void); void evt_force_one(int oil, int oi, Obj *o); extern int evt_nsel;
int cmp_val(const Val *a, const Val *b, int op);
void gen_event(Obj *o, int extcond);
// ext.c
enum { XK_NONE, XK_PERSP, XK_INI, XK_CLOCK, XK_KYSO, XK_CALCRECT, XK_LAYER, XK_PARSER, XK_TOUCH, XK_ANDROID, XK_ANDROIDPLUS, XK_STUB };
int ext_kind(const char *name);
void ext_create(Obj *o); void ext_destroy(Obj *o); void ext_handle(Obj *o); void ext_tick_end(void);
int ext_cond(Obj *o, Evt *c, int num); void ext_act(Obj *o, Evt *a, int num); Val ext_exp(Obj *o, int num);
int touch_over(Obj *o, int ti);
// font.c
int font_init(const char *ttf);
uint8_t *font_render(int px, const char *text, int w, int h, int flags, int color, int *outw, int *outh, int measure_only, int maxw);
// audio.c
void snd_init(void); void snd_preload_all(void); void snd_play(int handle, int chan, int loops); void snd_stop_chan(int chan); void snd_stop_all(void); void snd_stop_sample(int handle);
void snd_set_vol(int chan, int vol); void snd_set_main(int vol); int snd_chan_playing(int chan); int snd_chan_paused(int chan); int snd_sample_playing(int handle);
int snd_chan_pos(int chan); const char *snd_chan_name(int chan); void snd_mix(int16_t *out, int frames); void snd_pause_all(int p);
int snd_find(const char *name);
// plat
struct Tex *plat_tex(int w, int h, int fmt, const uint8_t *px, int stride);
void plat_tex_free(struct Tex *t);
void plat_begin(int offscreen); void plat_present(void); void plat_clear(unsigned rgb);
void plat_quad(struct Tex *t, float u0, float v0, float u1, float v1, const float *xy, unsigned rgba, int blend);
void plat_rect(float x, float y, float w, float h, unsigned rgba);
void plat_persp(float x, float y, float w, float h, float fb);
unsigned plat_ms(void);
#endif
