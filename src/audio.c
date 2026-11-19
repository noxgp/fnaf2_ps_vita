// sound channels + software mixer (wav / ogg / mp3), 44.1 kHz stereo output
#include "fp.h"
#include <pthread.h>
#define DR_MP3_NO_STDIO
#include "dr_mp3.h"
#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
#define NCH 48
typedef struct Snd { uint8_t *data; int size, kind, tried; const uint8_t *pcm; int pcmbytes, rate, ch, bits, tag, align; } Snd;
typedef struct Chan { int active, handle, loops, paused, kind; Snd *s; stb_vorbis *v; drmp3 mp3; int mp3ok; int pos; long played; int rate, ch;
    int16_t buf[4096]; int bn; double bp; } Chan;
static Snd *snds; static Chan ch[NCH]; static float vol[NCH], mainvol = 1; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
void snd_init(void) { snds = pa(sizeof(Snd) * (G.nsnd + 1)); for (int i = 0; i < NCH; i++) vol[i] = 1; }
int snd_find(const char *name) { for (int i = 0; i < G.nsnd; i++) if (G.sndname[i] && !strcasecmp(G.sndname[i], name)) return i; return -1; }
static Snd *snd_load(int h) {
    if (h < 0 || h >= G.nsnd) return NULL;
    Snd *s = &snds[h]; if (s->data) return s; if (s->tried) return NULL; s->tried = 1;
    static const char *ext[3] = {"wav", "ogg", "mp3"}; char path[400]; FILE *f = NULL; int k;
    for (k = 0; k < 3 && !f; k++) { snprintf(path, sizeof path, "%s/s%04d.%s", G.datadir, h, ext[k]); f = fopen(path, "rb"); if (f) break; }
    if (!f) { logf_("sound %d not found", h); return NULL; }
    fseek(f, 0, SEEK_END); s->size = (int)ftell(f); fseek(f, 0, SEEK_SET); s->data = malloc(s->size + 4);
    if (fread(s->data, 1, s->size, f) != (size_t)s->size) { fclose(f); free(s->data); s->data = NULL; return NULL; }
    fclose(f); s->kind = k + 1;
    if (s->kind == 1) { // RIFF parse
        const uint8_t *d = s->data, *e = d + s->size, *p = d + 12; s->rate = 44100; s->ch = 1; s->bits = 16;
        while (p + 8 <= e) { uint32_t cs = p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24);
            if (!memcmp(p, "fmt ", 4)) { s->tag = p[8] | (p[9] << 8); s->align = p[20] | (p[21] << 8); s->ch = p[10] | (p[11] << 8); s->rate = p[12] | (p[13] << 8) | (p[14] << 16); s->bits = p[22] | (p[23] << 8); }
            else if (!memcmp(p, "data", 4)) { s->pcm = p + 8; s->pcmbytes = (p + 8 + cs > e) ? (int)(e - p - 8) : (int)cs; break; }
            p += 8 + cs + (cs & 1); }
        if (!s->pcm || s->ch < 1) { s->pcmbytes = 0; s->ch = 1; }
    }
    return s;
}
void snd_preload_all(void) { for (int i = 0; i < G.nsnd; i++) snd_load(i); }
static void chan_close(Chan *c) { if (c->v) { stb_vorbis_close(c->v); c->v = NULL; } if (c->mp3ok) { drmp3_uninit(&c->mp3); c->mp3ok = 0; } c->active = 0; }
static int chan_open(Chan *c) {
    Snd *s = c->s; c->pos = 0; c->bn = 0; c->bp = 0;
    if (s->kind == 1) { c->rate = s->rate; c->ch = s->ch; return s->pcmbytes > 0; }
    if (s->kind == 2) { int err = 0; if (c->v) stb_vorbis_close(c->v); c->v = stb_vorbis_open_memory(s->data, s->size, &err, NULL); if (!c->v) return 0; stb_vorbis_info in = stb_vorbis_get_info(c->v); c->rate = in.sample_rate; c->ch = in.channels; return 1; }
    if (c->mp3ok) drmp3_uninit(&c->mp3);
    c->mp3ok = drmp3_init_memory(&c->mp3, s->data, s->size, NULL); if (!c->mp3ok) return 0;
    c->rate = c->mp3.sampleRate; c->ch = c->mp3.channels; return 1;
}
static const int ima_step[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int ima_idx[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static inline int ima_nib(int nib, int *pred, int *idx) {
    int st = ima_step[*idx], d = st >> 3;
    if (nib & 1) d += st >> 2; if (nib & 2) d += st >> 1; if (nib & 4) d += st; if (nib & 8) d = -d;
    int p = *pred + d; if (p > 32767) p = 32767; if (p < -32768) p = -32768; *pred = p;
    int i = *idx + ima_idx[nib]; if (i < 0) i = 0; if (i > 88) i = 88; *idx = i; return p;
}
static int ima_block(const uint8_t *b, int len, int ch, int16_t *out, int maxf) {
    if (ch < 1 || ch > 2 || len < 4 * ch) return 0;
    int pred[2], idx[2];
    for (int c = 0; c < ch; c++) { pred[c] = (int16_t)(b[c * 4] | (b[c * 4 + 1] << 8)); idx[c] = b[c * 4 + 2] > 88 ? 88 : b[c * 4 + 2]; }
    int f = 0; out[0] = (int16_t)pred[0]; out[1] = (int16_t)pred[ch - 1]; f = 1;
    const uint8_t *p = b + 4 * ch, *e = b + len;
    if (ch == 1) {
        for (; p < e && f + 1 < maxf; p++) { int v = ima_nib(*p & 15, &pred[0], &idx[0]); out[f * 2] = out[f * 2 + 1] = (int16_t)v; f++;
            v = ima_nib(*p >> 4, &pred[0], &idx[0]); out[f * 2] = out[f * 2 + 1] = (int16_t)v; f++; }
    } else {
        for (; p + 8 <= e && f + 8 <= maxf; p += 8, f += 8) for (int c = 0; c < 2; c++) for (int k = 0; k < 4; k++) {
            int by = p[c * 4 + k];
            out[(f + k * 2) * 2 + c] = (int16_t)ima_nib(by & 15, &pred[c], &idx[c]);
            out[(f + k * 2 + 1) * 2 + c] = (int16_t)ima_nib(by >> 4, &pred[c], &idx[c]);
        }
    }
    return f;
}
// decode up to n stereo frames at source rate; returns frames produced (0 = end)
static int chan_decode(Chan *c, int16_t *out, int n) {
    Snd *s = c->s;
    if (s->kind == 1 && s->tag == 0x11) { // IMA ADPCM: one block per call
        int nblk = s->align ? s->pcmbytes / s->align + (s->pcmbytes % s->align ? 1 : 0) : 0;
        if (c->pos >= nblk) return 0;
        const uint8_t *b = s->pcm + (size_t)c->pos * s->align; int len = s->pcmbytes - c->pos * s->align; if (len > s->align) len = s->align;
        c->pos++;
        return ima_block(b, len, s->ch, out, n);
    }
    if (s->kind == 1) {
        int bps = s->bits / 8 * s->ch, total = bps ? s->pcmbytes / bps : 0, got = 0;
        while (got < n && c->pos < total) {
            const uint8_t *p = s->pcm + (size_t)c->pos * bps; int l, r;
            if (s->bits == 8) { l = (p[0] - 128) << 8; r = s->ch > 1 ? (p[1] - 128) << 8 : l; }
            else { l = (int16_t)(p[0] | (p[1] << 8)); r = s->ch > 1 ? (int16_t)(p[2] | (p[3] << 8)) : l; }
            out[got * 2] = (int16_t)l; out[got * 2 + 1] = (int16_t)r; got++; c->pos++;
        }
        return got;
    }
    if (s->kind == 2) return stb_vorbis_get_samples_short_interleaved(c->v, 2, out, n * 2);
    if (c->ch == 2) return (int)drmp3_read_pcm_frames_s16(&c->mp3, n, out);
    int16_t tmp[1024]; if (n > 1024) n = 1024; int g = (int)drmp3_read_pcm_frames_s16(&c->mp3, n, tmp);
    for (int i = 0; i < g; i++) out[i * 2] = out[i * 2 + 1] = tmp[i];
    return g;
}
void snd_play(int handle, int chan, int loops) {
    Snd *s = snd_load(handle); if (!s) return;
    pthread_mutex_lock(&mu);
    if (chan < 0) { for (int i = 0; i < NCH; i++) if (!ch[i].active) { chan = i; break; } if (chan < 0) chan = NCH - 1; }
    if (chan >= NCH) chan = NCH - 1;
    Chan *c = &ch[chan]; chan_close(c);
    c->s = s; c->handle = handle; c->loops = loops; c->paused = 0; c->played = 0; c->kind = s->kind;
    c->active = chan_open(c);
    pthread_mutex_unlock(&mu);
}
void snd_stop_chan(int n) { if (n < 0 || n >= NCH) return; pthread_mutex_lock(&mu); chan_close(&ch[n]); pthread_mutex_unlock(&mu); }
void snd_stop_all(void) { pthread_mutex_lock(&mu); for (int i = 0; i < NCH; i++) chan_close(&ch[i]); pthread_mutex_unlock(&mu); }
void snd_stop_sample(int h) { pthread_mutex_lock(&mu); for (int i = 0; i < NCH; i++) if (ch[i].active && ch[i].handle == h) chan_close(&ch[i]); pthread_mutex_unlock(&mu); }
void snd_set_vol(int n, int v) { if (n < 0 || n >= NCH) return; if (v < 0) v = 0; if (v > 100) v = 100; vol[n] = v / 100.f; }
void snd_set_main(int v) { if (v < 0) v = 0; if (v > 100) v = 100; mainvol = v / 100.f; }
void snd_pause_all(int p) { for (int i = 0; i < NCH; i++) if (ch[i].active) ch[i].paused = p; }
int snd_chan_playing(int n) { return n >= 0 && n < NCH && ch[n].active && !ch[n].paused; }
int snd_chan_paused(int n) { return n >= 0 && n < NCH && ch[n].active && ch[n].paused; }
int snd_sample_playing(int h) { for (int i = 0; i < NCH; i++) if (ch[i].active && ch[i].handle == h) return 1; return 0; }
int snd_chan_pos(int n) { if (n < 0 || n >= NCH || !ch[n].active || !ch[n].rate) return 0; return (int)(ch[n].played * 1000 / ch[n].rate); }
const char *snd_chan_name(int n) { if (n < 0 || n >= NCH || !ch[n].active || !G.sndname[ch[n].handle]) return ""; return G.sndname[ch[n].handle]; }
void snd_mix(int16_t *out, int frames) {
    static int32_t acc[2 * 2048]; if (frames > 2048) frames = 2048;
    memset(acc, 0, sizeof(int32_t) * 2 * frames);
    pthread_mutex_lock(&mu);
    for (int k = 0; k < NCH; k++) {
        Chan *c = &ch[k]; if (!c->active || c->paused) continue;
        double step = c->rate / 44100.0; int v = (int)(vol[k] * mainvol * 256);
        for (int i = 0; i < frames; i++) {
            while ((int)c->bp >= c->bn) {
                c->bp -= c->bn; c->played += c->bn;
                c->bn = chan_decode(c, c->buf, 2048);
                if (c->bn <= 0) {
                    c->bn = 0;
                    if (c->loops == 0 || --c->loops > 0) { long pl = c->played; if (!chan_open(c)) { chan_close(c); break; } c->played = 0; (void)pl; c->bn = chan_decode(c, c->buf, 2048); if (c->bn <= 0) { chan_close(c); break; } }
                    else { chan_close(c); break; }
                }
            }
            if (!c->active) break;
            int idx = (int)c->bp; acc[i * 2] += c->buf[idx * 2] * v; acc[i * 2 + 1] += c->buf[idx * 2 + 1] * v; c->bp += step;
        }
    }
    pthread_mutex_unlock(&mu);
    if (out) for (int i = 0; i < frames * 2; i++) { int s = acc[i] >> 8; out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }
}
