// PS Vita backend: vita2d rendering, texture memory pool, touch + buttons, audio thread
#include "fp.h"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/io/stat.h>
#include <psp2/touch.h>
#include <psp2/ctrl.h>
#include <psp2/audioout.h>
#include <psp2/power.h>
#include <psp2/rtc.h>
#include <psp2/apputil.h>
#include <psp2/gxm.h>
#include <pthread.h>
#include <vita2d.h>

int _newlib_heap_size_user = 112 * 1024 * 1024;
unsigned int sceUserMainThreadStackSize = 2 * 1024 * 1024;

#define SCR_W 960
#define SCR_H 544
struct Tex { vita2d_texture vt; void *mem; unsigned size; int w, h; };

// ---------------- GPU memory pool (first fit, address ordered, coalescing) ----------------
typedef struct Blk { char *p; unsigned size; int used; struct Blk *next; } Blk;
static Blk *blocks; static long poolTotal;
static void pool_add(SceKernelMemBlockType type, unsigned size) {
    while (size >= 8u << 20) {
        SceUID uid = sceKernelAllocMemBlock("fp2_tex", type, size, NULL);
        if (uid >= 0) {
            void *mem; sceKernelGetMemBlockBase(uid, &mem);
            if (sceGxmMapMemory(mem, size, SCE_GXM_MEMORY_ATTRIB_READ | SCE_GXM_MEMORY_ATTRIB_WRITE) < 0) { sceKernelFreeMemBlock(uid); return; }
            Blk *b = malloc(sizeof *b); b->p = mem; b->size = size; b->used = 0;
            Blk **pp = &blocks; while (*pp && (*pp)->p < b->p) pp = &(*pp)->next; b->next = *pp; *pp = b;
            poolTotal += size; logf_("texture pool +%u MB (type %x)", size >> 20, type); return;
        }
        size -= 4u << 20;
    }
}
static void *pool_alloc(unsigned size) {
    size = (size + 255) & ~255u;
    for (Blk *b = blocks; b; b = b->next) if (!b->used && b->size >= size) {
        if (b->size > size + 256) { Blk *r = malloc(sizeof *r); r->p = b->p + size; r->size = b->size - size; r->used = 0; r->next = b->next; b->next = r; b->size = size; }
        b->used = 1; return b->p;
    }
    return NULL;
}
static void pool_free(void *p) {
    Blk *prev = NULL;
    for (Blk *b = blocks; b; prev = b, b = b->next) if (b->p == p) {
        b->used = 0;
        if (b->next && !b->next->used && b->p + b->size == b->next->p) { Blk *n = b->next; b->size += n->size; b->next = n->next; free(n); }
        if (prev && !prev->used && prev->p + prev->size == b->p) { prev->size += b->size; prev->next = b->next; free(b); }
        return;
    }
}
long plat_pool_bytes(void) { return poolTotal; }

