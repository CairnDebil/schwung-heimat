/*
 * Heimat - tape and microsound sample instrument for Schwung
 *
 * SPDX-License-Identifier: MIT
 *
 * An original tape and microsound instrument: a reel (a WAV file or a
 * recording) is cut into splices and played back as genes - grains whose size,
 * start, overlap, envelope, speed and scatter can all be varied. One engine,
 * two modules:
 *
 *   heimat       chain sound generator (HM_SOLO=0)
 *   heimat-solo  overtake instrument with its own screen (HM_SOLO=1): it reads
 *                the Move audio input and is driven by ui.js through the
 *                set_param / get_param keys listed in README.md
 *
 * What the engine does
 *   - Reel: a WAV loaded on a worker thread, resampled to the host rate, held in
 *     a buffer with room for 2.9 minutes. An empty reel can be recorded.
 *   - Splices: marker list that can be edited by hand (add / move / delete),
 *     read from and written to WAV cue points, divided equally, or detected
 *     from transients. Undo for marker edits and for new-splice recordings.
 *   - Gene engine: Gene-Size, Slide, Morph, Vari-Speed, Organize; Micro page:
 *     envelope shape, Scatter, pitch Spread, Width, selective threshold.
 *   - Record: into a new splice, or over the current splice (Time Lag
 *     Accumulation). Source = Sound-on-Sound mix of input and playback, or
 *     input only. Recording is independent of playback speed.
 *   - Clock: internal, Move transport, or MIDI clock; drives Gene Shift and
 *     Time Stretch and can quantize the start / stop of a recording.
 *   - Save: the reel is written back as WAV with cue markers (optionally at
 *     48 kHz).
 *
 * Realtime rules (docs/MODULES.md): every entry point runs on the audio
 * callback. Anything slow (file I/O, resampling, transient detection, saving)
 * runs on a SCHED_OTHER worker thread and is handed over with atomics.
 */

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "plugin_api_v1.h"

#ifndef HM_SOLO
#define HM_SOLO 0
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define HM_MAX_GENES      8
#define HM_MAX_SPLICES    300
#define HM_MAX_MARKERS    (HM_MAX_SPLICES - 1)
#define HM_MAX_NOTES      16
#define HM_PATH_MAX       512
#define HM_MAX_REEL_SEC   174.0            /* 2.9 minutes of tape per reel     */
#define HM_FADE_CLASSIC   64               /* anti-click window, in samples   */
#define HM_GENE_MIN_SEC   0.0005           /* smallest gene                   */
#define HM_PK_BLOCK       256              /* frames per peak-pyramid block   */
#define HM_VIEW_COLS      128
#define HM_RUI_COLS       256              /* reel overview sent to the browser editor */
#define HM_RUI_EVERY      14               /* blocks between live-view updates (~25 Hz) */
#define HM_UNDO           16
#define HM_MIN_SPLICE     32               /* shortest allowed splice, frames */
#define HM_STATE_MAX      4096
#define HM_STATE_FORMAT   1                /* bump only if a saved key changes meaning */

#ifndef HM_DEFAULT_SAVE_DIR
#define HM_DEFAULT_SAVE_DIR "/data/UserData/UserLibrary/Samples/Heimat"
#endif
#ifndef HM_DEFAULT_DATA_DIR
#define HM_DEFAULT_DATA_DIR "/data/UserData/schwung/heimat"
#endif

/* ------------------------------------------------------------------- reel */

typedef struct reel_s {
    float *l, *r;                          /* always two channels, cap frames each */
    uint32_t frames, cap;
    int n_markers;
    uint32_t markers[HM_MAX_MARKERS];      /* sorted, unique, 0 < m < frames       */
    int truncated, resampled;
    int8_t *pk_max, *pk_min;               /* peak pyramid, HM_PK_BLOCK frames per entry */
    uint32_t pk_n;
    struct reel_s *next;                   /* retire stack                         */
} reel_t;

/* ----------------------------------------------------------------- params */

enum {
    P_GENE, P_SLIDE, P_MORPH, P_VSPEED, P_ORGANIZE, P_VOLUME,
    P_PAD_MODE, P_PLAY_MODE, P_RELEASE, P_ROOT, P_OCT, P_BEND, P_SPLICE_BASE,
    P_SPLICE_SRC, P_DIVIDE, P_ORG_MODE, P_WINDOW, P_MCR1, P_MCR2, P_MCR3,
    P_SOS, P_IN_GAIN, P_REC_SRC, P_REC_SYNC,
    P_CLOCK_SRC, P_CLOCK_DIV, P_BPM, P_CLOCK_MODE, P_MUTE, P_SAVE48,
    P_SCATTER, P_SPREAD, P_WIDTH, P_THRESH, P_THR_MODE,
    P_EQ_LOW, P_EQ_MID, P_EQ_FREQ, P_EQ_HIGH,
    P_DLY_TIME, P_DLY_SYNC, P_DLY_FDBK, P_DLY_TONE, P_DLY_MIX, P_DLY_PING,
    P_REV_DECAY, P_REV_DAMP, P_REV_MIX, P_FX_REC,
    P_HD_MIX, P_HD_TIME, P_HD_HEADS, P_HD_SPACE, P_HD_FDBK, P_HD_REV, P_HD_WEAR, P_HD_WAMT, P_HD_MODE, P_HD_ROUTE, P_HD_FREEZE,
    P_GENE_REV, P_SPR_MODE,
    P_COUNT
};

enum { PAD_PITCH, PAD_SPLICE, PAD_DRONE };
enum { PLAY_CLASSIC, PLAY_MOMENTARY, PLAY_TRIGLOOP };
enum { CLK_OFF, CLK_INTERNAL, CLK_HOST, CLK_MIDI };
enum { CM_AUTO, CM_SHIFT, CM_STRETCH };
enum { REC_IDLE, REC_NEW, REC_CUR };

typedef enum { T_FLOAT, T_INT, T_ENUM } ptype_t;

typedef struct {
    const char *key, *name, *sname;
    ptype_t type;
    float min, max, def, step;
    const char *const *opts;
    int nopts;
} pdef_t;

static const char *const OPT_PAD[]   = { "Pitch", "Splice", "Drone" };
static const char *const OPT_PLAY[]  = { "Classic", "Momentary", "Trigger Loop" };
static const char *const OPT_SSRC[]  = { "Markers", "Divide" };
static const char *const OPT_ORG[]   = { "End of Gene", "Immediate" };
static const char *const OPT_WIN[]   = { "Edge", "Tukey", "Gauss", "Triangle", "Expodec", "Rexpodec" };
static const char *const OPT_THR[]   = { "Off", "Loud", "Quiet" };
static const char *const OPT_GAIN[]  = { "-3 dB", "0 dB", "+6 dB", "+12 dB" };
static const char *const OPT_RSRC[]  = { "Mix", "Input" };
static const char *const OPT_ONOFF[] = { "Off", "On" };
static const char *const OPT_CSRC[]  = { "Off", "Internal", "Move", "MIDI" };
static const char *const OPT_CDIV[]  = { "1/16", "1/8", "1/4", "1/2", "1 bar" };
static const char *const OPT_CMODE[] = { "Auto", "Gene Shift", "Time Stretch" };
static const char *const OPT_DSYNC[] = { "Off", "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/4.", "1/2" };
static const char *const OPT_HDTIME[] = { "2 bars", "1 bar", "1/2.", "1/2", "1/4.", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64", "1/128" };
static const char *const OPT_WEAR[]  = { "Dark", "Thin", "Crush", "Warm", "Fold", "Fuzz" };
static const char *const OPT_HDMODE[] = { "Fade", "Glide", "Rise", "Fall" };
static const char *const OPT_ROUTE[] = { "Own", "Cross", "Chain", "Wander" };
static const char *const OPT_SPRMODE[] = { "Free", "Semitones", "Octaves", "Fifths", "Major", "Minor" };

#define PF(k, n, s, mn, mx, d, st) { k, n, s, T_FLOAT, mn, mx, d, st, NULL, 0 }
#define PI(k, n, s, mn, mx, d)     { k, n, s, T_INT, (float)(mn), (float)(mx), (float)(d), 1.0f, NULL, 0 }
#define PE(k, n, s, o, c, d)       { k, n, s, T_ENUM, 0.0f, (float)((c) - 1), (float)(d), 1.0f, o, c }

static const pdef_t g_params[P_COUNT] = {
    [P_GENE]      = PF("gene_size",        "Gene-Size",          "Gene",  0.0f, 1.0f, 0.0f, 0.005f),
    [P_SLIDE]     = PF("slide",            "Slide",              "Slide", 0.0f, 1.0f, 0.0f, 0.005f),
    [P_MORPH]     = PF("morph",            "Morph",              "Morph", 0.0f, 1.0f, 0.18f, 0.005f),
    [P_VSPEED]    = PF("vari_speed",       "Vari-Speed",         "Speed", -1.0f, 1.0f, 0.5f, 0.005f),
    [P_ORGANIZE]  = PF("organize",         "Organize",           "Org",   0.0f, 1.0f, 0.0f, 0.005f),
    [P_VOLUME]    = PF("volume",           "Volume",             "Vol",   0.0f, 1.0f, 0.8f, 0.01f),
    [P_PAD_MODE]  = PE("pad_mode",         "Pad Mode",           "Pads",  OPT_PAD, 3, PAD_PITCH),
    [P_PLAY_MODE] = PE("play_mode",        "Play Gate",          "Play",  OPT_PLAY, 3, PLAY_MOMENTARY),
    [P_RELEASE]   = PF("release_ms",       "Release (ms)",       "Rel",   1.0f, 2000.0f, 15.0f, 1.0f),
    [P_ROOT]      = PI("root_note",        "Root Note",          "Root",  0, 127, 60),
    [P_OCT]       = PI("octave_transpose", "Octave",             "Oct",  -3, 3, 0),
    [P_BEND]      = PI("bend_range",       "Bend Range",         "Bend",  0, 12, 2),
    [P_SPLICE_BASE] = PI("splice_base",    "Splice Pad Base",    "Base",  0, 120, 36),
    [P_SPLICE_SRC]= PE("splice_source",    "Splice Source",      "SplSrc", OPT_SSRC, 2, 0),
    [P_DIVIDE]    = PI("divide",           "Divide Into",        "Divide", 1, 32, 8),
    [P_ORG_MODE]  = PE("organize_mode",    "Organize Change",    "OrgMod", OPT_ORG, 2, 0),
    [P_WINDOW]    = PE("gene_window",      "Gene Envelope",      "Env",   OPT_WIN, 6, 0),
    [P_MCR1]      = PF("mcr1",             "Morph Chord 2nd",    "Chrd2", -16.0f, 16.0f, 2.0f, 0.01f),
    [P_MCR2]      = PF("mcr2",             "Morph Chord 3rd",    "Chrd3", -16.0f, 16.0f, 3.0f, 0.01f),
    [P_MCR3]      = PF("mcr3",             "Morph Chord 4th",    "Chrd4", -16.0f, 16.0f, 4.0f, 0.01f),
    [P_SOS]       = PF("sos",              "Sound On Sound",     "SOS",   0.0f, 1.0f, 1.0f, 0.01f),
    [P_IN_GAIN]   = PE("in_gain",          "Input Gain",         "InGain", OPT_GAIN, 4, 1),
    [P_REC_SRC]   = PE("rec_source",       "Record Source",      "RecSrc", OPT_RSRC, 2, 0),
    [P_REC_SYNC]  = PE("rec_sync",         "Record Sync",        "RecSyn", OPT_ONOFF, 2, 0),
    [P_CLOCK_SRC] = PE("clock_source",     "Clock Source",       "Clock", OPT_CSRC, 4, CLK_OFF),
    [P_CLOCK_DIV] = PE("clock_div",        "Clock Division",     "Div",   OPT_CDIV, 5, 2),
    [P_BPM]       = PI("bpm",              "Internal BPM",       "BPM",   20, 300, 120),
    [P_CLOCK_MODE]= PE("clock_mode",       "Clock Action",       "ClkAct", OPT_CMODE, 3, CM_AUTO),
    [P_MUTE]      = PE("mute",             "Mute",               "Mute",  OPT_ONOFF, 2, 0),
    [P_SAVE48]    = PE("save_48k",         "Save at 48 kHz",     "Save48", OPT_ONOFF, 2, 0),
    [P_SCATTER]   = PF("scatter",          "Scatter",            "Scat",  0.0f, 1.0f, 0.0f, 0.005f),
    [P_SPREAD]    = PF("spread",           "Pitch Spread (st)",  "Sprd",  0.0f, 12.0f, 0.0f, 0.05f),
    [P_WIDTH]     = PF("width",            "Stereo Width",       "Width", 0.0f, 1.0f, 0.0f, 0.01f),
    [P_THRESH]    = PF("threshold",        "Threshold",          "Thr",   0.0f, 1.0f, 0.5f, 0.005f),
    [P_THR_MODE]  = PE("thr_mode",         "Selective",          "Select", OPT_THR, 3, 0),
    [P_EQ_LOW]    = PF("eq_low",           "EQ Low (dB)",        "Low",   -15.0f, 15.0f, 0.0f, 0.1f),
    [P_EQ_MID]    = PF("eq_mid",           "EQ Mid (dB)",        "Mid",   -15.0f, 15.0f, 0.0f, 0.1f),
    [P_EQ_FREQ]   = PF("eq_freq",          "EQ Mid Freq (Hz)",   "Freq",  100.0f, 8000.0f, 1000.0f, 10.0f),
    [P_EQ_HIGH]   = PF("eq_high",          "EQ High (dB)",       "High",  -15.0f, 15.0f, 0.0f, 0.1f),
    [P_DLY_TIME]  = PF("dly_time",         "Delay Time (ms)",    "Time",  10.0f, 2000.0f, 375.0f, 1.0f),
    [P_DLY_SYNC]  = PE("dly_sync",         "Delay Sync",         "Sync",  OPT_DSYNC, 8, 0),
    [P_DLY_FDBK]  = PF("dly_fdbk",         "Delay Feedback",     "Fdbk",  0.0f, 0.95f, 0.4f, 0.005f),
    [P_DLY_TONE]  = PF("dly_tone",         "Delay Tone",         "Tone",  0.0f, 1.0f, 0.6f, 0.005f),
    [P_DLY_MIX]   = PF("dly_mix",          "Delay Mix",          "Delay", 0.0f, 1.0f, 0.0f, 0.005f),
    [P_DLY_PING]  = PE("dly_ping",         "Delay Ping-Pong",    "Ping",  OPT_ONOFF, 2, 0),
    [P_REV_DECAY] = PF("rev_decay",        "Reverb Decay",       "Decay", 0.0f, 1.0f, 0.5f, 0.005f),
    [P_REV_DAMP]  = PF("rev_damp",         "Reverb Damping",     "Damp",  0.0f, 1.0f, 0.5f, 0.005f),
    [P_REV_MIX]   = PF("rev_mix",          "Reverb Mix",         "Reverb", 0.0f, 1.0f, 0.0f, 0.005f),
    [P_FX_REC]    = PE("fx_rec",           "Record Through FX",  "RecFX", OPT_ONOFF, 2, 0),
    [P_HD_MIX]    = PF("hd_mix",           "Heads Mix",          "Heads", 0.0f, 1.0f, 0.0f, 0.005f),
    [P_HD_TIME]   = PE("hd_time",          "Heads Time",         "HTime", OPT_HDTIME, 14, 7),
    [P_HD_HEADS]  = PI("hd_heads",         "Heads",              "HCount", 1, 4, 2),
    [P_HD_SPACE]  = PF("hd_spacing",       "Head Spacing",       "Space", 0.0f, 1.0f, 0.3f, 0.005f),
    [P_HD_FDBK]   = PF("hd_fdbk",          "Heads Feedback",     "HFdbk", 0.0f, 1.0f, 0.45f, 0.005f),
    [P_HD_REV]    = PF("hd_reverse",       "Reverse Heads",      "Rev",   0.0f, 1.0f, 0.0f, 0.005f),
    [P_HD_WEAR]   = PE("hd_wear",          "Wear",               "Wear",  OPT_WEAR, 6, 0),
    [P_HD_WAMT]   = PF("hd_wear_amt",      "Wear Amount",        "WAmt",  0.0f, 1.0f, 0.0f, 0.005f),
    [P_HD_MODE]   = PE("hd_mode",          "Heads Mode",         "HMode", OPT_HDMODE, 4, 0),
    [P_HD_ROUTE]  = PE("hd_route",         "Heads Routing",      "Route", OPT_ROUTE, 4, 0),
    [P_HD_FREEZE] = PE("hd_freeze",        "Freeze Heads",       "Freeze", OPT_ONOFF, 2, 0),
    [P_GENE_REV]  = PF("gene_reverse",     "Gene Reverse",       "GRev",  0.0f, 1.0f, 0.0f, 0.005f),
    [P_SPR_MODE]  = PE("spread_mode",      "Spread Intervals",   "Intvl", OPT_SPRMODE, 6, 0),
};

static const float k_dsync_beats[8] = { 0.0f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
static const float k_hd_beats[14] = { 8.0f, 4.0f, 3.0f, 2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 0.0625f, 0.03125f };

static const double k_div_beats[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };
static const float  k_in_gain[4]   = { 0.7079f, 1.0f, 1.9953f, 3.9811f };

/* --------------------------------------------------------------- instance */

typedef struct {
    int active;
    double pos;                /* absolute frame position in the reel          */
    double traveled;           /* source frames travelled (full-splice genes)  */
    uint32_t age;              /* output samples                                */
    uint32_t life;             /* time-mode length in samples; 0 = full splice  */
    double lim_src;            /* full mode: splice length in source frames     */
    uint32_t lo, len;          /* splice bounds                                 */
    float pitch;               /* Morph chord ratio                             */
    float gl, gr;              /* pan gains                                     */
    int kill_total, kill_left; /* fade-out after retrigger / release            */
    uint8_t shape;             /* envelope shape at birth                       */
} gene_t;

typedef struct { uint32_t mk[HM_MAX_MARKERS]; int n; uint32_t frames; } undo_t;

/* ------------------------------------------------------- internal effects */

#define FX_DLY_LEN   131072                /* 2.97 s at 44.1 kHz: power of two  */
#define FX_DLY_MASK  (FX_DLY_LEN - 1)
#define FX_REV_N     8
#define FX_REV_LEN   4096                  /* per FDN line, power of two        */
#define FX_AP_N      4
#define FX_AP_LEN    1024

typedef struct { float b0, b1, b2, a1, a2; float z1[2], z2[2]; } biquad_t;

typedef struct {
    /* EQ: low shelf, peak, high shelf; gains glide per block */
    biquad_t bq[3];
    float eq_cur[4];                       /* low, mid, freq, high as applied   */
    int eq_on;
    /* delay */
    float dl[FX_DLY_LEN], dr[FX_DLY_LEN];
    uint32_t dw;
    float dtime;                           /* smoothed delay time, samples      */
    float dlp[2], dhp[2];                  /* tone low-pass, DC/mud high-pass   */
    float dmix;
    /* reverb: allpass diffusers into an 8-line feedback delay network */
    float ap[FX_AP_N][FX_AP_LEN];
    uint32_t ap_len[FX_AP_N], ap_w;
    float line[FX_REV_N][FX_REV_LEN];
    uint32_t line_len[FX_REV_N], rw;
    float rlp[FX_REV_N];
    float rg[FX_REV_N], rdamp, rmix;
    float rdecay_cur, rdamp_cur;
    int rev_idle;                          /* lines cleared, nothing to process */
    float dc;                              /* anti-denormal offset              */
} fx_t;

#define HD_LINES   8                      /* four heads per channel             */
#define HD_LEN     (1u << 19)             /* 11.9 s of tape per head at 44.1 kHz */
#define HD_MASK    (HD_LEN - 1)
#define HD_WIN     2048.0f                /* window of the octave shift         */
#define HD_XF      2048.0f                /* cross-fade of Fade mode            */

typedef struct {
    float *buf;
    uint64_t w, clr;                      /* samples written; nothing before clr is heard */
    float target, t_old, t_new, xf, t_cur, ph;
    float out, f[4], hold, dcx;
    int hold_n, active, rev;
} hd_line_t;

typedef struct {
    hd_line_t ln[HD_LINES];               /* 0-3 left, 4-7 right */
    float mix, wear_a;
    int idle, clear_req;
} heads_t;
typedef struct { int n; uint32_t m[HM_MAX_MARKERS]; } mk_result_t;

typedef struct {
    float sr;
    float p[P_COUNT];

    /* reel handling */
    reel_t *reel;                          /* audio thread only                 */
    _Atomic(reel_t *) incoming;            /* worker -> audio                   */
    _Atomic(reel_t *) retire;              /* audio -> worker (stack)           */
    char req_path[2][HM_PATH_MAX];
    atomic_uint req_seq;
    atomic_int load_state;                 /* 0 none, 1 loading, 2 ok, 3 error  */
    char err[160];
    char cur_path[HM_PATH_MAX];
    atomic_int run;
    pthread_t th;
    int th_started;

    /* splices: the editable marker list */
    uint32_t mk[HM_MAX_MARKERS];
    int n_mk;
    uint32_t mk_ver, wave_ver;
    uint32_t mk_pub[HM_MAX_MARKERS];       /* seqlock copy for the worker (save) */
    int n_mk_pub;
    atomic_uint mk_pub_seq;
    undo_t undo[HM_UNDO];
    int undo_n;
    uint32_t bounds[HM_MAX_SPLICES + 1];
    int n_splices;
    int bounds_dirty;
    int sel_splice, prev_sel, sel_override;
    int pad_splice_raw;
    uint32_t view_a, view_b;

    /* play state */
    uint8_t notes[HM_MAX_NOTES];
    int n_notes;
    int spawn_on, started;
    double spawn_cd;
    int spawn_full;
    uint32_t spawn_idx;
    uint32_t genes_spawned, genes_skipped;
    float load_avg, load_pk;   /* time spent in render, as a share of the block's real time */
    uint32_t load_over;        /* blocks that took longer than real time */
    float speed;
    float bend_cur, bend_target;
    uint32_t rng;
    gene_t genes[HM_MAX_GENES];
    float out_peak, in_peak;

    /* recording */
    int rec_mode;
    int rec_armed;                         /* mode waiting for the next clock tick */
    int rec_stop_armed;
    uint32_t rec_pos, rec_start, rec_lo, rec_len;
    atomic_int saving;

    /* clock */
    int clk_running;
    double clk_acc, clk_period;
    uint32_t clk_idx;
    double since_tick, tick_len;
    uint32_t midi_pulses;
    int ticks_pending;
    double last_beat;

    /* worker jobs */
    char save_name[2][HM_PATH_MAX];
    atomic_uint save_seq;
    atomic_int save_state;                 /* 0 idle, 1 saving, 2 saved, 3 error */
    char save_path[HM_PATH_MAX], save_err[96];
    atomic_int detect_req;
    float detect_sens;
    mk_result_t mk_res;
    atomic_int mk_res_ready;
    atomic_uint state_gen;
    char restore_json[HM_STATE_MAX];
    atomic_int restore_ready;

    fx_t fx;
    heads_t hd;

    /* browser editor (Schwung Remote UI, Tool tab): live-view counter and a
     * cached overview of the whole reel */
    uint32_t rui_viz, rui_blk;
    uint32_t rui_pk_ver, rui_pk_frames;
    char rui_pk[HM_RUI_COLS * 4 + 1];
} hm_t;

static const host_api_v1_t *g_host = NULL;

/* ----------------------------------------------------------------- helpers */

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

static void set_param_value(hm_t *h, int i, float v)
{
    const pdef_t *d = &g_params[i];
    v = clampf(v, d->min, d->max);
    if (d->type != T_FLOAT) v = floorf(v + 0.5f);
    h->p[i] = v;
    if (i == P_SPLICE_SRC || i == P_DIVIDE) h->bounds_dirty = 1;
    atomic_fetch_add_explicit(&h->state_gen, 1, memory_order_relaxed);
}

static int param_index(const char *key)
{
    for (int i = 0; i < P_COUNT; i++) if (strcmp(key, g_params[i].key) == 0) return i;
    return -1;
}

/* An enum may arrive as its option name ("1/4", "0 dB") or as its index. The
 * name wins, so labels that start with a digit are not read as numbers. */
static int parse_enum(const pdef_t *d, const char *val, float *out)
{
    for (int i = 0; i < d->nopts; i++) if (strcmp(val, d->opts[i]) == 0) { *out = (float)i; return 0; }
    char *end;
    float f = strtof(val, &end);                    /* a plain number: the index (a decimal point is tolerated) */
    if (end == val || *end) return -1;
    *out = f;
    return 0;
}

static int json_num(const char *json, const char *key, float *out)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    char *end;
    float f = strtof(p, &end);
    if (end == p) return -1;
    *out = f;
    return 0;
}

static int json_str(const char *json, const char *key, char *out, int out_len)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return -1;
    p += strlen(pat);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return -1;
    p++;
    int n = 0;
    while (*p && *p != '"' && n < out_len - 1) {
        if (*p == '\\' && p[1]) p++;
        out[n++] = *p++;
    }
    out[n] = 0;
    return 0;
}

