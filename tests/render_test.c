/*
 * Offline test for Heimat.
 *
 *   bash scripts/test.sh            # normal build
 *   SAN=1 bash scripts/test.sh      # AddressSanitizer + UBSan
 *
 * Includes heimat.c directly. Test WAVs are generated into a temp directory
 * (several sample formats, with and without cue markers) and played through
 * the real worker-thread loader.
 */
#define _GNU_SOURCE
#include <unistd.h>
#include "../src/dsp/heimat.c"
#include <ctype.h>

#define SR 44100
#define BLK 128

static host_api_v1_t g_h;
static plugin_api_v2_t *api;
static char g_dir[256];

#define GK24 0.3547f                 /* a gene of about half a second on a 4 s splice */
static int g_fail = 0;
#define CHECK(cond, ...) do { int ok_ = (cond); printf("  [%s] ", ok_ ? "ok" : "FAIL"); \
    printf(__VA_ARGS__); printf("\n"); if (!ok_) g_fail++; } while (0)

/* Checks on time measured with a wall clock. They hold on a quiet computer;
 * on shared CI machines (CI set), with sanitizers, or with HEIMAT_LAX_TIMING
 * a miss is reported but does not fail the run. */
static int timing_lax(void)
{
#if defined(__SANITIZE_ADDRESS__) || defined(HM_SANITIZE)
    return 1;
#else
    return getenv("CI") != NULL || getenv("HEIMAT_LAX_TIMING") != NULL;
#endif
}
#define TCHECK(cond, ...) do { if (!(cond) && timing_lax()) { printf("  [info] "); printf(__VA_ARGS__); \
    printf(" -- tempo non verificato su questa macchina\n"); } else CHECK(cond, __VA_ARGS__); } while (0)

/* ------------------------------------------------------------- WAV writer */

typedef float (*gen_fn)(void *ctx, uint32_t frame, int ch);


static void write_wav(const char *path, int fmt_tag, int bits, int ch, int sr, uint32_t frames,
                      gen_fn gen, void *ctx, const uint32_t *cues, int n_cues, int cue_after_data, int extensible)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int bytes = bits / 8;
    uint32_t data = frames * (uint32_t)ch * (uint32_t)bytes;
    uint32_t fmt_sz = extensible ? 40 : 16;
    uint32_t cue_sz = n_cues ? 8 + 4 + 24 * (uint32_t)n_cues : 0;
    uint32_t riff = 4 + (8 + fmt_sz) + (8 + data + (data & 1)) + cue_sz;
    fwrite("RIFF", 1, 4, f); put32(f, riff); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, fmt_sz);
    put16(f, extensible ? 0xFFFE : (uint16_t)fmt_tag); put16(f, (uint16_t)ch); put32(f, (uint32_t)sr);
    put32(f, (uint32_t)(sr * ch * bytes)); put16(f, (uint16_t)(ch * bytes)); put16(f, (uint16_t)bits);
    if (extensible) {
        put16(f, 22); put16(f, (uint16_t)bits); put32(f, 3);
        put16(f, (uint16_t)fmt_tag);
        static const uint8_t guid[14] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71 };
        fwrite(guid, 1, 14, f);
    }
    for (int pass = 0; pass < 2; pass++) {
        int do_cue = n_cues && ((pass == 0) == !cue_after_data);
        if (do_cue) {
            fwrite("cue ", 1, 4, f); put32(f, 4 + 24 * (uint32_t)n_cues); put32(f, (uint32_t)n_cues);
            for (int i = 0; i < n_cues; i++) {
                put32(f, (uint32_t)i + 1); put32(f, cues[i]); fwrite("data", 1, 4, f);
                put32(f, 0); put32(f, 0); put32(f, cues[i]);
            }
        }
        if (pass == 0) {
            if (!cue_after_data || n_cues == 0) { /* data follows */ }
            fwrite("data", 1, 4, f); put32(f, data);
            for (uint32_t i = 0; i < frames; i++)
                for (int c = 0; c < ch; c++) {
                    float x = gen(ctx, i, c);
                    if (x > 1.0f) x = 1.0f;
                    if (x < -1.0f) x = -1.0f;
                    if (fmt_tag == 3) fwrite(&x, 4, 1, f);
                    else if (bits == 8)  { uint8_t v = (uint8_t)lrintf(x * 127.0f + 128.0f); fwrite(&v, 1, 1, f); }
                    else if (bits == 16) { int16_t v = (int16_t)lrintf(x * 32767.0f); fwrite(&v, 2, 1, f); }
                    else if (bits == 24) { int32_t v = (int32_t)lrintf(x * 8388607.0f); uint8_t b[3] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16) }; fwrite(b, 1, 3, f); }
                    else                 { int32_t v = (int32_t)llrint((double)x * 2147483647.0); fwrite(&v, 4, 1, f); }
                }
            if (data & 1) fputc(0, f);
        }
    }
    fclose(f);
}

/* generators */
typedef struct { double freq, sr, amp; } sine_ctx;
static float gen_sine(void *c, uint32_t i, int ch)
{ (void)ch; sine_ctx *s = (sine_ctx *)c; return (float)(s->amp * sin(2.0 * M_PI * s->freq * (double)i / s->sr)); }

typedef struct { double sr; } tones_ctx;
static float gen_tones(void *c, uint32_t i, int ch)
{
    (void)ch;
    static const double f[4] = { 200, 300, 450, 600 };
    tones_ctx *t = (tones_ctx *)c;
    int seg = (int)((double)i / (2.0 * t->sr)); if (seg > 3) seg = 3;
    return (float)(0.5 * sin(2.0 * M_PI * f[seg] * (double)i / t->sr));
}

typedef struct { double sr; } burst_ctx;
static float gen_burst(void *c, uint32_t i, int ch)
{
    (void)ch; burst_ctx *b = (burst_ctx *)c;
    double t = (double)i / b->sr;
    return (t >= 2.0 && t < 2.2) ? (float)(0.6 * sin(2.0 * M_PI * 1000.0 * t)) : 0.0f;
}


static float gen_steps(void *c, uint32_t i, int ch)                 /* 8 segments of 0.5 s: 200, 300 ... 900 Hz */
{
    (void)ch; (void)c;
    int seg = (int)((double)i / (0.5 * SR)); if (seg > 7) seg = 7;
    return (float)(0.5 * sin(2.0 * M_PI * (200.0 + 100.0 * seg) * (double)i / SR));
}
static float gen_bursts(void *c, uint32_t i, int ch)                /* 0.1 s bursts at 1, 2, 3 s */
{
    (void)ch; (void)c;
    double t = (double)i / SR;
    int on = (t >= 1.0 && t < 1.1) || (t >= 2.0 && t < 2.1) || (t >= 3.0 && t < 3.1);
    return on ? (float)(0.6 * sin(2.0 * M_PI * 1000.0 * t)) : 0.0f;
}

static uint32_t g_nz = 12345u;
static float gen_noise(void *c, uint32_t i, int ch)
{
    (void)c; (void)i; (void)ch;
    g_nz ^= g_nz << 13; g_nz ^= g_nz >> 17; g_nz ^= g_nz << 5;
    return 0.3f * ((float)(int32_t)g_nz * (1.0f / 2147483648.0f));
}

static void mkpath(char *out, size_t n, const char *name) { snprintf(out, n, "%s/%s", g_dir, name); }

/* --------------------------------------------------------------- analysis */

static double goertzel(const float *x, int n, double f)
{
    double w = 2.0 * M_PI * f / SR, c = 2.0 * cos(w), s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));
        double s0 = x[i] * win + c * s1 - s2; s2 = s1; s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static double peak_freq(const float *x, int n, double flo, double fhi, double step)
{
    double best = 0, bf = 0;
    for (double f = flo; f <= fhi; f += step) { double p = goertzel(x, n, f); if (p > best) { best = p; bf = f; } }
    return bf;
}

static double band_e(const float *x, int n, double f, double half)
{
    double e = 0;
    for (double d = -half; d <= half; d += half / 4.0) e += goertzel(x, n, f + d);
    return e;
}

static double rms_db(const float *x, int n)
{
    double ss = 0;
    for (int i = 0; i < n; i++) ss += (double)x[i] * x[i];
    return 20.0 * log10(sqrt(ss / (n > 0 ? n : 1)) + 1e-12);
}

/* share of the energy above lo_hz that is not f0 or one of its harmonics, dB
 * (Hann window, radix-2 FFT; n a power of two) */
static double hiss_above(const float *x, int n, double f0, double lo_hz)
{
    double *re = (double *)calloc((size_t)n, sizeof(double)), *im = (double *)calloc((size_t)n, sizeof(double));
    for (int i = 0; i < n; i++) re[i] = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)));
    for (int i = 1, j = 0; i < n; i++) { int b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; } }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double ur = 1, ui = 0;
            for (int k = 0; k < len / 2; k++) {
                const int a = i + k, b = a + len / 2;
                const double xr = re[b] * ur - im[b] * ui, xi = re[b] * ui + im[b] * ur;
                re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
                const double t = ur * wr - ui * wi; ui = ur * wi + ui * wr; ur = t;
            }
        }
    }
    double tot = 0, hs = 0;
    for (int k = 1; k < n / 2; k++) {
        const double f = (double)k * SR / n, pw = re[k] * re[k] + im[k] * im[k];
        tot += pw;
        const double hn = f / f0, dh = fabs(hn - round(hn)) * f0;
        if (f > lo_hz && dh > 40) hs += pw;
    }
    free(re); free(im);
    return 10.0 * log10(hs / (tot + 1e-30) + 1e-30);
}

static int autocorr_peak(const float *x, int n, int lag_lo, int lag_hi)
{
    double e0 = 0;
    for (int i = 0; i < n - lag_hi; i++) e0 += (double)x[i] * x[i];
    double best = -2; int bl = lag_lo;
    for (int lag = lag_lo; lag <= lag_hi; lag++) {
        double s = 0;
        for (int i = 0; i < n - lag_hi; i++) s += (double)x[i] * x[i + lag];
        if (s / (e0 + 1e-30) > best) { best = s / (e0 + 1e-30); bl = lag; }
    }
    return bl;
}

static double silent_fraction(const float *x, int n)
{
    int z = 0;
    for (int i = 0; i < n; i++) if (fabsf(x[i]) < 1e-4f) z++;
    return (double)z / (double)(n > 0 ? n : 1);
}

/* ---------------------------------------------------------------- driving */
/* ------------------------------------------------ host mocks: input, clocks */
static uint8_t g_mem[8192];
static double g_beat = 0, g_bpm_mock = 120;
static int g_clk_run = 0;
static double g_in_freq = 0, g_in_amp = 0, g_in_ph = 0;
static double g_midi_bpm = 0, g_midi_acc = 0;
static int mock_clock_status(void) { return g_clk_run ? MOVE_CLOCK_STATUS_RUNNING : MOVE_CLOCK_STATUS_STOPPED; }
static double mock_beat(void) { return g_beat; }
static void rt_msg(void *in, int st) { uint8_t m = (uint8_t)st; api->on_midi(in, &m, 1, 0); }

static void feed_block(void *in)
{
    int16_t *mic = (int16_t *)(g_mem + MOVE_AUDIO_IN_OFFSET);
    for (int i = 0; i < BLK; i++) {
        double v = g_in_amp > 0 ? g_in_amp * sin(2.0 * M_PI * g_in_ph) : 0.0;
        g_in_ph += g_in_freq / SR; if (g_in_ph >= 1.0) g_in_ph -= 1.0;
        mic[2 * i] = mic[2 * i + 1] = (int16_t)lrint(v * 32767.0);
    }
    if (g_clk_run) g_beat += (double)BLK / SR * g_bpm_mock / 60.0;
    if (g_midi_bpm > 0) {
        g_midi_acc += BLK;
        double per = SR * 60.0 / g_midi_bpm / 24.0;
        while (g_midi_acc >= per) { g_midi_acc -= per; rt_msg(in, 0xF8); }
    }
}


typedef struct { float *L, *R; int n, cap; } rec_t;
static void rec_init(rec_t *r, double secs)
{
    r->cap = (int)(secs * SR) + BLK;
    r->L = (float *)calloc((size_t)r->cap, sizeof(float));
    r->R = (float *)calloc((size_t)r->cap, sizeof(float));
    r->n = 0;
}
static void rec_free(rec_t *r) { free(r->L); free(r->R); }

static void blocks(void *in, double secs, rec_t *r)
{
    int nb = (int)(secs * SR) / BLK;
    int16_t out[BLK * 2];
    for (int b = 0; b < nb; b++) {
        feed_block(in);
        api->render_block(in, out, BLK);
        if (r && r->n + BLK <= r->cap)
            for (int i = 0; i < BLK; i++) { r->L[r->n + i] = out[2 * i] / 32768.0f; r->R[r->n + i] = out[2 * i + 1] / 32768.0f; }
        if (r) r->n += BLK;
    }
}

static void midi(void *in, int status, int a, int b)
{
    uint8_t m[3] = { (uint8_t)status, (uint8_t)a, (uint8_t)b };
    api->on_midi(in, m, 3, 0);
}
static void key_on(void *in, int n)  { midi(in, 0x90, n, 100); }
static void key_off(void *in, int n) { midi(in, 0x80, n, 0); }
static void setp(void *in, const char *k, const char *v) { api->set_param(in, k, v); }
static void setf(void *in, const char *k, double v) { char b[32]; snprintf(b, sizeof b, "%.5f", v); api->set_param(in, k, b); }
static __attribute__((unused)) void seti(void *in, const char *k, int v) { char b[16]; snprintf(b, sizeof b, "%d", v); api->set_param(in, k, b); }

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }

/* load through the real worker and keep rendering (like the host) until the reel arrives */
static int load_wait(void *in, const char *path, double timeout)
{
    setp(in, "sample_path", path);
    double t0 = now_s();
    int16_t out[BLK * 2];
    while (now_s() - t0 < timeout) {
        api->render_block(in, out, BLK);
        char info[200]; api->get_param(in, "reel_info", info, sizeof info);
        char err[200]; err[0] = 0;
        if (api->get_error(in, err, sizeof err) > 0) return 0;
        if (strstr(info, "state=2") && !strstr(info, "frames=0")) return 1;
        struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL);
    }
    return 0;
}

static void *new_inst(void) { return api->create_instance(".", NULL); }

/* a ready-to-play instance: file loaded, Drone off, Pitch pad mode, full-splice genes */
static void *inst_with(const char *file, int divide)
{
    void *in = new_inst();
    char p[400]; mkpath(p, sizeof p, file);
    if (!load_wait(in, p, 10.0)) { printf("  [FAIL] load di %s\n", file); g_fail++; }
    setp(in, "splice_source", "Divide"); setf(in, "divide", divide);
    return in;
}

static double freq_of(void *in, double secs_settle, double lo, double hi)
{
    rec_t r; rec_init(&r, secs_settle + 0.6);
    blocks(in, secs_settle + 0.5, &r);
    double f = peak_freq(r.L + (int)(secs_settle * SR), (int)(0.5 * SR), lo, hi, (hi - lo) / 2000.0);
    rec_free(&r);
    return f;
}


static __attribute__((unused)) double ui_num(void *in, const char *key)
{
    char js[1024]; api->get_param(in, "ui_state", js, sizeof js);
    float f = 0; json_num(js, key, &f); return f;
}
static int markers_of(void *in, uint32_t *out)
{
    char csv[4096]; int n = 0;
    api->get_param(in, "markers", csv, sizeof csv);
    for (char *t = strtok(csv, ","); t; t = strtok(NULL, ",")) out[n++] = (uint32_t)strtoul(t, NULL, 10);
    return n;
}
static __attribute__((unused)) int wait_blank(void *in)
{
    setp(in, "sample_path", "");
    for (int i = 0; i < 2000; i++) {
        blocks(in, 0.003, NULL);
        char info[200]; api->get_param(in, "reel_info", info, sizeof info);
        if (strstr(info, "state=2")) return 1;
        struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL);
    }
    return 0;
}
static __attribute__((unused)) hm_t *H(void *in) { return (hm_t *)in; }
static __attribute__((unused)) double reel_rms(void *in, uint32_t a, uint32_t b)
{
    const reel_t *r = H(in)->reel; double ss = 0;
    for (uint32_t i = a; i < b; i++) ss += (double)r->l[i] * r->l[i];
    return sqrt(ss / (double)(b > a ? b - a : 1));
}


/* genes separated by silence (Morph below 1/1): start index and length of each */
static int find_genes(const float *x, int n, int *st, int *ln, int max)
{
    int k = 0, quiet = 1000, in = 0, start = 0;
    for (int i = 0; i < n && k < max; i++) {
        int loud = fabsf(x[i]) > 1e-4f;
        if (loud && !in && quiet >= 40) { in = 1; start = i; }
        if (!loud) quiet++; else quiet = 0;
        if (in && quiet >= 40) { st[k] = start; ln[k] = i - 40 - start + 1; k++; in = 0; }
    }
    return k;
}
static double centroid01(const float *x, int a, int len)
{
    double num = 0, den = 0;
    for (int i = 0; i < len; i++) { double e = (double)x[a + i] * x[a + i]; num += e * i; den += e; }
    return den > 0 ? num / den / (double)(len > 1 ? len - 1 : 1) : 0.5;
}