static struct Tex *pendingFree[4096]; static int npending, drawing;
void plat_tex_free(struct Tex *t) { if (!t) return; if (npending < 4096) pendingFree[npending++] = t; else { pool_free(t->mem); free(t); } }
static void flush_frees(void) { for (int i = 0; i < npending; i++) { pool_free(pendingFree[i]->mem); free(pendingFree[i]); } npending = 0; }
struct Tex *plat_tex(int w, int h, int fmt, const uint8_t *px, int stride) {
    SceGxmTextureFormat gf; int bpp;
    switch (fmt) {
    case 1: gf = SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_RGBA; bpp = 2; break;
    case 2: gf = SCE_GXM_TEXTURE_FORMAT_U5U5U5U1_RGBA; bpp = 2; break;
    case 4: gf = SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB; bpp = 2; break;
    case 8: gf = SCE_GXM_TEXTURE_FORMAT_P8_ABGR; bpp = 1; break;
    default: gf = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR; bpp = 4; break;
    }
    int aw = (w + 7) & ~7; unsigned isize = ((unsigned)aw * h * bpp + 255) & ~255u, size = isize + (fmt == 8 ? 1024 : 0);
    void *mem = pool_alloc(size);
    if (!mem && drawing && npending) { flush_frees(); mem = pool_alloc(size); } // safe: the GPU finished every earlier frame
    if (!mem) return NULL;
    const uint8_t *pal = NULL; if (fmt == 8) { pal = px; px += 1024; }
    for (int y = 0; y < h; y++) memcpy((char *)mem + (size_t)y * aw * bpp, px + (size_t)y * stride, (size_t)w * bpp);
    if (pal) memcpy((char *)mem + isize, pal, 1024);
    struct Tex *t = calloc(1, sizeof *t); t->mem = mem; t->size = size; t->w = w; t->h = h;
    sceGxmTextureInitLinear(&t->vt.gxm_tex, mem, gf, w, h, 0);
    if (pal) sceGxmTextureSetPalette(&t->vt.gxm_tex, (char *)mem + isize);
    sceGxmTextureSetMinFilter(&t->vt.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetMagFilter(&t->vt.gxm_tex, SCE_GXM_TEXTURE_FILTER_LINEAR);
    sceGxmTextureSetUAddrMode(&t->vt.gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    sceGxmTextureSetVAddrMode(&t->vt.gxm_tex, SCE_GXM_TEXTURE_ADDR_CLAMP);
    return t;
}

// ---------------- drawing ----------------
static float S, SX, OX, OY; static int inRT; static vita2d_texture *rt; static int rtW, rtH, curBlend = -1;
// Options > display mode (global value 16, like the Android version): 0 = stretch to fill the 16:9 screen, 1 = keep 4:3 with black bars
static int keep43(void) { return G.ngv > 16 && G.gv[16].t != 2 && vi(&G.gv[16]) == 1; }
static void set_xf(int rtmode) { inRT = rtmode; if (rtmode) { OX = 0; OY = 0; SX = S; } else if (keep43()) { SX = S; OX = (SCR_W - G.w * S) / 2; OY = (SCR_H - G.h * S) / 2; } else { SX = (float)SCR_W / G.w; OX = 0; OY = (SCR_H - G.h * S) / 2; } }
static void clip_game(void) { // keep everything inside the 4:3 game area (objects outside the frame must not show in the side bars)
    vita2d_enable_clipping(); vita2d_set_clip_rectangle((int)OX, (int)OY, (int)(OX + G.w * SX + 0.5f), (int)(OY + G.h * S + 0.5f)); }
static unsigned col(unsigned rgba) { return RGBA8(rgba >> 24, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255); }
static void blend(int b) { if (b != curBlend) { vita2d_set_blend_mode_add(b); curBlend = b; } }
static uint64_t tDraw0; static double accLogic, accDraw, accWait; static int nFrames, nTicks;
void plat_begin(int off) {
    uint64_t w0 = sceKernelGetProcessTimeWide();
    vita2d_wait_rendering_done(); flush_frees(); // the previous frame is finished: its vertices and freed textures can be reused
    tDraw0 = sceKernelGetProcessTimeWide(); accWait += (tDraw0 - w0) / 1000.0; drawing = 1;
    vita2d_pool_reset(); curBlend = -1;
    if (off && rt) { vita2d_start_drawing_advanced(rt, SCE_GXM_SCENE_FRAGMENT_SET_DEPENDENCY); set_xf(1); }
    else { vita2d_start_drawing_advanced(NULL, 0); set_xf(0); }
    vita2d_disable_clipping(); blend(0);
}
void plat_clear(unsigned rgb) {
    vita2d_set_clear_color(RGBA8(rgb >> 16, (rgb >> 8) & 255, rgb & 255, 255)); vita2d_clear_screen();
    if (!inRT) { vita2d_set_clear_color(RGBA8(0, 0, 0, 255)); // pillarbox bars
        vita2d_draw_rectangle(0, 0, OX, SCR_H, RGBA8(0, 0, 0, 255)); vita2d_draw_rectangle(SCR_W - OX, 0, OX + 1, SCR_H, RGBA8(0, 0, 0, 255)); clip_game(); }
}
void plat_quad(struct Tex *t, float u0, float v0, float u1, float v1, const float *xy, unsigned rgba, int b) {
    vita2d_texture_vertex *v = vita2d_pool_memalign(4 * sizeof(vita2d_texture_vertex), sizeof(vita2d_texture_vertex)); if (!v) return;
    static const int ord[4] = {0, 1, 3, 2}; float us[4] = {u0, u1, u1, u0}, vs_[4] = {v0, v0, v1, v1};
    for (int i = 0; i < 4; i++) { int k = ord[i]; v[i].x = OX + xy[k * 2] * SX; v[i].y = OY + xy[k * 2 + 1] * S; v[i].z = 0.5f; v[i].u = us[k]; v[i].v = vs_[k]; }
    blend(b); vita2d_draw_array_textured(&t->vt, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, 4, col(rgba));
}
void plat_rect(float x, float y, float w, float h, unsigned rgba) { blend(0); vita2d_draw_rectangle(OX + x * SX, OY + y * S, w * SX, h * S, col(rgba)); }
// panorama: the scene drawn so far (in the render target) is redrawn to the screen in vertical strips, each squeezed like the original shader
void plat_persp(float x, float y, float w, float h, float fb) {
    if (!inRT) return;
    vita2d_end_drawing();
    vita2d_start_drawing_advanced(NULL, SCE_GXM_SCENE_VERTEX_WAIT_FOR_DEPENDENCY); set_xf(0);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255)); vita2d_clear_screen(); blend(0);
    const int N = 96; float rw = (float)vita2d_texture_get_width(rt), rh = (float)vita2d_texture_get_height(rt);
    vita2d_texture_vertex *v = vita2d_pool_memalign((N + 1) * 2 * sizeof(vita2d_texture_vertex), sizeof(vita2d_texture_vertex)); if (!v) return;
    for (int i = 0; i <= N; i++) {
        float s = (float)i / N, fc = 1.0f + sinf(s * 3.1415f) * fb - fb;
        float gx = x + s * w, ut = (gx * S) / rw, vt0 = (y * S + (1 - fc) / 2 * h * S) / rh, vt1 = (y * S + ((1 - fc) / 2 + fc) * h * S) / rh;
        v[i * 2] = (vita2d_texture_vertex){OX + gx * SX, OY + y * S, 0.5f, ut, vt0};
        v[i * 2 + 1] = (vita2d_texture_vertex){OX + gx * SX, OY + (y + h) * S, 0.5f, ut, vt1};
    }
    clip_game();
    vita2d_draw_array_textured(rt, SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, v, (N + 1) * 2, RGBA8(255, 255, 255, 255));
}
// rumble: works with a DualShock on PlayStation TV; the handheld Vita has no motor, so the call does nothing there
static uint64_t rumbleEnd;
void plat_vibrate(int ms) {
    if (ms <= 0) return; SceCtrlActuator act; memset(&act, 0, sizeof act); act.small = 1; act.large = 200;
    for (int port = 1; port <= 4; port++) sceCtrlSetActuator(port, &act);
    rumbleEnd = sceKernelGetProcessTimeWide() + (uint64_t)ms * 1000;
}
static void rumble_tick(void) {
    if (rumbleEnd && sceKernelGetProcessTimeWide() >= rumbleEnd) { SceCtrlActuator act; memset(&act, 0, sizeof act); for (int port = 1; port <= 4; port++) sceCtrlSetActuator(port, &act); rumbleEnd = 0; }
}
void plat_present(void) { rumble_tick(); if (inRT) plat_persp(0, 0, G.w, G.h, 0); vita2d_end_drawing(); vita2d_swap_buffers(); drawing = 0;
    accDraw += (sceKernelGetProcessTimeWide() - tDraw0) / 1000.0; nFrames++; }