static void json_escape(const char *in, char *out, int out_len)
{
    int n = 0;
    for (; *in && n < out_len - 2; in++) {
        if (*in == '"' || *in == '\\') out[n++] = '\\';
        out[n++] = *in;
    }
    out[n] = 0;
}

typedef struct { char *b; int cap, len, ovf; } jb_t;

static void jb_add(jb_t *j, const char *fmt, ...)
{
    if (j->ovf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(j->b + j->len, (size_t)(j->cap - j->len), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= j->cap - j->len) { j->ovf = 1; return; }
    j->len += n;
}

static uint32_t rng_next(hm_t *h)
{
    uint32_t x = h->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    h->rng = x;
    return x;
}
static float rng_unit(hm_t *h) { return (float)(rng_next(h) >> 8) * (1.0f / 16777216.0f); }

static const char *env_or(const char *name, const char *def)
{
    const char *v = getenv(name);
    return (v && v[0]) ? v : def;
}

static void mkdir_p(const char *path)
{
    char tmp[HM_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0755); *p = '/'; }
    mkdir(tmp, 0755);
}

/* ============================================================ WAV loader ==
 * Everything in this block runs on the worker thread only.
 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : (x > y);
}

static void free_reel(reel_t *r)
{
    if (!r) return;
    free(r->l); free(r->r); free(r->pk_max); free(r->pk_min);
    free(r);
}

static void pk_block(const reel_t *r, uint32_t b)
{
    uint32_t a = b * HM_PK_BLOCK, e = a + HM_PK_BLOCK;
    if (e > r->frames) e = r->frames;
    float mx = 0.0f, mn = 0.0f;
    for (uint32_t i = a; i < e; i++) {
        float v = 0.5f * (r->l[i] + r->r[i]);
        if (v > mx) mx = v;
        if (v < mn) mn = v;
    }
    r->pk_max[b] = (int8_t)lrintf(clampf(mx, -1.0f, 1.0f) * 127.0f);
    r->pk_min[b] = (int8_t)lrintf(clampf(mn, -1.0f, 1.0f) * 127.0f);
}

static reel_t *alloc_reel(uint32_t cap)
{
    reel_t *r = (reel_t *)calloc(1, sizeof *r);
    if (!r) return NULL;
    r->cap = cap;
    r->pk_n = cap / HM_PK_BLOCK + 2;
    r->l = (float *)calloc(cap, sizeof(float));          /* untouched pages cost no RAM */
    r->r = (float *)calloc(cap, sizeof(float));
    r->pk_max = (int8_t *)calloc(r->pk_n, 1);
    r->pk_min = (int8_t *)calloc(r->pk_n, 1);
    if (!r->l || !r->r || !r->pk_max || !r->pk_min) { free_reel(r); return NULL; }
    return r;
}

static void reel_all_peaks(reel_t *r)
{
    uint32_t n = r->frames / HM_PK_BLOCK + 1;
    for (uint32_t b = 0; b < n && b < r->pk_n; b++) pk_block(r, b);
}

static inline float hermite4(float y0, float y1, float y2, float y3, float t)
{
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

/* cubic resample of src (n frames at ratio = src_rate / dst_rate) into dst */
static uint32_t resample(const float *src, uint32_t n, double ratio, float *dst, uint32_t dst_cap)
{
    uint32_t out_n = (uint32_t)((double)n / ratio + 0.5);
    if (out_n > dst_cap) out_n = dst_cap;
    for (uint32_t i = 0; i < out_n; i++) {
        double p = (double)i * ratio;
        int i1 = (int)p;
        float t = (float)(p - i1);
        int i0 = i1 - 1 < 0 ? 0 : i1 - 1, i2 = i1 + 1 >= (int)n ? (int)n - 1 : i1 + 1, i3 = i1 + 2 >= (int)n ? (int)n - 1 : i1 + 2;
        if (i1 >= (int)n) i1 = (int)n - 1;
        dst[i] = hermite4(src[i0], src[i1], src[i2], src[i3], t);
    }
    return out_n;
}

static reel_t *load_wav(const char *path, uint32_t target_sr, char *err, int err_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(err, (size_t)err_len, "cannot open file"); return NULL; }

    uint8_t hdr[12];
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        snprintf(err, (size_t)err_len, "not a RIFF/WAVE file"); fclose(f); return NULL;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 12, SEEK_SET);

    int have_fmt = 0, fmt_tag = 0, channels = 0, bits = 0;
    uint32_t sr = 0;
    long data_off = -1;
    uint32_t data_size = 0;
    uint32_t cues[HM_MAX_SPLICES + 8];
    int n_cues = 0;

    uint8_t ch[8];
    while (fread(ch, 1, 8, f) == 8) {
        uint32_t sz = rd32(ch + 4);
        long body = ftell(f);
        if (memcmp(ch, "fmt ", 4) == 0 && sz >= 16) {
            uint8_t fm[40];
            size_t want = sz < 40 ? sz : 40;
            if (fread(fm, 1, want, f) != want) break;
            fmt_tag = rd16(fm); channels = rd16(fm + 2); sr = rd32(fm + 4); bits = rd16(fm + 14);
            if (fmt_tag == 0xFFFE && want >= 26) fmt_tag = rd16(fm + 24);
            have_fmt = 1;
        } else if (memcmp(ch, "data", 4) == 0) {
            data_off = body; data_size = sz;
            if ((long)data_size > file_size - body) data_size = (uint32_t)(file_size - body);
        } else if (memcmp(ch, "cue ", 4) == 0 && sz >= 4) {
            uint8_t cn[4];
            if (fread(cn, 1, 4, f) == 4) {
                uint32_t n = rd32(cn);
                for (uint32_t i = 0; i < n && 4 + (i + 1) * 24 <= sz; i++) {
                    uint8_t cp[24];
                    if (fread(cp, 1, 24, f) != 24) break;
                    if (n_cues < HM_MAX_SPLICES + 8) cues[n_cues++] = rd32(cp + 20);
                }
            }
        }
        long next = body + (long)sz + (long)(sz & 1);
        if (next > file_size || fseek(f, next, SEEK_SET) != 0) break;
    }

    if (!have_fmt || data_off < 0 || channels < 1 || channels > 8 || sr < 1000 || sr > 384000) {
        snprintf(err, (size_t)err_len, "unsupported or damaged WAV"); fclose(f); return NULL;
    }
    int is_float = (fmt_tag == 3);
    if (!((fmt_tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (is_float && bits == 32))) {
        snprintf(err, (size_t)err_len, "unsupported WAV format (PCM 8/16/24/32 or float32)"); fclose(f); return NULL;
    }
    const int bytes = bits / 8, block = bytes * channels;
    uint32_t total = data_size / (uint32_t)block;
    uint32_t max_src = (uint32_t)(HM_MAX_REEL_SEC * (double)sr);
    int truncated = 0;
    if (total > max_src) { total = max_src; truncated = 1; }
    if (total < 2) { snprintf(err, (size_t)err_len, "file is empty"); fclose(f); return NULL; }

    const double ratio = (double)sr / (double)target_sr;
    const uint32_t cap = (uint32_t)(HM_MAX_REEL_SEC * (double)target_sr);
    float *tl = NULL, *tr = NULL;
    reel_t *r = NULL;
    uint8_t *buf = (uint8_t *)malloc(4096 * (size_t)block);
    const int need_rs = (sr != target_sr);
    if (need_rs) {
        tl = (float *)malloc((size_t)total * sizeof(float));
        tr = (float *)malloc((size_t)total * sizeof(float));
    }
    r = alloc_reel(cap);
    if (!r || !buf || (need_rs && (!tl || !tr))) {
        snprintf(err, (size_t)err_len, "out of memory");
        free(buf); free(tl); free(tr); free_reel(r); fclose(f); return NULL;
    }
    float *dl = need_rs ? tl : r->l, *dr = need_rs ? tr : r->r;

    fseek(f, data_off, SEEK_SET);
    uint32_t done = 0;
    while (done < total) {
        size_t n = total - done < 4096 ? total - done : 4096;
        if (fread(buf, (size_t)block, n, f) != n) { total = done; break; }
        for (size_t i = 0; i < n; i++) {
            float v[2];
            for (int c = 0; c < 2; c++) {
                int cc = c < channels ? c : 0;
                const uint8_t *s = buf + i * (size_t)block + (size_t)cc * (size_t)bytes;
                float x;
                if (is_float) { uint32_t u = rd32(s); memcpy(&x, &u, 4); }
                else if (bits == 8)  x = ((float)s[0] - 128.0f) * (1.0f / 128.0f);
                else if (bits == 16) x = (float)(int16_t)rd16(s) * (1.0f / 32768.0f);
                else if (bits == 24) { int32_t t = (int32_t)(((uint32_t)s[0] << 8) | ((uint32_t)s[1] << 16) | ((uint32_t)s[2] << 24)); x = (float)(t >> 8) * (1.0f / 8388608.0f); }
                else                 x = (float)((int32_t)rd32(s)) * (1.0f / 2147483648.0f);
                v[c] = x;
            }
            dl[done + i] = v[0];
            dr[done + i] = v[1];
        }
        done += (uint32_t)n;
    }
    free(buf);
    fclose(f);
    if (total < 2) { snprintf(err, (size_t)err_len, "file is empty"); free(tl); free(tr); free_reel(r); return NULL; }

    if (need_rs) {
        r->frames = resample(tl, total, ratio, r->l, cap);
        resample(tr, total, ratio, r->r, cap);
        r->resampled = 1;
        free(tl); free(tr);
    } else {
        r->frames = total;
    }
    r->truncated = truncated;

    qsort(cues, (size_t)n_cues, sizeof cues[0], cmp_u32);
    for (int i = 0; i < n_cues && r->n_markers < HM_MAX_MARKERS; i++) {
        uint32_t m = (uint32_t)((double)cues[i] / ratio + 0.5);
        if (m == 0 || m >= r->frames) continue;
        if (r->n_markers > 0 && r->markers[r->n_markers - 1] == m) continue;
        r->markers[r->n_markers++] = m;
    }
    reel_all_peaks(r);
    return r;
}