/* ================================================================== main */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    setenv("HEIMAT_NO_STATE", "1", 1);                       /* tests must not read or write a real state file */
    snprintf(g_dir, sizeof g_dir, "/tmp/heimat_test_%d", (int)getpid());
    char cmd[300]; snprintf(cmd, sizeof cmd, "mkdir -p %s", g_dir); if (system(cmd)) return 2;

    memset(&g_h, 0, sizeof g_h);
    g_h.api_version = 1; g_h.sample_rate = SR; g_h.frames_per_block = BLK;
    g_h.mapped_memory = g_mem; g_h.audio_in_offset = MOVE_AUDIO_IN_OFFSET;
    g_h.get_clock_status = mock_clock_status; g_h.get_beat_position = mock_beat;
    api = move_plugin_init_v2(&g_h);
    printf("Heimat offline test (%d parametri)\n", P_COUNT);

    char p[400];
    /* ---- test material ---- */
    { tones_ctx t = { 48000 }; uint32_t cu[3] = { 96000, 192000, 288000 };
      mkpath(p, sizeof p, "tones.wav"); write_wav(p, 3, 32, 2, 48000, 384000, gen_tones, &t, cu, 3, 1, 0); }
    { sine_ctx s = { 440, 44100, 0.5 }; mkpath(p, sizeof p, "sine16m.wav"); write_wav(p, 1, 16, 1, 44100, 44100 * 4, gen_sine, &s, NULL, 0, 0, 0); }
    { sine_ctx s = { 440, 48000, 0.5 }; mkpath(p, sizeof p, "sine24s48.wav"); write_wav(p, 1, 24, 2, 48000, 48000 * 4, gen_sine, &s, NULL, 0, 0, 0); }
    { sine_ctx s = { 440, 44100, 0.5 }; mkpath(p, sizeof p, "sine8.wav");  write_wav(p, 1, 8, 1, 44100, 44100 * 2, gen_sine, &s, NULL, 0, 0, 0); }
    { sine_ctx s = { 440, 44100, 0.5 }; mkpath(p, sizeof p, "sine32i.wav"); write_wav(p, 1, 32, 1, 44100, 44100 * 2, gen_sine, &s, NULL, 0, 0, 0); }
    { sine_ctx s = { 440, 44100, 0.5 }; mkpath(p, sizeof p, "sine_ext.wav"); write_wav(p, 1, 16, 2, 44100, 44100 * 2, gen_sine, &s, NULL, 0, 0, 1); }
    { burst_ctx b = { 44100 }; mkpath(p, sizeof p, "burst.wav"); write_wav(p, 1, 16, 1, 44100, 44100 * 4, gen_burst, &b, NULL, 0, 0, 0); }
    { mkpath(p, sizeof p, "noise.wav"); write_wav(p, 1, 16, 1, 44100, 44100 * 4, gen_noise, NULL, NULL, 0, 0, 0); }
    { sine_ctx s = { 100, 8000, 0.5 }; mkpath(p, sizeof p, "long.wav"); write_wav(p, 1, 16, 1, 8000, 8000 * 200, gen_sine, &s, NULL, 0, 0, 0); }
    { tones_ctx t = { 48000 }; uint32_t cu[3] = { 288000, 96000, 192000 };           /* unsorted, before the data chunk */
      mkpath(p, sizeof p, "tones_cue_first.wav"); write_wav(p, 3, 32, 2, 48000, 384000, gen_tones, &t, cu, 3, 0, 0); }
    { mkpath(p, sizeof p, "steps.wav"); write_wav(p, 1, 16, 1, 44100, 44100 * 4, gen_steps, NULL, NULL, 0, 0, 0); }
    { mkpath(p, sizeof p, "bursts.wav"); write_wav(p, 1, 16, 1, 44100, 44100 * 4, gen_bursts, NULL, NULL, 0, 0, 0); }
    { mkpath(p, sizeof p, "notwav.wav"); FILE *f = fopen(p, "wb"); fputs("this is not a wav file at all", f); fclose(f); }

    /* ---------------------------------------------------------------- 1 */
    printf("\n1. Caricamento WAV su thread di lavoro: formati, marker, limiti, errori\n");
    {
        struct { const char *file; const char *expect; } T[] = {
            { "tones.wav",            "frames=352800 splices=4 sr=44100 markers=3 truncated=0 resampled=1" },
            { "tones_cue_first.wav",  "frames=352800 splices=4 sr=44100 markers=3" },
            { "sine16m.wav",          "frames=176400 splices=1 sr=44100 markers=0" },
            { "sine24s48.wav",        "frames=176400 splices=1 sr=44100" },
            { "sine8.wav",            "frames=88200" },
            { "sine32i.wav",          "frames=88200" },
            { "sine_ext.wav",         "frames=88200" },
            { "long.wav",             "frames=7673400 splices=1 sr=44100 markers=0 truncated=1" },
        };
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            void *in = new_inst();
            mkpath(p, sizeof p, T[i].file);
            int ok = load_wait(in, p, 10.0);
            char info[200]; api->get_param(in, "reel_info", info, sizeof info);
            CHECK(ok && strstr(info, T[i].expect), "%-22s -> %s", T[i].file, info);
            api->destroy_instance(in);
        }
        void *in = new_inst();
        mkpath(p, sizeof p, "notwav.wav");
        int ok = load_wait(in, p, 3.0);
        char err[200]; int el = api->get_error(in, err, sizeof err);
        CHECK(!ok && el > 0, "file non WAV rifiutato con messaggio (\"%s\")", err);
        ok = load_wait(in, "/nonexistent/file.wav", 3.0); el = api->get_error(in, err, sizeof err);
        CHECK(!ok && el > 0, "file inesistente rifiutato (\"%s\")", err);
        mkpath(p, sizeof p, "sine16m.wav");
        ok = load_wait(in, p, 5.0);
        CHECK(ok && api->get_error(in, err, sizeof err) == 0, "dopo un errore un file valido si carica di nuovo");
        setp(in, "sample_path", "");
        int16_t o[BLK * 2]; for (int i = 0; i < 200; i++) { api->render_block(in, o, BLK); struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); }
        char info[200]; api->get_param(in, "reel_info", info, sizeof info);
        CHECK(strstr(info, "frames=0") != NULL, "percorso vuoto scarica il reel (%s)", info);
        api->destroy_instance(in);
    }

    /* ---------------------------------------------------------------- 2 */
    printf("\n2. Modo Pitch: le note sono la velocita' a 1V/ottava, la nota radice e' la velocita' originale\n");
    {
        struct { const char *file; int note; double expect; } T[] = {
            { "sine16m.wav",   60, 440.0 }, { "sine16m.wav", 72, 880.0 }, { "sine16m.wav", 48, 220.0 },
            { "sine16m.wav",   67, 659.26 }, { "sine24s48.wav", 60, 440.0 }, { "sine24s48.wav", 72, 880.0 },
        };
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            void *in = inst_with(T[i].file, 1);
            key_on(in, T[i].note);
            double f = freq_of(in, 0.3, T[i].expect * 0.9, T[i].expect * 1.1);
            CHECK(fabs(f - T[i].expect) < T[i].expect * 0.006, "%-14s nota %3d: atteso %.1f Hz, misurato %.1f Hz", T[i].file, T[i].note, T[i].expect, f);
            api->destroy_instance(in);
        }
        void *in = inst_with("sine16m.wav", 1);
        setf(in, "octave_transpose", 1); setf(in, "root_note", 72);
        key_on(in, 60);
        double f = freq_of(in, 0.3, 400, 480);
        CHECK(fabs(f - 440.0) < 3.0, "octave_transpose +1 con root 72: nota 60 -> %.1f Hz", f);
        api->destroy_instance(in);
    }

    /* ---------------------------------------------------------------- 3 */
    printf("\n3. Vari-Speed: bipolare, centro = fermo, +-0.5 = velocita' originale\n");
    {
        struct { double v; double expect; } T[] = { { 0.5, 440.0 }, { 1.0, 880.0 }, { 0.25, 110.0 }, { -0.5, 440.0 }, { -1.0, 880.0 } };
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "vari_speed", T[i].v);
            key_on(in, 60);
            double f = freq_of(in, 0.8, T[i].expect * 0.85, T[i].expect * 1.15);
            CHECK(fabs(f - T[i].expect) < T[i].expect * 0.01, "vari_speed %+.2f: atteso %.1f Hz, misurato %.1f Hz", T[i].v, T[i].expect, f);
            api->destroy_instance(in);
        }
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "vari_speed", k ? 0.015 : 0.0);
            key_on(in, 60); blocks(in, 1.0, NULL);
            rec_t r; rec_init(&r, 0.5); blocks(in, 0.4, &r);
            double d = rms_db(r.L, r.n);
            CHECK(d < -80.0, "vari_speed %.3f: riproduzione ferma, silenzio (%.1f dBFS)", k ? 0.015 : 0.0, d);
            rec_free(&r); api->destroy_instance(in);
        }
    }

    /* ---------------------------------------------------------------- 4 */
    printf("\n4. Organize: splice distribuiti in modo uniforme; cambio a fine gene o immediato\n");
    {
        double orgs[4] = { 0.10, 0.30, 0.60, 0.95 }, expect[4] = { 200, 300, 450, 600 };
        for (int i = 0; i < 4; i++) {
            void *in = inst_with("tones.wav", 1);
            setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", orgs[i]);
            key_on(in, 60);
            double f = freq_of(in, 0.4, expect[i] * 0.9, expect[i] * 1.1);
            CHECK(fabs(f - expect[i]) < expect[i] * 0.01, "organize %.2f -> splice %d: %.1f Hz (atteso %.0f Hz)", orgs[i], i + 1, f, expect[i]);
            api->destroy_instance(in);
        }
        for (int mode = 0; mode < 2; mode++) {
            void *in = inst_with("tones.wav", 1);
            setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", 0.10);
            key_on(in, 60); blocks(in, 0.5, NULL);
            setp(in, "organize_mode", mode ? "Immediate" : "End of Gene"); setf(in, "organize", 0.60);
            double f_early = freq_of(in, 0.15, 150, 700);
            blocks(in, 2.6, NULL);
            double f_late = freq_of(in, 0.0, 150, 700);
            if (mode == 0) CHECK(fabs(f_early - 200) < 8 && fabs(f_late - 450) < 8, "End of Gene: dopo 0.2 s ancora %.0f Hz, a fine splice %.0f Hz", f_early, f_late);
            else           CHECK(fabs(f_early - 450) < 8, "Immediate: dopo 0.2 s gia' %.0f Hz", f_early);
            api->destroy_instance(in);
        }
    }

    /* ---------------------------------------------------------------- 5 */
    printf("\n5. Modo Splice: un pad per splice (note - splice_base, con avvolgimento)\n");
    {
        void *in = inst_with("tones.wav", 1);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Splice");
        int notes[6] = { 36, 37, 38, 39, 40, 41 };
        double expect[6] = { 200, 300, 450, 600, 200, 300 };
        for (int i = 0; i < 6; i++) {
            key_on(in, notes[i]);
            double f = freq_of(in, 0.3, expect[i] * 0.9, expect[i] * 1.1);
            CHECK(fabs(f - expect[i]) < expect[i] * 0.01, "pad %d -> %.1f Hz (atteso %.0f Hz)", notes[i], f, expect[i]);
            key_off(in, notes[i]); blocks(in, 0.1, NULL);
        }
        setf(in, "splice_base", 60);
        key_on(in, 62);
        double f = freq_of(in, 0.3, 400, 500);
        CHECK(fabs(f - 450) < 5, "splice_base 60: pad 62 -> splice 3 (%.1f Hz)", f);
        api->destroy_instance(in);
    }

    /* ---------------------------------------------------------------- 6 */
    printf("\n6. Slide sposta il punto di partenza dentro lo splice\n");
    {
        double early[2];
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("burst.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "slide", k ? 0.5 : 0.0);
            key_on(in, 60);
            rec_t r; rec_init(&r, 0.6); blocks(in, 0.5, &r);
            early[k] = rms_db(r.L + (int)(0.03 * SR), (int)(0.4 * SR));
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      primi 0.4 s: slide 0 = %.1f dBFS, slide 0.5 = %.1f dBFS (il burst e' a 2.0 s)\n", early[0], early[1]);
        CHECK(early[0] < -80.0 && early[1] > -30.0, "con slide 0.5 si parte dal burst, con slide 0 si sente prima il silenzio");
    }

    /* ---------------------------------------------------------------- 7 */
    printf("\n7. Gene-Size: lunghezza in tempo, indipendente dalla velocita'\n");
    {
        const double tmax = 4.0, tmin = HM_GENE_MIN_SEC;
        double periods[2];
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("noise.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5); setf(in, "vari_speed", k ? 1.0 : 0.5);
            key_on(in, 60); blocks(in, 0.6, NULL);
            rec_t r; rec_init(&r, 1.2); blocks(in, 1.0, &r);
            double T = gene_seconds(0.5f, tmax) * SR; (void)tmin;
            double expect = T - HM_FADE_CLASSIC;
            int lag = autocorr_peak(r.L, r.n, (int)(expect * 0.7), (int)(expect * 1.3));
            periods[k] = lag;
            CHECK(fabs(lag - expect) < expect * 0.03, "velocita' %s: periodo del gene %d campioni (atteso %.0f)", k ? "x2" : "x1", lag, expect);
            rec_free(&r); api->destroy_instance(in);
        }
        CHECK(fabs(periods[0] - periods[1]) < 0.03 * periods[0], "stessa durata del gene a velocita' diversa (%d contro %d)", (int)periods[0], (int)periods[1]);
    }

    /* ---------------------------------------------------------------- 8 */
    printf("\n8. Morph: pause, loop senza buchi, sovrapposizioni, accordi e pan\n");
    {
        double sil[3];
        double ms[3] = { 0.0, 0.18, 0.5 };
        for (int k = 0; k < 3; k++) {
            void *in = inst_with("noise.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5); setf(in, "morph", ms[k]);
            key_on(in, 60); blocks(in, 0.6, NULL);
            rec_t r; rec_init(&r, 1.5); blocks(in, 1.2, &r);
            sil[k] = silent_fraction(r.L, r.n);
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      frazione di silenzio: morph 0 = %.2f, 0.18 = %.3f, 0.5 = %.3f\n", sil[0], sil[1], sil[2]);
        CHECK(sil[0] > 0.2 && sil[0] < 0.45, "morph 0: pause tra i gene (%.0f%% di silenzio)", sil[0] * 100);
        CHECK(sil[1] < 0.01 && sil[2] < 0.01, "morph 0.18 e 0.5: nessun buco");

        double e_chord[2], e_base[2], side[2];
        double mm[2] = { 0.60, 1.0 };
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.35); setf(in, "morph", mm[k]);
            key_on(in, 60); blocks(in, 0.8, NULL);
            rec_t r; rec_init(&r, 2.2); blocks(in, 2.0, &r);
            e_base[k]  = band_e(r.L + SR / 2, SR, 440, 6);
            e_chord[k] = band_e(r.L + SR / 2, SR, 880, 6) + band_e(r.L + SR / 2, SR, 1320, 6) + band_e(r.L + SR / 2, SR, 1760, 6);
            double sd = 0, tt = 0;
            for (int i = 0; i < r.n; i++) { double s = r.L[i] - r.R[i]; sd += s * s; tt += (double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]; }
            side[k] = sd / (tt + 1e-20);
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      accordi/base: morph 0.6 = %.2e, morph 1.0 = %.2e;  energia laterale: %.3f / %.3f\n",
               e_chord[0] / e_base[0], e_chord[1] / e_base[1], side[0], side[1]);
        CHECK(e_chord[0] / e_base[0] < 1e-3 && e_chord[1] / e_base[1] > 0.05, "pitch-up casuale 2x/3x/4x solo oltre ~2:30 (morph 1.0)");
        CHECK(e_base[1] > 0.05 * e_base[0], "a Morph 1.0 il gene in unisono (440 Hz) resta, gli altri sono intonati come accordo");
        CHECK(side[1] > side[0] + 0.05, "il pan dei gene si apre con Morph alto (side %.3f -> %.3f)", side[0], side[1]);

        void *in = inst_with("sine16m.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.35); setf(in, "morph", 1.0); setf(in, "mcr1", 1.5); setf(in, "mcr2", 1.5); setf(in, "mcr3", 1.5);
        key_on(in, 60); blocks(in, 0.8, NULL);
        rec_t r; rec_init(&r, 2.2); blocks(in, 2.0, &r);
        double e660 = band_e(r.L + SR / 2, SR, 660, 6), e880 = band_e(r.L + SR / 2, SR, 880, 6);
        CHECK(e660 > 20 * e880, "mcr1-3 = 1.5: i gene salgono di una quinta (660 Hz), non di un'ottava");
        rec_free(&r); api->destroy_instance(in);
    }

    /* ---------------------------------------------------------------- 9 */
    printf("\n9. Modi Play (Classic / Momentary / Trigger Loop) e Drone\n");
    {
        double after[4][2];       /* [mode][0] soon after release, [1] later */
        const char *names[3] = { "Classic", "Momentary", "Trigger Loop" };
        for (int m = 0; m < 3; m++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "play_mode", names[m]); setf(in, "gene_size", 0.3); setf(in, "release_ms", 15);
            key_on(in, 60); blocks(in, 1.0, NULL);
            key_off(in, 60);
            rec_t r; rec_init(&r, 1.6); blocks(in, 1.5, &r);
            after[m][0] = rms_db(r.L + (int)(0.03 * SR), (int)(0.05 * SR));
            after[m][1] = rms_db(r.L + (int)(0.8 * SR), (int)(0.5 * SR));
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      dopo il rilascio (0.03 s / 0.8 s): Classic %.0f / %.0f, Momentary %.0f / %.0f, Trigger Loop %.0f / %.0f dBFS\n",
               after[0][0], after[0][1], after[1][0], after[1][1], after[2][0], after[2][1]);
        CHECK(after[0][0] > -40 && after[0][1] < -80, "Classic: il gene in corso finisce poi si ferma");
        CHECK(after[1][0] < -80 && after[1][1] < -80, "Momentary: silenzio subito dopo il rilascio");
        CHECK(after[2][0] > -40 && after[2][1] > -40, "Trigger Loop: continua a suonare dopo il note-off");

        void *in = inst_with("sine16m.wav", 1);
        setp(in, "pad_mode", "Drone");
        rec_t r; rec_init(&r, 0.7); blocks(in, 0.5, &r);
        CHECK(rms_db(r.L + (int)(0.2 * SR), (int)(0.2 * SR)) > -30, "Drone: suona subito, senza note");
        rec_free(&r); api->destroy_instance(in);

        in = inst_with("sine16m.wav", 1);
        rec_init(&r, 0.7); blocks(in, 0.5, &r);
        CHECK(rms_db(r.L, r.n) < -80, "Pitch: silenzio finche' non si preme un pad");
        rec_free(&r); api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 10 */
    printf("\n10. Monofonia con legato e priorita' all'ultima nota\n");
    {
        void *in = inst_with("sine16m.wav", 1);
        key_on(in, 60);
        double f1 = freq_of(in, 0.3, 400, 480);
        key_on(in, 67);
        double f2 = freq_of(in, 0.4, 600, 720);
        key_off(in, 67);
        double f3 = freq_of(in, 0.4, 400, 480);
        key_off(in, 60);
        rec_t r; rec_init(&r, 0.5); blocks(in, 0.4, &r);
        double sil = rms_db(r.L + (int)(0.1 * SR), (int)(0.2 * SR));
        CHECK(fabs(f1 - 440) < 3 && fabs(f2 - 659.26) < 4 && fabs(f3 - 440) < 3, "60 -> 67 -> torna a 60: %.1f, %.1f, %.1f Hz", f1, f2, f3);
        CHECK(sil < -80, "rilasciate tutte le note: silenzio");
        rec_free(&r); api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 11 */
    printf("\n11. Scambio del reel durante la riproduzione (vale con ASAN: SAN=1)\n");
    {
        void *in = new_inst();
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.4); setf(in, "morph", 0.9);
        const char *files[4] = { "sine16m.wav", "tones.wav", "noise.wav", "sine24s48.wav" };
        int bad = 0;
        for (int i = 0; i < 24; i++) {
            mkpath(p, sizeof p, files[i % 4]);
            setp(in, "sample_path", p);
            rec_t r; rec_init(&r, 0.3); blocks(in, 0.25, &r);
            for (int k = 0; k < r.n; k++) if (!isfinite(r.L[k]) || !isfinite(r.R[k])) { bad = 1; break; }
            rec_free(&r);
        }
        CHECK(!bad, "24 cambi di file sotto carico: audio sempre finito, nessun crash");
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 12 */
    printf("\n12. Il caricamento non blocca il thread audio\n");
    {
        sine_ctx s = { 220, 48000, 0.3 };
        mkpath(p, sizeof p, "big.wav");
        write_wav(p, 1, 16, 2, 48000, 48000 * 100, gen_sine, &s, NULL, 0, 0, 0);
        void *in = inst_with("sine16m.wav", 1);
        setp(in, "pad_mode", "Drone");
        blocks(in, 0.3, NULL);
        double max_us = 0, sum_us = 0; int nb = 0;
        int16_t out[BLK * 2];
        setp(in, "sample_path", p);
        double t0 = now_s(), t_loaded = -1;
        while (now_s() - t0 < 6.0) {
            double a = now_s();
            api->render_block(in, out, BLK);
            double us = (now_s() - a) * 1e6;
            if (us > max_us) max_us = us;
            sum_us += us; nb++;
            char info[200]; api->get_param(in, "reel_info", info, sizeof info);
            if (strstr(info, "frames=4410000")) { t_loaded = now_s() - t0; break; }
            struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL);
        }
        printf("      file da 100 s stereo 48 kHz (~19 MB) caricato in %.2f s; blocco audio: medio %.1f us, massimo %.1f us (budget 2902 us)\n",
               t_loaded, sum_us / (nb ? nb : 1), max_us);
        CHECK(t_loaded > 0, "il file grande arriva e viene adottato");
        TCHECK(max_us < 1500.0, "nessun blocco audio lungo durante il caricamento (massimo %.0f us)", max_us);
        api->destroy_instance(in);

        in = new_inst();                                                    /* destroy while the worker is loading */
        setp(in, "sample_path", p);
        double t1 = now_s(); api->destroy_instance(in);
        CHECK(now_s() - t1 < 3.0, "destroy_instance durante un caricamento non si blocca (%.2f s)", now_s() - t1);
        unlink(p);
    }

    /* --------------------------------------------------------------- 13 */
    printf("\n13. Stato e JSON\n");
    {
        void *a = inst_with("tones.wav", 1);
        setp(a, "splice_source", "Markers"); setp(a, "pad_mode", "Splice"); setf(a, "morph", 0.66); setf(a, "mcr2", -1.5); setp(a, "gene_window", "Smooth");
        static char st[16384], cp[16384];
        int ls = api->get_param(a, "state", st, sizeof st);
        int lc = api->get_param(a, "ui_hierarchy", cp, sizeof cp);
        printf("      state %d byte\n", ls);
        CHECK(ls > 0 && lc <= 0, "get_param restituisce lo state; le pagine del modulo per la traccia (sospeso) non ci sono piu'");
        void *b = new_inst();
        setp(b, "state", st);
        int16_t o[BLK * 2];
        for (int i = 0; i < 1500; i++) {
            api->render_block(b, o, BLK);
            char info[200]; api->get_param(b, "reel_info", info, sizeof info);
            if (strstr(info, "state=2") && !strstr(info, "frames=0")) break;
            struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL);
        }
        static char st2[16384]; api->get_param(b, "state", st2, sizeof st2);
        { char *c1 = strstr(st, ",\"rev\""), *c2 = strstr(st2, ",\"rev\""); if (c1) *c1 = 0; if (c2) *c2 = 0; }   /* the live view follows */
        CHECK(strcmp(st, st2) == 0, "round-trip dello stato (percorso del file compreso) identico");
        char info[200]; api->get_param(b, "reel_info", info, sizeof info);
        CHECK(strstr(info, "splices=4") != NULL, "il reel dello stato salvato si ricarica da solo (%s)", info);
        api->destroy_instance(a); api->destroy_instance(b);
    }

    /* --------------------------------------------------------------- 14 */
    printf("\n14. CPU nel caso peggiore: 4 gene, Morph 1.0, gene piccoli, velocita' x2\n");
    {
        void *in = inst_with("tones.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.8); setf(in, "morph", 1.0); setf(in, "vari_speed", 1.0);
        setp(in, "gene_window", "Smooth");
        key_on(in, 60); blocks(in, 1.0, NULL);
        int nb = (30 * SR) / BLK; int16_t out[BLK * 2];
        double t0 = now_s();
        for (int b = 0; b < nb; b++) api->render_block(in, out, BLK);
        double el = now_s() - t0;
        printf("      30 s di audio in %.3f s  => %.2f%% di un core su questa macchina (%.1f us/blocco di 2902 us)\n", el, 100.0 * el / 30.0, 1e6 * el / nb);
        TCHECK(el / 30.0 < 0.05, "carico < 5%% di un core x86 (stima Move: 3-5x)");
        api->destroy_instance(in);
    }


    /* --------------------------------------------------------------- 15 */
    printf("\n15. Reel a frequenza di campionamento dell'host: ricampionamento e marker in scala\n");
    {
        void *in = new_inst();
        mkpath(p, sizeof p, "tones.wav");
        load_wait(in, p, 10.0);
        setp(in, "splice_source", "Markers");
        uint32_t mk[300]; int n = markers_of(in, mk);
        CHECK(n == 3 && labs((long)mk[0] - 88200) <= 2 && labs((long)mk[1] - 176400) <= 2 && labs((long)mk[2] - 264600) <= 2,
              "marker a 2/4/6 s di un file a 48 kHz: %u %u %u (attesi 88200 176400 264600)", n > 0 ? mk[0] : 0, n > 1 ? mk[1] : 0, n > 2 ? mk[2] : 0);
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 16 */
    printf("\n16. Registrazione in un nuovo splice (l'uscita modulata) e annullamento\n");
    {
        g_in_amp = 0;
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", 0.1);
        key_on(in, 60); blocks(in, 0.5, NULL);
        double fr0 = ui_num(in, "fr");
        setp(in, "rec_new", "1"); blocks(in, 2.0, NULL);
        CHECK(ui_num(in, "rec") == REC_NEW && ui_num(in, "rpos") > 80000, "in registrazione, testina di scrittura a %.0f", ui_num(in, "rpos"));
        setp(in, "rec_stop", "1"); blocks(in, 0.3, NULL);
        double fr1 = ui_num(in, "fr");
        CHECK(fabs((fr1 - fr0) - 2.0 * SR) < 1500, "il reel e' cresciuto di %.0f frame (atteso ~88200)", fr1 - fr0);
        CHECK(ui_num(in, "nmk") == 4 && ui_num(in, "ns") == 5 && ui_num(in, "sel") == 4, "nuovo splice creato e selezionato (nmk %.0f, ns %.0f, sel %.0f)", ui_num(in, "nmk"), ui_num(in, "ns"), ui_num(in, "sel"));
        double f = freq_of(in, 0.3, 150, 700);
        CHECK(fabs(f - 200) < 6, "lo splice registrato contiene cio' che si sentiva (%.1f Hz, atteso 200)", f);
        setp(in, "undo", "1"); blocks(in, 0.1, NULL);
        CHECK(fabs(ui_num(in, "fr") - fr0) < 1 && ui_num(in, "nmk") == 3, "undo: reel e marker tornano come prima (fr %.0f, nmk %.0f)", ui_num(in, "fr"), ui_num(in, "nmk"));
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 17 */
    printf("\n17. Time Lag Accumulation: si registra sopra lo splice corrente, ingresso audio e Sound-on-Sound\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", 0.35);
        setf(in, "sos", 0); setp(in, "rec_source", "Input");
        g_in_freq = 1000; g_in_amp = 0.5;
        key_on(in, 60); blocks(in, 0.3, NULL);
        double fr0 = ui_num(in, "fr");
        setp(in, "rec_cur", "1"); blocks(in, 3.0, NULL);                  /* piu' lungo dello splice: il punto di scrittura ricomincia */
        setp(in, "rec_stop", "1"); blocks(in, 0.2, NULL);
        g_in_amp = 0; setf(in, "sos", 1);
        CHECK(fabs(ui_num(in, "fr") - fr0) < 1 && ui_num(in, "nmk") == 3, "TLA non cambia lunghezza ne' marker");
        setp(in, "pad_mode", "Splice");
        double expect[3] = { 200, 1000, 450 };
        for (int i = 0; i < 3; i++) {
            key_on(in, 36 + i);
            double f = freq_of(in, 0.3, 150, 1100);
            CHECK(fabs(f - expect[i]) < expect[i] * 0.02, "splice %d dopo la registrazione: %.1f Hz (atteso %.0f)", i + 1, f, expect[i]);
            key_off(in, 36 + i); blocks(in, 0.1, NULL);
        }
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 18 */
    printf("\n18. Registrare dall'ingresso in un reel vuoto; guadagno d'ingresso; S.O.S. come monitor\n");
    {
        double rms[2];
        for (int g = 0; g < 2; g++) {
            void *in = new_inst(); wait_blank(in);
            g_in_freq = 330; g_in_amp = 0.2;
            setf(in, "sos", 0); setp(in, "rec_source", "Input"); setf(in, "in_gain", g ? 2 : 1);
            setp(in, "rec_cur", "1"); blocks(in, 1.5, NULL); setp(in, "rec_stop", "1"); blocks(in, 0.1, NULL);
            double fr = ui_num(in, "fr");
            CHECK(fabs(fr - 1.5 * SR) < 1500, "gain %s: registrati %.0f frame in un reel vuoto (atteso ~66150)", g ? "+6 dB" : "0 dB", fr);
            rms[g] = reel_rms(in, 2000, (uint32_t)fr - 2000);
            if (g == 1) {
                g_in_amp = 0; setf(in, "sos", 1); setp(in, "pad_mode", "Drone"); key_on(in, 60);
                double f = freq_of(in, 0.4, 250, 450);
                CHECK(fabs(f - 330) < 4, "il reel registrato suona a %.1f Hz (atteso 330)", f);
            }
            api->destroy_instance(in);
        }
        CHECK(fabs(20 * log10(rms[1] / rms[0]) - 6.0) < 0.6, "+6 dB di guadagno = +%.2f dB nel reel registrato", 20 * log10(rms[1] / rms[0]));

        void *in = new_inst(); mkpath(p, sizeof p, "sine16m.wav"); load_wait(in, p, 10.0);
        g_in_freq = 700; g_in_amp = 0.3; setp(in, "pad_mode", "Drone"); key_on(in, 60);
        rec_t r; rec_init(&r, 1.0);
        setf(in, "sos", 1); blocks(in, 0.3, NULL); blocks(in, 0.4, &r);
        double e_in_sos1 = band_e(r.L, r.n, 700, 8);
        r.n = 0; setf(in, "sos", 0); blocks(in, 0.3, NULL); blocks(in, 0.4, &r);
        double e_in_sos0 = band_e(r.L, r.n, 700, 8);
        CHECK(e_in_sos0 > 1000 * e_in_sos1, "S.O.S. 0 fa sentire l'ingresso (700 Hz), S.O.S. 1 no");
        rec_free(&r); g_in_amp = 0; api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 19 */
    printf("\n19. Registrazione sincronizzata al clock\n");
    {
        void *in = new_inst(); wait_blank(in);
        g_in_freq = 330; g_in_amp = 0.2;
        setp(in, "clock_source", "Internal"); setf(in, "bpm", 120); setp(in, "clock_div", "1 bar");     /* indice 4 = 4 beat = 2 s */
        setp(in, "clock_div", "1/2");                                                                      /* indice 3 = 2 beat = 1 s */
        setp(in, "rec_sync", "On"); setf(in, "sos", 0); setp(in, "rec_source", "Input");
        blocks(in, 0.7, NULL);
        setp(in, "rec_cur", "1"); blocks(in, 0.1, NULL);
        CHECK(ui_num(in, "arm") != 0 && ui_num(in, "rec") == 0, "richiesta di registrazione: in attesa del prossimo tick");
        blocks(in, 1.0, NULL);
        CHECK(ui_num(in, "rec") == REC_NEW, "parte sul tick a 1.0 s");
        setp(in, "rec_stop", "1"); blocks(in, 0.05, NULL);
        CHECK(ui_num(in, "rec") == REC_NEW && ui_num(in, "stp") == 1, "lo stop aspetta il tick successivo");
        blocks(in, 1.5, NULL);
        double fr = ui_num(in, "fr");
        CHECK(ui_num(in, "rec") == 0 && fabs(fr - SR) < 400, "durata = esattamente un intervallo di clock: %.0f frame (atteso 44100)", fr);
        g_in_amp = 0; api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 20 */
    printf("\n20. Marker a mano: aggiungi, sposta, elimina, svuota, dividi, annulla\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Splice");
        uint32_t mk[300]; int n;
        setp(in, "marker_add", "44100"); blocks(in, 0.05, NULL);
        n = markers_of(in, mk);
        CHECK(n == 4 && mk[0] == 44100 && mk[1] == 88200, "aggiunto un marker a 44100: %d marker, primo %u", n, mk[0]);
        key_on(in, 37); double f = freq_of(in, 0.3, 150, 700);
        CHECK(fabs(f - 200) < 5, "il nuovo splice 2 e' la seconda meta' del vecchio splice 1 (%.1f Hz)", f);
        key_off(in, 37);
        setp(in, "marker_add", "44110"); setp(in, "marker_add", "10"); setp(in, "marker_add", "999999999"); blocks(in, 0.05, NULL);
        CHECK(markers_of(in, mk) == 4, "marker troppo vicini o fuori dal reel rifiutati");
        setp(in, "marker_move", "0,60000"); blocks(in, 0.05, NULL);
        markers_of(in, mk);
        CHECK(mk[0] == 60000, "marker 0 spostato a 60000 (%u)", mk[0]);
        setp(in, "marker_move", "0,90000"); blocks(in, 0.05, NULL);
        markers_of(in, mk);
        CHECK(mk[0] < mk[1] && mk[1] - mk[0] >= HM_MIN_SPLICE, "uno spostamento non puo' scavalcare il marker vicino (%u, %u)", mk[0], mk[1]);
        setp(in, "marker_del", "0"); blocks(in, 0.05, NULL);
        CHECK(markers_of(in, mk) == 3, "marker eliminato: gli splice si uniscono");
        for (int i = 0; i < 4; i++) setp(in, "undo", "1");
        blocks(in, 0.05, NULL);
        CHECK(markers_of(in, mk) == 3 && mk[0] == 88200 && ui_num(in, "up") == 0, "quattro undo (add, move, move, del) tornano ai tre marker del file");
        setp(in, "markers_clear", "1"); blocks(in, 0.05, NULL);
        CHECK(markers_of(in, mk) == 0 && ui_num(in, "ns") == 1, "svuota: un solo splice");
        setp(in, "markers_divide", "4"); blocks(in, 0.05, NULL);
        n = markers_of(in, mk);
        CHECK(n == 3 && labs((long)mk[1] - 176400) <= 1, "dividi in 4: %d marker, secondo a %u", n, mk[1]);
        setp(in, "undo", "1"); setp(in, "undo", "1"); blocks(in, 0.05, NULL);
        CHECK(markers_of(in, mk) == 3, "undo ripristina");
        /* a marker dropped at the playhead */
        setp(in, "markers_clear", "1"); setp(in, "pad_mode", "Drone"); key_on(in, 60); blocks(in, 1.0, NULL);
        setp(in, "marker_add", "play"); blocks(in, 0.05, NULL);
        n = markers_of(in, mk);
        CHECK(n == 1 && mk[0] > 30000 && mk[0] < 60000, "marker \"play\": caduto sulla testina di lettura (%u, attesi ~44100)", n ? mk[0] : 0);
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 21 */
    printf("\n21. Picchi per la forma d'onda\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "sine16m.wav"); load_wait(in, p, 10.0);
        char pk[1024];
        setp(in, "view", "0,0"); int len = api->get_param(in, "view_peaks", pk, sizeof pk);
        int hi = 0, lo = 255;
        for (int c = 0; c < 128; c++) { int mx = (int)strtol((char[]){ pk[c*4], pk[c*4+1], 0 }, NULL, 16), mn = (int)strtol((char[]){ pk[c*4+2], pk[c*4+3], 0 }, NULL, 16); if (mx > hi) hi = mx; if (mn < lo) lo = mn; }
        CHECK(len == 512 && hi >= 0x80 + 60 && lo <= 0x80 - 60, "vista completa: 128 colonne, massimo %d, minimo %d (sinusoide 0.5)", hi - 128, lo - 128);
        setp(in, "view", "0,2000"); len = api->get_param(in, "view_peaks", pk, sizeof pk);
        CHECK(len == 512, "vista ingrandita calcolata dal buffer");
        void *e = new_inst(); wait_blank(e);
        api->get_param(e, "view_peaks", pk, sizeof pk);
        CHECK(strncmp(pk, "80808080", 8) == 0, "reel vuoto: linea piatta");
        api->destroy_instance(in); api->destroy_instance(e);
    }

    /* --------------------------------------------------------------- 22 */
    printf("\n22. Salvataggio del reel in WAV con marker (anche a 48 kHz)\n");
    {
        char sd[300]; snprintf(sd, sizeof sd, "%s/save", g_dir); setenv("HEIMAT_SAVE_DIR", sd, 1);
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        setp(in, "splice_source", "Markers"); setp(in, "marker_add", "44100"); blocks(in, 0.05, NULL);
        const char *names[3] = { "t1", "t48", "auto" };
        for (int k = 0; k < 3; k++) {
            if (k == 1) setp(in, "save_48k", "On");
            setp(in, "save_reel", names[k]);
            char info[400]; int ok = 0;
            for (int i = 0; i < 1500 && !ok; i++) { blocks(in, 0.003, NULL); api->get_param(in, "save_info", info, sizeof info); ok = (info[0] == '2'); struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL); }
            CHECK(ok, "salvataggio '%s': %s", names[k], info);
            if (!ok) continue;
            const char *wp = strchr(info, '|') + 1;
            char err[100];
            uint32_t sr_load = (k == 1) ? 48000u : 44100u;
            reel_t *r = load_wav(wp, sr_load, err, sizeof err);
            uint32_t want = (k == 1) ? 384000u : 352800u;
            CHECK(r && labs((long)r->frames - (long)want) <= 2 && r->n_markers == 4, "riletto: %u frame, %d marker (attesi ~%u, 4)", r ? r->frames : 0, r ? r->n_markers : 0, want);
            if (r && k == 1) {
                reel_t *r2 = load_wav(wp, 44100, err, sizeof err);
                CHECK(r2 && labs((long)r2->frames - 352800) <= 3 && labs((long)r2->markers[0] - 44100) <= 3, "il file a 48 kHz riletto a 44.1: %u frame, primo marker %u", r2 ? r2->frames : 0, r2 ? r2->markers[0] : 0);
                free_reel(r2);
            }
            if (k == 2) CHECK(strstr(wp, "heimat_001.wav") != NULL, "nome automatico: %s", wp);
            free_reel(r);
        }
        void *e = new_inst(); wait_blank(e);
        setp(e, "save_reel", "x"); blocks(e, 0.05, NULL);
        char info[200]; for (int i = 0; i < 300; i++) { api->get_param(e, "save_info", info, sizeof info); if (info[0] == '3' || info[0] == '2') break; struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL); }
        CHECK(info[0] == '3', "salvare un reel vuoto da' errore (%s)", info);
        api->destroy_instance(in); api->destroy_instance(e);
    }

    /* --------------------------------------------------------------- 23 */
    printf("\n23. Rilevamento dei transienti\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "bursts.wav"); load_wait(in, p, 10.0);
        setp(in, "detect_markers", "0.6");
        uint32_t mk[300]; int n = 0;
        for (int i = 0; i < 1500 && n == 0; i++) { blocks(in, 0.003, NULL); n = markers_of(in, mk); struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL); }
        int ok = (n == 3);
        for (int i = 0; i < n && i < 3; i++) if (labs((long)mk[i] - (long)(44100 * (i + 1))) > 1500) ok = 0;
        CHECK(ok, "3 marker sui 3 burst a 1, 2 e 3 s: n=%d [%u %u %u]", n, n > 0 ? mk[0] : 0, n > 1 ? mk[1] : 0, n > 2 ? mk[2] : 0);
        api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 24 */
    printf("\n24. Clock: Gene Shift, Time Stretch (interno, MIDI, Move)\n");
    {
        const double T = gene_seconds(GK24, 4.0);
        printf("      gene = %.3f s su un file a gradini di 0.5 s (200, 300 ... 900 Hz)\n", T);
        /* Gene Shift, internal clock, tick every second */
        {
            void *in = new_inst(); mkpath(p, sizeof p, "steps.wav"); load_wait(in, p, 10.0);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", GK24); setf(in, "morph", 0.18);
            setp(in, "clock_source", "Internal"); setf(in, "bpm", 120); setp(in, "clock_div", "1/2"); setp(in, "clock_mode", "Gene Shift");
            key_on(in, 60);
            double f[3];
            for (int k = 0; k < 3; k++) { blocks(in, k == 0 ? 0.2 : 0.4, NULL); rec_t r; rec_init(&r, 0.7); blocks(in, 0.4, &r); f[k] = peak_freq(r.L, r.n, 150, 1000, 1.0); rec_free(&r); blocks(in, 0.0 + (k == 0 ? 0.4 : 0.2), NULL); }
            CHECK(fabs(f[0] - 200) < 15 && fabs(f[1] - 300) < 15 && fabs(f[2] - 400) < 15, "ogni tick salta al gene successivo: %.0f -> %.0f -> %.0f Hz (attesi 200, 300, 400)", f[0], f[1], f[2]);
            api->destroy_instance(in);
        }
        /* Time Stretch: the start point glides with the clock */
        {
            void *in = new_inst(); mkpath(p, sizeof p, "steps.wav"); load_wait(in, p, 10.0);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", GK24); setf(in, "morph", 0.55);
            setp(in, "clock_source", "Internal"); setf(in, "bpm", 120); setp(in, "clock_div", "1/2"); setp(in, "clock_mode", "Time Stretch");
            key_on(in, 60);
            double f[3];
            double at[3] = { 0.6, 1.6, 2.6 };
            double t = 0;
            for (int k = 0; k < 3; k++) { blocks(in, at[k] - 0.25 - t, NULL); rec_t r; rec_init(&r, 0.6); blocks(in, 0.5, &r); f[k] = peak_freq(r.L, r.n, 150, 1000, 1.0); rec_free(&r); t = at[k] + 0.25; }
            CHECK(f[0] + 40 < f[1] && f[1] + 40 < f[2], "il punto di lettura avanza con il clock: %.0f -> %.0f -> %.0f Hz", f[0], f[1], f[2]);
            api->destroy_instance(in);
        }
        /* MIDI clock: 24 pulses per beat */
        {
            void *in = new_inst(); mkpath(p, sizeof p, "steps.wav"); load_wait(in, p, 10.0);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", GK24); setf(in, "morph", 0.18);
            setp(in, "clock_source", "MIDI"); setp(in, "clock_div", "1/4"); setp(in, "clock_mode", "Gene Shift");
            rt_msg(in, 0xFA); g_midi_bpm = 120; g_midi_acc = 0;
            key_on(in, 60);
            double f[3];
            for (int k = 0; k < 3; k++) { blocks(in, k == 0 ? 0.05 : 0.2, NULL); rec_t r; rec_init(&r, 0.3); blocks(in, 0.2, &r); f[k] = peak_freq(r.L, r.n, 150, 1000, 1.0); rec_free(&r); blocks(in, 0.05, NULL); }
            g_midi_bpm = 0;
            CHECK(f[0] < f[1] && f[1] < f[2], "con il clock MIDI i gene avanzano: %.0f -> %.0f -> %.0f Hz", f[0], f[1], f[2]);
            api->destroy_instance(in);
        }
        /* Move transport via the host */
        {
            void *in = new_inst(); mkpath(p, sizeof p, "steps.wav"); load_wait(in, p, 10.0);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", GK24);
            setp(in, "clock_source", "Move"); setp(in, "clock_div", "1/4"); setp(in, "clock_mode", "Gene Shift");
            g_beat = 0; g_bpm_mock = 120; g_clk_run = 0;
            blocks(in, 0.5, NULL);
            double ck0 = ui_num(in, "ck");
            g_clk_run = 1; blocks(in, 2.1, NULL);
            double ck1 = ui_num(in, "ck");
            g_clk_run = 0; blocks(in, 1.0, NULL);
            double ck2 = ui_num(in, "ck");
            CHECK(ck0 == 0 && ck1 >= 3 && ck1 <= 5 && ck2 == ck1, "tempo di Move: nessun tick da fermo, %.0f tick in 2.1 s a 120 BPM (attesi ~4), fermo di nuovo = %.0f", ck1, ck2);
            api->destroy_instance(in);
        }
    }

    /* --------------------------------------------------------------- 25 */
    printf("\n25. Stato per l'interfaccia (ui_state) e tempo reale durante registrazione e salvataggio\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        char js[1024]; int len = api->get_param(in, "ui_state", js, sizeof js);
        FILE *f = fopen("/tmp/heimat_ui_state.json", "w"); fputs(js, f); fclose(f);
        printf("      ui_state: %d byte\n", len);
        CHECK(len > 0 && len < 600 && js[0] == '{' && js[len - 1] == '}', "ui_state e' un JSON compatto");
        api->destroy_instance(in);
    }
    {
        /* recording up to the capacity of the reel stops by itself */
        void *in = new_inst(); wait_blank(in);
        g_in_freq = 220; g_in_amp = 0.2; setf(in, "sos", 0); setp(in, "rec_source", "Input");
        setp(in, "rec_cur", "1");
        blocks(in, HM_MAX_REEL_SEC + 3.0, NULL);
        double fr = ui_num(in, "fr");
        CHECK(ui_num(in, "rec") == 0 && fr >= HM_MAX_REEL_SEC * SR - 300, "registrazione ferma da sola a 2.9 minuti (%.0f frame)", fr);
        g_in_amp = 0; api->destroy_instance(in);
    }

    /* ------------------------------------------------------------------ 26 */
    printf("\n26. Impostazioni salvate e ripristinate tra una sessione e l'altra\n");
    {
        char dd[300]; snprintf(dd, sizeof dd, "%s/data", g_dir); setenv("HEIMAT_DATA_DIR", dd, 1);
        unsetenv("HEIMAT_NO_STATE");
        void *a = new_inst(); mkpath(p, sizeof p, "sine16m.wav"); load_wait(a, p, 10.0);
        setf(a, "morph", 0.77); setf(a, "mcr2", -1.5); setp(a, "pad_mode", "Splice"); setf(a, "bpm", 97);
        double t0 = now_s();
        while (now_s() - t0 < 3.2) { blocks(a, 0.01, NULL); struct timespec ts = { 0, 8000000 }; nanosleep(&ts, NULL); }
        char sp2[400]; snprintf(sp2, sizeof sp2, "%s/state.json", dd);
        CHECK(access(sp2, F_OK) == 0, "dopo 2 s senza modifiche il file di stato e' scritto");
        api->destroy_instance(a);
        void *b = new_inst();
        for (int i = 0; i < 400; i++) { blocks(b, 0.003, NULL); char info[200]; api->get_param(b, "reel_info", info, sizeof info); if (strstr(info, "state=2") && !strstr(info, "frames=0")) break; struct timespec ts = { 0, 5000000 }; nanosleep(&ts, NULL); }
        char v[64]; api->get_param(b, "morph", v, sizeof v);
        char pth[400]; api->get_param(b, "sample_path", pth, sizeof pth);
        char info[200]; api->get_param(b, "reel_info", info, sizeof info);
        CHECK(fabs(atof(v) - 0.77) < 0.01 && strstr(pth, "sine16m.wav") && strstr(info, "frames=176400"), "nuova sessione: morph %s, reel %s ricaricato", v, strrchr(pth, '/') ? strrchr(pth, '/') + 1 : pth);
        api->destroy_instance(b);
        setenv("HEIMAT_NO_STATE", "1", 1);
    }


    /* --------------------------------------------------------------- 27 */
    printf("\n27. MICRO: forme d'inviluppo del gene\n");
    {
        int okr = 1, oksym = 1;
        for (int sh = 0; sh < 6; sh++)
            for (int i = 0; i <= 100; i++) {
                double age = i * 10.0, rem = 1000.0 - age;
                float v = gene_env(sh, age, rem);
                if (!(v >= -1e-4f && v <= 1.0001f)) okr = 0;
                if (sh <= ENV_TRI && fabsf(v - gene_env(sh, rem, age)) > 1e-3f) oksym = 0;
            }
        CHECK(okr && oksym, "tutte le forme tra 0 e 1; Edge, Tukey, Gauss e Triangle simmetriche nel tempo");
        CHECK(gene_env(ENV_EXPO, 100, 900) > 4 * gene_env(ENV_EXPO, 900, 100) && gene_env(ENV_REXPO, 900, 100) > 4 * gene_env(ENV_REXPO, 100, 900),
              "Expodec decade, Rexpodec cresce: non simmetriche");
        const char *nm[3] = { "Expodec", "Triangle", "Rexpodec" };
        double cen[3];
        for (int k = 0; k < 3; k++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.45); setf(in, "morph", 0.0); setp(in, "gene_window", nm[k]);
            key_on(in, 60); blocks(in, 0.4, NULL);
            rec_t r; rec_init(&r, 2.2); blocks(in, 2.0, &r);
            int st[64], ln[64]; int ng = find_genes(r.L, r.n, st, ln, 64);
            double c = 0; int used = 0;
            for (int i = 1; i < ng - 1; i++) { c += centroid01(r.L, st[i], ln[i]); used++; }
            cen[k] = used ? c / used : 0.5;
            rec_free(&r); api->destroy_instance(in);
        }
        CHECK(cen[0] < 0.36 && fabs(cen[1] - 0.5) < 0.04 && cen[2] > 0.64, "baricentro dell'energia nel gene: Expodec %.2f, Triangle %.2f, Rexpodec %.2f", cen[0], cen[1], cen[2]);
        void *in = inst_with("sine16m.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.45); setf(in, "morph", 0.18); setp(in, "gene_window", "Triangle");
        key_on(in, 60); blocks(in, 0.4, NULL);
        rec_t r; rec_init(&r, 1.2); blocks(in, 1.0, &r);
        double mx = 0, mn = 1e9;
        for (int w = 0; w + 441 <= r.n; w += 441) { double ss = 0; for (int i = 0; i < 441; i++) ss += (double)r.L[w + i] * r.L[w + i]; ss = sqrt(ss / 441); if (ss > mx) mx = ss; if (ss < mn) mn = ss; }
        CHECK(silent_fraction(r.L, r.n) < 0.01 && mx / mn < 1.6, "Triangle a 1/1: i gene si sommano senza buchi (oscillazione %.2f)", mx / mn);
        rec_free(&r); api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 28 */
    printf("\n28. MICRO: Scatter (da sincrono ad asincrono, lettura da sequenziale a casuale)\n");
    {
        double cv[2];
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("noise.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5650); setf(in, "morph", 0.0); setf(in, "scatter", k ? 0.8 : 0.0);
            key_on(in, 60); blocks(in, 0.4, NULL);
            rec_t r; rec_init(&r, 4.2); blocks(in, 4.0, &r);
            int st[128], ln[128]; int ng = find_genes(r.L, r.n, st, ln, 128);
            double m = 0, v = 0; int c = 0;
            for (int i = 1; i < ng; i++) { m += st[i] - st[i - 1]; c++; }
            m /= c > 0 ? c : 1;
            for (int i = 1; i < ng; i++) { double d = st[i] - st[i - 1] - m; v += d * d; }
            cv[k] = c > 1 ? sqrt(v / c) / m : 0;
            rec_free(&r); api->destroy_instance(in);
        }
        CHECK(cv[0] < 0.02 && cv[1] > 0.15, "irregolarita' degli intervalli tra i gene: scatter 0 = %.3f, scatter 0.8 = %.3f", cv[0], cv[1]);
        int distinct[2];
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("steps.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5193); setf(in, "slide", 0.55); setf(in, "morph", 0.18); setf(in, "scatter", k ? 1.0 : 0.0);
            key_on(in, 60); blocks(in, 0.3, NULL);
            int seen[8] = { 0 };
            for (int w = 0; w < 40; w++) {
                rec_t r; rec_init(&r, 0.06); blocks(in, 0.05, &r);
                double f = peak_freq(r.L, r.n, 150, 950, 5.0);
                int idx = (int)lrint((f - 200.0) / 100.0); if (idx >= 0 && idx < 8) seen[idx] = 1;
                rec_free(&r);
            }
            distinct[k] = 0; for (int i = 0; i < 8; i++) distinct[k] += seen[i];
            api->destroy_instance(in);
        }
        CHECK(distinct[0] <= 2 && distinct[1] >= 5, "gradini del file ascoltati: scatter 0 = %d, scatter 1 = %d", distinct[0], distinct[1]);
    }

    /* --------------------------------------------------------------- 29 */
    printf("\n29. MICRO: Spread (banda di frequenza) e Width (dispersione stereo)\n");
    {
        double fmin[2] = { 1e9, 1e9 }, fmax[2] = { 0, 0 };
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("sine16m.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.30); setf(in, "morph", 0.0); setf(in, "spread", k ? 7.0 : 0.0);
            key_on(in, 60); blocks(in, 0.3, NULL);
            rec_t r; rec_init(&r, 3.2); blocks(in, 3.0, &r);
            int st[128], ln[128]; int ng = find_genes(r.L, r.n, st, ln, 128);
            for (int i = 0; i < ng; i++) {
                if (ln[i] < 2000) continue;
                double f = peak_freq(r.L + st[i] + ln[i] / 4, ln[i] / 2, 250, 700, 1.0);
                if (f < fmin[k]) fmin[k] = f;
                if (f > fmax[k]) fmax[k] = f;
            }
            rec_free(&r); api->destroy_instance(in);
        }
        CHECK(fmax[0] - fmin[0] < 6 && fmax[1] / fmin[1] > 1.3, "altezza dei gene: spread 0 = %.0f-%.0f Hz, spread 7 st = %.0f-%.0f Hz", fmin[0], fmax[0], fmin[1], fmax[1]);
        double side[2];
        for (int k = 0; k < 2; k++) {
            void *in = inst_with("noise.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.35); setf(in, "morph", 0.18); setf(in, "width", k ? 1.0 : 0.0);
            key_on(in, 60); blocks(in, 0.3, NULL);
            rec_t r; rec_init(&r, 2.2); blocks(in, 2.0, &r);
            double sd = 0, tt = 0;
            for (int i = 0; i < r.n; i++) { double d = r.L[i] - r.R[i]; sd += d * d; tt += (double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]; }
            side[k] = sd / (tt + 1e-20);
            rec_free(&r); api->destroy_instance(in);
        }
        CHECK(side[0] < 1e-6 && side[1] > 0.1, "a Morph 1/1 (un solo gene) lo stereo si apre solo con Width: %.1e -> %.3f", side[0], side[1]);
    }

    /* --------------------------------------------------------------- 30 */
    printf("\n30. MICRO: granulazione selettiva (soglia)\n");
    {
        double e1k[3]; unsigned skip[3], spawned[3];
        const char *mode[3] = { "Off", "Loud", "Quiet" };
        for (int k = 0; k < 3; k++) {
            void *in = inst_with("bursts.wav", 1);
            setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5193); setf(in, "morph", 0.18); setf(in, "scatter", 1.0); setf(in, "slide", 0.5);
            setp(in, "thr_mode", mode[k]); setf(in, "threshold", 0.5);
            key_on(in, 60); blocks(in, 0.2, NULL);
            rec_t r; rec_init(&r, 6.2); blocks(in, 6.0, &r);
            e1k[k] = band_e(r.L, r.n, 1000, 15);
            spawned[k] = (unsigned)ui_num(in, "gsp"); skip[k] = (unsigned)ui_num(in, "gsk");
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      gene saltati / creati: Off %u/%u, Loud %u/%u, Quiet %u/%u\n", skip[0], spawned[0], skip[1], spawned[1], skip[2], spawned[2]);
        CHECK(skip[0] == 0 && skip[1] > spawned[1] / 2 && e1k[1] > 0.3 * e1k[0], "Loud: suonano solo i gene sui colpi (la maggior parte saltati, colpi ancora presenti)");
        CHECK(e1k[2] < 0.02 * e1k[0], "Quiet: i gene sui colpi a 1 kHz vengono saltati (energia %.1e contro %.1e)", e1k[2], e1k[0]);
    }

    /* --------------------------------------------------------------- 31 */
    printf("\n31. MICRO: indicatori di durata, densita' e fill factor\n");
    {
        void *in = inst_with("noise.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5);
        key_on(in, 60); blocks(in, 0.5, NULL);
        const double T = gene_seconds(0.5f, 4.0), L = T * SR;
        struct { double m; double sp; const char *what; } C[3] = { { 0.0, L * 1.5, "rada" }, { 0.18, L - HM_FADE_CLASSIC, "coperta" }, { 1.0, (L - HM_FADE_CLASSIC) / 4.0, "piena" } };
        for (int k = 0; k < 3; k++) {
            setf(in, "morph", C[k].m); blocks(in, 0.1, NULL);
            double gms = ui_num(in, "gms"), dens = ui_num(in, "dens"), ff = ui_num(in, "ff");
            double ed = SR / C[k].sp;
            CHECK(fabs(gms - T * 1000) < 0.05 && fabs(dens - ed) < 0.02 * ed + 0.02 && fabs(ff - T * ed) < 0.02,
                  "Morph %.2f (%s): gene %.2f ms, %.2f gene/s, fill factor %.2f (attesi %.2f ms, %.2f, %.2f)", C[k].m, C[k].what, gms, dens, ff, T * 1000, ed, T * ed);
        }
        setf(in, "morph", 0.0); blocks(in, 0.3, NULL);
        rec_t r; rec_init(&r, 3.2); blocks(in, 3.0, &r);
        int st[128], ln[128]; int ng = find_genes(r.L, r.n, st, ln, 128);
        double measured = ng > 2 ? (double)(ng - 1) / ((double)(st[ng - 1] - st[0]) / SR) : 0;
        CHECK(fabs(measured - ui_num(in, "dens")) < 0.05 * measured, "densita' misurata nell'audio %.2f gene/s = indicatore %.2f", measured, ui_num(in, "dens"));
        /* smooth envelopes: each gene fades over half its length, so Morph x4 means
         * genes every L/8 and eight sounding at once, all of them with a slot */
        setp(in, "gene_window", "Tukey"); setf(in, "morph", 1.0); blocks(in, 0.5, NULL);
        double ed = SR / (L / 8.0);
        CHECK(fabs(ui_num(in, "dens") - ed) < 0.02 * ed, "Tukey, Morph 1.00: %.1f gene/s (attesi %.1f, un gene ogni L/8)", ui_num(in, "dens"), ed);
        int maxact = 0; unsigned sp0 = H(in)->genes_spawned;
        for (int b = 0; b < 400; b++) {
            blocks(in, BLK / (double)SR, NULL);
            int a = 0; for (int i = 0; i < HM_MAX_GENES; i++) a += H(in)->genes[i].active; if (a > maxact) maxact = a;
        }
        double rate = (H(in)->genes_spawned - sp0) / (400.0 * BLK / SR);
        CHECK(maxact >= 7 && maxact <= HM_MAX_GENES && fabs(rate - ed) < 0.05 * ed,
              "fino a %d gene insieme, %.1f gene/s creati davvero (nessuno perso per mancanza di posti)", maxact, rate);
        rec_free(&r); api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 32 */
    printf("\n32. FX: ai valori di default gli effetti sono neutri (uscita identica bit per bit)\n");
    {
        void *a = inst_with("noise.wav", 1), *b = inst_with("noise.wav", 1);
        void *in2[2] = { a, b };
        for (int k = 0; k < 2; k++) { setp(in2[k], "pad_mode", "Drone"); setf(in2[k], "gene_size", 0.4); setf(in2[k], "morph", 0.6); key_on(in2[k], 60); }
        setf(b, "eq_low", 12); setf(b, "eq_high", -9); setf(b, "rev_mix", 0.6); setf(b, "rev_tilt", 0.5); setf(b, "rev_depth", 0.95);
        blocks(a, 1.0, NULL); blocks(b, 1.0, NULL);
        setf(b, "eq_low", 0); setf(b, "eq_high", 0); setf(b, "rev_mix", 0); setf(b, "rev_tilt", 0); setf(b, "rev_depth", 0);
        blocks(a, 1.0, NULL); blocks(b, 1.0, NULL);
        rec_t ra, rb; rec_init(&ra, 1.1); rec_init(&rb, 1.1);
        blocks(a, 1.0, &ra); blocks(b, 1.0, &rb);
        int diff = 0; for (int i = 0; i < ra.n; i++) if (ra.L[i] != rb.L[i] || ra.R[i] != rb.R[i]) diff++;
        CHECK(diff == 0 && H(b)->fx.rev_idle == 1 && H(b)->fx.eq_on == 0,
              "dopo aver riportato EQ e Reverb a zero l'uscita torna identica (%d campioni diversi), riverbero e EQ spenti", diff);
        rec_free(&ra); rec_free(&rb); api->destroy_instance(a); api->destroy_instance(b);
    }

    /* --------------------------------------------------------------- 33 */
    printf("\n33. FX: equalizzatore a tre bande\n");
    {
        /* the same fixed white noise straight through the effects chain, flat and with
           each band moved: exact, whatever the reel and its timing */
        const int N = 2 * SR;
        float *x = (float *)malloc(sizeof(float) * N), *y = (float *)malloc(sizeof(float) * N);
        uint32_t sd = 12345u;
        for (int i = 0; i < N; i++) { sd = sd * 1664525u + 1013904223u; x[i] = 0.25f * ((float)(sd >> 8) / 8388608.0f - 1.0f); }
        double e[4][3];
        const char *key[4] = { NULL, "eq_low", "eq_mid", "eq_high" };
        const double gain[4] = { 0, 12, -12, 12 };
        for (int k = 0; k < 4; k++) {
            void *in = new_inst();
            if (key[k]) setf(in, key[k], gain[k]);
            hm_t *h = H(in);
            for (int b = 0; b < 40; b++) fx_block(h);                     /* let the EQ glide to its setting */
            for (int i = 0; i < N; i++) {
                if (i % BLK == 0) fx_block(h);
                float l = x[i], r = x[i];
                fx_frame(h, &l, &r);
                y[i] = l;
            }
            e[k][0] = band_e(y, N, 70, 20); e[k][1] = band_e(y, N, 1000, 200); e[k][2] = band_e(y, N, 12000, 2000);
            api->destroy_instance(in);
        }
        free(x); free(y);
        double d[4][3];
        for (int k = 1; k < 4; k++) for (int b = 0; b < 3; b++) d[k][b] = 10 * log10(e[k][b] / e[0][b]);
        printf("      dB a 70 Hz / 1 kHz / 12 kHz: Low +12 %.1f %.1f %.1f | Mid -12 %.1f %.1f %.1f | High +12 %.1f %.1f %.1f\n",
               d[1][0], d[1][1], d[1][2], d[2][0], d[2][1], d[2][2], d[3][0], d[3][1], d[3][2]);
        CHECK(d[1][0] > 10 && fabs(d[1][1]) < 1.0 && fabs(d[1][2]) < 0.3, "Low +12 dB alza i bassi e lascia medi e alti");
        CHECK(d[2][1] < -10 && fabs(d[2][0]) < 1.0 && fabs(d[2][2]) < 1.0, "Mid -12 dB a 1 kHz scava i medi e lascia gli estremi");
        CHECK(d[3][2] > 10 && fabs(d[3][1]) < 1.0 && fabs(d[3][0]) < 0.3, "High +12 dB alza gli alti e lascia bassi e medi");
    }

    /* a short blip: burst.wav from its 1 kHz burst, gate held for 0.1 s */
#define BLIP_SETUP(in) do { setp(in, "pad_mode", "Pitch"); setp(in, "play_mode", "Momentary"); setf(in, "slide", 0.5); setf(in, "volume", 0.6); } while (0)
#define WIN(x, a, b) rms_db((x) + (int)((a) * SR), (int)(((b) - (a)) * SR))

    /* --------------------------------------------------------------- 34 */
    printf("\n34. FX: riverbero - pre-delay, dimensione, decadimento infinito, tilt, modulazione\n");
    {
        /* the reverb alone: a click straight through the chain, wet only */
        const int N = 6 * SR;
        float *y = (float *)malloc(sizeof(float) * N), *z = (float *)malloc(sizeof(float) * N);
#define REV_RUN(setup, out, n_, src) do { void *in_ = new_inst(); hm_t *h_ = H(in_); setf(in_, "rev_mix", 1.0); setup; \
            for (int i_ = 0; i_ < SR / 2; i_++) { if (i_ % BLK == 0) fx_block(h_); float l_ = 0, r_ = 0; fx_frame(h_, &l_, &r_); } \
            for (int i_ = 0; i_ < (n_); i_++) { if (i_ % BLK == 0) fx_block(h_); float l_ = (src), r_ = l_; fx_frame(h_, &l_, &r_); (out)[i_] = l_; \
                if (!isfinite(l_) || !isfinite(r_)) nan_++; } \
            api->destroy_instance(in_); } while (0)
        int nan_ = 0;
        /* pre-delay: nothing before it, the space after */
        REV_RUN({ setf(in_, "rev_predelay", 200); setf(in_, "rev_size", 0.15); setf(in_, "rev_decay", 0.3); }, y, SR, i_ == 0 ? 1.0f : 0.0f);
        int on = -1; for (int i = 0; i < SR; i++) if (fabsf(y[i]) > 1e-4f) { on = i; break; }
        printf("      pre-delay 200 ms: primo suono a %.1f ms\n", on * 1000.0 / SR);
        CHECK(on > 0.199 * SR && on < 0.215 * SR, "pre-delay 200 ms: prima niente, poi lo spazio (%.1f ms)", on * 1000.0 / SR);

        /* Size: a bigger space answers later and, at the same Decay, rings longer */
        double first[2], tail[2];
        const double sz[2] = { 0.15, 0.9 };
        for (int k = 0; k < 2; k++) {
            REV_RUN({ setf(in_, "rev_predelay", 7); setf(in_, "rev_size", sz[k]); setf(in_, "rev_decay", 0.5); setf(in_, "rev_damp", 0.3); }, y, N, i_ < 64 ? 0.5f : 0.0f);
            int f1 = -1; double pk = 0; for (int i = 0; i < N; i++) pk = fmax(pk, fabs(y[i]));
            for (int i = 0; i < N; i++) if (fabs(y[i]) > pk * 0.05) { f1 = i; break; }
            first[k] = f1 * 1000.0 / SR; tail[k] = rms_db(y + 2 * SR, SR);
        }
        printf("      Size 0.15 / 0.9: prima riflessione %.1f / %.1f ms, coda a 2-3 s %.1f / %.1f dB\n", first[0], first[1], tail[0], tail[1]);
        CHECK(first[1] > first[0] + 15, "uno spazio grande risponde piu' tardi");
        CHECK(tail[1] > tail[0] + 15, "e, a parita' di Decay, suona piu' a lungo");

        /* Decay at the top: holds */
        REV_RUN({ setf(in_, "rev_predelay", 7); setf(in_, "rev_size", 0.6); setf(in_, "rev_decay", 1.0); setf(in_, "rev_damp", 0.3); }, y, N, i_ < 2000 ? 0.3f * sinf(i_ * 0.07f) : 0.0f);
        const double h1 = rms_db(y + 1 * SR, SR / 2), h5 = rms_db(y + (int)(5.5 * SR), SR / 2);
        printf("      Decay al massimo: livello a 1 s %.1f dB, a 5.5 s %.1f dB\n", h1, h5);
        CHECK(h1 > -60 && fabs(h5 - h1) < 3.0, "Decay al massimo tiene il suono senza fine (%.1f dB di differenza)", h5 - h1);

        /* Tilt: to the left the lows, to the right the highs */
        uint32_t sd = 777u;
        double lo[2], hi[2];
        for (int k = 0; k < 2; k++) {
            sd = 777u;
            REV_RUN({ setf(in_, "rev_tilt", k ? 1.0 : -1.0); setf(in_, "rev_decay", 0.4); }, y, 2 * SR, (sd = sd * 1664525u + 1013904223u, 0.2f * ((float)(sd >> 8) / 8388608.0f - 1.0f)));
            lo[k] = band_e(y, 2 * SR, 100, 30); hi[k] = band_e(y, 2 * SR, 8000, 1500);
        }
        const double tl = 10 * log10((hi[1] / lo[1]) / (hi[0] / lo[0]));
        printf("      Tilt da -1 a +1: alti contro bassi %+.1f dB\n", tl);
        CHECK(tl > 30, "Tilt sposta l'equilibrio da bassi cupi ad alti brillanti (%+.1f dB)", tl);

        /* modulation: cyclic and ergodic stay bounded; shimmer adds the octave above */
        double oct[2];
        for (int k = 0; k < 2; k++) {
            REV_RUN({ setf(in_, "rev_size", 0.6); setf(in_, "rev_decay", 0.85); setf(in_, "rev_damp", 0.3); setf(in_, "rev_depth", k ? 1.0 : 0.0); setf(in_, "rev_speed", 0.3); },
                    y, N, i_ < SR / 2 ? 0.25f * sinf(2.0f * (float)M_PI * 500.0f * i_ / SR) : 0.0f);
            oct[k] = 10 * log10(band_e(y + 2 * SR, 3 * SR, 1000, 60) / band_e(y + 2 * SR, 3 * SR, 500, 60));
        }
        printf("      shimmer: ottava sopra contro fondamentale nella coda %.1f dB (Depth 0) / %.1f dB (Depth +1)\n", oct[0], oct[1]);
        CHECK(oct[1] > oct[0] + 10, "Depth tutto a destra: compare l'ottava sopra (shimmer)");
        double pkm = 0;
        const double dp[3] = { -1.0, 0.5, 1.0 };
        for (int k = 0; k < 3; k++) {
            REV_RUN({ setf(in_, "rev_size", 1.0); setf(in_, "rev_decay", 1.0); setf(in_, "rev_damp", 0.0); setf(in_, "rev_depth", dp[k]); setf(in_, "rev_speed", 1.0); },
                    z, N, i_ < SR ? 0.5f * sinf(i_ * 0.031f) : 0.0f);
            for (int i = 0; i < N; i++) pkm = fmax(pkm, fabs(z[i]));
        }
        printf("      tutto al massimo (cyclic, ergodic, shimmer, decay infinito): picco %.2f\n", pkm);
        CHECK(nan_ == 0 && pkm < 4.0, "modulazione e decay infinito restano limitati, nessun NaN");

        /* Reverse: the pre-delay played backwards changes the sound, not the level */
        double ef, er;
        REV_RUN({ setf(in_, "rev_predelay", 300); setf(in_, "rev_decay", 0.2); setf(in_, "rev_size", 0.2); }, y, SR, i_ < 3000 ? (1.0f - i_ / 3000.0f) * sinf(i_ * 0.05f) : 0.0f);
        REV_RUN({ setf(in_, "rev_predelay", 300); setf(in_, "rev_decay", 0.2); setf(in_, "rev_size", 0.2); setp(in_, "rev_reverse", "On"); }, z, SR, i_ < 3000 ? (1.0f - i_ / 3000.0f) * sinf(i_ * 0.05f) : 0.0f);
        ef = rms_db(y, SR); er = rms_db(z, SR);
        double cf = 0, cr = 0, wf = 0, wr = 0;
        for (int i = 0; i < SR; i++) { cf += (double)i * y[i] * y[i]; wf += (double)y[i] * y[i]; cr += (double)i * z[i] * z[i]; wr += (double)z[i] * z[i]; }
        printf("      Reverse: livello %.1f / %.1f dB, baricentro %.0f / %.0f ms\n", ef, er, cf / wf * 1000 / SR, cr / wr * 1000 / SR);
        CHECK(fabs(ef - er) < 6 && fabs(cf / wf - cr / wr) > 0.01 * SR, "Reverse cambia il tempo del suono (non il livello)");
#undef REV_RUN
        free(y); free(z);
    }

    /* --------------------------------------------------------------- 35 */
    printf("\n35. FX: riverbero - coda, durata, smorzamento, stereo\n");
    {
        double tail[3], corr = 0;
        const double decay[3] = { 0.2, 0.8, 0.8 }, damp[3] = { 0.3, 0.3, 1.0 };
        for (int k = 0; k < 3; k++) {
            void *in = inst_with("burst.wav", 1);
            BLIP_SETUP(in);
            setf(in, "rev_mix", 0.5); setf(in, "rev_decay", decay[k]); setf(in, "rev_damp", damp[k]);
            blocks(in, 0.3, NULL);
            rec_t r; rec_init(&r, 3.1);
            key_on(in, 60); blocks(in, 0.1, &r); key_off(in, 60); blocks(in, 2.9, &r);
            tail[k] = WIN(r.L, 1.0, 1.5);
            if (k == 1) {
                double lr = 0, ll = 0, rr = 0; int a = (int)(0.3 * SR), b = (int)(1.3 * SR);
                for (int i = a; i < b; i++) { lr += (double)r.L[i] * r.R[i]; ll += (double)r.L[i] * r.L[i]; rr += (double)r.R[i] * r.R[i]; }
                corr = lr / sqrt(ll * rr + 1e-30);
            }
            rec_free(&r); api->destroy_instance(in);
        }
        printf("      coda 1.0-1.5 s: decay 0.2 %.1f dBFS, decay 0.8 %.1f dBFS; correlazione L/R %.2f\n", tail[0], tail[1], corr);
        CHECK(tail[1] > -60 && tail[1] > tail[0] + 20, "decay lungo = coda lunga (%.1f contro %.1f dBFS)", tail[1], tail[0]);
        CHECK(fabs(corr) < 0.5, "la coda e' stereo (L e R poco correlati: %.2f)", corr);
        CHECK(tail[2] < tail[1] - 6, "damping: la coda di un tono a 1 kHz si spegne prima (%.1f contro %.1f dBFS)", tail[2], tail[1]);
    }

    /* --------------------------------------------------------------- 36 */
    printf("\n36. FX: stabilita' e costo con tutto al massimo\n");
    {
        void *in = inst_with("noise.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "morph", 1.0); setf(in, "gene_size", 0.5); setf(in, "volume", 1.0);
        setf(in, "eq_low", 15); setf(in, "eq_mid", 15); setf(in, "eq_high", 15);
        setf(in, "rev_mix", 0.5); setf(in, "rev_decay", 1.0); setf(in, "rev_damp", 0.0); setf(in, "rev_size", 1.0);
        setf(in, "rev_depth", 1.0); setf(in, "rev_speed", 0.6); setf(in, "rev_tilt", 1.0);
        key_on(in, 60);
        rec_t r; rec_init(&r, 20.1);
        double t0 = now_s(); blocks(in, 20.0, &r); double el = now_s() - t0;
        int bad = 0; for (int i = 0; i < r.n; i++) if (!isfinite(r.L[i]) || !isfinite(r.R[i])) bad++;
        double late = WIN(r.L, 18.0, 20.0);
        printf("      20 s renderizzati in %.2f s (%.1f%% del tempo reale su questo computer), livello finale %.1f dBFS\n", el, el / 20.0 * 100.0, late);
        CHECK(bad == 0 && late < 0.0 && late > -40, "nessun NaN, il feedback resta limitato");
        key_off(in, 60); setf(in, "rev_mix", 0);
        blocks(in, 1.0, NULL);
        CHECK(H(in)->fx.rev_idle == 1, "riportato il mix a zero il riverbero si ferma e non consuma CPU");
        rec_free(&r); api->destroy_instance(in);
    }

    /* --------------------------------------------------------------- 37 */
    printf("\n37. FX: registrare attraverso gli effetti (Record Through FX)\n");
    {
        double pre[2], post[2];
        for (int k = 0; k < 2; k++) {
            void *in = new_inst(); wait_blank(in);
            setf(in, "sos", 0); setp(in, "rec_source", "Mix");
            setf(in, "rev_mix", 1.0); setf(in, "rev_predelay", 400); setf(in, "rev_decay", 0.3); setf(in, "rev_size", 0.3);
            setp(in, "fx_rec", k ? "On" : "Off");
            blocks(in, 1.0, NULL);
            g_in_freq = 330; g_in_amp = 0.3;
            setp(in, "rec_cur", "1"); blocks(in, 0.3, NULL);
            g_in_amp = 0; blocks(in, 0.7, NULL);
            setp(in, "rec_stop", "1"); blocks(in, 0.1, NULL);
            pre[k] = reel_rms(in, (uint32_t)(0.05 * SR), (uint32_t)(0.25 * SR));
            post[k] = reel_rms(in, (uint32_t)(0.48 * SR), (uint32_t)(0.65 * SR));
            api->destroy_instance(in);
        }
        printf("      rms 0.05-0.25 s / 0.48-0.65 s: senza FX %.3f / %.3f, con FX %.3f / %.3f\n", pre[0], post[0], pre[1], post[1]);
        CHECK(pre[0] > 0.1 && post[0] < 0.001, "Off: nel reel va il suono asciutto (gli effetti restano solo in uscita)");
        CHECK(pre[1] < 0.001 && post[1] > 0.1, "On: nel reel va il suono del riverbero (dopo 400 ms di pre-delay)");
    }


    /* --------------------------------------------------------------- 38 */
    printf("\n38. Editor nel browser (Remote UI di Schwung, scheda Tool)\n");
    {
        void *in = inst_with("tones.wav", 1);
        static char buf[65536], b2[65536];
        int n = api->get_param(in, "module_id", buf, sizeof buf);
        CHECK(n > 0 && strcmp(buf, "heimat-solo") == 0, "module_id risponde heimat-solo (il manager apre la scheda Tool)");
        n = api->get_param(in, "state", buf, sizeof buf);
        int nested = 0, quotes = 0;
        for (int i = 1; i < n - 1; i++) { if (buf[i] == '{' || buf[i] == '[') nested++; if (buf[i] == '"') quotes++; }
        int allp = 1;
        for (int i = 0; i < P_COUNT; i++) { char k[64]; snprintf(k, sizeof k, "\"%s\":", g_params[i].key); if (!strstr(buf, k)) allp = 0; }
        printf("      state: %d byte\n", n);
        CHECK(n > 0 && buf[0] == '{' && buf[n - 1] == '}' && nested == 0 && quotes % 2 == 0, "state e' un oggetto JSON piatto (niente array o oggetti annidati)");
        CHECK(allp && strstr(buf, "\"v_mk\":\"") && strstr(buf, "\"v_pk\":\"") && strstr(buf, "\"v_fr\":") && strstr(buf, "\"rev\":"),
              "contiene tutti i parametri, i marker, la panoramica del reel e il contatore rev");
        const char *pk = strstr(buf, "\"v_pk\":\"") + 8;
        int hexok = 1, loud = 0;
        for (int i = 0; i < HM_RUI_COLS * 4; i++) if (!isxdigit((unsigned char)pk[i])) hexok = 0;
        for (int c = 0; c < HM_RUI_COLS; c++) { unsigned v; sscanf(pk + c * 4, "%2x", &v); if (v > 128 + 20) loud++; }
        CHECK(hexok && pk[HM_RUI_COLS * 4] == '"' && loud > HM_RUI_COLS / 4, "panoramica: %d colonne esadecimali, %d con segnale", HM_RUI_COLS, loud);
        char mk[256]; { const char *m = strstr(buf, "\"v_mk\":\"") + 8; int k = 0; while (m[k] && m[k] != '"' && k < 255) { mk[k] = m[k]; k++; } mk[k] = 0; }
        CHECK(strchr(mk, ',') != NULL, "i marker del file arrivano come lista (%s)", mk);

        char p1[64], p2[64];
        blocks(in, 0.3, NULL);
        api->get_param(in, "rui_poll", p1, sizeof p1); blocks(in, 0.5, NULL); api->get_param(in, "rui_poll", p2, sizeof p2);
        CHECK(strcmp(p1, p2) == 0, "a strumento fermo rui_poll non cambia (%s)", p1);
        setp(in, "splice_source", "Markers"); setp(in, "gene_window", "Tukey"); setf(in, "gene_size", 0.3); setf(in, "morph", 0.62);
        setf(in, "scatter", 0.3); setf(in, "rev_size", 0.7); setf(in, "rev_mix", 0.25); setf(in, "eq_low", 4); setf(in, "eq_mid", -5); setf(in, "eq_freq", 1800);
        setp(in, "pad_mode", "Drone"); key_on(in, 60); blocks(in, 0.1, NULL);
        api->get_param(in, "rui_poll", p1, sizeof p1); blocks(in, 0.5, NULL); api->get_param(in, "rui_poll", p2, sizeof p2);
        unsigned r1 = (unsigned)strtoul(p1, NULL, 10), r2 = (unsigned)strtoul(p2, NULL, 10);
        CHECK(r2 > r1 + 8 && r2 < r1 + 20, "mentre suona rui_poll avanza circa 25 volte al secondo (%u in 0.5 s)", r2 - r1);
        api->get_param(in, "state", buf, sizeof buf);
        { FILE *f = fopen("/tmp/heimat_remote_state.json", "w"); if (f) { fputs(buf, f); fclose(f); } }   /* used by tests/web_test.mjs */
        CHECK(strstr(buf, "\"v_gp\":\"\"") == NULL && strstr(buf, "\"v_run\":1"), "con i geni attivi la lista v_gp non e' vuota");
        setf(in, "morph", 0.4);
        api->get_param(in, "rui_poll", b2, sizeof b2);
        CHECK((unsigned)strtoul(b2, NULL, 10) > r2, "una modifica dal browser o dall'hardware fa avanzare rev");
        api->get_param(in, "state", buf, sizeof buf);
        setf(in, "morph", 0.9);
        setp(in, "state", buf);
        float mo = H(in)->p[P_MORPH];
        CHECK(fabsf(mo - 0.4f) < 1e-3f, "lo stato con i campi live si puo' riapplicare: i campi v_ sono ignorati");
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 39 */
    printf("\n39. HEADS: una rete di testine di lettura sul nastro (dall'ingresso)\n");
    {
#define HD_NEW(in) do { in = new_inst(); wait_blank(in); setf(in, "sos", 0); setf(in, "bpm", 120); setf(in, "hd_mix", 1.0); setf(in, "hd_fdbk", 0); blocks(in, 0.3, NULL); } while (0)
        void *in;
        HD_NEW(in);
        CHECK(H(in)->hd.idle == 0, "con Mix sopra zero le testine lavorano");
        /* timing: two heads, Spacing 0.5, 1/8 at 120 BPM: 250 ms and 250 x 1.535 = 384 ms */
        seti(in, "hd_heads", 2); setf(in, "hd_spacing", 0.5); setp(in, "hd_time", "1/8"); blocks(in, 0.3, NULL);
        rec_t r; rec_init(&r, 0.8);
        g_in_freq = 330; g_in_amp = 0.5; blocks(in, 0.05, &r); g_in_amp = 0; blocks(in, 0.7, &r);
        double gap = WIN(r.L, 0.08, 0.24), h1 = WIN(r.L, 0.256, 0.29), mid = WIN(r.L, 0.31, 0.37), h2 = WIN(r.L, 0.39, 0.425);
        printf("      dBFS: pausa %.1f, testina 1 %.1f, tra le due %.1f, testina 2 %.1f\n", gap, h1, mid, h2);
        CHECK(gap < -60 && mid < -50 && h1 > -20 && h2 > -20, "due testine: eco a 250 ms e a 384 ms, silenzio intorno");
        rec_free(&r);

        /* Chain against Own: one head heard; in Chain the sound goes round all four before it comes back */
        seti(in, "hd_heads", 1); setf(in, "hd_spacing", 0); setp(in, "hd_time", "1/16"); setf(in, "hd_fdbk", 0.9);
        double own2T, chain2T, chain5T;
        for (int k = 0; k < 2; k++) {
            setp(in, "hd_route", k ? "Chain" : "Own"); api->set_param(in, "hd_clear", "1"); blocks(in, 0.4, NULL);
            rec_init(&r, 1.0);
            g_in_amp = 0.5; blocks(in, 0.04, &r); g_in_amp = 0; blocks(in, 0.9, &r);
            if (!k) own2T = WIN(r.L, 0.252, 0.285);
            else { chain2T = WIN(r.L, 0.252, 0.285); chain5T = WIN(r.L, 0.627, 0.66); }
            rec_free(&r);
        }
        printf("      seconda eco a 2T: Own %.1f dBFS, Chain %.1f dBFS; Chain a 5T %.1f dBFS\n", own2T, chain2T, chain5T);
        CHECK(own2T > -25 && chain2T < -60 && chain5T > -30, "Chain: l'eco ritorna solo dopo aver fatto il giro delle quattro testine");
        setp(in, "hd_route", "Own"); setf(in, "hd_fdbk", 0);

        /* Reverse: a sound with a sharp attack and a decay comes back swelling and cut */
        double fwd_lead = 0, rev_lead = 0;
        setp(in, "hd_time", "1/4");
        for (int k = 0; k < 2; k++) {
            setf(in, "hd_reverse", k ? 1.0 : 0.0); api->set_param(in, "hd_clear", "1"); blocks(in, 0.6, NULL);
            rec_init(&r, 1.8);
            const int nb = (int)(0.12 * SR / BLK);
            for (int b = 0; b < nb; b++) { g_in_amp = 0.6 * (1.0 - (double)b / nb); blocks(in, BLK / (double)SR, &r); }
            g_in_amp = 0; blocks(in, 1.6, &r);
            /* a backwards head reads with two overlapping windows, so an echo can come back
               in two pieces: look at the shape of the loudest piece only */
            double env[200], top = 0; int best = -1;
            for (int w = 0; w < 160; w++) { env[w] = 0; for (int i = 0; i < (int)(0.01 * SR); i++) { float v = r.L[w * (int)(0.01 * SR) + i]; env[w] += v * v; } if (env[w] > top) top = env[w]; }
            double bestE = 0; int s0 = 0, s1 = 0;
            for (int w = 0; w < 160;) {
                if (env[w] < top * 1e-3) { w++; continue; }
                int a = w; double e = 0; while (w < 160 && env[w] >= top * 1e-3) e += env[w++];
                if (e > bestE) { bestE = e; s0 = a; s1 = w; best = a; }
            }
            double tot = 0, cen = 0; int amax = best;
            for (int w = s0; w < s1; w++) { tot += env[w]; cen += env[w] * w; if (env[w] > env[amax]) amax = w; }
            cen /= tot + 1e-30;
            (void)amax;
            double e1 = 0, e2 = 0; const double midw = 0.5 * (s0 + s1);
            for (int w = s0; w < s1; w++) { if (w + 0.5 < midw) e1 += env[w]; else e2 += env[w]; }
            const double tilt = 10 * log10((e1 + 1e-30) / (e2 + 1e-30));       /* first half against second, dB */
            if (!k) fwd_lead = tilt; else rev_lead = tilt;
            rec_free(&r);
        }
        printf("      prima meta' dell'eco contro la seconda: avanti %+.1f dB, al contrario %+.1f dB\n", fwd_lead, rev_lead);
        CHECK(fwd_lead > 3 && rev_lead < -3, "Reverse: l'attacco secco torna come una crescita che finisce di colpo");
        setf(in, "hd_reverse", 0);

        /* Rise and Fall: every pass an octave up or down */
        const char *modes[2] = { "Rise", "Fall" };
        for (int k = 0; k < 2; k++) {
            setp(in, "hd_mode", modes[k]); api->set_param(in, "hd_clear", "1"); blocks(in, 0.6, NULL);
            rec_init(&r, 0.9);
            g_in_freq = 330; g_in_amp = 0.5; blocks(in, 0.15, &r); g_in_amp = 0; blocks(in, 0.7, &r);
            const int a = (int)(0.53 * SR), n = (int)(0.08 * SR);
            double e330 = band_e(r.L + a, n, 330, 30), eto = band_e(r.L + a, n, k ? 165 : 660, 30);
            CHECK(eto > 20 * e330, "%s: la prima eco esce un'ottava %s (%.0f Hz %.1f dB sopra i 330 Hz)", modes[k], k ? "sotto" : "sopra",
                  k ? 165.0 : 660.0, 10 * log10((eto + 1e-30) / (e330 + 1e-30)));
            rec_free(&r);
        }
        setp(in, "hd_mode", "Fade");

        /* Freeze keeps what is on the tape, without feedback, and ignores new input; Clear wipes it */
        api->set_param(in, "hd_clear", "1"); blocks(in, 0.3, NULL);
        g_in_freq = 330; g_in_amp = 0.5; blocks(in, 0.1, NULL); g_in_amp = 0; blocks(in, 0.1, NULL);
        setp(in, "hd_freeze", "On"); blocks(in, 0.2, NULL);
        g_in_freq = 550; g_in_amp = 0.5;
        rec_init(&r, 2.1); blocks(in, 2.0, &r);
        double late = WIN(r.L, 1.5, 2.0), e330 = band_e(r.L + (int)(1.5 * SR), (int)(0.5 * SR), 330, 30), e550 = band_e(r.L + (int)(1.5 * SR), (int)(0.5 * SR), 550, 30);
        g_in_amp = 0; rec_free(&r);
        CHECK(late > -30 && e330 > 100 * e550, "Freeze: dopo 2 s il nastro ripete ancora i 330 Hz (%.1f dBFS) e non registra i 550 Hz nuovi", late);
        api->set_param(in, "hd_clear", "1"); blocks(in, 0.3, NULL);
        rec_init(&r, 0.6); blocks(in, 0.5, &r);
        double cleared = WIN(r.L, 0.0, 0.5); rec_free(&r);
        CHECK(cleared < -70, "Clear: le testine tacciono subito (%.1f dBFS), senza toccare la memoria nel thread audio", cleared);
        setp(in, "hd_freeze", "Off");

        /* stability: every wear and routing, feedback at maximum, a tone into it */
        int bad = 0; double worst = -200;
        const char *wear[6] = { "Dark", "Thin", "Crush", "Warm", "Fold", "Fuzz" }, *route[4] = { "Own", "Cross", "Chain", "Wander" };
        setf(in, "hd_fdbk", 1.0); setf(in, "hd_wear_amt", 1.0); seti(in, "hd_heads", 4); setf(in, "hd_reverse", 0.5); setp(in, "hd_mode", "Rise");
        g_in_freq = 220; g_in_amp = 0.5;
        for (int a = 0; a < 6; a++) for (int b = 0; b < 4; b++) {
            setp(in, "hd_wear", wear[a]); setp(in, "hd_route", route[b]);
            rec_init(&r, 0.6); blocks(in, 0.5, &r);
            for (int i = 0; i < r.n; i++) if (!isfinite(r.L[i]) || !isfinite(r.R[i])) bad++;
            double lv = WIN(r.L, 0.3, 0.5); if (lv > worst) worst = lv;
            rec_free(&r);
        }
        CHECK(bad == 0 && worst < 0.5, "sei usure x quattro percorsi con feedback al massimo: nessun NaN, uscita limitata (max %.1f dBFS)", worst);

        /* cost at full load, then idle again */
        rec_init(&r, 10.1);
        double t0 = now_s(); blocks(in, 10.0, &r); double el = now_s() - t0;
        rec_free(&r); g_in_amp = 0;
        printf("      otto testine, Rise, Reverse, Fold: 10 s in %.3f s (%.2f%% del tempo reale su questo computer)\n", el, el / 10.0 * 100.0);
        TCHECK(el < 0.5, "costo contenuto a pieno carico");
        setf(in, "hd_mix", 0); blocks(in, 1.0, NULL);
        CHECK(H(in)->hd.idle == 1, "Mix a zero: le testine si fermano e non costano nulla");
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 40 */
    printf("\n40. Gene Reverse e Spread Intervals\n");
    {
        void *in = inst_with("sine16m.wav", 1);
        hm_t *h = H(in);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.5650); setf(in, "morph", 0.5);
        key_on(in, 60); blocks(in, 0.3, NULL);

        /* count forward and backward genes as they are born */
        int fwd = 0, bwd = 0, moved_back = 1;
        const double shares[3] = { 0.0, 0.5, 1.0 };
        int got[3][2];
        for (int k = 0; k < 3; k++) {
            setf(in, "gene_reverse", shares[k]); blocks(in, 0.3, NULL);
            fwd = bwd = 0;
            double pos0[HM_MAX_GENES]; int act0[HM_MAX_GENES];
            for (int b = 0; b < 700; b++) {
                for (int i = 0; i < HM_MAX_GENES; i++) { act0[i] = h->genes[i].active && h->genes[i].age > 0; pos0[i] = h->genes[i].pos; }
                blocks(in, (double)BLK / SR + 1e-9, NULL);
                for (int i = 0; i < HM_MAX_GENES; i++) {
                    const gene_t *g = &h->genes[i];
                    if (!g->active) continue;
                    if (g->age > 0 && g->age <= BLK) { if (g->pitch < 0) bwd++; else fwd++; }
                    if (k == 2 && act0[i] && g->age > BLK && g->pos > pos0[i] + 1e-6) moved_back = 0;
                }
            }
            got[k][0] = fwd; got[k][1] = bwd;
        }
        printf("      geni avanti/indietro: 0%% %d/%d, 50%% %d/%d, 100%% %d/%d\n",
               got[0][0], got[0][1], got[1][0], got[1][1], got[2][0], got[2][1]);
        CHECK(got[0][1] == 0 && got[0][0] > 30, "Gene Reverse a 0: tutti i geni seguono il nastro");
        CHECK(got[2][0] == 0 && got[2][1] > 30, "Gene Reverse al 100%%: tutti i geni vanno al contrario");
        const double share = (double)got[1][1] / (double)(got[1][0] + got[1][1]);
        CHECK(share > 0.35 && share < 0.65, "Gene Reverse al 50%%: avanti e indietro si mescolano (%.0f%% indietro)", share * 100.0);
        CHECK(moved_back, "un gene al contrario legge davvero il nastro all'indietro");
        setf(in, "gene_reverse", 0.0);

        /* the intervals a gene may take, straight from the drawing function */
        static const char *mn[6] = { "Free", "Semitones", "Octaves", "Fifths", "Major", "Minor" };
        static const float allow[6][7] = {
            { 0 }, { 0 }, { 0, 12, -12, 99, 99, 99, 99 }, { 0, 5, -5, 7, -7, 12, -12 },
            { 0, 4, -4, 7, -7, 12, -12 }, { 0, 3, -3, 7, -7, 12, -12 } };
        int ok_sets = 1, all_seen = 1, free_ok = 1, semi_ok = 1;
        for (int m = 0; m < 6; m++) {
            int hits[7] = { 0 };
            for (int i = 0; i < 4000; i++) {
                const float st = spread_semitones(h, 12.0f, m);
                if (m == 0) { if (fabsf(st) > 12.0f) free_ok = 0; continue; }
                if (m == 1) { if (st != floorf(st) || fabsf(st) > 12.0f) semi_ok = 0; continue; }
                int j = -1;
                for (int q = 0; q < 7; q++) if (allow[m][q] == st) j = q;
                if (j < 0) ok_sets = 0; else hits[j]++;
            }
            if (m >= 2) for (int q = 0; q < 7; q++) if (allow[m][q] != 99 && hits[q] < 100) all_seen = 0;
            (void)mn;
        }
        CHECK(free_ok && semi_ok, "Free resta un detune libero entro +-12; Semitones da' solo semitoni interi");
        CHECK(ok_sets && all_seen, "Octaves, Fifths, Major, Minor: solo i loro intervalli (su e giu') e l'unisono, tutti usati");
        int narrow = 1;
        for (int i = 0; i < 2000; i++) if (spread_semitones(h, 6.0f, 2) != 0.0f) narrow = 0;
        int fifths6 = 1;
        for (int i = 0; i < 2000; i++) { float st = spread_semitones(h, 6.0f, 3); if (st != 0 && fabsf(st) != 5.0f) fifths6 = 0; }
        CHECK(narrow && fifths6, "Spread e' il salto piu' ampio: a 6 st le ottave restano ferme, le quinte offrono solo la quarta");

        /* and by ear: a 440 Hz reel in Octaves turns into 220, 440, 880 and nothing in between */
        setf(in, "spread", 12); setp(in, "spread_mode", "Octaves"); blocks(in, 0.5, NULL);
        rec_t r; rec_init(&r, 2.1); blocks(in, 2.0, &r);
        const int n = (int)(2.0 * SR);
        const double e220 = band_e(r.L, n, 220, 8), e440 = band_e(r.L, n, 440, 8), e880 = band_e(r.L, n, 880, 8);
        const double e587 = band_e(r.L, n, 587.3, 8), e659 = band_e(r.L, n, 659.3, 8);
        printf("      Octaves, energia: 220 %.2g, 440 %.2g, 880 %.2g; quarta %.2g, quinta %.2g\n", e220, e440, e880, e587, e659);
        CHECK(e220 > 20 * e659 && e880 > 20 * e659 && e440 > 20 * e587, "in Octaves si sentono 220, 440 e 880 Hz, niente quarte o quinte");
        rec_free(&r);
        setp(in, "spread_mode", "Fifths"); blocks(in, 0.5, NULL);
        rec_init(&r, 2.1); blocks(in, 2.0, &r);
        const double f659 = band_e(r.L, n, 659.3, 8), f330 = band_e(r.L, n, 329.6, 8), f500 = band_e(r.L, n, 500, 8);
        printf("      Fifths, energia: quinta su %.2g, quarta giu' %.2g, a 500 Hz %.2g\n", f659, f330, f500);
        CHECK(f659 > 20 * f500 && f330 > 20 * f500, "in Fifths compaiono quinte e quarte (659 e 330 Hz), non un detune qualsiasi");
        rec_free(&r);
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 41 */
    printf("\n41. Compatibilita': chiavi congelate e sessioni delle versioni precedenti\n");
    {
        /* every key of the first beta is still there, with the same type, and no enum lost options */
        FILE *f = fopen("tests/fixtures/params_0.7.0.txt", "r");
        int listed = 0, kept = 0;
        char line[256];
        while (f && fgets(line, sizeof line, f)) {
            char key[64], type[16]; int nopt = 0;
            if (line[0] == '#' || sscanf(line, "%63s %15s %d", key, type, &nopt) != 3) continue;
            listed++;
            /* retired in 0.8.0 with the old delay; their values in old sessions are ignored */
            static const char *const retired[] = { "dly_time", "dly_sync", "dly_fdbk", "dly_tone", "dly_mix", "dly_ping" };
            int gone = 0;
            for (size_t q = 0; q < sizeof retired / sizeof *retired; q++) if (!strcmp(key, retired[q])) gone = 1;
            const int i = param_index(key);
            if (gone) { if (i < 0) kept++; else printf("      %s doveva essere ritirata\n", key); continue; }
            if (i < 0) { printf("      manca la chiave %s\n", key); continue; }
            const pdef_t *d = &g_params[i];
            const char *t = d->type == T_FLOAT ? "float" : d->type == T_INT ? "int" : "enum";
            if (strcmp(t, type) != 0) { printf("      %s: era %s, ora %s\n", key, type, t); continue; }
            if (d->type == T_ENUM && d->nopts < nopt) { printf("      %s: opzioni da %d a %d\n", key, nopt, d->nopts); continue; }
            kept++;
        }
        if (f) fclose(f);
        CHECK(listed >= 62 && kept == listed, "le %d chiavi della 0.7.0 esistono ancora (tranne le 6 del delay, ritirate), stesso tipo, nessuna opzione persa", listed);
        /* and from 0.8.0 on, nothing is retired any more */
        f = fopen("tests/fixtures/params_0.8.0.txt", "r");
        listed = kept = 0;
        while (f && fgets(line, sizeof line, f)) {
            char key[64], type[16]; int nopt = 0;
            if (line[0] == '#' || sscanf(line, "%63s %15s %d", key, type, &nopt) != 3) continue;
            listed++;
            const int i = param_index(key);
            if (i < 0) { printf("      manca la chiave %s\n", key); continue; }
            const pdef_t *d = &g_params[i];
            const char *t = d->type == T_FLOAT ? "float" : d->type == T_INT ? "int" : "enum";
            if (strcmp(t, type) != 0 || (d->type == T_ENUM && d->nopts < nopt)) { printf("      %s cambiata\n", key); continue; }
            kept++;
        }
        if (f) fclose(f);
        CHECK(listed == 62 && kept == listed, "le %d chiavi della 0.8.0 esistono tutte, stesso tipo, nessuna opzione persa", listed);
        /* the newest list names every key there is */
        f = fopen("tests/fixtures/params_0.14.0.txt", "r");
        listed = kept = 0;
        while (f && fgets(line, sizeof line, f)) {
            char key[64], type[16]; int nopt = 0;
            if (line[0] == '#' || sscanf(line, "%63s %15s %d", key, type, &nopt) != 3) continue;
            listed++;
            const int i = param_index(key);
            if (i < 0) { printf("      manca la chiave %s\n", key); continue; }
            const pdef_t *d = &g_params[i];
            const char *t = d->type == T_FLOAT ? "float" : d->type == T_INT ? "int" : "enum";
            if (strcmp(t, type) != 0 || (d->type == T_ENUM && d->nopts < nopt)) { printf("      %s cambiata\n", key); continue; }
            kept++;
        }
        if (f) fclose(f);
        CHECK(listed == P_COUNT && kept == listed, "le %d chiavi della 0.14.0 sono tutte quelle che ci sono", listed);

        static const char *const files[3] = { "tests/fixtures/state_0.5.0.json", "tests/fixtures/state_0.6.0.json", "tests/fixtures/state_0.7.0.json" };
        char *js = (char *)malloc(HM_STATE_MAX);
        for (int v = 0; v < 3; v++) {
            void *in = new_inst(); hm_t *h = H(in);
            /* a used instance: newer controls away from their default, as after another patch */
            setf(in, "gene_reverse", 0.9); setp(in, "spread_mode", "Minor"); setf(in, "hd_mix", 0.8); setf(in, "volume", 0.3);
            FILE *g = fopen(files[v], "rb");
            size_t n = g ? fread(js, 1, HM_STATE_MAX - 1, g) : 0;
            if (g) fclose(g);
            js[n] = 0;
            setp(in, "state", js);
            const int ok_old = fabsf(h->p[P_MORPH] - 0.62f) < 1e-3f && fabsf(h->p[P_SCATTER] - 0.3f) < 1e-3f && (int)h->p[P_BPM] == 97;
            const int ok_vol = fabsf(h->p[P_VOLUME] - 0.8f) < 1e-3f;
            if (v == 0) {
                CHECK(ok_old && ok_vol && h->p[P_HD_MIX] == 0.0f && h->p[P_GENE_REV] == 0.0f && h->p[P_SPR_MODE] == 0.0f,
                      "una sessione della 0.5.0 si apre: i suoi valori restano, HEADS e i controlli nuovi tornano a zero");
            } else if (v == 1) {
                CHECK(ok_old && fabsf(h->p[P_HD_MIX] - 0.35f) < 1e-3f && (int)h->p[P_HD_ROUTE] == 2 && h->p[P_GENE_REV] == 0.0f && h->p[P_SPR_MODE] == 0.0f,
                      "una sessione della 0.6.0 si apre: HEADS come era salvato, Gene Reverse e Intervals ai valori iniziali");
            } else {
                CHECK(ok_old && fabsf(h->p[P_GENE_REV] - 0.4f) < 1e-3f && (int)h->p[P_SPR_MODE] == 3 && (int)h->p[P_HD_WEAR] == 4 && fabsf(h->p[P_SPREAD] - 12.0f) < 1e-3f,
                      "una sessione della 0.7.0 torna identica");
            }
            api->destroy_instance(in);
        }
        /* a state from a later version, with keys this one does not know, still loads */
        {
            void *in = new_inst(); hm_t *h = H(in);
            setp(in, "state", "{\"sample_path\":\"\",\"state_format\":9,\"morph\":0.5,\"knob_of_the_future\":3,\"tape_age\":0.7}");
            CHECK(fabsf(h->p[P_MORPH] - 0.5f) < 1e-3f && fabsf(h->p[P_VOLUME] - 0.8f) < 1e-3f, "una sessione di una versione futura: le chiavi sconosciute vengono ignorate");
            /* the state written now says its format, so a later version can tell */
            api->get_param(in, "state", js, HM_STATE_MAX);
            CHECK(strstr(js, "\"state_format\":1") != NULL, "lo stato salvato dichiara il suo formato (1)");
            api->destroy_instance(in);
        }
        free(js);
    }
    /* --------------------------------------------------------------- 42 */
    printf("\n42. Misuratore di carico per la beta\n");
    {
        void *in = inst_with("sine16m.wav", 1);
        setp(in, "pad_mode", "Drone"); setf(in, "gene_size", 0.3); setf(in, "morph", 0.9); setf(in, "rev_mix", 0.3);
        key_on(in, 60); blocks(in, 2.0, NULL);
        char b[64]; float avg = -1, pk = -1; unsigned over = 99;
        api->get_param(in, "dsp_load", b, sizeof b);
        sscanf(b, "%f %f %u", &avg, &pk, &over);
        printf("      dsp_load \"%s\"\n", b);
        CHECK(avg > 0.0f && avg < 1.0f && pk >= avg, "dsp_load: media sopra zero e sotto il tempo reale, picco non sotto la media");
        char js[2048]; api->get_param(in, "ui_state", js, sizeof js);
        CHECK(strstr(js, "\"cpu\":") && strstr(js, "\"cpk\":") && strstr(js, "\"ovr\":"), "ui_state porta cpu, cpk e ovr alla schermata About");
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 43 */
    printf("\n43. Pattern: il caso che si ripete ogni N geni\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0);
        hm_t *h = H(in);
        setp(in, "splice_source", "Markers"); setf(in, "gene_size", 0.5);
        setf(in, "scatter", 1.0); setf(in, "spread", 7.0); setf(in, "gene_reverse", 0.5); setf(in, "width", 1.0); setf(in, "morph", 0.95);
        blocks(in, 0.1, NULL);
        /* one gene at a time, straight from the engine: where it starts, how it sounds, where it sits, what comes next */
        #define PAT_TAKE(arr, n) do { for (int k_ = 0; k_ < (n); k_++) { \
            for (int i_ = 0; i_ < HM_MAX_GENES; i_++) h->genes[i_].active = 0; \
            double sp_ = spawn_gene(h, k_ % 4, 1.0f); \
            arr[k_][0] = h->genes[0].pos; arr[k_][1] = h->genes[0].pitch; arr[k_][2] = h->genes[0].gl; arr[k_][3] = sp_; } } while (0)
        double a[16][4], b[16][4], c[16][4];
        setp(in, "pattern", "4"); seti(in, "pattern_seed", 7); retrigger(h);
        PAT_TAKE(a, 16);
        int rep = 1, varied = 0;
        for (int k = 4; k < 16; k++) for (int q = 0; q < 4; q++) if (fabs(a[k][q] - a[k - 4][q]) > 1e-9) rep = 0;
        for (int k = 1; k < 4; k++) if (fabs(a[k][0] - a[0][0]) > 1.0) varied++;
        CHECK(rep && varied == 3, "Pattern 4: inizio, intonazione, posizione e distanza tornano identici ogni 4 geni, e i 4 sono diversi tra loro");
        retrigger(h); PAT_TAKE(b, 4);
        CHECK(fabs(b[0][0] - a[0][0]) < 1e-9 && fabs(b[3][3] - a[3][3]) < 1e-9, "dopo un nuovo attacco il Pattern ricomincia dal primo gene");
        seti(in, "pattern_seed", 8); retrigger(h); PAT_TAKE(c, 4);
        int diff = 0; for (int k = 0; k < 4; k++) if (fabs(c[k][0] - a[k][0]) > 1.0) diff++;
        CHECK(diff >= 3, "un altro seme, un'altra frase (%d geni su 4 diversi)", diff);
        const uint32_t r0 = h->rng;
        setp(in, "pattern", "Off"); retrigger(h); PAT_TAKE(c, 8);
        int same = 0; for (int k = 4; k < 8; k++) if (fabs(c[k][0] - c[k - 4][0]) < 1e-9) same++;
        CHECK(same == 0 && h->rng != r0, "Pattern Off: il caso scorre libero come prima");
        #undef PAT_TAKE
        /* and it sounds: a cloud with Pattern on plays */
        setp(in, "pattern", "8"); setp(in, "pad_mode", "Drone"); key_on(in, 60);
        rec_t r; rec_init(&r, 1.0); blocks(in, 1.0, &r);
        CHECK(rms_db(r.L, r.n) > -40.0, "con Pattern la nuvola suona (%.1f dB)", rms_db(r.L, r.n));
        rec_free(&r);
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 44 */
    printf("\n44. Nastro da looper: lunghezza fissa, sovraincisione dopo, testina a Vari-Speed\n");
    {
        /* Record Length: one bar at 120 BPM, then it stops by itself and plays */
        void *in = new_inst(); wait_blank(in);
        g_in_freq = 330; g_in_amp = 0.3;
        setf(in, "bpm", 120); setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_length", "1 bar");
        setp(in, "rec_new", "1"); blocks(in, 3.0, NULL);
        double fr = ui_num(in, "fr");
        CHECK(ui_num(in, "rec") == 0 && fabs(fr - 2.0 * SR) <= BLK, "1 bar a 120 BPM: si ferma da sola a %.0f frame (attesi 88200)", fr);
        api->destroy_instance(in);

        /* After Recording = Overdub: the loop closes and recording goes on over it */
        in = new_inst(); wait_blank(in); hm_t *h = H(in);
        setf(in, "bpm", 120); setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_length", "1 bar"); setp(in, "rec_after", "Overdub");
        g_in_freq = 330; g_in_amp = 0.3;
        setp(in, "rec_new", "1"); blocks(in, 2.5, NULL);
        fr = ui_num(in, "fr");
        CHECK(ui_num(in, "rec") == REC_CUR && fabs(fr - 2.0 * SR) <= BLK, "a fine battuta passa in sovraincisione sullo stesso loop (%.0f frame)", fr);
        g_in_freq = 660; blocks(in, 2.2, NULL);
        setp(in, "rec_stop", "1"); blocks(in, 0.05, NULL);
        const float *L = h->reel->l; int n = (int)(1.5 * SR);
        float *tmp = (float *)malloc(sizeof(float) * (size_t)n); for (int i = 0; i < n; i++) tmp[i] = L[(int)(0.2 * SR) + i];
        double f = peak_freq(tmp, n, 200, 900, 1.0); free(tmp);
        CHECK(ui_num(in, "rec") == 0 && fabs(ui_num(in, "fr") - fr) < 1 && fabs(f - 660) < 8, "il secondo giro ha scritto sopra il primo (%.0f Hz), lunghezza invariata; stop ferma tutto", f);
        g_in_amp = 0; api->destroy_instance(in);

        /* Overdub Head = Vari-Speed */
        in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0); h = H(in);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", 0.35);
        setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_head", "Vari-Speed");
        setf(in, "vari_speed", -0.5);                                            /* -1x: backwards */
        key_on(in, 60); blocks(in, 0.6, NULL);
        g_in_amp = 0.0;
        setp(in, "rec_cur", "1"); blocks(in, 0.1, NULL);
        const uint32_t lo = h->rec_lo, len = h->rec_len;
        double p0 = ui_num(in, "rpos"); blocks(in, 0.2, NULL); double p1 = ui_num(in, "rpos");
        double d = p1 - p0; if (d > len / 2.0) d -= len; if (d < -(double)len / 2.0) d += len;
        CHECK(fabs(d + 0.2 * SR) < 2 * BLK, "Vari-Speed all'indietro: la testina di scrittura torna indietro (%.0f frame in 0.2 s)", d);
        setp(in, "rec_stop", "1"); blocks(in, 0.05, NULL);
        setf(in, "vari_speed", 1.0);                                              /* 2x */
        blocks(in, 0.4, NULL);
        setp(in, "rec_cur", "1"); blocks(in, (double)len / SR / 2.0 + 0.15, NULL);
        setp(in, "rec_stop", "1");
        float mx = 0; for (uint32_t i = lo; i < lo + len; i++) if (fabsf(h->reel->l[i]) > mx) mx = fabsf(h->reel->l[i]);
        CHECK(mx < 1e-6f, "a 2x un giro dura mezzo splice e non lascia buchi: tutto lo splice e' stato riscritto (resto %.4f)", mx);
        setf(in, "vari_speed", 0.0); blocks(in, 0.4, NULL);
        g_in_freq = 500; g_in_amp = 0.4;
        setp(in, "rec_cur", "1"); blocks(in, 0.3, NULL); setp(in, "rec_stop", "1");
        mx = 0; for (uint32_t i = lo; i < lo + len; i++) if (fabsf(h->reel->l[i]) > mx) mx = fabsf(h->reel->l[i]);
        CHECK(mx < 0.01f, "nastro fermo: la testina non scrive (%.4f)", mx);
        g_in_amp = 0; api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 45 */
    printf("\n45. Modulatori: Wave, Random, Follow, Envelope e i loro percorsi\n");
    {
        void *in = inst_with("sine16m.wav", 1); hm_t *h = H(in);
        char b[1024];
        /* Wave at 1 Hz onto Volume, depth 50%: the engine moves, the knob stays */
        setf(in, "volume", 0.5); setf(in, "mod_wave_rate", log(50.0) / log(1000.0));
        setp(in, "mod_set", "0 volume 0.5");
        float mn = 9, mx = -9; int cross = 0; float prev = 0.5f;
        for (int k = 0; k < (int)(2.0 * SR / BLK); k++) {
            blocks(in, (double)BLK / SR, NULL);
            const float v = h->p[P_VOLUME];
            if (v < mn) mn = v;
            if (v > mx) mx = v;
            if ((prev - 0.5f) * (v - 0.5f) < 0) cross++;
            prev = v;
        }
        api->get_param(in, "volume", b, sizeof b);
        CHECK(mn < 0.03f && mx > 0.97f && cross >= 3 && cross <= 5 && fabs(atof(b) - 0.5) < 1e-3,
              "Wave 1 Hz su Volume al 50%%: il volume va da %.2f a %.2f (%d passaggi in 2 s), il knob resta a %.2f", mn, mx, cross, atof(b));
        api->get_param(in, "mod_routes", b, sizeof b);
        CHECK(strcmp(b, "volume:0:0.500:1") == 0, "mod_routes: \"%s\" (Wave: bipolare)", b);
        /* a knob can be turned while modulated: the modulation follows it */
        setf(in, "volume", 0.2); blocks(in, 0.6, NULL);
        CHECK(h->p[P_VOLUME] <= 0.7f + 1e-3f && h->base[P_VOLUME] == 0.2f, "girando il knob la modulazione si sposta con lui");
        setp(in, "mod_set", "0 volume 0");
        CHECK(h->n_mr == 0 && h->p[P_VOLUME] == 0.2f, "profondita' 0: il percorso sparisce e il controllo torna al knob");
        setp(in, "mod_set", "0 pad_mode 0.5");
        CHECK(h->n_mr == 0, "le scelte (enum) non si modulano");
        for (int k = 0; k < 20; k++) { char m[64]; snprintf(m, sizeof m, "%d %s 0.3", k % 4, g_params[k % 5].key); setp(in, "mod_set", m); }
        CHECK(h->n_mr <= HM_MAX_ROUTES && h->n_mr == 16, "al massimo 16 percorsi (%d)", h->n_mr);
        setp(in, "mod_clear", "2");
        int left2 = 0; for (int r = 0; r < h->n_mr; r++) if (h->mr[r].src == 2) left2++;
        CHECK(left2 == 0 && h->n_mr > 0, "mod_clear di una sorgente toglie solo i suoi percorsi");
        setp(in, "mod_clear", "all");
        CHECK(h->n_mr == 0 && fabsf(h->p[P_GENE] - h->base[P_GENE]) < 1e-6f, "mod_clear all: nessun percorso, tutto al knob");

        /* Random, stepped at 1/4 note at 120 BPM: two new values a second */
        setp(in, "mod_rand_sync", "1/4"); setf(in, "bpm", 120); blocks(in, 0.1, NULL);
        float last = h->mod[1]; int changes = 0;
        for (int k = 0; k < (int)(2.0 * SR / BLK); k++) { blocks(in, (double)BLK / SR, NULL); if (h->mod[1] != last) changes++; last = h->mod[1]; }
        CHECK(changes >= 3 && changes <= 5, "Random a gradini, 1/4 a 120 BPM: %d valori nuovi in 2 s", changes);
        setf(in, "mod_rand_smooth", 1.0); changes = 0;
        for (int k = 0; k < (int)(1.0 * SR / BLK); k++) { blocks(in, (double)BLK / SR, NULL); if (h->mod[1] != last) changes++; last = h->mod[1]; }
        CHECK(changes > 200, "Random con Smooth: scivola da un valore all'altro (%d blocchi su 344 diversi)", changes);

        /* Follow the input onto Scatter */
        setp(in, "mod_set", "2 scatter 1"); setf(in, "scatter", 0);
        g_in_freq = 300; g_in_amp = 0.5; blocks(in, 0.5, NULL);
        const float up = h->p[P_SCATTER];
        g_in_amp = 0; blocks(in, 3.5, NULL);
        CHECK(up > 0.8f && h->p[P_SCATTER] < 0.02f, "Follow: l'ingresso alza Scatter (%.2f), il silenzio lo riporta giu' (%.3f)", up, h->p[P_SCATTER]);
        setp(in, "mod_clear", "all");

        /* Envelope from the pads */
        setf(in, "mod_env_attack", 0); setf(in, "mod_env_decay", 0.4);       /* 1 ms, 30 ms */
        key_on(in, 60); blocks(in, 0.01, NULL);
        const float pk = h->mod[3];
        blocks(in, 0.2, NULL);
        CHECK(pk > 0.5f && h->mod[3] == 0.0f, "Envelope AD da un pad: sale (%.2f) e ricade a zero", pk);
        key_off(in, 60); setp(in, "mod_env_mode", "Hold");
        key_on(in, 60); blocks(in, 0.5, NULL);
        const float held = h->mod[3];
        key_off(in, 60); blocks(in, 0.3, NULL);
        CHECK(held > 0.99f && h->mod[3] == 0.0f, "Hold: resta su finche' il pad e' premuto, poi ricade");
        setp(in, "mod_env_mode", "AD"); setp(in, "mod_env_trigger", "Clock"); setp(in, "clock_source", "Internal"); setp(in, "clock_div", "1/4");
        int fires = 0; float pe = 0;
        for (int k = 0; k < (int)(2.0 * SR / BLK); k++) { blocks(in, (double)BLK / SR, NULL); if (h->mod[3] > 0.5f && pe <= 0.5f) fires++; pe = h->mod[3]; }
        key_on(in, 61); blocks(in, 0.005, NULL);
        CHECK(fires >= 3 && fires <= 5, "trigger Clock: un inviluppo per tick (%d in 2 s a 1/4, 120 BPM)", fires);
        key_off(in, 61); setp(in, "clock_source", "Off");

        /* routes are saved with the session and come back */
        setp(in, "mod_set", "1 gene_size -0.25"); setp(in, "mod_set", "3 rev_mix 0.6");
        char *js = (char *)malloc(HM_STATE_MAX);
        api->get_param(in, "state", js, HM_STATE_MAX);
        void *in2 = new_inst(); hm_t *h2 = H(in2);
        setp(in2, "state", js);
        api->get_param(in2, "mod_routes", b, sizeof b);
        CHECK(h2->n_mr == 2 && strstr(b, "gene_size:1:-0.250") && strstr(b, "rev_mix:3:0.600"), "i percorsi tornano con lo stato salvato: \"%s\"", b);
        setp(in2, "state", "{\"sample_path\":\"\",\"morph\":0.4}");
        CHECK(h2->n_mr == 0, "uno stato completo senza percorsi (versioni precedenti) li toglie");
        setp(in2, "state", "{\"morph\":0.5,\"mod_routes\":\"width:0:0.400,unknown_key:1:0.5\"}");
        CHECK(h2->n_mr == 1 && h2->mr[0].tgt == P_WIDTH, "chiavi sconosciute nei percorsi: ignorate");
        free(js);
        api->destroy_instance(in2);
        char js2[2048]; api->get_param(in, "ui_state", js2, sizeof js2);
        CHECK(strstr(js2, "\"mrv\":") && strstr(js2, "\"mo\":["), "ui_state porta la versione dei percorsi e i quattro valori");
        api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 46 */
    printf("\n46. Ritagliare: una splice, l'annullamento, il rumore dei tasti\n");
    {
        void *in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0); hm_t *h = H(in);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone");
        key_on(in, 60); setp(in, "sel_splice", "1"); blocks(in, 0.2, NULL);
        uint32_t mk0[64]; const int n0 = markers_of(in, mk0);
        const double fr0 = ui_num(in, "fr");
        const uint32_t lo = (uint32_t)ui_num(in, "lo"), hi = (uint32_t)ui_num(in, "hi");
        const uint32_t cut = (uint32_t)(0.25 * SR);
        char m[64]; snprintf(m, sizeof m, "%u,%u", lo + cut, hi - cut);
        setp(in, "trim_set", m); blocks(in, 0.6, NULL);
        int inside = 1, seen = 0;
        for (int i = 0; i < HM_MAX_GENES; i++) if (h->genes[i].active) { seen++; if (h->genes[i].lo != lo + cut || h->genes[i].len != hi - lo - 2 * cut) inside = 0; }
        CHECK(ui_num(in, "ton") == 1 && seen > 0 && inside, "anteprima: i geni suonano solo la parte che resta (%d geni)", seen);
        setp(in, "trim_apply", "1");
        for (int k = 0; k < 400 && fabs(ui_num(in, "fr") - fr0) < 1; k++) { blocks(in, 0.003, NULL); struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL); }
        uint32_t mk1[64]; const int n1 = markers_of(in, mk1);
        CHECK(fabs(ui_num(in, "fr") - (fr0 - 2.0 * cut)) < 1 && ui_num(in, "ton") == 0, "applicato: il reel e' piu' corto di 0.5 s (%.0f -> %.0f)", fr0, ui_num(in, "fr"));
        CHECK(n1 == n0 && mk1[0] == mk0[0] && mk1[1] == mk0[1] - 2 * cut && ui_num(in, "sel") == 1 && fabs(ui_num(in, "hi") - ui_num(in, "lo") - (hi - lo - 2.0 * cut)) < 1,
              "i marker dopo il taglio arretrano, la splice resta selezionata e dura 0.5 s in meno");
        const double fade_r = reel_rms(in, lo, lo + 40) / reel_rms(in, lo + 4410, lo + 4450);
        CHECK(fade_r < 0.25, "una breve dissolvenza dove il suono e' stato tagliato (primo millisecondo a %.2f)", fade_r);
        setp(in, "pad_mode", "Splice");
        key_on(in, 36 + 1); double f = freq_of(in, 0.3, 150, 1100); key_off(in, 37);
        key_on(in, 36 + 2); double f2 = freq_of(in, 0.3, 150, 1100); key_off(in, 38);
        CHECK(fabs(f - 300) < 6 && fabs(f2 - 450) < 9, "la splice ritagliata e quella dopo suonano come prima (%.0f / %.0f Hz)", f, f2);
        setp(in, "undo", "1"); blocks(in, 0.05, NULL);
        uint32_t mk2[64]; const int n2 = markers_of(in, mk2);
        CHECK(fabs(ui_num(in, "fr") - fr0) < 1 && n2 == n0 && mk2[1] == mk0[1], "Undo: torna il reel di prima, con i suoi marker");
        /* in Divide mode the divisions become markers */
        setp(in, "splice_source", "Divide"); setf(in, "divide", 4); setp(in, "sel_splice", "0"); blocks(in, 0.05, NULL);
        const uint32_t dhi = (uint32_t)ui_num(in, "hi"), dfr = (uint32_t)ui_num(in, "fr");
        snprintf(m, sizeof m, "%u,%u", 0u, dhi - cut); setp(in, "trim_set", m); setp(in, "trim_apply", "1");
        for (int k = 0; k < 400 && fabs(ui_num(in, "fr") - dfr) < 1; k++) { blocks(in, 0.003, NULL); struct timespec ts = { 0, 2000000 }; nanosleep(&ts, NULL); }
        CHECK(h->p[P_SPLICE_SRC] == 0 && h->n_mk == 3 && h->mk[0] == dhi - cut, "da Divide: le divisioni diventano marker, la prima arretra (%u)", h->n_mk ? h->mk[0] : 0);
        api->destroy_instance(in);

        /* Trim Key Noise on a new splice: the press at the start is skipped, the one at the end taken off */
        in = new_inst(); wait_blank(in);
        g_in_freq = 300; g_in_amp = 0.3;
        setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_trim", "100 ms");
        setp(in, "rec_new", "1"); blocks(in, 1.0, NULL); setp(in, "rec_stop", "1"); blocks(in, 0.05, NULL);
        double fr = ui_num(in, "fr");
        CHECK(fabs(fr - 0.8 * SR) <= BLK + 2, "1 s premuto: restano 0.8 s (%.0f frame), 100 ms in meno all'inizio e alla fine", fr);
        api->destroy_instance(in);
        /* ... and on an overdub: what the last frames replaced comes back */
        in = new_inst(); mkpath(p, sizeof p, "tones.wav"); load_wait(in, p, 10.0); h = H(in);
        setp(in, "splice_source", "Markers"); setp(in, "pad_mode", "Drone"); setf(in, "organize", 0.0);
        setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_trim", "200 ms");
        key_on(in, 60); blocks(in, 0.2, NULL);
        g_in_amp = 0;
        const uint32_t s0 = (uint32_t)ui_num(in, "lo");
        setp(in, "rec_cur", "1"); blocks(in, 0.5, NULL); setp(in, "rec_stop", "1"); blocks(in, 0.05, NULL);
        const double r_a = reel_rms(in, s0 + (uint32_t)(0.02 * SR), s0 + (uint32_t)(0.17 * SR));
        const double r_b = reel_rms(in, s0 + (uint32_t)(0.21 * SR), s0 + (uint32_t)(0.28 * SR));
        const double r_c = reel_rms(in, s0 + (uint32_t)(0.33 * SR), s0 + (uint32_t)(0.48 * SR));
        CHECK(r_a > 0.05 && r_b < 1e-6 && r_c > 0.05, "sovraincisione: inizio non scritto (%.3f), meta' scritta (%.4f), ultimi 200 ms rimessi com'erano (%.3f)", r_a, r_b, r_c);
        /* with Record Sync nothing is cut: the press is not at the edges */
        api->destroy_instance(in);
        in = new_inst(); wait_blank(in);
        g_in_freq = 330; g_in_amp = 0.2;
        setp(in, "clock_source", "Internal"); setf(in, "bpm", 120); setp(in, "clock_div", "1/2");
        setp(in, "rec_sync", "On"); setf(in, "sos", 0); setp(in, "rec_source", "Input"); setp(in, "rec_trim", "300 ms");
        blocks(in, 0.7, NULL); setp(in, "rec_cur", "1"); blocks(in, 1.1, NULL); setp(in, "rec_stop", "1"); blocks(in, 1.5, NULL);
        fr = ui_num(in, "fr");
        CHECK(ui_num(in, "rec") == 0 && fabs(fr - SR) < 400, "con Record Sync la durata resta esattamente un tick (%.0f frame)", fr);
        g_in_amp = 0; api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 47 */
    printf("\n47. Rumore: modulazione del riverbero, Wear nel feedback, Input Gate\n");
    {
        /* the effects alone, a 440 Hz sine in: how much comes out that is not 440 Hz and its harmonics */
        #define FX_RUN(h_, amp_, n_, out_) do { for (int i_ = 0; i_ < (n_); i_++) { \
            if ((i_ & 127) == 0) { fx_block(h_); heads_block(h_); } \
            float l_ = (amp_) * sinf(2.0f * (float)M_PI * 440.0f * (float)(fx_n++) / SR), r_ = l_; \
            fx_frame(h_, &l_, &r_); if (out_) (out_)[i_] = l_; } } while (0)
        long fx_n = 0;
        const int NW = 32768;
        float *w = (float *)malloc(sizeof(float) * (size_t)NW);
        const char *cases[][7] = {
            { "Depth +0.6", "rev_depth", "0.6", "rev_speed", "0.35", NULL, NULL },
            { "Depth +0.6, Speed 0.8", "rev_depth", "0.6", "rev_speed", "0.8", NULL, NULL },
            { "Depth -0.8, Speed 0.8", "rev_depth", "-0.8", "rev_speed", "0.8", NULL, NULL },
        };
        for (int c = 0; c < 3; c++) {
            void *in = new_inst(); hm_t *h = H(in);
            setf(in, "rev_mix", 0.5);
            for (int k = 1; cases[c][k]; k += 2) setf(in, cases[c][k], atof(cases[c][k + 1]));
            FX_RUN(h, 0.25f, 3 * SR, (float *)NULL);
            FX_RUN(h, 0.25f, NW, w);
            const double res = hiss_above(w, NW, 440.0, 4000.0);
            CHECK(res < -50.0, "%s: sopra i 4 kHz, fuori dalle armoniche, quasi niente (%.1f dB sotto il segnale)", cases[c][0], -res);
            api->destroy_instance(in);
        }
        free(w);
        /* Warm, Fold, Fuzz never make the loop play by itself */
        const char *wear[] = { "Warm", "Fold", "Fuzz" };
        for (int c = 0; c < 3; c++) {
            void *in = new_inst(); hm_t *h = H(in);
            setf(in, "hd_mix", 1); setf(in, "hd_fdbk", 0.97); setp(in, "hd_wear", wear[c]); setf(in, "hd_wear_amt", 0.7);
            float t[4096]; float *tp = t;
            FX_RUN(h, 0.0f, 8 * SR, (float *)NULL);
            FX_RUN(h, 0.0f, 4096, tp);
            CHECK(rms_db(t, 4096) < -150.0, "%s al 70%%, feedback 0.97, nessun ingresso: silenzio (%.0f dBFS)", wear[c], rms_db(t, 4096));
            FX_RUN(h, 0.3f, SR / 2, (float *)NULL);
            FX_RUN(h, 0.0f, 20 * SR, (float *)NULL);
            FX_RUN(h, 0.0f, 4096, tp);
            CHECK(rms_db(t, 4096) < -40.0, "%s: dopo un suono le ripetizioni si spengono (%.0f dBFS dopo 20 s)", wear[c], rms_db(t, 4096));
            api->destroy_instance(in);
        }
        #undef FX_RUN
        /* Input Gate: hiss at -66 dBFS is pushed down, a real signal passes untouched */
        void *in = new_inst(); wait_blank(in);
        setf(in, "sos", 0); setf(in, "rev_mix", 0.0);
        rec_t r; double lv[2][2];
        for (int g = 0; g < 2; g++) {
            setp(in, "in_gate", g ? "-60 dB" : "Off");
            g_in_freq = 3000; g_in_amp = 0.0005; blocks(in, 0.5, NULL);
            rec_init(&r, 0.5); blocks(in, 0.4, &r); lv[g][0] = rms_db(r.L, r.n); rec_free(&r);
            g_in_amp = 0.1; blocks(in, 0.3, NULL);
            rec_init(&r, 0.5); blocks(in, 0.4, &r); lv[g][1] = rms_db(r.L, r.n); rec_free(&r);
        }
        CHECK(lv[1][0] < lv[0][0] - 30.0, "Input Gate -60 dB: il fruscio a -66 dBFS scende di %.0f dB", lv[0][0] - lv[1][0]);
        CHECK(fabs(lv[1][1] - lv[0][1]) < 0.05, "un segnale vero passa identico (%.2f dB di differenza)", lv[1][1] - lv[0][1]);
        g_in_amp = 0; api->destroy_instance(in);
    }
    /* --------------------------------------------------------------- 48 */
    printf("\n48. Gesti di base: polarita', valori vivi, TEMP, filtro DJ, modulatori dal clock\n");
    {
        void *in = inst_with("sine16m.wav", 1); hm_t *h = H(in);
        char b[1024];
        /* polarity: Wave one way stays above the knob, Envelope both ways goes below */
        setf(in, "volume", 0.5); setf(in, "mod_wave_rate", log(50.0) / log(1000.0));
        setp(in, "mod_set", "0 volume 0.4");
        setp(in, "mod_pol", "0 volume");
        float mn = 9, mx = -9;
        for (int k = 0; k < (int)(1.2 * SR / BLK); k++) { blocks(in, (double)BLK / SR, NULL); if (h->p[P_VOLUME] < mn) mn = h->p[P_VOLUME]; if (h->p[P_VOLUME] > mx) mx = h->p[P_VOLUME]; }
        api->get_param(in, "mod_routes", b, sizeof b);
        CHECK(strcmp(b, "volume:0:0.400:0") == 0 && mn >= 0.499f && mx > 0.88f, "Wave unipolare: solo sopra il knob (%.2f .. %.2f), \"%s\"", mn, mx, b);
        setp(in, "mod_clear", "all");
        setf(in, "mod_env_attack", 0); setf(in, "mod_env_decay", 0.6);
        setp(in, "mod_set", "3 volume 0.3"); setp(in, "mod_pol", "3 volume");
        blocks(in, 0.4, NULL);
        const float idle = h->p[P_VOLUME];
        CHECK(fabsf(idle - 0.2f) < 0.01f, "Envelope bipolare a riposo: sotto il knob (%.2f)", idle);
        /* live values */
        setp(in, "mod_set", "1 scatter 0.5");
        blocks(in, 0.1, NULL);
        api->get_param(in, "mod_live", b, sizeof b);
        CHECK(strstr(b, "volume:0.2") && strstr(b, "scatter:"), "mod_live: dove sono ora i controlli modulati (\"%s\")", b);
        /* a saved route keeps its polarity; an old one without it takes the source's */
        setp(in, "state", "{\"mod_routes\":\"width:2:0.400,morph:0:0.300:0\"}");
        int okp = 0; for (int r = 0; r < h->n_mr; r++) { if (h->mr[r].tgt == P_WIDTH && h->mr[r].bip == 0) okp++; if (h->mr[r].tgt == P_MORPH && h->mr[r].bip == 0) okp++; }
        CHECK(okp == 2, "percorsi salvati: polarita' letta, o quella della sorgente se manca");
        setp(in, "mod_clear", "all");
        /* TEMP */
        setf(in, "gene_size", 0.3); setf(in, "rev_mix", 0.1); setp(in, "pad_mode", "Pitch");
        setp(in, "mod_set", "0 slide 0.2");
        setp(in, "temp", "begin");
        api->get_param(in, "ui_state", b, sizeof b);
        const int tmp_on = strstr(b, "\"tmp\":1") != NULL;
        setf(in, "gene_size", 0.9); setf(in, "rev_mix", 0.8); setp(in, "pad_mode", "Drone"); setp(in, "mod_set", "0 slide 0"); setp(in, "mod_set", "2 morph 0.5");
        blocks(in, 0.05, NULL);
        setp(in, "temp", "end"); blocks(in, 0.05, NULL);
        api->get_param(in, "mod_routes", b, sizeof b);
        CHECK(tmp_on && fabsf(h->base[P_GENE] - 0.3f) < 1e-6f && fabsf(h->base[P_REV_MIX] - 0.1f) < 1e-6f && (int)h->base[P_PAD_MODE] == 0 && strcmp(b, "slide:0:0.200:1") == 0,
              "TEMP: al termine tornano knob, scelte e percorsi (\"%s\")", b);
        setp(in, "temp", "begin"); setf(in, "gene_size", 0.7); setp(in, "temp", "cancel");
        CHECK(fabsf(h->base[P_GENE] - 0.7f) < 1e-6f, "TEMP annullato: i cambiamenti restano");
        setp(in, "temp", "end");
        CHECK(fabsf(h->base[P_GENE] - 0.7f) < 1e-6f, "un end senza begin non fa niente");
        api->destroy_instance(in);

        /* DJ filter on the input: low-pass to the left cuts 3 kHz, high-pass to the right cuts 100 Hz */
        in = new_inst(); wait_blank(in);
        setf(in, "sos", 0);
        rec_t r; double lv[3][2];
        const char *fv[3] = { "0", "-0.6", "0.6" };
        const double fq[2] = { 100, 3000 };
        for (int m = 0; m < 3; m++) for (int q = 0; q < 2; q++) {
            setf(in, "filter", atof(fv[m])); g_in_freq = fq[q]; g_in_amp = 0.3; blocks(in, 0.4, NULL);
            rec_init(&r, 0.4); blocks(in, 0.3, &r); lv[m][q] = rms_db(r.L, r.n); rec_free(&r);
        }
        CHECK(lv[1][1] < lv[0][1] - 24 && lv[1][0] > lv[0][0] - 3, "filtro a sinistra: 3 kHz giu' di %.0f dB, 100 Hz quasi intatti (%.1f)", lv[0][1] - lv[1][1], lv[1][0] - lv[0][0]);
        CHECK(lv[2][0] < lv[0][0] - 20 && lv[2][1] > lv[0][1] - 3, "filtro a destra: 100 Hz giu' di %.0f dB, 3 kHz quasi intatti (%.1f)", lv[0][0] - lv[2][0], lv[2][1] - lv[0][1]);
        CHECK(fabs(lv[0][0] - lv[0][1]) < 0.5, "al centro non tocca niente");
        g_in_amp = 0; api->destroy_instance(in);

        /* Restart Mods On Clock */
        in = new_inst(); h = H(in);
        setf(in, "mod_wave_rate", 0.3); blocks(in, 0.7, NULL);
        setp(in, "mod_restart", "On"); setp(in, "clock_source", "Internal"); blocks(in, (double)BLK / SR, NULL);
        CHECK(h->m_ph[0] < 0.01, "il clock parte: la Wave riparte da capo (fase %.3f)", h->m_ph[0]);
        api->destroy_instance(in);
    }
    snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir); if (system(cmd)) { }
    printf("\n%s (%d controlli falliti)\n", g_fail ? "ESITO: FALLITO" : "ESITO: TUTTO OK", g_fail);
    return g_fail ? 1 : 0;
}