unsigned plat_ms(void) { return (unsigned)(sceKernelGetProcessTimeWide() / 1000); }

// ---------------- audio ----------------
static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp; int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, 1024, 44100, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) { logf_("audio port error %x", port); return 0; }
    int vol[2] = {SCE_AUDIO_VOLUME_0DB, SCE_AUDIO_VOLUME_0DB}; sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vol);
    static int16_t buf[2][1024 * 2]; int k = 0;
    for (;;) { snd_mix(buf[k], 1024); sceAudioOutOutput(port, buf[k]); k ^= 1; }
    return 0;
}

// ---------------- input ----------------
typedef struct VBtn { unsigned mask; int x, y, slot; const char *obj; int mode; } VBtn; // mode: 0 any, 1 office only, 2 cameras only, 3 cam 11 only, 4 only while obj is visible
static VBtn vbtn[] = {
    {SCE_CTRL_LTRIGGER | SCE_CTRL_L1, 267, 724, 4, NULL, 0},   // mask bar
    {SCE_CTRL_RTRIGGER | SCE_CTRL_R1, 756, 724, 4, NULL, 0},   // camera bar
    {SCE_CTRL_SELECT | SCE_CTRL_START, 0, 0, 4, "mute call", 4}, // mute call (only while the button is shown, otherwise the tap would turn the office view)
    {SCE_CTRL_TRIANGLE, 233, 595, 7, NULL, 3},                 // wind the music box (cam 11, hold)
    {SCE_CTRL_CROSS, 490, 345, 5, NULL, 1},                    // flashlight down the hall (hold)
    {SCE_CTRL_CROSS, 232, 296, 5, NULL, 2},                    // flashlight on the camera (hold)
    {SCE_CTRL_LEFT, 40, 640, 5, NULL, 1},                      // look left (hold)
    {SCE_CTRL_RIGHT, 984, 640, 5, NULL, 1},                    // look right (hold)
    {SCE_CTRL_SQUARE, 0, 0, 6, "lightLeftHitbox", 1},          // left vent light (hold)
    {SCE_CTRL_CIRCLE, 0, 0, 6, "lightRightHitbox", 1},         // right vent light (hold)
};
// camera map buttons (screen position of CAM 01..12)
static const short camPos[12][2] = {{602,565},{726,566},{602,491},{729,491},{604,646},{727,646},{758,419},{603,416},{915,390},{847,497},{966,465},{941,570}};
static int tapTicks;
// screen position of a named object in the current frame (for buttons that follow the scrolling office)
static Obj *find_obj(const char *name) {
    for (int i = 0; i < G.nobjslots; i++) { Obj *o = G.obj[i]; if (o && !o->dead && o->oi->name && !strncmp(o->oi->name, name, strlen(name))) return o; }
    return NULL;
}
static int obj_screen_pos(const char *name, int *x, int *y) {
    Obj *o = find_obj(name); if (!o) return 0;
    int l, t, r, b; obj_rect(o, &l, &t, &r, &b); float xc = o->layer < G.nlayer ? G.layer[o->layer].xc : 1, yc = o->layer < G.nlayer ? G.layer[o->layer].yc : 1;
    (void)xc; (void)yc; *x = (l + r) / 2 - G.winx; *y = (t + b) / 2 - G.winy; return 1; // objects are in frame space
}
static int cam_view(void) { Obj *o = find_obj("viewing"); return (o && o->type == 7 && G.fi == 3) ? vi(&o->cv) : 0; } // 0 = office, else camera number
// pick the camera button nearest in the pressed direction
static int cam_step(int cur, int dx, int dy) {
    if (cur < 1 || cur > 12) return -1;
    int best = -1; float bs = 1e9f, cx = camPos[cur - 1][0], cy = camPos[cur - 1][1];
    for (int i = 0; i < 12; i++) { if (i == cur - 1) continue;
        float ax = camPos[i][0] - cx, ay = camPos[i][1] - cy, along = ax * dx + ay * dy, perp = fabsf(ax * dy - ay * dx);
        if (along <= 8 || perp > along * 2.5f) continue;
        float s = along + perp * 2; if (s < bs) { bs = s; best = i; } }
    return best;
}
static int tid[4] = {-1, -1, -1, -1};
static void read_input(unsigned *prevButtons) {
    for (int i = 0; i < MAXTOUCH; i++) { G.in.t[i].isnew = 0; G.in.t[i].ended = 0; }
    SceTouchData td; memset(&td, 0, sizeof td); sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
    int seen[4] = {0, 0, 0, 0};
    for (unsigned r = 0; r < td.reportNum && r < 8; r++) {
        int id = td.report[r].id, slot = -1;
        for (int s = 0; s < 4; s++) if (tid[s] == id) slot = s;
        if (slot < 0) for (int s = 0; s < 4; s++) if (tid[s] < 0) { slot = s; tid[s] = id; G.in.t[s].isnew = 1; G.in.t[s].down = 1; break; }
        if (slot < 0) continue;
        seen[slot] = 1;
        float sx = td.report[r].x / 2.0f, sy = td.report[r].y / 2.0f; set_xf(0);
        G.in.t[slot].x = (int)((sx - OX) / SX); G.in.t[slot].y = (int)((sy - OY) / S);
    }
    for (int s = 0; s < 4; s++) if (tid[s] >= 0 && !seen[s]) { tid[s] = -1; G.in.t[s].down = 0; G.in.t[s].ended = 1; }
    SceCtrlData pad; memset(&pad, 0, sizeof pad); sceCtrlPeekBufferPositive(0, &pad, 1);
    unsigned b = pad.buttons;
    static int stickOk; if (pad.lx > 64 && pad.lx < 192) stickOk = 1; // only trust the stick once it reported a centred value
    if (stickOk) { if (pad.lx < 50) b |= SCE_CTRL_LEFT; if (pad.lx > 205) b |= SCE_CTRL_RIGHT; if (pad.ly < 50) b |= SCE_CTRL_UP; if (pad.ly > 205) b |= SCE_CTRL_DOWN; }
    unsigned pressed = b & ~*prevButtons, released = ~b & *prevButtons;
    int view = cam_view();
    // minigames: the arrows hold the on-screen joystick (vjoy_dpad) in the pressed direction
    { static int vjOn; Touch *t = &G.in.t[5]; int cx, cy; Obj *vj = find_obj("vjoy_dpad");
      int dx = ((b & SCE_CTRL_RIGHT) ? 1 : 0) - ((b & SCE_CTRL_LEFT) ? 1 : 0), dy = ((b & SCE_CTRL_DOWN) ? 1 : 0) - ((b & SCE_CTRL_UP) ? 1 : 0);
      if (G.fi != 3 && vj && !vj->hidden && (dx || dy) && obj_screen_pos("vjoy_dpad", &cx, &cy)) {
          if (!t->down) t->isnew = 1; t->down = 1; int rad = 64; if (G.ngv > 19 && G.gv[19].t != 2) { int js = vi(&G.gv[19]); if (js >= 40 && js <= 200) rad = 64 * js / 100; } // follows Options > Joystick Size (100 = default)
          t->x = cx + dx * rad; t->y = cy + dy * rad; t->owner = 98; vjOn = 1;
      } else if (vjOn) { vjOn = 0; if (t->down && t->owner == 98) { t->down = 0; t->ended = 1; t->owner = 0; } }
      if (G.fi != 3) { *prevButtons = b; return; } }
    // a short tap on slot 4 releases by itself
    if (tapTicks > 0 && --tapTicks == 0 && G.in.t[4].down && G.in.t[4].owner == 99) { G.in.t[4].down = 0; G.in.t[4].ended = 1; G.in.t[4].owner = 0; }
    if (view > 0) { // in the cameras the arrows move between cameras
        static const unsigned dm[4] = {SCE_CTRL_LEFT, SCE_CTRL_RIGHT, SCE_CTRL_UP, SCE_CTRL_DOWN}; static const int ddx[4] = {-1, 1, 0, 0}, ddy[4] = {0, 0, -1, 1};
        for (int k = 0; k < 4; k++) if (pressed & dm[k]) { int c = cam_step(view, ddx[k], ddy[k]); Touch *t = &G.in.t[4];
            if (c >= 0 && !t->down) { t->down = 1; t->isnew = 1; t->x = camPos[c][0]; t->y = camPos[c][1]; t->owner = 99; tapTicks = 3; } }
    }
    for (unsigned i = 0; i < sizeof vbtn / sizeof vbtn[0]; i++) {
        VBtn *v = &vbtn[i]; Touch *t = &G.in.t[v->slot];
        if (pressed & v->mask) {
            if (G.fi != 3 && v->mode != 0 && v->mode != 4) continue;
            if ((v->mode == 1 && view != 0) || (v->mode == 2 && view == 0) || (v->mode == 3 && view != 11)) continue;
            if (v->mode == 4) { Obj *m = find_obj(v->obj); if (!m || m->hidden) continue; }
            int x = v->x, y = v->y; if (v->obj && !obj_screen_pos(v->obj, &x, &y)) continue;
            if (t->down) t->ended = 1; t->down = 1; t->isnew = 1; t->x = x; t->y = y; t->owner = (int)i + 1;
        }
        else if ((released & v->mask) && t->down && t->owner == (int)i + 1) { t->down = 0; t->ended = 1; t->owner = 0; }
    }
    // taps (non-hold buttons) are released on the next frame
    *prevButtons = b;
}