/* transient detector: 5 ms energy hops, onset when the level jumps over a
 * running average; markers at least 60 ms apart */
static void detect_transients(const reel_t *r, float sens, float sr, mk_result_t *out)
{
    out->n = 0;
    uint32_t hop = (uint32_t)(0.005f * sr);
    if (hop < 16) hop = 16;
    uint32_t nh = r->frames / hop;
    if (nh < 4) return;
    float avg = 1e-4f;
    uint32_t last = 0;
    int have_last = 0;
    float rise = 1.5f + (1.0f - clampf(sens, 0.0f, 1.0f)) * 6.0f;      /* amplitude ratio over the average */
    for (uint32_t h = 1; h < nh && out->n < HM_MAX_MARKERS; h++) {
        float e = 0.0f;
        for (uint32_t i = h * hop; i < (h + 1) * hop; i++) e += fabsf(0.5f * (r->l[i] + r->r[i]));
        e /= (float)hop;
        uint32_t pos = h * hop;
        if (e > 0.01f && e > avg * rise && (!have_last || pos - last > (uint32_t)(0.06f * sr))) {
            out->m[out->n++] = pos;
            last = pos; have_last = 1;
        }
        avg += (e - avg) * (e > avg ? 0.05f : 0.01f);
        if (avg < 1e-4f) avg = 1e-4f;
    }
}

static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }
static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }

static void do_save(hm_t *h, const char *req)
{
    const reel_t *r = h->reel;
    const char *dir = env_or("HEIMAT_SAVE_DIR", HM_DEFAULT_SAVE_DIR);
    char path[HM_PATH_MAX], tmp[HM_PATH_MAX + 8], name[128];
    h->save_err[0] = 0;
    if (!r || r->frames < 2) { snprintf(h->save_err, sizeof h->save_err, "reel is empty"); atomic_store(&h->save_state, 3); return; }
    mkdir_p(dir);

    if (strcmp(req, "auto") == 0 || !req[0]) {
        int n = 1;
        for (; n < 9999; n++) {
            snprintf(path, sizeof path, "%s/heimat_%03d.wav", dir, n);
            if (access(path, F_OK) != 0) break;
        }
        snprintf(name, sizeof name, "heimat_%03d", n);
    } else {
        int k = 0;
        for (const char *c = req; *c && k < (int)sizeof name - 1; c++)
            name[k++] = (*c == '/' || *c == '\\' || *c == ':') ? '_' : *c;
        name[k] = 0;
    }
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);

    uint32_t mk[HM_MAX_MARKERS]; int nmk = 0;
    for (int tries = 0; tries < 8; tries++) {                           /* seqlock read of the marker list */
        unsigned s0 = atomic_load_explicit(&h->mk_pub_seq, memory_order_acquire);
        if (s0 & 1) continue;
        nmk = h->n_mk_pub;
        memcpy(mk, h->mk_pub, sizeof mk);
        if (atomic_load_explicit(&h->mk_pub_seq, memory_order_acquire) == s0) break;
    }

    const uint32_t src_sr = (uint32_t)h->sr;
    const uint32_t dst_sr = h->p[P_SAVE48] > 0.5f ? 48000u : src_sr;
    const double ratio = (double)src_sr / (double)dst_sr;
    const uint32_t out_frames = dst_sr == src_sr ? r->frames : (uint32_t)((double)r->frames / ratio + 0.5);

    FILE *f = fopen(tmp, "wb");
    if (!f) { snprintf(h->save_err, sizeof h->save_err, "cannot write file"); atomic_store(&h->save_state, 3); return; }
    uint32_t data = out_frames * 8u, cue_sz = nmk ? 12u + 24u * (uint32_t)nmk : 0u;
    fwrite("RIFF", 1, 4, f); put32(f, 4 + 24 + 8 + data + cue_sz); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 3); put16(f, 2); put32(f, dst_sr);
    put32(f, dst_sr * 8u); put16(f, 8); put16(f, 32);
    fwrite("data", 1, 4, f); put32(f, data);
    float chunk[2048];
    if (dst_sr == src_sr) {
        for (uint32_t i = 0; i < out_frames; i += 1024) {
            uint32_t n = out_frames - i < 1024 ? out_frames - i : 1024;
            for (uint32_t k = 0; k < n; k++) { chunk[2 * k] = r->l[i + k]; chunk[2 * k + 1] = r->r[i + k]; }
            fwrite(chunk, 8, n, f);
        }
    } else {
        for (uint32_t i = 0; i < out_frames; i += 1024) {
            uint32_t n = out_frames - i < 1024 ? out_frames - i : 1024;
            for (uint32_t k = 0; k < n; k++) {
                double p = (double)(i + k) * ratio;
                int i1 = (int)p; float t = (float)(p - i1);
                int N = (int)r->frames;
                int i0 = i1 - 1 < 0 ? 0 : i1 - 1, i2 = i1 + 1 >= N ? N - 1 : i1 + 1, i3 = i1 + 2 >= N ? N - 1 : i1 + 2;
                if (i1 >= N) i1 = N - 1;
                chunk[2 * k]     = hermite4(r->l[i0], r->l[i1], r->l[i2], r->l[i3], t);
                chunk[2 * k + 1] = hermite4(r->r[i0], r->r[i1], r->r[i2], r->r[i3], t);
            }
            fwrite(chunk, 8, n, f);
        }
    }
    if (nmk) {
        fwrite("cue ", 1, 4, f); put32(f, 4u + 24u * (uint32_t)nmk); put32(f, (uint32_t)nmk);
        for (int i = 0; i < nmk; i++) {
            uint32_t pos = (uint32_t)((double)mk[i] / ratio + 0.5);
            put32(f, (uint32_t)i + 1); put32(f, pos); fwrite("data", 1, 4, f); put32(f, 0); put32(f, 0); put32(f, pos);
        }
    }
    fflush(f);
    fsync(fileno(f));
    int bad = ferror(f);
    fclose(f);
    if (bad || rename(tmp, path) != 0) {
        snprintf(h->save_err, sizeof h->save_err, "write failed");
        unlink(tmp);
        atomic_store(&h->save_state, 3);
        return;
    }
    snprintf(h->save_path, sizeof h->save_path, "%s", path);
    atomic_store(&h->save_state, 2);
}

static void build_state_json(const hm_t *h, char *buf, int len);

static int state_disabled(void) { return getenv("HEIMAT_NO_STATE") != NULL; }