// shown when no game data is installed
static void missing_data(void) {
    static const char *lines[] = {"Game data not found.", "", "Build it from your own FNaF 2 Android APK with", "tools/build_data.py (see README on GitHub)", "and copy the 'game' folder to:", "ux0:data/fnaf2/game/", "", "Press START to exit."};
    struct Tex *tx[8] = {0}; int tw[8], th[8];
    for (int i = 0; i < 8; i++) { if (!lines[i][0]) continue; int mw, mh; font_render(26, lines[i], 0, 0, 0, 0, &mw, &mh, 1, 0); uint8_t *px = font_render(26, lines[i], mw + 4, mh, 0, 0xFFFFFF, &tw[i], &th[i], 0, 0); if (px) { tx[i] = plat_tex(tw[i], th[i], 0, px, tw[i] * 4); free(px); } }
    for (;;) {
        SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1); if (pad.buttons & SCE_CTRL_START) break;
        vita2d_wait_rendering_done(); vita2d_start_drawing(); vita2d_clear_screen();
        for (int i = 0; i < 8; i++) if (tx[i]) vita2d_draw_texture(&tx[i]->vt, (SCR_W - tw[i]) / 2, 120 + i * 40);
        vita2d_end_drawing(); vita2d_swap_buffers();
    }
}