static void write_state_file(hm_t *h)
{
    if (state_disabled()) return;
    const char *dir = env_or("HEIMAT_DATA_DIR", HM_DEFAULT_DATA_DIR);
    char path[HM_PATH_MAX], tmp[HM_PATH_MAX + 8];
    snprintf(path, sizeof path, "%s/state.json", dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    mkdir_p(dir);
    char *json = (char *)malloc(HM_STATE_MAX);
    if (!json) return;
    build_state_json(h, json, HM_STATE_MAX);
    FILE *f = fopen(tmp, "wb");
    if (f) { fputs(json, f); fflush(f); fsync(fileno(f)); fclose(f); rename(tmp, path); }
    free(json);
}

static void read_state_file(hm_t *h)
{
    if (state_disabled()) return;
    const char *dir = env_or("HEIMAT_DATA_DIR", HM_DEFAULT_DATA_DIR);
    char path[HM_PATH_MAX];
    snprintf(path, sizeof path, "%s/state.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    size_t n = fread(h->restore_json, 1, HM_STATE_MAX - 1, f);
    fclose(f);
    h->restore_json[n] = 0;
    atomic_store(&h->restore_ready, 1);
}

static void retire_free_all(hm_t *h)
{
    reel_t *p = atomic_exchange(&h->retire, NULL);
    while (p) { reel_t *n = p->next; free_reel(p); p = n; }
}

static void *worker_main(void *arg)
{
    hm_t *h = (hm_t *)arg;
    /* the worker inherits the audio callback's SCHED_FIFO 70: demote first */
    struct sched_param sp; memset(&sp, 0, sizeof sp);
    sched_setscheduler(0, SCHED_OTHER, &sp);
    cpu_set_t set; CPU_ZERO(&set);
    CPU_SET(0, &set); CPU_SET(1, &set); CPU_SET(2, &set);
    sched_setaffinity(0, sizeof set, &set);

    unsigned last_req = 0, last_save = 0, last_gen = 0;
    int idle_ticks = 0;
    if (HM_SOLO) read_state_file(h);
    while (atomic_load_explicit(&h->run, memory_order_acquire)) {
        unsigned s = atomic_load_explicit(&h->req_seq, memory_order_acquire);
        if (s != last_req) {
            char path[HM_PATH_MAX];
            memcpy(path, h->req_path[s & 1], HM_PATH_MAX);
            path[HM_PATH_MAX - 1] = 0;
            if (atomic_load_explicit(&h->req_seq, memory_order_acquire) == s) {
                last_req = s;
                char err[160]; err[0] = 0;
                atomic_store(&h->load_state, 1);
                reel_t *r = path[0] ? load_wav(path, (uint32_t)h->sr, err, sizeof err)
                                    : alloc_reel((uint32_t)(HM_MAX_REEL_SEC * (double)h->sr));
                if (r) {
                    reel_t *old = atomic_exchange_explicit(&h->incoming, r, memory_order_acq_rel);
                    if (old) free_reel(old);
                    h->err[0] = 0;
                    atomic_store(&h->load_state, 2);
                } else {
                    snprintf(h->err, sizeof h->err, "%s", err[0] ? err : "out of memory");
                    atomic_store(&h->load_state, 3);
                }
            }
            continue;
        }
        unsigned ss = atomic_load_explicit(&h->save_seq, memory_order_acquire);
        if (ss != last_save) {
            char name[HM_PATH_MAX];
            memcpy(name, h->save_name[ss & 1], HM_PATH_MAX);
            name[HM_PATH_MAX - 1] = 0;
            if (atomic_load_explicit(&h->save_seq, memory_order_acquire) == ss) {
                last_save = ss;
                atomic_store(&h->save_state, 1);
                do_save(h, name);
                atomic_store(&h->saving, 0);
            }
            continue;
        }
        if (atomic_exchange(&h->detect_req, 0) && h->reel && !h->rec_mode) {
            if (atomic_load(&h->mk_res_ready) == 0) {
                detect_transients(h->reel, h->detect_sens, h->sr, &h->mk_res);
                atomic_store(&h->mk_res_ready, 1);
            }
            continue;
        }
        retire_free_all(h);
        if (HM_SOLO) {                                        /* persist the settings 2 s after the last change */
            unsigned g = atomic_load_explicit(&h->state_gen, memory_order_relaxed);
            if (g != last_gen) { last_gen = g; idle_ticks = 0; }
            else if (idle_ticks >= 0 && ++idle_ticks == 200) { write_state_file(h); idle_ticks = -1; }
        }
        struct timespec ts = { 0, 10 * 1000 * 1000 };          /* 10 ms */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void request_load(hm_t *h, const char *path)
{
    snprintf(h->cur_path, sizeof h->cur_path, "%s", path);
    unsigned s = atomic_load_explicit(&h->req_seq, memory_order_relaxed);
    int slot = (int)((s + 1) & 1);
    snprintf(h->req_path[slot], HM_PATH_MAX, "%s", path);
    atomic_store_explicit(&h->req_seq, s + 1, memory_order_release);
    atomic_store(&h->load_state, 1);
    atomic_fetch_add_explicit(&h->state_gen, 1, memory_order_relaxed);
}

static void request_save(hm_t *h, const char *name)
{
    if (atomic_load(&h->saving) || h->rec_mode) return;
    atomic_store(&h->saving, 1);
    unsigned s = atomic_load_explicit(&h->save_seq, memory_order_relaxed);
    snprintf(h->save_name[(s + 1) & 1], HM_PATH_MAX, "%s", name);
    atomic_store(&h->save_state, 1);
    atomic_store_explicit(&h->save_seq, s + 1, memory_order_release);
}

/* ====================================================== splices & markers */

static void publish_markers(hm_t *h)
{
    unsigned s = atomic_load_explicit(&h->mk_pub_seq, memory_order_relaxed);
    atomic_store_explicit(&h->mk_pub_seq, s + 1, memory_order_release);
    h->n_mk_pub = h->n_mk;
    memcpy(h->mk_pub, h->mk, sizeof h->mk);
    atomic_store_explicit(&h->mk_pub_seq, s + 2, memory_order_release);
    h->mk_ver++;
}

static void compute_bounds(hm_t *h)
{
    h->bounds_dirty = 0;
    const reel_t *r = h->reel;
    if (!r || r->frames < 2) { h->n_splices = 0; return; }
    if ((int)h->p[P_SPLICE_SRC] == 0) {                       /* Markers: the editable list, or one splice */
        h->bounds[0] = 0;
        int n = 0;
        for (int i = 0; i < h->n_mk; i++)
            if (h->mk[i] > h->bounds[n] && h->mk[i] < r->frames) h->bounds[++n] = h->mk[i];
        h->n_splices = n + 1;
        h->bounds[h->n_splices] = r->frames;
    } else {                                                   /* Divide: equal parts */
        int n = (int)h->p[P_DIVIDE];
        if (n < 1) n = 1;
        if ((uint32_t)n > r->frames / 2) n = (int)(r->frames / 2);
        if (n < 1) n = 1;
        for (int i = 0; i <= n; i++) h->bounds[i] = (uint32_t)(((uint64_t)r->frames * (uint64_t)i) / (uint64_t)n);
        h->n_splices = n;
    }
}

static void push_undo(hm_t *h)
{
    if (h->undo_n == HM_UNDO) { memmove(&h->undo[0], &h->undo[1], sizeof(undo_t) * (HM_UNDO - 1)); h->undo_n--; }
    undo_t *u = &h->undo[h->undo_n++];
    u->n = h->n_mk;
    memcpy(u->mk, h->mk, sizeof h->mk);
    u->frames = h->reel ? h->reel->frames : 0;
}

static void markers_changed(hm_t *h)
{
    h->p[P_SPLICE_SRC] = 0;                                    /* edited markers become the splice source */
    h->bounds_dirty = 1;
    publish_markers(h);
    atomic_fetch_add_explicit(&h->state_gen, 1, memory_order_relaxed);
}

static int marker_add(hm_t *h, uint32_t pos)
{
    const reel_t *r = h->reel;
    if (!r || h->rec_mode || h->n_mk >= HM_MAX_MARKERS) return -1;
    if (pos < HM_MIN_SPLICE || pos + HM_MIN_SPLICE > r->frames) return -1;
    int at = 0;
    while (at < h->n_mk && h->mk[at] < pos) at++;
    if (at < h->n_mk && h->mk[at] - pos < HM_MIN_SPLICE) return -1;
    if (at > 0 && pos - h->mk[at - 1] < HM_MIN_SPLICE) return -1;
    push_undo(h);
    memmove(&h->mk[at + 1], &h->mk[at], sizeof(uint32_t) * (size_t)(h->n_mk - at));
    h->mk[at] = pos;
    h->n_mk++;
    markers_changed(h);
    return at;
}

static int marker_del(hm_t *h, int idx)
{
    if (h->rec_mode || idx < 0 || idx >= h->n_mk) return -1;
    push_undo(h);
    memmove(&h->mk[idx], &h->mk[idx + 1], sizeof(uint32_t) * (size_t)(h->n_mk - idx - 1));
    h->n_mk--;
    markers_changed(h);
    return idx;
}

static int marker_move(hm_t *h, int idx, uint32_t pos)
{
    const reel_t *r = h->reel;
    if (!r || h->rec_mode || idx < 0 || idx >= h->n_mk) return -1;
    uint32_t lo = idx > 0 ? h->mk[idx - 1] + HM_MIN_SPLICE : HM_MIN_SPLICE;
    uint32_t hi = (idx + 1 < h->n_mk ? h->mk[idx + 1] : r->frames) - HM_MIN_SPLICE;
    if (pos < lo) pos = lo;
    if (pos > hi) pos = hi;
    if (pos == h->mk[idx]) return idx;
    push_undo(h);
    h->mk[idx] = pos;
    markers_changed(h);
    return idx;
}

static void markers_clear(hm_t *h)
{
    if (h->rec_mode || h->n_mk == 0) return;
    push_undo(h);
    h->n_mk = 0;
    markers_changed(h);
}

static void markers_divide(hm_t *h, int n)
{
    const reel_t *r = h->reel;
    if (!r || h->rec_mode || n < 1 || r->frames < 2 * HM_MIN_SPLICE) return;
    if (n > HM_MAX_SPLICES) n = HM_MAX_SPLICES;
    push_undo(h);
    h->n_mk = 0;
    for (int i = 1; i < n; i++) h->mk[h->n_mk++] = (uint32_t)(((uint64_t)r->frames * (uint64_t)i) / (uint64_t)n);
    markers_changed(h);
}

static void do_undo(hm_t *h)
{
    if (h->rec_mode || h->undo_n == 0) return;
    undo_t *u = &h->undo[--h->undo_n];
    h->n_mk = u->n;
    memcpy(h->mk, u->mk, sizeof h->mk);
    if (h->reel && u->frames < h->reel->frames) {                 /* undo of a new-splice recording */
        reel_t *r = h->reel;
        memset(r->l + u->frames, 0, sizeof(float) * (r->frames - u->frames));
        memset(r->r + u->frames, 0, sizeof(float) * (r->frames - u->frames));
        uint32_t a = u->frames / HM_PK_BLOCK, b = r->frames / HM_PK_BLOCK;
        r->frames = u->frames;
        for (uint32_t k = a; k <= b && k < r->pk_n; k++) pk_block(r, k);
    }
    h->bounds_dirty = 1;
    h->wave_ver++;
    publish_markers(h);
    atomic_fetch_add_explicit(&h->state_gen, 1, memory_order_relaxed);
}

static void adopt_markers_from_reel(hm_t *h)
{
    const reel_t *r = h->reel;
    h->n_mk = r ? r->n_markers : 0;
    if (r) memcpy(h->mk, r->markers, sizeof(uint32_t) * (size_t)r->n_markers);
    h->undo_n = 0;
    publish_markers(h);
}

/* ------------------------------------------------------------ playback */

static inline void read_reel(const reel_t *r, double p, float *ol, float *orr)
{
    int n = (int)r->frames;
    int i = (int)floor(p);
    float t = (float)(p - (double)i);
    int i0 = i - 1, i1 = i, i2 = i + 1, i3 = i + 2;
    i0 = i0 < 0 ? 0 : (i0 > n - 1 ? n - 1 : i0);
    i1 = i1 < 0 ? 0 : (i1 > n - 1 ? n - 1 : i1);
    i2 = i2 < 0 ? 0 : (i2 > n - 1 ? n - 1 : i2);
    i3 = i3 < 0 ? 0 : (i3 > n - 1 ? n - 1 : i3);
    *ol  = hermite4(r->l[i0], r->l[i1], r->l[i2], r->l[i3], t);
    *orr = hermite4(r->r[i0], r->r[i1], r->r[i2], r->r[i3], t);
}

/* Morph knob -> number of overlapping genes. Breakpoints follow the manual's
 * clock positions on a 300-degree knob: seamless 1/1 at ~9:00, 2 genes ~12:00,
 * 3 equally spaced ~1:00, 4th gene and random pitch-up beyond ~2:30. */
static float morph_neff(float m)
{
    if (m <= 0.18f) return 1.0f;
    if (m < 0.50f)  return 1.0f + (m - 0.18f) / 0.32f;
    if (m < 0.60f)  return 2.0f + (m - 0.50f) / 0.10f;
    if (m < 0.75f)  return 3.0f;
    if (m < 0.90f)  return 3.0f + (m - 0.75f) / 0.15f;
    return 4.0f;
}

static void kill_all(hm_t *h, int fade_samples)
{
    for (int i = 0; i < HM_MAX_GENES; i++) {
        gene_t *g = &h->genes[i];
        if (!g->active) continue;
        if (fade_samples < 1) { g->active = 0; continue; }
        if (g->kill_left == 0 || g->kill_left > fade_samples) { g->kill_total = g->kill_left = fade_samples; }
    }
}

static void retrigger(hm_t *h)
{
    kill_all(h, (int)(0.003f * h->sr));
    h->spawn_cd = 0.0;
    h->spawn_idx = 0;                      /* the first gene after a trigger is the unison one */
}

static int clock_action(const hm_t *h)                     /* 0 none, 1 gene shift, 2 time stretch */
{
    if ((int)h->p[P_CLOCK_SRC] == CLK_OFF || !h->clk_running) return 0;
    int m = (int)h->p[P_CLOCK_MODE];
    if (m == CM_SHIFT) return 1;
    if (m == CM_STRETCH) return 2;
    return h->p[P_MORPH] <= 0.5f ? 1 : 2;                  /* Auto: Gene Shift up to 2/1, Time Stretch above */
}

/* ---------------------------------------------------------------- envelopes
 * Gene envelopes, after the grain envelopes of the microsound literature:
 *   Edge     flat, with a short anti-click fade at both ends
 *   Tukey    raised-cosine taper over the whole gene (quasi-Gaussian)
 *   Gauss    bell curve
 *   Triangle linear up and down
 *   Expodec  sharp attack, exponential decay  (not time-symmetric)
 *   Rexpodec exponential rise, sharp release (not time-symmetric)
 * Symmetric shapes overlap by half a gene at 1/1, the others by the edge fade.
 */
enum { ENV_EDGE, ENV_TUKEY, ENV_GAUSS, ENV_TRI, ENV_EXPO, ENV_REXPO };

static int env_overlaps(int shape) { return shape == ENV_TUKEY || shape == ENV_GAUSS || shape == ENV_TRI; }

static inline float smooth01(float t) { t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); return t * t * (3.0f - 2.0f * t); }

/* age and remain in output samples */
static inline float gene_env(int shape, double age, double remain)
{
    double len = age + remain;
    if (len < 2.0) return 0.0f;
    double edge_d = age < remain ? age : remain;
    double F = (double)HM_FADE_CLASSIC;
    if (F > len * 0.5) F = len * 0.5;
    float edge = smooth01((float)(edge_d / (F < 1.0 ? 1.0 : F)));
    float u = (float)(age / len);
    switch (shape) {
    case ENV_TUKEY: return smooth01((float)(edge_d / (len * 0.5)));
    case ENV_GAUSS: { float d = (u - 0.5f) / 0.18f; float g = expf(-0.5f * d * d); return (g - 0.0211f) / 0.9789f * edge; }
    case ENV_TRI:   return (1.0f - fabsf(2.0f * u - 1.0f));
    case ENV_EXPO:  return expf(-4.6f * u) * edge;
    case ENV_REXPO: return expf(-4.6f * (1.0f - u)) * edge;
    default:        return edge;
    }
}

/* the cloud as the gene engine will produce it now: gene length and the
 * interval between gene starts, both in output samples (before Scatter) */
static void gene_metrics(const hm_t *h, double *len_out_s, double *spacing_out_s)
{
    const float *P = h->p;
    int sp = clampi(h->sel_splice, 0, h->n_splices > 0 ? h->n_splices - 1 : 0);
    uint32_t len = h->n_splices > 0 ? h->bounds[sp + 1] - h->bounds[sp] : 0;
    if (len < 2) { *len_out_s = 0; *spacing_out_s = 0; return; }
    double aspd = fabs((double)h->speed);
    if (aspd < 1e-3) aspd = 1e-3;
    const float m = P[P_MORPH], gk = P[P_GENE];
    const int ov = env_overlaps((int)P[P_WINDOW]);
    double len_out, spacing;
    if (gk < 0.005f) {
        len_out = (double)len / aspd;
        double fade = ov ? len_out * 0.5 : (double)HM_FADE_CLASSIC;
        if (fade > len_out * 0.5) fade = len_out * 0.5;
        spacing = m < 0.18f ? len_out * (1.0 + (double)(0.18f - m) / 0.18 * 0.5) : (len_out - fade) / (double)morph_neff(m);
    } else {
        double tmax = (double)len / (double)h->sr, tmin = HM_GENE_MIN_SEC;
        if (tmax < tmin * 2.0) tmax = tmin * 2.0;
        len_out = tmax * pow(tmin / tmax, (double)(gk - 0.005f) / 0.995) * (double)h->sr;
        if (len_out < 16.0) len_out = 16.0;
        double fade = ov ? len_out * 0.5 : (double)HM_FADE_CLASSIC;
        if (fade > len_out * 0.5) fade = len_out * 0.5;
        spacing = m < 0.18f ? len_out * (1.0 + (double)(0.18f - m) / 0.18 * 0.5) : (len_out - fade) / (double)morph_neff(m);
    }
    if (spacing < 8.0) spacing = 8.0;
    *len_out_s = len_out;
    *spacing_out_s = spacing;
}

/* loudest point of the reel between two frames, from the peak pyramid */
static float reel_level(const reel_t *r, double a, double b)
{
    if (!r || r->frames < 2) return 0.0f;
    if (a < 0) a = 0;
    if (b > (double)r->frames) b = (double)r->frames;
    if (b <= a) b = a + 1;
    uint32_t ka = (uint32_t)a / HM_PK_BLOCK, kb = (uint32_t)(b - 1) / HM_PK_BLOCK;
    if (kb - ka > 64) kb = ka + 64;
    int pk = 0;
    for (uint32_t k = ka; k <= kb && k < r->pk_n; k++) {
        int v = r->pk_max[k] > -r->pk_min[k] ? r->pk_max[k] : -r->pk_min[k];
        if (v > pk) pk = v;
    }
    return (float)pk * (1.0f / 127.0f);
}

/* Pitch Spread for one gene, in semitones. Free draws any detune within
 * +-range; the other modes draw an interval from a set (and its inversion
 * downwards), keeping only the intervals that fit within the range, plus the
 * unison. So with Octaves the range must reach 12 before anything moves. */
static float spread_semitones(hm_t *h, float range, int mode)
{
    static const float sets[4][3] = { { 12, 12, 12 }, { 5, 7, 12 }, { 4, 7, 12 }, { 3, 7, 12 } };
    const float u = rng_unit(h);
    if (mode <= 0) return range * (2.0f * u - 1.0f);
    if (mode == 1) {                                              /* any whole semitone */
        const int k = (int)floorf(range + 0.001f);
        return (float)((int)floorf(u * (float)(2 * k + 1)) - k);
    }
    float c[7]; int n = 0;
    c[n++] = 0.0f;
    const float *set = sets[clampi(mode - 2, 0, 3)];
    for (int i = 0; i < 3; i++) {
        if (set[i] > range + 0.001f) continue;
        int dup = 0;
        for (int j = 0; j < n; j++) if (c[j] == set[i]) dup = 1;
        if (dup) continue;
        c[n++] = set[i]; c[n++] = -set[i];
    }
    return c[clampi((int)(u * (float)n), 0, n - 1)];
}

/* returns the spacing until the next gene starts: output samples for time-mode
 * genes, source frames for full-splice genes */
static double spawn_gene(hm_t *h, int rank, float speed_signed)
{
    const reel_t *r = h->reel;
    const float *P = h->p;
    int sp = clampi(h->sel_splice, 0, h->n_splices - 1);
    uint32_t lo = h->bounds[sp], len = h->bounds[sp + 1] - lo;
    if (len < 2) len = 2;

    gene_t *g = NULL;
    for (int i = 0; i < HM_MAX_GENES; i++) if (!h->genes[i].active) { g = &h->genes[i]; break; }

    const float m = P[P_MORPH];
    const float aspd = fabsf(speed_signed);                      /* source frames per output sample (reel at host rate) */
    const float gk = P[P_GENE];

    uint32_t life = 0;
    double lim_src = (double)len;
    double len_out;
    if (gk < 0.005f) {
        len_out = (double)len / (double)(aspd > 1e-6f ? aspd : 1e-6f);
    } else {
        double tmax = (double)len / (double)h->sr;
        double tmin = HM_GENE_MIN_SEC;
        if (tmax < tmin * 2.0) tmax = tmin * 2.0;
        double T = tmax * pow(tmin / tmax, (double)(gk - 0.005f) / 0.995);
        double l = T * (double)h->sr;
        if (l < 16.0) l = 16.0;
        life = (uint32_t)l;
        len_out = l;
    }
    float pitch = 1.0f, pan = 0.0f;
    if (rank >= 1) {
        float pc = clampf((m - 0.75f) / 0.25f, 0.0f, 1.0f);
        if (rng_unit(h) < pc) pitch = P[P_MCR1 + (rank > 3 ? 2 : rank - 1)];
        float spread = clampf((m - 0.60f) / 0.15f, 0.0f, 1.0f);
        static const float pat[4] = { 0.0f, -1.0f, 1.0f, 0.5f };
        pan = pat[rank & 3] * spread;
        if (rank == 3 && rng_unit(h) < 0.5f) pan = -pan;
    }
    if (P[P_SPREAD] > 0.0f)                                       /* frequency band of the cloud */
        pitch *= exp2f(spread_semitones(h, P[P_SPREAD], (int)P[P_SPR_MODE]) * (1.0f / 12.0f));
    /* Gene Reverse: the chance that a gene runs against the tape */
    const float dir = (P[P_GENE_REV] > 0.0f && rng_unit(h) < P[P_GENE_REV]) ? -1.0f : 1.0f;
    pitch *= dir;
    speed_signed *= dir;
    if (P[P_WIDTH] > 0.0f)                                        /* spatial dispersion */
        pan = clampf(pan + P[P_WIDTH] * (2.0f * rng_unit(h) - 1.0f), -1.0f, 1.0f);

    const int win_smooth = env_overlaps((int)P[P_WINDOW]);
    double fade = win_smooth ? len_out * 0.5 : (double)HM_FADE_CLASSIC;
    if (fade > len_out * 0.5) fade = len_out * 0.5;

    /* clock: Gene Shift steps through the genes of the splice, Time Stretch glides */
    double clk_off = 0.0;
    int act = clock_action(h);
    if (act && life) {
        double span = (double)life;
        double n_g = ceil((double)len / span);
        if (n_g < 1.0) n_g = 1.0;
        double pos = (double)(h->clk_idx % (uint32_t)n_g);
        if (act == 2 && h->tick_len > 1.0) pos += clampf((float)(h->since_tick / h->tick_len), 0.0f, 1.0f);
        clk_off = fmod(pos * span, (double)len);
    }

    /* Scatter: start points wander around Slide (quasi-sequential -> random) */
    double jitter = 0.0;
    const float sc = P[P_SCATTER];
    if (sc > 0.0f) jitter = (double)sc * 0.5 * (double)len * (double)(2.0f * rng_unit(h) - 1.0f);
    double rel0 = fmod((double)P[P_SLIDE] * (double)len + clk_off + jitter, (double)len);
    if (rel0 < 0.0) rel0 += (double)len;

    /* selective granulation: skip genes above or below a level (silence keeps its place) */
    const int thr_mode = (int)P[P_THR_MODE];
    if (g && thr_mode != 0) {
        double span_src = life ? (double)life * (double)aspd * fabsf(pitch) : (double)len;
        double a = speed_signed < 0.0f ? (double)lo + rel0 - span_src : (double)lo + rel0;
        float lvl = reel_level(r, a, a + span_src);
        float thr = powf(10.0f, (-60.0f + 60.0f * P[P_THRESH]) * 0.05f);
        if ((thr_mode == 1 && lvl < thr) || (thr_mode == 2 && lvl > thr)) { g = NULL; h->genes_skipped++; }
    }

    if (g) {
        memset(g, 0, sizeof *g);
        g->active = 1;
        g->shape = (uint8_t)P[P_WINDOW];
        g->lo = lo; g->len = len;
        g->life = life; g->lim_src = lim_src;
        g->pitch = pitch;
        g->gl = 1.0f - (pan > 0.0f ? pan : 0.0f);
        g->gr = 1.0f + (pan < 0.0f ? pan : 0.0f);
        double span = life ? (double)life * (double)aspd * fabsf(pitch) : (double)len;
        g->pos = (double)lo + rel0 + (speed_signed < 0.0f ? span : 0.0);
    }

    double spacing;
    h->spawn_full = (life == 0);
    if (life == 0) {
        double len_src = (double)len;
        double fade_src = win_smooth ? len_src * 0.5 : (double)HM_FADE_CLASSIC * (double)aspd;
        if (fade_src > len_src * 0.5) fade_src = len_src * 0.5;
        if (m < 0.18f) spacing = len_src * (1.0 + (double)(0.18f - m) / 0.18 * 0.5);
        else           spacing = (len_src - fade_src) / (double)morph_neff(m);
    } else if (m < 0.18f) {
        double gap = (double)(0.18f - m) / 0.18 * 0.5 * len_out;
        spacing = len_out + gap;
    } else {
        spacing = (len_out - fade) / (double)morph_neff(m);
    }
    if (sc > 0.0f) spacing *= 1.0 + (double)sc * (double)(2.0f * rng_unit(h) - 1.0f);   /* synchronous -> asynchronous */
    if (spacing < 8.0) spacing = 8.0;
    return spacing;
}

/* --------------------------------------------------------------- recording */

static void rec_begin(hm_t *h, int mode)
{
    reel_t *r = h->reel;
    if (!r || h->rec_mode || atomic_load(&h->saving) || h->n_splices < 0) return;
    if (mode == REC_CUR && r->frames < 2) mode = REC_NEW;      /* nothing to record over yet */
    if (mode == REC_NEW) {
        if (r->frames + HM_MIN_SPLICE >= r->cap) return;
        push_undo(h);
        h->rec_start = h->rec_pos = r->frames;
        if (r->frames >= HM_MIN_SPLICE && h->n_mk < HM_MAX_MARKERS) {   /* the new splice starts where the reel ended */
            h->mk[h->n_mk++] = r->frames;
            markers_changed(h);
        }
        h->sel_override = -1;
    } else {
        int sp = clampi(h->sel_splice, 0, h->n_splices - 1);
        h->rec_lo = h->bounds[sp];
        h->rec_len = h->bounds[sp + 1] - h->bounds[sp];
        h->rec_pos = 0;
    }
    h->rec_mode = mode;
    h->rec_armed = 0;
    h->bounds_dirty = 1;
}

static void rec_end(hm_t *h)
{
    reel_t *r = h->reel;
    if (!h->rec_mode) return;
    int mode = h->rec_mode;
    h->rec_mode = REC_IDLE;
    h->rec_stop_armed = 0;
    if (mode == REC_NEW && r) {
        if (r->frames <= h->rec_start + HM_MIN_SPLICE) {              /* too short: discard, undo the marker */
            if (h->undo_n > 0) { h->rec_mode = REC_IDLE; do_undo(h); }
        } else {
            h->sel_override = h->n_mk;                                 /* hear the new splice at once */
            retrigger(h);
        }
    }
    h->bounds_dirty = 1;
    h->wave_ver++;
    atomic_fetch_add_explicit(&h->state_gen, 1, memory_order_relaxed);
}

static void rec_request(hm_t *h, int mode)
{
    if (h->rec_mode || h->rec_armed) return;
    if (h->p[P_REC_SYNC] > 0.5f && (int)h->p[P_CLOCK_SRC] != CLK_OFF && h->clk_running) h->rec_armed = mode;
    else rec_begin(h, mode);
}

static void rec_stop_request(hm_t *h)
{
    if (h->rec_armed) { h->rec_armed = 0; return; }
    if (!h->rec_mode) return;
    if (h->p[P_REC_SYNC] > 0.5f && (int)h->p[P_CLOCK_SRC] != CLK_OFF && h->clk_running) h->rec_stop_armed = 1;
    else rec_end(h);
}

static void clock_tick(hm_t *h)
{
    h->clk_idx++;
    if (h->since_tick > 1.0) h->tick_len = h->tick_len > 1.0 ? h->tick_len * 0.5 + h->since_tick * 0.5 : h->since_tick;
    h->since_tick = 0.0;
    if (h->rec_armed) rec_begin(h, h->rec_armed);
    else if (h->rec_stop_armed) rec_end(h);
    if (clock_action(h) == 1 && h->p[P_GENE] >= 0.005f && h->spawn_on) retrigger(h);
}

/* ------------------------------------------------------------ note logic */

static void note_stack_remove(hm_t *h, int note)
{
    int j = 0;
    for (int i = 0; i < h->n_notes; i++) if (h->notes[i] != note) h->notes[j++] = h->notes[i];
    h->n_notes = j;
}

static void gate_close(hm_t *h)
{
    int pm = (int)h->p[P_PLAY_MODE];
    if ((int)h->p[P_PAD_MODE] == PAD_DRONE) return;
    if (pm == PLAY_CLASSIC)        h->spawn_on = 0;
    else if (pm == PLAY_MOMENTARY) { h->spawn_on = 0; kill_all(h, (int)(h->p[P_RELEASE] * 0.001f * h->sr)); }
}

static void note_on(hm_t *h, int note)
{
    const int pad = (int)h->p[P_PAD_MODE];
    const int from_silent = (h->n_notes == 0);
    note_stack_remove(h, note);
    if (h->n_notes < HM_MAX_NOTES) h->notes[h->n_notes++] = (uint8_t)note;

    if (pad == PAD_SPLICE) {
        h->pad_splice_raw = note - (int)h->p[P_SPLICE_BASE];
        h->sel_override = -1;
        h->spawn_on = 1; h->started = 1;
        retrigger(h);
    } else if (pad == PAD_PITCH) {
        if (from_silent) { h->spawn_on = 1; h->started = 1; retrigger(h); }
    } else {
        retrigger(h);
    }
}

static void note_off(hm_t *h, int note)
{
    note_stack_remove(h, note);
    if (h->n_notes == 0) gate_close(h);
}

static void all_notes_off(hm_t *h)
{
    h->n_notes = 0;
    if ((int)h->p[P_PAD_MODE] != PAD_DRONE) {
        h->spawn_on = ((int)h->p[P_PLAY_MODE] == PLAY_TRIGLOOP && h->started) ? 1 : 0;
        kill_all(h, (int)(0.005f * h->sr));
    }
}

/* ----------------------------------------------------------------- state */

static void build_state_json(const hm_t *h, char *buf, int len)
{
    jb_t j = { buf, len, 0, 0 };
    char esc[HM_PATH_MAX * 2];
    json_escape(h->cur_path, esc, sizeof esc);
    jb_add(&j, "{\"sample_path\":\"%s\",\"state_format\":%d", esc, HM_STATE_FORMAT);
    for (int i = 0; i < P_COUNT; i++) jb_add(&j, ",\"%s\":%.4f", g_params[i].key, h->p[i]);
    jb_add(&j, "}");
}

/* A complete saved state (it carries "sample_path") restores every parameter:
 * the ones it does not mention, because they are newer than the state, go
 * back to their default instead of keeping whatever the instance had. A
 * partial state only touches the keys it names. Unknown keys are ignored, so
 * a state from a later version still loads. */
static void apply_state_json(hm_t *h, const char *val)
{
    float f;
    const int full = strstr(val, "\"sample_path\"") != NULL;
    for (int i = 0; i < P_COUNT; i++) {
        if (json_num(val, g_params[i].key, &f) == 0) set_param_value(h, i, f);
        else if (full) set_param_value(h, i, g_params[i].def);
    }
    char path[HM_PATH_MAX];
    if (json_str(val, "sample_path", path, sizeof path) == 0 && strcmp(path, h->cur_path) != 0) request_load(h, path);
}

/* ----------------------------------------------------------------- render */

/* -------------------------------------------------------- internal effects
 * EQ -> Delay -> Reverb, in series, after the playback / input mix and before
 * the master volume. Every effect is neutral at its default (EQ flat, both mix
 * controls at 0) and costs nothing then: the EQ is bypassed at 0 dB and the
 * delay and reverb stop processing once their mix has faded to 0. */

static void bq_shelf(biquad_t *q, float sr, float f, float db, int high)
{
    const float A = powf(10.0f, db / 40.0f), w = 2.0f * (float)M_PI * f / sr;
    const float cs = cosf(w), sn = sinf(w), al = sn * 0.70710678f, sa = 2.0f * sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = A * ((A + 1) - (A - 1) * cs + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cs); b2 = A * ((A + 1) - (A - 1) * cs - sa);
        a0 = (A + 1) + (A - 1) * cs + sa;       a1 = -2 * ((A - 1) + (A + 1) * cs);    a2 = (A + 1) + (A - 1) * cs - sa;
    } else {
        b0 = A * ((A + 1) + (A - 1) * cs + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sa);
        a0 = (A + 1) - (A - 1) * cs + sa;       a1 = 2 * ((A - 1) - (A + 1) * cs);      a2 = (A + 1) - (A - 1) * cs - sa;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}

static void bq_peak(biquad_t *q, float sr, float f, float db, float Q)
{
    const float A = powf(10.0f, db / 40.0f), w = 2.0f * (float)M_PI * f / sr;
    const float cs = cosf(w), al = sinf(w) / (2.0f * Q), a0 = 1.0f + al / A;
    q->b0 = (1.0f + al * A) / a0; q->b1 = -2.0f * cs / a0; q->b2 = (1.0f - al * A) / a0;
    q->a1 = -2.0f * cs / a0;      q->a2 = (1.0f - al / A) / a0;
}

static inline float bq_run(biquad_t *q, int c, float x)
{
    const float y = q->b0 * x + q->z1[c];
    q->z1[c] = q->b1 * x - q->a1 * y + q->z2[c];
    q->z2[c] = q->b2 * x - q->a2 * y;
    return y;
}

static inline float soft_clip(float x)                 /* tanh-like, cheap; |x| <= 3 */
{
    x = clampf(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static void fx_init(hm_t *h)
{
    static const uint32_t k_lines[FX_REV_N] = { 1123, 1291, 1447, 1613, 1777, 1949, 2111, 2293 };
    static const uint32_t k_ap[FX_AP_N] = { 142, 107, 379, 277 };
    fx_t *f = &h->fx;
    const float k = h->sr / 44100.0f;
    for (int i = 0; i < FX_REV_N; i++) {
        uint32_t L = (uint32_t)lrintf((float)k_lines[i] * k);
        f->line_len[i] = L < 2 ? 2 : (L > FX_REV_LEN - 1 ? FX_REV_LEN - 1 : L);
    }
    for (int i = 0; i < FX_AP_N; i++) {
        uint32_t L = (uint32_t)lrintf((float)k_ap[i] * k);
        f->ap_len[i] = L < 2 ? 2 : (L > FX_AP_LEN - 1 ? FX_AP_LEN - 1 : L);
    }
    f->eq_cur[0] = f->eq_cur[1] = f->eq_cur[3] = 0.0f;
    f->eq_cur[2] = 1000.0f;
    f->dtime = 0.375f * h->sr;
    f->rev_idle = 1;
    f->rdecay_cur = -1.0f;
    f->rdamp_cur = -1.0f;
    f->dc = 1e-18f;
}

static float fx_bpm(const hm_t *h)
{
    float bpm = h->p[P_BPM];
    if ((int)h->p[P_CLOCK_SRC] != CLK_INTERNAL && g_host && g_host->get_bpm) {
        float b = g_host->get_bpm();
        if (b >= 20.0f && b <= 400.0f) bpm = b;
    }
    return bpm;
}

/* delay time in samples as set now (free time or a note value at the tempo) */
static float fx_delay_target(const hm_t *h)
{
    const int sync = clampi((int)h->p[P_DLY_SYNC], 0, 7);
    float t = sync ? k_dsync_beats[sync] * 60.0f / fx_bpm(h) * h->sr : h->p[P_DLY_TIME] * 0.001f * h->sr;
    return clampf(t, 1.0f, (float)(FX_DLY_LEN - 4));
}

static void fx_block(hm_t *h)
{
    fx_t *f = &h->fx;
    const float *P = h->p, sr = h->sr;
    f->dc = -f->dc;

    /* EQ: glide toward the knobs, recompute the coefficients only while moving */
    const float tgt[4] = { P[P_EQ_LOW], P[P_EQ_MID], P[P_EQ_FREQ], P[P_EQ_HIGH] };
    int moved = 0;
    for (int i = 0; i < 4; i++) {
        float d = tgt[i] - f->eq_cur[i];
        if (d == 0.0f) continue;
        if (fabsf(d) < (i == 2 ? 0.5f : 0.01f)) f->eq_cur[i] = tgt[i];
        else f->eq_cur[i] += d * 0.3f;
        moved = 1;
    }
    if (moved || (f->bq[0].b0 == 0.0f)) {
        bq_shelf(&f->bq[0], sr, 180.0f, f->eq_cur[0], 0);
        bq_peak(&f->bq[1], sr, clampf(f->eq_cur[2], 20.0f, sr * 0.45f), f->eq_cur[1], 0.8f);
        bq_shelf(&f->bq[2], sr, 5000.0f, f->eq_cur[3], 1);
    }
    const int on = f->eq_cur[0] != 0.0f || f->eq_cur[1] != 0.0f || f->eq_cur[3] != 0.0f;
    if (!on && f->eq_on) for (int i = 0; i < 3; i++) memset(f->bq[i].z1, 0, sizeof f->bq[i].z1), memset(f->bq[i].z2, 0, sizeof f->bq[i].z2);
    f->eq_on = on;

    /* reverb: decay -> per-line feedback gain, damping -> line low-pass */
    if (P[P_REV_DECAY] != f->rdecay_cur) {
        f->rdecay_cur = P[P_REV_DECAY];
        const float t60 = 0.3f * powf(40.0f, f->rdecay_cur);         /* 0.3 s .. 12 s */
        for (int i = 0; i < FX_REV_N; i++) f->rg[i] = powf(10.0f, -3.0f * (float)f->line_len[i] / (t60 * sr));
    }
    if (P[P_REV_DAMP] != f->rdamp_cur) {
        f->rdamp_cur = P[P_REV_DAMP];
        const float fc = 18000.0f * powf(0.06f, f->rdamp_cur);       /* 18 kHz .. 1.1 kHz */
        f->rdamp = 1.0f - expf(-2.0f * (float)M_PI * fc / sr);
    }
    if (P[P_REV_MIX] > 0.0f) f->rev_idle = 0;
    else if (!f->rev_idle && f->rmix < 1e-5f) {                       /* faded out: clear and stop */
        memset(f->line, 0, sizeof f->line);
        memset(f->ap, 0, sizeof f->ap);
        memset(f->rlp, 0, sizeof f->rlp);
        f->rmix = 0.0f;
        f->rev_idle = 1;
    }
}

static inline void heads_frame(hm_t *h, float *pl, float *pr);

/* one stereo frame through EQ -> Delay -> Heads -> Reverb */
static inline void fx_frame(hm_t *h, float *pl, float *pr, float dtarget, float tone_a, float hp_a)
{
    fx_t *f = &h->fx;
    const float *P = h->p;
    float l = *pl, r = *pr;

    if (f->eq_on) {
        l += f->dc; r += f->dc;
        for (int i = 0; i < 3; i++) { l = bq_run(&f->bq[i], 0, l); r = bq_run(&f->bq[i], 1, r); }
    }

    /* delay: the time glides, as on a tape echo; tone and a gentle high-pass in the feedback */
    f->dmix += (P[P_DLY_MIX] - f->dmix) * 0.002f;
    f->dtime += (dtarget - f->dtime) * 0.0005f;
    {
        const float rp = (float)f->dw - f->dtime;
        const float fl = floorf(rp), fr = rp - fl;
        const uint32_t i0 = (uint32_t)(int32_t)fl & FX_DLY_MASK, i1 = (i0 + 1) & FX_DLY_MASK;
        const float yl = f->dl[i0] + (f->dl[i1] - f->dl[i0]) * fr;
        const float yr = f->dr[i0] + (f->dr[i1] - f->dr[i0]) * fr;
        const float fb = P[P_DLY_FDBK];
        f->dlp[0] += (yl - f->dlp[0]) * tone_a; f->dlp[1] += (yr - f->dlp[1]) * tone_a;
        f->dhp[0] += (f->dlp[0] - f->dhp[0]) * hp_a; f->dhp[1] += (f->dlp[1] - f->dhp[1]) * hp_a;
        const float tl = f->dlp[0] - f->dhp[0], tr = f->dlp[1] - f->dhp[1];
        float wl, wr;
        if (P[P_DLY_PING] > 0.5f) { wl = 0.5f * (l + r) + fb * tr; wr = fb * tl; }
        else { wl = l + fb * tl; wr = r + fb * tr; }
        f->dl[f->dw & FX_DLY_MASK] = soft_clip(wl) + f->dc;
        f->dr[f->dw & FX_DLY_MASK] = soft_clip(wr) + f->dc;
        f->dw++;
        const float m = f->dmix, dry = m < 0.5f ? 1.0f : 2.0f * (1.0f - m), wet = m > 0.5f ? 1.0f : 2.0f * m;
        l = l * dry + yl * wet;
        r = r * dry + yr * wet;
    }

    heads_frame(h, &l, &r);

    if (!f->rev_idle) {
        f->rmix += (P[P_REV_MIX] - f->rmix) * 0.002f;
        float x = 0.25f * (l + r) + f->dc;
        const uint32_t aw = f->ap_w++;
        for (int k = 0; k < FX_AP_N; k++) {                           /* input diffusion */
            float *b = f->ap[k];
            const float d = b[(aw - f->ap_len[k]) & (FX_AP_LEN - 1)];
            const float w = x + 0.6f * d;
            b[aw & (FX_AP_LEN - 1)] = w;
            x = d - 0.6f * w;
        }
        float o[FX_REV_N];
        const uint32_t rw = f->rw++;
        for (int i = 0; i < FX_REV_N; i++) {
            const float y = f->line[i][(rw - f->line_len[i]) & (FX_REV_LEN - 1)];
            f->rlp[i] += (y - f->rlp[i]) * f->rdamp;
            o[i] = f->rlp[i];
        }
        float m[FX_REV_N];                                            /* 8-point Hadamard */
        for (int i = 0; i < FX_REV_N; i++) m[i] = o[i];
        for (int s = 1; s < FX_REV_N; s <<= 1)
            for (int i = 0; i < FX_REV_N; i += s << 1)
                for (int j = i; j < i + s; j++) { const float a = m[j], b = m[j + s]; m[j] = a + b; m[j + s] = a - b; }
        static const float k_in_sign[FX_REV_N] = { 1, 1, -1, 1, -1, -1, 1, -1 };
        for (int i = 0; i < FX_REV_N; i++)
            f->line[i][rw & (FX_REV_LEN - 1)] = f->rg[i] * m[i] * 0.35355339f + k_in_sign[i] * x;
        const float revl = 0.6f * (o[0] + o[2] - o[4] + o[6]), revr = 0.6f * (o[1] - o[3] + o[5] + o[7]);
        const float rm = f->rmix, dry = rm < 0.5f ? 1.0f : 2.0f * (1.0f - rm), wet = rm > 0.5f ? 1.0f : 2.0f * rm;
        l = l * dry + revl * wet;
        r = r * dry + revr * wet;
    }
    *pl = l; *pr = r;
}

/* ------------------------------------------------------------------ heads
 * A loop of tape with up to four playback heads per channel. Each head is a
 * delay line of its own; how the heads feed each other is the Routing:
 *   Own     each head feeds back into itself
 *   Cross   each head feeds the same head on the other channel
 *   Chain   the heads of a channel in series, the last back into the first
 *   Wander  in series, crossing to the other channel at every step
 * Time is a note value at Heimat's tempo; Spacing spreads the heads from a
 * tight cluster to wide intervals (and detunes right against left). Reverse
 * makes heads read backwards, in the order 1L 1R 2L 2R ...; Wear is what
 * each pass through the feedback does to the sound. Mode decides how a head
 * follows a change of time (Fade, Glide) or shifts every repeat an octave
 * up (Rise) or down (Fall). Freeze keeps what is on the tape and loops it;
 * Clear makes everything written so far inaudible, without touching memory
 * on the audio thread. With Mix at 0 the heads stop and cost nothing. */

static void heads_init(hm_t *h)
{
    heads_t *H = &h->hd;
    for (int i = 0; i < HD_LINES; i++) {
        H->ln[i].buf = (float *)calloc(HD_LEN, sizeof(float));
        H->ln[i].t_old = H->ln[i].t_new = H->ln[i].t_cur = 0.25f * h->sr;
        H->ln[i].xf = 1.0f;
    }
    H->idle = 1;
}

static void heads_free(hm_t *h)
{
    for (int i = 0; i < HD_LINES; i++) { free(h->hd.ln[i].buf); h->hd.ln[i].buf = NULL; }
}

static int heads_ok(const hm_t *h)
{
    for (int i = 0; i < HD_LINES; i++) if (!h->hd.ln[i].buf) return 0;
    return 1;
}

static inline float hd_read(const hd_line_t *L, double d)
{
    const double pos = (double)L->w - d;
    if (pos < (double)L->clr || pos < 0.0) return 0.0f;
    const double fl = floor(pos);
    const float fr = (float)(pos - fl);
    const uint64_t i0 = (uint64_t)fl;
    const float a = L->buf[i0 & HD_MASK], b = L->buf[(i0 + 1) & HD_MASK];
    return a + (b - a) * fr;
}

/* what one pass through the feedback does to the sound */
static inline float hd_wear(hd_line_t *L, int kind, float d, float a, float x)
{
    if (d < 1e-4f) return x;
    switch (kind) {
    case 0:                                                          /* Dark: 4-pole low-pass */
        for (int s = 0; s < 4; s++) { L->f[s] += (x - L->f[s]) * a; x = L->f[s]; }
        return x;
    case 1:                                                          /* Thin: 4-pole high-pass */
        for (int s = 0; s < 4; s++) { L->f[s] += (x - L->f[s]) * a; x = x - L->f[s]; }
        return x;
    case 2: {                                                        /* Crush: bits and sample rate */
        const int hold = 1 + (int)(23.0f * d * d);
        if (++L->hold_n >= hold) {
            L->hold_n = 0;
            const float q = exp2f(15.0f - 13.0f * d);
            L->hold = floorf(x * q + 0.5f) / q;
        }
        return L->hold;
    }
    case 3: {                                                        /* Warm: soft saturation */
        const float g = 1.0f + 5.0f * d;
        return x + fminf(1.0f, 3.0f * d) * (soft_clip(g * x) / sqrtf(g) - x);
    }
    case 4: {                                                        /* Fold: wavefolder */
        const float e = sinf(1.5707963f * (1.0f + 6.0f * d) * x);
        return x + fminf(1.0f, 3.0f * d) * (e - x);
    }
    default: {                                                       /* Fuzz: hard, a little lopsided */
        const float e = clampf((1.0f + 30.0f * d) * x + 0.12f * d, -0.7f, 0.7f);
        return x + fminf(1.0f, 3.0f * d) * (e - x);
    }
    }
}

static float heads_time_target(const hm_t *h)
{
    const int i = clampi((int)h->p[P_HD_TIME], 0, 13);
    return k_hd_beats[i] * 60.0f / fx_bpm(h) * h->sr;
}

static void heads_block(hm_t *h)
{
    heads_t *H = &h->hd;
    const float *P = h->p;
    if (!heads_ok(h)) { H->idle = 1; return; }
    if (P[P_HD_MIX] > 0.0f || P[P_HD_FREEZE] > 0.5f) {
        if (H->idle) for (int i = 0; i < HD_LINES; i++) { H->ln[i].clr = H->ln[i].w; H->ln[i].active = 0; }  /* forget old tape */
        H->idle = 0;
    } else if (!H->idle && H->mix < 1e-5f) { H->mix = 0.0f; H->idle = 1; }
    if (H->clear_req) {
        H->clear_req = 0;
        for (int i = 0; i < HD_LINES; i++) { H->ln[i].clr = H->ln[i].w; H->ln[i].out = 0.0f; H->ln[i].ph = 0.0f; }   /* backwards chunks start again */
    }
    if (H->idle) return;

    const int route = clampi((int)P[P_HD_ROUTE], 0, 3);
    const int n = clampi((int)P[P_HD_HEADS], 1, 4);
    const int lines = route >= 2 ? 4 : n;                           /* Chain and Wander always run four */
    const float T = heads_time_target(h), sp = P[P_HD_SPACE];
    const int rc = (int)lrintf(P[P_HD_REV] * (float)(2 * lines));
    const float lim = (float)(HD_LEN / 2) - HD_WIN - 16.0f;
    for (int c = 0; c < 2; c++)
        for (int k = 0; k < 4; k++) {
            hd_line_t *L = &H->ln[c * 4 + k];
            const int on = k < lines;
            if (on && !L->active) { L->clr = L->w; L->out = 0.0f; }   /* a head that joins starts silent */
            L->active = on;
            float t = route >= 2 ? T * (1.0f + 0.25f * (float)k * sp) : T * (1.0f + (float)k * (0.07f + 0.93f * sp));
            if (c) t *= 1.0f + 0.02f * sp;
            L->target = clampf(t, 32.0f, lim);
            L->rev = (2 * k + c) < rc;
            if (L->xf >= 1.0f && fabsf(L->target - L->t_new) > 0.5f) { L->t_old = L->t_new; L->t_new = L->target; L->xf = 0.0f; }
        }
    const float d = P[P_HD_WAMT];
    const int kind = clampi((int)P[P_HD_WEAR], 0, 5);
    if (kind == 0) H->wear_a = 1.0f - expf(-2.0f * (float)M_PI * 18000.0f * powf(300.0f / 18000.0f, d) / h->sr);
    else if (kind == 1) H->wear_a = 1.0f - expf(-2.0f * (float)M_PI * 20.0f * powf(3000.0f / 20.0f, d) / h->sr);
}

static inline float hd_line_out(hd_line_t *L, int mode)
{
    if (L->rev) {                                                    /* backwards, in windowed chunks */
        L->t_cur += (L->target - L->t_cur) * 0.002f;
        const float D = L->t_cur;
        L->ph += 1.0f / D; if (L->ph >= 1.0f) L->ph -= 1.0f;
        float y = 0.0f;
        for (int r = 0; r < 2; r++) {
            float p = L->ph + 0.5f * (float)r; if (p >= 1.0f) p -= 1.0f;
            const float wv = sinf((float)M_PI * p);
            y += wv * wv * hd_read(L, 2.0 * p * D + 1.0);
        }
        return y;
    }
    if (mode >= 2) {                                                 /* Rise / Fall: an octave per pass */
        L->t_cur += (L->target - L->t_cur) * 0.002f;
        L->ph += 1.0f / HD_WIN; if (L->ph >= 1.0f) L->ph -= 1.0f;
        float y = 0.0f;
        for (int r = 0; r < 2; r++) {
            float p = L->ph + 0.5f * (float)r; if (p >= 1.0f) p -= 1.0f;
            const float wv = sinf((float)M_PI * p);
            const double dl = mode == 2 ? L->t_cur + HD_WIN * (1.0f - p) : L->t_cur + 0.5f * HD_WIN * p;
            y += wv * wv * hd_read(L, dl);
        }
        return y;
    }
    if (mode == 1) {                                                 /* Glide: the time bends like tape */
        L->t_cur += (L->target - L->t_cur) * 0.0004f;
        return hd_read(L, L->t_cur);
    }
    L->t_cur = L->t_new;                                             /* Fade: cross-fade to the new time */
    if (L->xf < 1.0f) {
        const float y = (1.0f - L->xf) * hd_read(L, L->t_old) + L->xf * hd_read(L, L->t_new);
        L->xf += 1.0f / HD_XF;
        return y;
    }
    return hd_read(L, L->t_new);
}

static inline void heads_frame(hm_t *h, float *pl, float *pr)
{
    heads_t *H = &h->hd;
    if (H->idle) return;
    const float *P = h->p;
    H->mix += (P[P_HD_MIX] - H->mix) * 0.002f;
    const int route = clampi((int)P[P_HD_ROUTE], 0, 3), mode = clampi((int)P[P_HD_MODE], 0, 3);
    const int n = clampi((int)P[P_HD_HEADS], 1, 4), lines = route >= 2 ? 4 : n;
    const int freeze = P[P_HD_FREEZE] > 0.5f, kind = clampi((int)P[P_HD_WEAR], 0, 5);
    const float fb = P[P_HD_FDBK], d = P[P_HD_WAMT], a = H->wear_a;
    const float in[2] = { *pl, *pr };
    float o[HD_LINES];
    for (int i = 0; i < HD_LINES; i++) o[i] = H->ln[i].active ? hd_line_out(&H->ln[i], mode) : 0.0f;
    float wet[2] = { 0.0f, 0.0f };
    for (int c = 0; c < 2; c++) {
        for (int k = 0; k < lines; k++) {
            hd_line_t *L = &H->ln[c * 4 + k];
            if (k < n) wet[c] += o[c * 4 + k];
            float x;
            if (freeze) x = o[c * 4 + k];
            else {
                int src, first;
                switch (route) {
                case 1:  src = (1 - c) * 4 + k; first = 1; break;                       /* Cross */
                case 2:  src = c * 4 + (k ? k - 1 : lines - 1); first = k == 0; break; /* Chain */
                case 3:  src = (1 - c) * 4 + (k ? k - 1 : lines - 1); first = k == 0; break; /* Wander */
                default: src = c * 4 + k; first = 1; break;                             /* Own */
                }
                const float worn = hd_wear(L, kind, d, a, o[src]);
                x = first ? in[c] + fb * worn : worn;
            }
            L->dcx += (x - L->dcx) * 0.0028f;                         /* keep DC out of the loop */
            L->buf[L->w & HD_MASK] = soft_clip(x - L->dcx) + h->fx.dc;
            L->w++;
        }
        wet[c] *= 1.0f / sqrtf((float)n);
    }
    const float m = H->mix, dry = m < 0.5f ? 1.0f : 2.0f * (1.0f - m), wg = m > 0.5f ? 1.0f : 2.0f * m;
    *pl = in[0] * dry + wet[0] * wg;
    *pr = in[1] * dry + wet[1] * wg;
}

static void hm_render(hm_t *h, int16_t *out, int frames)
{
    const float sr = h->sr;

    if (atomic_load_explicit(&h->restore_ready, memory_order_acquire)) {          /* settings saved by a previous session */
        apply_state_json(h, h->restore_json);
        atomic_store(&h->restore_ready, 0);
    }

    /* --- adopt a freshly loaded reel; hand the old one back to the worker --- */
    reel_t *nr = atomic_exchange_explicit(&h->incoming, NULL, memory_order_acquire);
    if (nr) {
        reel_t *old = h->reel;
        for (int i = 0; i < HM_MAX_GENES; i++) h->genes[i].active = 0;
        h->rec_mode = REC_IDLE; h->rec_armed = 0; h->rec_stop_armed = 0;
        h->reel = nr;
        adopt_markers_from_reel(h);
        h->bounds_dirty = 1;
        h->spawn_cd = 0.0;
        h->sel_override = -1;
        h->wave_ver++;
        if (old) {
            old->next = atomic_load_explicit(&h->retire, memory_order_relaxed);
            while (!atomic_compare_exchange_weak_explicit(&h->retire, &old->next, old,
                                                          memory_order_release, memory_order_relaxed)) {}
        }
    }
    if (atomic_load_explicit(&h->mk_res_ready, memory_order_acquire)) {           /* transient detection result */
        if (!h->rec_mode && h->reel) {
            push_undo(h);
            h->n_mk = h->mk_res.n;
            memcpy(h->mk, h->mk_res.m, sizeof(uint32_t) * (size_t)h->mk_res.n);
            markers_changed(h);
        }
        atomic_store(&h->mk_res_ready, 0);
    }

    reel_t *reel = (h->reel && h->reel->cap >= 2) ? h->reel : NULL;
    if (h->bounds_dirty || h->rec_mode == REC_NEW) compute_bounds(h);

    /* --- clock --- */
    {
        const int cs = (int)h->p[P_CLOCK_SRC];
        const double div = k_div_beats[clampi((int)h->p[P_CLOCK_DIV], 0, 4)];
        int ticks = 0;
        if (cs == CLK_INTERNAL) {
            h->clk_running = 1;
            h->clk_period = (double)sr * 60.0 / (double)h->p[P_BPM] * div;
            h->clk_acc += frames;
            while (h->clk_acc >= h->clk_period) { h->clk_acc -= h->clk_period; ticks++; }
        } else if (cs == CLK_HOST && g_host && g_host->get_clock_status && g_host->get_beat_position) {
            h->clk_running = (g_host->get_clock_status() == MOVE_CLOCK_STATUS_RUNNING);
            if (h->clk_running) {
                double b = g_host->get_beat_position() / div;
                if (floor(b) > floor(h->last_beat) && b - h->last_beat < 4.0) ticks = 1;
                h->last_beat = b;
            }
        } else if (cs == CLK_MIDI) {
            ticks = h->ticks_pending;
        } else {
            h->clk_running = 0;
        }
        h->ticks_pending = 0;
        h->since_tick += frames;
        for (int i = 0; i < ticks; i++) clock_tick(h);
    }

    const int16_t *in16 = (HM_SOLO && g_host && g_host->mapped_memory)
                              ? (const int16_t *)(g_host->mapped_memory + g_host->audio_in_offset) : NULL;

    /* nothing to play: stop here, unless Solo can hear its input (then the input
       and the effect tails go on through the effects, reel or no reel) */
    if ((!reel || h->n_splices < 1) && !in16) {
        if (!(reel && h->rec_mode)) {
            h->out_peak *= 0.9f;
            memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
            return;
        }
    }

    const float *P = h->p;
    const int pad = (int)P[P_PAD_MODE];
    const int pm = (int)P[P_PLAY_MODE];
    if (pad == PAD_DRONE) h->spawn_on = 1;
    else if (pm == PLAY_TRIGLOOP && h->started) h->spawn_on = 1;

    /* --- splice selection --- */
    if (h->n_splices > 0) {
        int n = h->n_splices, sel;
        if (h->sel_override >= 0) sel = clampi(h->sel_override, 0, n - 1);
        else if (pad == PAD_SPLICE) sel = ((h->pad_splice_raw % n) + n) % n;
        else { sel = (int)(P[P_ORGANIZE] * (float)n); if (sel > n - 1) sel = n - 1; if (sel < 0) sel = 0; }
        if (sel != h->sel_splice) {
            h->sel_splice = sel;
            if ((int)P[P_ORG_MODE] == 1 && pad != PAD_SPLICE) retrigger(h);
        }
    }

    float speed_target;
    {
        float v = P[P_VSPEED], u = fabsf(v), r;
        if (u < 0.02f) r = 0.0f;
        else if (u < 0.5f) { float t = u / 0.5f; r = t * t; }
        else r = exp2f((u - 0.5f) * 2.0f);
        float pr = 1.0f;
        if (pad == PAD_PITCH && h->n_notes > 0) {
            int note = h->notes[h->n_notes - 1] + 12 * (int)P[P_OCT];
            pr = exp2f((float)(note - (int)P[P_ROOT]) * (1.0f / 12.0f));
        }
        speed_target = (v < 0.0f ? -r : r) * pr;
        speed_target = clampf(speed_target, -16.0f, 16.0f);
    }
    const float morph = P[P_MORPH];
    const float neff = morph_neff(morph);
    const float play_gain = 1.0f / sqrtf(neff > 1.0f ? neff : 1.0f);
    const float master = P[P_MUTE] > 0.5f ? 0.0f : P[P_VOLUME];
    const float sos = P[P_SOS];
    const float ing = k_in_gain[clampi((int)P[P_IN_GAIN], 0, 3)];
    const int rec_input_only = (int)P[P_REC_SRC] == 1;
    float in_pk = 0.0f, out_pk = 0.0f;
    uint32_t rec_w0 = h->rec_pos;
    const int fx_rec = P[P_FX_REC] > 0.5f;
    fx_block(h);
    heads_block(h);
    const float fx_dt = fx_delay_target(h);
    const float fx_tone = 1.0f - expf(-2.0f * (float)M_PI * (800.0f * exp2f(P[P_DLY_TONE] * 4.5f)) / sr);
    const float fx_hp = 1.0f - expf(-2.0f * (float)M_PI * 40.0f / sr);

    for (int n = 0; n < frames; n++) {
        h->bend_cur += (h->bend_target - h->bend_cur) * 0.01f;
        const float bend = exp2f(h->bend_cur * (1.0f / 12.0f));
        h->speed += (speed_target * bend - h->speed) * 0.003f;
        const float speed = h->speed, aspd_k = fabsf(speed);
        const int run = aspd_k > 1e-3f;

        if (reel && h->n_splices > 0 && h->spawn_on && run) {
            h->spawn_cd -= h->spawn_full ? (double)aspd_k : 1.0;
            if (h->spawn_cd <= 0.0) {
                int roles = (int)ceilf(neff);
                if (roles < 1) roles = 1;
                if (roles > HM_MAX_GENES) roles = HM_MAX_GENES;
                int rank = (int)(h->spawn_idx++ % (uint32_t)roles);
                double sp = spawn_gene(h, rank, speed);
                h->genes_spawned++;
                h->spawn_cd += sp;
                if (h->spawn_cd < 1.0) h->spawn_cd = 1.0;
            }
        }

        float sl = 0.0f, sr_ = 0.0f;
        for (int i = 0; i < HM_MAX_GENES && reel; i++) {
            gene_t *g = &h->genes[i];
            if (!g->active) continue;

            const double step = (double)speed * (double)g->pitch;
            double u_out;
            int ended = 0;
            if (g->life) { u_out = (double)g->life - (double)g->age; ended = (g->age >= g->life); }
            else {
                double as = fabs(step); if (as < 1e-6) as = 1e-6;
                u_out = (g->lim_src - g->traveled) / as;
                ended = (g->traveled >= g->lim_src);
            }
            if (ended) { g->active = 0; continue; }

            float env = gene_env(g->shape, (double)g->age, u_out);
            if (g->kill_left > 0) {
                env *= (float)g->kill_left / (float)g->kill_total;
                if (--g->kill_left == 0) { g->active = 0; continue; }
            }

            double rel = g->pos - (double)g->lo;
            double L = (double)g->len;
            if (rel < 0.0 || rel >= L) rel -= L * floor(rel / L);
            float a, b;
            read_reel(reel, (double)g->lo + rel, &a, &b);
            sl += a * env * g->gl;
            sr_ += b * env * g->gr;

            if (run) {
                g->pos += step;
                g->traveled += fabs(step);
                g->age++;
            }
        }

        const float stop_g = aspd_k >= 0.05f ? 1.0f : aspd_k * 20.0f;
        float pl = sl * play_gain * stop_g, pr_ = sr_ * play_gain * stop_g;

        float il = 0.0f, ir = 0.0f;
        if (in16) {
            il = (float)in16[2 * n] * (1.0f / 32768.0f) * ing;
            ir = (float)in16[2 * n + 1] * (1.0f / 32768.0f) * ing;
            float a = fabsf(il) > fabsf(ir) ? fabsf(il) : fabsf(ir);
            if (a > in_pk) in_pk = a;
        }
        const float ml = il * (1.0f - sos) + pl * sos, mr = ir * (1.0f - sos) + pr_ * sos;
        float xl = ml, xr = mr;
        fx_frame(h, &xl, &xr, fx_dt, fx_tone, fx_hp);

        if (h->rec_mode && reel) {
            float wl = rec_input_only ? il : (fx_rec ? xl : ml), wr = rec_input_only ? ir : (fx_rec ? xr : mr);
            uint32_t w;
            if (h->rec_mode == REC_NEW) {
                if (h->rec_pos >= reel->cap) { rec_end(h); goto recorded; }
                w = h->rec_pos;
            } else {
                w = h->rec_lo + (h->rec_pos % (h->rec_len ? h->rec_len : 1));
            }
            reel->l[w] = clampf(wl, -1.0f, 1.0f);
            reel->r[w] = clampf(wr, -1.0f, 1.0f);
            h->rec_pos++;
            if (h->rec_mode == REC_NEW && h->rec_pos > reel->frames) reel->frames = h->rec_pos;
        }
    recorded:;
        float L = tanhf(xl * master);
        float R = tanhf(xr * master);
        float a = fabsf(L) > fabsf(R) ? fabsf(L) : fabsf(R);
        if (a > out_pk) out_pk = a;
        out[2 * n]     = (int16_t)lrintf(L * 32767.0f);
        out[2 * n + 1] = (int16_t)lrintf(R * 32767.0f);
    }
    h->out_peak = out_pk > h->out_peak * 0.9f ? out_pk : h->out_peak * 0.9f;
    h->in_peak  = in_pk  > h->in_peak  * 0.9f ? in_pk  : h->in_peak  * 0.9f;

    if (h->rec_mode && reel) {                         /* keep the peak pyramid in step with what was written */
        uint32_t a, b;
        if (h->rec_mode == REC_NEW) { a = rec_w0; b = h->rec_pos; }
        else { a = h->rec_lo; b = h->rec_lo + h->rec_len; }
        if (b > reel->frames) b = reel->frames;
        if (b > a) for (uint32_t k = a / HM_PK_BLOCK; k <= (b - 1) / HM_PK_BLOCK && k < reel->pk_n; k++) pk_block(reel, k);
        h->wave_ver++;
    }

    /* the browser editor follows the genes while something sounds or records */
    if (h->spawn_on || h->rec_mode || h->rec_armed || out_pk > 1e-4f) {
        if (++h->rui_blk >= HM_RUI_EVERY) { h->rui_blk = 0; h->rui_viz++; }
    }
}

/* -------------------------------------------------------------- plugin API */

static void *v2_create_instance(const char *module_dir, const char *json_defaults)
{
    (void)module_dir;
    hm_t *h = (hm_t *)calloc(1, sizeof(hm_t));
    if (!h) return NULL;
    h->sr = (g_host && g_host->sample_rate > 0) ? (float)g_host->sample_rate : 44100.0f;
    h->rng = 0x2545F491u;
    h->sel_override = -1;
    h->view_b = 0;
    for (int i = 0; i < P_COUNT; i++) h->p[i] = g_params[i].def;
    fx_init(h);
    heads_init(h);
    atomic_init(&h->incoming, NULL);
    atomic_init(&h->retire, NULL);
    atomic_init(&h->req_seq, 0);
    atomic_init(&h->load_state, 0);
    atomic_init(&h->run, 1);
    atomic_init(&h->saving, 0);
    atomic_init(&h->save_seq, 0);
    atomic_init(&h->save_state, 0);
    atomic_init(&h->detect_req, 0);
    atomic_init(&h->mk_res_ready, 0);
    atomic_init(&h->mk_pub_seq, 0);
    atomic_init(&h->state_gen, 0);
    atomic_init(&h->restore_ready, 0);
    h->bounds_dirty = 1;
    if (HM_SOLO) { h->p[P_PAD_MODE] = PAD_PITCH; h->p[P_PLAY_MODE] = PLAY_MOMENTARY; }
    if (pthread_create(&h->th, NULL, worker_main, h) == 0) h->th_started = 1;
    if (json_defaults) {
        char path[HM_PATH_MAX];
        if (json_str(json_defaults, "sample_path", path, sizeof path) == 0 && path[0]) request_load(h, path);
    }
    if (HM_SOLO && h->cur_path[0] == 0) request_load(h, "");              /* start with an empty reel */
    return h;
}

static void v2_destroy_instance(void *instance)
{
    hm_t *h = (hm_t *)instance;
    if (!h) return;
    atomic_store(&h->run, 0);
    if (h->th_started) pthread_join(h->th, NULL);
    reel_t *in = atomic_exchange(&h->incoming, NULL);
    free_reel(in);
    free_reel(h->reel);
    reel_t *p = atomic_exchange(&h->retire, NULL);
    while (p) { reel_t *n = p->next; free_reel(p); p = n; }
    heads_free(h);
    free(h);
}

static void v2_on_midi(void *instance, const uint8_t *msg, int len, int source)
{
    hm_t *h = (hm_t *)instance;
    (void)source;
    if (!h || len < 1) return;
    uint8_t st = msg[0];
    if (st >= 0xF8) {                                                  /* realtime: MIDI clock */
        if (st == 0xF8 && (int)h->p[P_CLOCK_SRC] == CLK_MIDI) {
            h->clk_running = 1;
            uint32_t per = (uint32_t)(24.0 * k_div_beats[clampi((int)h->p[P_CLOCK_DIV], 0, 4)]);
            if (per < 1) per = 1;
            if (h->midi_pulses % per == 0) h->ticks_pending++;
            h->midi_pulses++;
        } else if (st == 0xFA) { h->midi_pulses = 0; h->clk_idx = 0; h->clk_running = 1; }
        else if (st == 0xFC)   { h->clk_running = 0; }
        else if (st == 0xFB)   { h->clk_running = 1; }
        return;
    }
    if (len < 3) return;
    switch (st & 0xF0) {
    case 0x90:
        if (msg[2] > 0) note_on(h, msg[1]); else note_off(h, msg[1]);
        break;
    case 0x80: note_off(h, msg[1]); break;
    case 0xB0:
        if (msg[1] == 123 || msg[1] == 120) all_notes_off(h);
        break;
    case 0xE0: {
        int raw = ((msg[2] << 7) | msg[1]) - 8192;
        h->bend_target = ((float)raw / 8192.0f) * h->p[P_BEND];
        break;
    }
    default: break;
    }
}

static uint32_t parse_u32(const char *s) { return (uint32_t)strtoul(s, NULL, 10); }

static void v2_set_param(void *instance, const char *key, const char *val)
{
    hm_t *h = (hm_t *)instance;
    if (!h || !key || !val) return;

    if (strcmp(key, "sample_path") == 0) {
        if (strcmp(val, h->cur_path) == 0 && atomic_load(&h->load_state) != 3 && h->cur_path[0]) return;
        if (h->rec_mode) return;
        request_load(h, val);
        return;
    }
    if (strcmp(key, "state") == 0) { apply_state_json(h, val); return; }
    if (strcmp(key, "all_notes_off") == 0) { all_notes_off(h); return; }

    /* editing and transport commands (used by the Solo UI) */
    if (strcmp(key, "rec_cur") == 0)  { rec_request(h, REC_CUR); return; }
    if (strcmp(key, "rec_new") == 0)  { rec_request(h, REC_NEW); return; }
    if (strcmp(key, "rec_stop") == 0) { rec_stop_request(h); return; }
    if (strcmp(key, "hd_clear") == 0) { h->hd.clear_req = 1; return; }
    if (strcmp(key, "rec_toggle") == 0) { if (h->rec_mode || h->rec_armed) rec_stop_request(h); else rec_request(h, REC_CUR); return; }
    if (strcmp(key, "rec_toggle_new") == 0) { if (h->rec_mode || h->rec_armed) rec_stop_request(h); else rec_request(h, REC_NEW); return; }
    if (strcmp(key, "marker_add") == 0) {
        uint32_t pos;
        if (strcmp(val, "play") == 0) {                                        /* at the playhead of the newest gene */
            pos = 0;
            for (int i = 0; i < HM_MAX_GENES; i++) if (h->genes[i].active) {
                double rel = h->genes[i].pos - (double)h->genes[i].lo, L = (double)h->genes[i].len;
                rel -= L * floor(rel / L);
                pos = (uint32_t)((double)h->genes[i].lo + rel);
            }
        } else pos = parse_u32(val);
        marker_add(h, pos);
        return;
    }
    if (strcmp(key, "marker_del") == 0)     { marker_del(h, atoi(val)); return; }
    if (strcmp(key, "marker_del_cur") == 0) { if (h->sel_splice < h->n_mk) marker_del(h, h->sel_splice); return; }
    if (strcmp(key, "marker_move") == 0) {
        int idx = atoi(val);
        const char *c = strchr(val, ',');
        if (c) marker_move(h, idx, parse_u32(c + 1));
        return;
    }
    if (strcmp(key, "markers_clear") == 0)  { markers_clear(h); return; }
    if (strcmp(key, "markers_divide") == 0) { markers_divide(h, atoi(val)); return; }
    if (strcmp(key, "detect_markers") == 0) { h->detect_sens = (float)atof(val); atomic_store(&h->detect_req, 1); return; }
    if (strcmp(key, "undo") == 0)           { do_undo(h); return; }
    if (strcmp(key, "save_reel") == 0)      { request_save(h, val); return; }
    if (strcmp(key, "sel_splice") == 0)     { h->sel_override = atoi(val); retrigger(h); return; }
    if (strcmp(key, "play_latch") == 0)     { h->spawn_on = atoi(val) ? 1 : 0; h->started = 1; if (h->spawn_on) retrigger(h); else kill_all(h, (int)(h->p[P_RELEASE] * 0.001f * h->sr)); return; }
    if (strcmp(key, "view") == 0) {
        const char *c = strchr(val, ',');
        h->view_a = parse_u32(val);
        h->view_b = c ? parse_u32(c + 1) : 0;
        return;
    }
    if (strcmp(key, "organize") == 0) h->sel_override = -1;                    /* the knob takes over again */

    int i = param_index(key);
    if (i < 0) return;
    float f;
    if (g_params[i].type == T_ENUM) { if (parse_enum(&g_params[i], val, &f) != 0) return; }
    else f = (float)atof(val);
    set_param_value(h, i, f);
}

static int get_chain_params(char *buf, int buf_len)
{
    jb_t j = { buf, buf_len, 0, 0 };
    jb_add(&j, "[{\"key\":\"sample_path\",\"name\":\"Reel (WAV file)\",\"type\":\"filepath\","
               "\"root\":\"/data/UserData\",\"start_path\":\"/data/UserData/UserLibrary/Samples\","
               "\"filter\":\".wav\",\"default\":\"\"}");
    for (int i = 0; i < P_COUNT; i++) {
        const pdef_t *d = &g_params[i];
        jb_add(&j, ",{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\"", d->key, d->name, d->sname);
        if (d->type == T_ENUM) {
            jb_add(&j, ",\"type\":\"enum\",\"options\":[");
            for (int o = 0; o < d->nopts; o++) jb_add(&j, "%s\"%s\"", o ? "," : "", d->opts[o]);
            jb_add(&j, "],\"default\":%d}", (int)d->def);
        } else if (d->type == T_INT) {
            jb_add(&j, ",\"type\":\"int\",\"min\":%d,\"max\":%d,\"step\":1,\"default\":%d}", (int)d->min, (int)d->max, (int)d->def);
        } else {
            jb_add(&j, ",\"type\":\"float\",\"min\":%g,\"max\":%g,\"step\":%g,\"default\":%g}", d->min, d->max, d->step, d->def);
        }
    }
    jb_add(&j, "]");
    return j.ovf ? -1 : j.len;
}

static int get_ui_hierarchy(char *buf, int buf_len)
{
    static const char *hh =
        "{\"modes\":null,\"levels\":{"
        "\"root\":{\"name\":\"Heimat\",\"children\":null,"
            "\"knobs\":[\"gene_size\",\"slide\",\"morph\",\"vari_speed\",\"organize\",\"volume\",\"pad_mode\",\"play_mode\"],"
            "\"params\":[{\"level\":\"reel\",\"label\":\"Reel / Splices\"},{\"level\":\"play\",\"label\":\"Play / Pads\"},"
            "{\"level\":\"micro\",\"label\":\"Micro\"},{\"level\":\"fx\",\"label\":\"Effects\"},{\"level\":\"heads\",\"label\":\"Heads\"},{\"level\":\"chords\",\"label\":\"Morph Chords\"}]},"
        "\"reel\":{\"name\":\"Reel / Splices\",\"children\":null,"
            "\"knobs\":[\"splice_source\",\"divide\",\"splice_base\"],"
            "\"params\":[\"sample_path\",\"splice_source\",\"divide\",\"splice_base\"]},"
        "\"play\":{\"name\":\"Play / Pads\",\"children\":null,"
            "\"knobs\":[\"pad_mode\",\"play_mode\",\"release_ms\",\"root_note\",\"octave_transpose\",\"bend_range\",\"organize_mode\"],"
            "\"params\":[\"pad_mode\",\"play_mode\",\"release_ms\",\"root_note\",\"octave_transpose\",\"bend_range\",\"organize_mode\"]},"
        "\"micro\":{\"name\":\"Micro\",\"children\":null,"
            "\"knobs\":[\"gene_window\",\"scatter\",\"spread\",\"width\",\"threshold\",\"thr_mode\",\"spread_mode\",\"gene_reverse\"],"
            "\"params\":[\"gene_window\",\"scatter\",\"spread\",\"spread_mode\",\"gene_reverse\",\"width\",\"threshold\",\"thr_mode\",\"gene_size\",\"morph\"]},"
        "\"heads\":{\"name\":\"Heads\",\"children\":null,"
            "\"knobs\":[\"hd_mix\",\"hd_time\",\"hd_heads\",\"hd_spacing\",\"hd_fdbk\",\"hd_reverse\",\"hd_wear\",\"hd_wear_amt\"],"
            "\"params\":[\"hd_mix\",\"hd_time\",\"hd_heads\",\"hd_spacing\",\"hd_fdbk\",\"hd_reverse\",\"hd_wear\",\"hd_wear_amt\",\"hd_mode\",\"hd_route\",\"hd_freeze\"]},"
        "\"fx\":{\"name\":\"Effects\",\"children\":null,"
            "\"knobs\":[\"eq_low\",\"eq_mid\",\"eq_freq\",\"eq_high\",\"dly_time\",\"dly_fdbk\",\"dly_mix\",\"rev_mix\"],"
            "\"params\":[\"eq_low\",\"eq_mid\",\"eq_freq\",\"eq_high\",\"dly_time\",\"dly_sync\",\"dly_fdbk\",\"dly_tone\",\"dly_mix\",\"dly_ping\",\"rev_decay\",\"rev_damp\",\"rev_mix\"]},"
        "\"chords\":{\"name\":\"Morph Chords\",\"children\":null,"
            "\"knobs\":[\"mcr1\",\"mcr2\",\"mcr3\"],"
            "\"params\":[\"mcr1\",\"mcr2\",\"mcr3\"]}"
        "}}";
    int len = (int)strlen(hh);
    if (len >= buf_len) return -1;
    memcpy(buf, hh, (size_t)len + 1);
    return len;
}

/* flat JSON polled by the Solo UI */
static int get_ui_state(hm_t *h, char *buf, int buf_len)
{
    const reel_t *r = h->reel;
    jb_t j = { buf, buf_len, 0, 0 };
    int sel = clampi(h->sel_splice, 0, h->n_splices > 0 ? h->n_splices - 1 : 0);
    uint32_t lo = h->n_splices > 0 ? h->bounds[sel] : 0, hi = h->n_splices > 0 ? h->bounds[sel + 1] : 0;
    int mm = 1;
    float m = h->p[P_MORPH];
    if (m < 0.18f) mm = 0; else if (m < 0.30f) mm = 1; else mm = (int)ceilf(morph_neff(m));
    jb_add(&j, "{\"fr\":%u,\"cap\":%u,\"ns\":%d,\"sel\":%d,\"nmk\":%d,\"lo\":%u,\"hi\":%u,\"rec\":%d,\"arm\":%d,\"stp\":%d,\"rpos\":%u,"
               "\"spd\":%.3f,\"mm\":%d,\"act\":%d,\"ck\":%u,\"cr\":%d,\"run\":%d,\"in\":%.3f,\"out\":%.3f,\"sv\":%d,\"ls\":%d,"
               "\"wv\":%u,\"mv\":%u,\"sr\":%d,\"up\":%d,\"tr\":%d,\"pg\":%u",
           r ? r->frames : 0, r ? r->cap : 0, h->n_splices, sel, h->n_mk, lo, hi, h->rec_mode, h->rec_armed, h->rec_stop_armed,
           h->rec_mode == REC_NEW ? h->rec_pos : (h->rec_mode == REC_CUR ? h->rec_lo + (h->rec_pos % (h->rec_len ? h->rec_len : 1)) : 0),
           (double)h->speed, mm, clock_action(h), h->clk_idx, h->clk_running, h->spawn_on, (double)h->in_peak, (double)h->out_peak,
           (int)atomic_load(&h->save_state), (int)atomic_load(&h->load_state), h->wave_ver, h->mk_ver, (int)h->sr, h->undo_n,
           h->tick_len > 1.0 ? (int)(h->tick_len) : 0, (unsigned)atomic_load_explicit(&h->state_gen, memory_order_relaxed));
    {
        double lo_s, sp_s;
        gene_metrics(h, &lo_s, &sp_s);
        double dur = lo_s / (double)h->sr, dens = sp_s > 0.0 ? (double)h->sr / sp_s : 0.0;
        jb_add(&j, ",\"gms\":%.2f,\"dens\":%.2f,\"ff\":%.3f,\"gsp\":%u,\"gsk\":%u",
               dur * 1000.0, dens, dur * dens, h->genes_spawned, h->genes_skipped);
        jb_add(&j, ",\"cpu\":%.3f,\"cpk\":%.3f,\"ovr\":%u", (double)h->load_avg, (double)h->load_pk, h->load_over);
    }
    jb_add(&j, ",\"gp\":[");
    int first = 1;
    for (int i = 0; i < HM_MAX_GENES; i++) {
        const gene_t *g = &h->genes[i];
        if (!g->active) continue;
        double rel = g->pos - (double)g->lo, L = (double)g->len;
        rel -= L * floor(rel / L);
        jb_add(&j, "%s%u", first ? "" : ",", (uint32_t)((double)g->lo + rel));
        first = 0;
    }
    jb_add(&j, "]}");
    return j.ovf ? -1 : j.len;
}

static int get_view_peaks(hm_t *h, char *buf, int buf_len)
{
    const reel_t *r = h->reel;
    if (buf_len < HM_VIEW_COLS * 4 + 1) return -1;
    uint32_t a = h->view_a, b = h->view_b;
    if (!r || r->frames < 2) {                                   /* flat line for an empty reel */
        for (int c = 0; c < HM_VIEW_COLS; c++) memcpy(buf + c * 4, "8080", 4);
        buf[HM_VIEW_COLS * 4] = 0;
        return HM_VIEW_COLS * 4;
    }
    if (b <= a || b > r->frames) { a = 0; b = r->frames; }
    int p = 0;
    for (int c = 0; c < HM_VIEW_COLS; c++) {
        uint32_t ca = a + (uint32_t)(((uint64_t)(b - a) * (uint64_t)c) / HM_VIEW_COLS);
        uint32_t cb = a + (uint32_t)(((uint64_t)(b - a) * (uint64_t)(c + 1)) / HM_VIEW_COLS);
        if (cb <= ca) cb = ca + 1;
        float mx = 0.0f, mn = 0.0f;
        if (cb - ca <= 2 * HM_PK_BLOCK) {
            for (uint32_t i = ca; i < cb && i < r->frames; i++) {
                float v = 0.5f * (r->l[i] + r->r[i]);
                if (v > mx) mx = v;
                if (v < mn) mn = v;
            }
        } else {
            for (uint32_t k = ca / HM_PK_BLOCK; k <= (cb - 1) / HM_PK_BLOCK && k < r->pk_n; k++) {
                float vx = (float)r->pk_max[k] * (1.0f / 127.0f), vn = (float)r->pk_min[k] * (1.0f / 127.0f);
                if (vx > mx) mx = vx;
                if (vn < mn) mn = vn;
            }
        }
        int ix = (int)lrintf(clampf(mx, 0.0f, 1.0f) * 127.0f) + 128, in = (int)lrintf(clampf(mn, -1.0f, 0.0f) * 127.0f) + 128;
        p += snprintf(buf + p, 5, "%02x%02x", ix, in);
    }
    return p;
}

static int get_markers_csv(hm_t *h, char *buf, int buf_len)
{
    jb_t j = { buf, buf_len, 0, 0 };
    for (int i = 0; i < h->n_mk; i++) jb_add(&j, "%s%u", i ? "," : "", h->mk[i]);
    if (h->n_mk == 0 && buf_len > 0) buf[0] = 0;
    return j.ovf ? -1 : j.len;
}

#if HM_SOLO
/* ------------------------------------------------- browser editor (Tool tab)
 * schwung-manager shows web_ui.html in its Remote UI "Tool" tab for an overtake
 * module that answers "module_id". It reads "state" as a FLAT JSON object (every
 * value a number or a string; arrays are dropped) and pushes it again whenever
 * "rui_poll" changes. The parameters come first, exactly as in the saved
 * settings; the live view follows under "rev" and keys starting with v_. */

static const char *rui_peaks(hm_t *h)
{
    const reel_t *r = h->reel;
    const uint32_t fr = r ? r->frames : 0;
    if (h->rui_pk[0] && h->rui_pk_ver == h->wave_ver && h->rui_pk_frames == fr) return h->rui_pk;
    h->rui_pk_ver = h->wave_ver;
    h->rui_pk_frames = fr;
    for (int c = 0; c < HM_RUI_COLS; c++) {
        int mx = 0, mn = 0;
        if (fr >= 2) {
            const uint32_t a = (uint32_t)((uint64_t)fr * (uint64_t)c / HM_RUI_COLS) / HM_PK_BLOCK;
            uint32_t b = (uint32_t)((uint64_t)fr * (uint64_t)(c + 1) / HM_RUI_COLS) / HM_PK_BLOCK;
            if (b < a) b = a;
            for (uint32_t k = a; k <= b && k < r->pk_n; k++) {
                if (r->pk_max[k] > mx) mx = r->pk_max[k];
                if (r->pk_min[k] < mn) mn = r->pk_min[k];
            }
        }
        static const char hx[] = "0123456789abcdef";
        const int ix = mx + 128, in = mn + 128;
        char *o = h->rui_pk + c * 4;
        o[0] = hx[(ix >> 4) & 15]; o[1] = hx[ix & 15]; o[2] = hx[(in >> 4) & 15]; o[3] = hx[in & 15];
    }
    h->rui_pk[HM_RUI_COLS * 4] = 0;
    return h->rui_pk;
}

static uint32_t rui_rev(const hm_t *h)
{
    return (unsigned)atomic_load_explicit(&h->state_gen, memory_order_relaxed) + h->rui_viz + h->wave_ver + h->mk_ver;
}

static int get_remote_state(hm_t *h, char *buf, int buf_len)
{
    build_state_json(h, buf, buf_len);
    int n = (int)strlen(buf);
    if (n < 2 || buf[n - 1] != '}') return n;
    jb_t j = { buf, buf_len, n - 1, 0 };                         /* reopen the object */
    const reel_t *r = h->reel;
    const int sel = clampi(h->sel_splice, 0, h->n_splices > 0 ? h->n_splices - 1 : 0);
    double lo_s, sp_s;
    gene_metrics(h, &lo_s, &sp_s);
    const double dur = lo_s / (double)h->sr, dens = sp_s > 0.0 ? (double)h->sr / sp_s : 0.0;
    jb_add(&j, ",\"rev\":%u,\"v_fr\":%u,\"v_sr\":%d,\"v_ns\":%d,\"v_sel\":%d,\"v_run\":%d,\"v_rec\":%d,\"v_arm\":%d,"
               "\"v_rpos\":%u,\"v_spd\":%.4f,\"v_in\":%.3f,\"v_out\":%.3f,\"v_gms\":%.2f,\"v_dens\":%.2f,\"v_ff\":%.3f,"
               "\"v_clk\":%d,\"v_bpm\":%.1f,\"v_load\":%d,\"v_save\":%d,\"v_undo\":%d,\"v_wv\":%u",
           rui_rev(h), r ? r->frames : 0, (int)h->sr, h->n_splices, sel, h->spawn_on, h->rec_mode, h->rec_armed,
           h->rec_mode == REC_NEW ? h->rec_pos : (h->rec_mode == REC_CUR ? h->rec_lo + (h->rec_pos % (h->rec_len ? h->rec_len : 1)) : 0),
           (double)h->speed, (double)h->in_peak, (double)h->out_peak, dur * 1000.0, dens, dur * dens,
           h->clk_running, (double)fx_bpm(h), (int)atomic_load(&h->load_state), (int)atomic_load(&h->save_state), h->undo_n, h->wave_ver);
    jb_add(&j, ",\"v_mk\":\"");
    for (int i = 0; i < h->n_mk; i++) jb_add(&j, "%s%u", i ? "," : "", h->mk[i]);
    jb_add(&j, "\",\"v_gp\":\"");
    int first = 1;
    for (int i = 0; i < HM_MAX_GENES; i++) {
        const gene_t *g = &h->genes[i];
        if (!g->active) continue;
        double rel = g->pos - (double)g->lo, L = (double)g->len;
        rel -= L * floor(rel / L);
        jb_add(&j, "%s%u", first ? "" : ",", (uint32_t)((double)g->lo + rel));
        first = 0;
    }
    jb_add(&j, "\",\"v_pk\":\"%s\"}", rui_peaks(h));
    return j.ovf ? -1 : j.len;
}
#endif

static int v2_get_param(void *instance, const char *key, char *buf, int buf_len)
{
    hm_t *h = (hm_t *)instance;
    if (!h || !key || !buf || buf_len <= 0) return -1;

    if (strcmp(key, "name") == 0)         return snprintf(buf, (size_t)buf_len, "Heimat");
    if (strcmp(key, "ui_hierarchy") == 0) return get_ui_hierarchy(buf, buf_len);
    if (strcmp(key, "chain_params") == 0) return get_chain_params(buf, buf_len);
    if (strcmp(key, "ui_state") == 0)     return get_ui_state(h, buf, buf_len);
    if (strcmp(key, "view_peaks") == 0)   return get_view_peaks(h, buf, buf_len);
    if (strcmp(key, "markers") == 0)      return get_markers_csv(h, buf, buf_len);
    if (strcmp(key, "sample_path") == 0)  return snprintf(buf, (size_t)buf_len, "%s", h->cur_path);
    if (strcmp(key, "save_info") == 0) {
        int st = atomic_load(&h->save_state);
        return snprintf(buf, (size_t)buf_len, "%d|%s", st, st == 3 ? h->save_err : h->save_path);
    }
    if (strcmp(key, "reel_info") == 0) {
        const reel_t *r = h->reel;
        int st = atomic_load(&h->load_state);
        if (!r || r->frames < 2) return snprintf(buf, (size_t)buf_len, "state=%d frames=0", st);
        return snprintf(buf, (size_t)buf_len, "state=%d frames=%u splices=%d sr=%d markers=%d truncated=%d resampled=%d",
                        st, r->frames, h->n_splices, (int)h->sr, h->n_mk, r->truncated, r->resampled);
    }
#if HM_SOLO
    if (strcmp(key, "module_id") == 0)    return snprintf(buf, (size_t)buf_len, "heimat-solo");
    if (strcmp(key, "rui_poll") == 0)
        return snprintf(buf, (size_t)buf_len, "%u:%d:%u:%d", rui_rev(h), h->clk_running, h->clk_running ? h->clk_idx : 0u, (int)lrintf(fx_bpm(h)));
    if (strcmp(key, "state") == 0)        return get_remote_state(h, buf, buf_len);
#endif
    if (strcmp(key, "state") == 0) {
        build_state_json(h, buf, buf_len);
        return (int)strlen(buf);
    }
    if (strcmp(key, "dsp_load") == 0)                             /* "average peak over-budget", for the beta */
        return snprintf(buf, (size_t)buf_len, "%.3f %.3f %u", (double)h->load_avg, (double)h->load_pk, h->load_over);
    int i = param_index(key);
    if (i < 0) return -1;
    if (g_params[i].type == T_FLOAT) return snprintf(buf, (size_t)buf_len, "%.4f", h->p[i]);
    return snprintf(buf, (size_t)buf_len, "%d", (int)h->p[i]);
}

static int v2_get_error(void *instance, char *buf, int buf_len)
{
    hm_t *h = (hm_t *)instance;
    if (!h || !buf || buf_len <= 0) return 0;
    if (atomic_load(&h->load_state) != 3) return 0;
    return snprintf(buf, (size_t)buf_len, "%s", h->err);
}

/* The load meter for the beta: how much of each block's real time Heimat
 * itself spends (not the rest of the Move). Average over about a second,
 * peak that decays over a few seconds, and a count of blocks over budget. */
static void v2_render_block(void *instance, int16_t *out, int frames)
{
    hm_t *h = (hm_t *)instance;
    if (!h) { memset(out, 0, (size_t)frames * 2 * sizeof(int16_t)); return; }
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    hm_render(h, out, frames);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (frames > 0 && h->sr > 0) {
        const double spent = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;
        const float frac = (float)(spent * (double)h->sr / (double)frames);
        h->load_avg += (frac - h->load_avg) * 0.003f;
        h->load_pk = frac > h->load_pk ? frac : h->load_pk * 0.9995f;
        if (frac > 1.0f) h->load_over++;
    }
}

static plugin_api_v2_t g_api = {
    .api_version      = 2,
    .create_instance  = v2_create_instance,
    .destroy_instance = v2_destroy_instance,
    .on_midi          = v2_on_midi,
    .set_param        = v2_set_param,
    .get_param        = v2_get_param,
    .get_error        = v2_get_error,
    .render_block     = v2_render_block,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