int main(void) {
    scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222); scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
    sceIoMkdir("ux0:data/fnaf2", 0777); sceIoMkdir("ux0:data/fnaf2/save", 0777);
    snprintf(G.savedir, sizeof G.savedir, "ux0:data/fnaf2/save");
    vita2d_init_advanced(2 * 1024 * 1024); vita2d_set_vblank_wait(1);
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    G.rng = (unsigned)sceKernelGetProcessTimeWide();
    // panorama render target: allocate it before the texture pool takes the video memory, otherwise it silently fails
    rt = vita2d_create_empty_texture_rendertarget(SCR_W, SCR_H, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
    pool_add(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 88u << 20);
    pool_add(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, 48u << 20);
    font_init("app0:data/font.ttf");
    // game data: inside the VPK (personal builds) or in ux0:data/fnaf2/game (made from your own APK with tools/build_data.py)
    if (app_load("app0:data") && app_load("ux0:data/fnaf2/game")) { logf_("data missing"); missing_data(); sceKernelExitProcess(0); return 0; }
    snprintf(G.savedir, sizeof G.savedir, "ux0:data/fnaf2/save");
    S = (float)SCR_H / G.h; if (G.w * S > SCR_W) S = (float)SCR_W / G.w;
    rtW = (int)(G.w * S + 0.5f); rtH = (int)(G.h * S + 0.5f);
    if (!rt) logf_("panorama render target could not be created");
    cache_set_budget(poolTotal - (12L << 20));
    snd_init(); snd_preload_all();
    SceUID ath = sceKernelCreateThread("fp2_audio", audio_thread, 0x10000100 - 10, 256 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
    if (ath >= 0) sceKernelStartThread(ath, 0, NULL); else logf_("audio thread error %x", ath);
    frame_start(0);
    unsigned prevButtons = 0; double acc = 0, tacc = 0; uint64_t last = sceKernelGetProcessTimeWide(), perfT0 = last;
    const double step = 1000000.0 / (G.fps > 0 ? G.fps : 60);
    for (;;) {
        sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
        uint64_t now = sceKernelGetProcessTimeWide(); acc += (double)(now - last); last = now;
        if (acc > step * 4) acc = step * 4;
        int n = 0;
        while (acc >= step * 0.98 || n == 0) {
            read_input(&prevButtons);
            tacc += step; G.timerDelta = (int)(tacc / 1000.0); tacc -= G.timerDelta * 1000.0;
            uint64_t l0 = sceKernelGetProcessTimeWide(); frame_tick(); accLogic += (sceKernelGetProcessTimeWide() - l0) / 1000.0; n++; nTicks++;
            acc -= step; if (acc < 0) acc = 0;
            if (n >= 3) { acc = 0; break; }
        }
        frame_draw();
        if (nFrames >= 300) {
            logf_("perf frame %d: %.1f fps, logic %.2f ms/tick, %.2f ticks/frame, draw %.2f ms, gpu wait %.2f ms, textures %ld KB, loads %d (%.0f ms)",
                G.fi, nFrames * 1000000.0 / (double)(sceKernelGetProcessTimeWide() - perfT0), accLogic / nTicks, (double)nTicks / nFrames, accDraw / nFrames, accWait / nFrames,
                cache_bytes() >> 10, img_loads, img_load_ms);
            nFrames = nTicks = 0; accLogic = accDraw = accWait = 0; img_loads = 0; img_load_ms = 0; perfT0 = sceKernelGetProcessTimeWide();
        }
    }
    return 0;
}
