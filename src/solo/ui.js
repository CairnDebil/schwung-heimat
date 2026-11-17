/*
 * Heimat Solo 0.2 - tape and microsound instrument, takeover module for Move.
 *
 * MIT License
 *
 * The screen is laid out like a hardware panel: a splice window, two
 * direction windows with the Morph state between them, and one cell per
 * encoder. The 16 step LEDs show the splices in colour. All sound, recording, marker
 * and file work happens in dsp.so; this file draws, reads the hardware and
 * talks to the DSP with host_module_set_param / get_param / send_midi.
 *
 * Page buttons (Track 1-4):  PLAY, REEL, REC/CLOCK, MICRO -> SETUP -> SETUP 2
 */

import * as moveConstants from '/data/UserData/schwung/shared/constants.mjs';
import * as moveInputFilter from '/data/UserData/schwung/shared/input_filter.mjs';
import * as os from 'os';

function constant(name, fallback) {
    const v = moveConstants[name];
    return Number.isFinite(v) ? v : fallback;
}

/* ---------------------------------------------------------------- hardware */

const MoveMainButton = constant('MoveMainButton', 3);
const MoveMainKnob = constant('MoveMainKnob', 14);
const MoveShift = constant('MoveShift', 49);
const MoveMenu = constant('MoveMenu', 50);
const MoveBack = constant('MoveBack', 51);
const MoveCapture = constant('MoveCapture', 52);
const MoveDown = constant('MoveDown', 54);
const MoveUp = constant('MoveUp', 55);
const MoveUndo = constant('MoveUndo', 56);
const MoveLoop = constant('MoveLoop', 58);
const MoveCopy = constant('MoveCopy', 60);
const MoveLeft = constant('MoveLeft', 62);
const MoveRight = constant('MoveRight', 63);
const MovePlay = constant('MovePlay', 85);
const MoveRec = constant('MoveRec', 86);
const MoveMute = constant('MoveMute', 88);
const MoveSample = constant('MoveSample', 118);
const MoveDelete = constant('MoveDelete', 119);
const MoveKnob1 = constant('MoveKnob1', 71);
const ROWS = [constant('MoveRow1', 43), constant('MoveRow2', 42), constant('MoveRow3', 41), constant('MoveRow4', 40)];
const PAD0 = 68, PAD_N = 32, STEP0 = 16, STEP_N = 16;

const COL = {
    off: 0, white: constant('White', 120), dim: constant('DarkGrey', 119), grey: constant('LightGrey', 118),
    red: constant('BrightRed', 1), orange: constant('BrightOrange', 3), yellow: constant('BrightYellow', 8),
    lime: constant('BrightLime', 9), green: constant('NeonGreen', 11), teal: constant('TealGreen', 12),
    azure: constant('AzureBlue', 16), sky: constant('SkyBlue', 47), violet: constant('Violet', 19),
    magenta: constant('HotMagenta', 21), peach: constant('LightOrange', 37), pink: constant('BrightPink', 25),
};
/* splice colours, cycling */
const SPLICE_COLORS = [COL.green, COL.azure, COL.orange, COL.violet, COL.yellow, COL.teal, COL.magenta, COL.red];

const W = 128, H = 64;
const SAMPLES_ROOT = '/data/UserData/UserLibrary/Samples';

/* ------------------------------------------------------------- parameters */

const PD = {
    gene_size:   { t: 'f', min: 0, max: 1, lab: 'GENE', name: 'Gene-Size' },
    slide:       { t: 'f', min: 0, max: 1, lab: 'SLDE', name: 'Slide' },
    morph:       { t: 'f', min: 0, max: 1, lab: 'MRP', name: 'Morph' },
    vari_speed:  { t: 'f', min: -1, max: 1, lab: 'SPD', name: 'Vari-Speed' },
    organize:    { t: 'f', min: 0, max: 1, lab: 'ORG', name: 'Organize' },
    volume:      { t: 'f', min: 0, max: 1, lab: 'VOL', name: 'Volume' },
    sos:         { t: 'f', min: 0, max: 1, lab: 'SOS', name: 'Sound On Sound' },
    in_gain:     { t: 'e', opts: ['-3dB', '0dB', '+6dB', '+12dB'], lab: 'GAIN', name: 'Input Gain' },
    rec_source:  { t: 'e', opts: ['MIX', 'INPUT'], lab: 'SRC', name: 'Record Source' },
    rec_sync:    { t: 'e', opts: ['OFF', 'ON'], lab: 'SYNC', name: 'Record Sync' },
    clock_source:{ t: 'e', opts: ['OFF', 'INTERNAL', 'MOVE', 'MIDI'], lab: 'CLK', name: 'Clock' },
    clock_div:   { t: 'e', opts: ['1/16', '1/8', '1/4', '1/2', '1 BAR'], lab: 'DIV', name: 'Clock Division' },
    bpm:         { t: 'i', min: 20, max: 300, lab: 'BPM', name: 'Internal BPM' },
    clock_mode:  { t: 'e', opts: ['AUTO', 'GENE SHIFT', 'TIME STRETCH'], lab: 'ACT', name: 'Clock Action' },
    pad_mode:    { t: 'e', opts: ['PITCH', 'SPLICE', 'DRONE'], lab: 'PADS', name: 'Pad Mode' },
    play_mode:   { t: 'e', opts: ['CLASSIC', 'MOMENTARY', 'TRIG LOOP'], lab: 'PLAY', name: 'Play Gate' },
    root_note:   { t: 'i', min: 0, max: 127, lab: 'ROOT', name: 'Root Note' },
    octave_transpose: { t: 'i', min: -3, max: 3, lab: 'OCT', name: 'Octave' },
    gene_window: { t: 'e', opts: ['EDGE', 'TUKEY', 'GAUSS', 'TRIANGLE', 'EXPODEC', 'REXPODEC'], lab: 'ENV', name: 'Gene Envelope' },
    scatter:     { t: 'f', min: 0, max: 1, lab: 'SCAT', name: 'Scatter' },
    spread:      { t: 'f', min: 0, max: 12, lab: 'SPRD', name: 'Pitch Spread', step: 0.05 },
    width:       { t: 'f', min: 0, max: 1, lab: 'WIDE', name: 'Stereo Width' },
    threshold:   { t: 'f', min: 0, max: 1, lab: 'THR', name: 'Threshold' },
    thr_mode:    { t: 'e', opts: ['OFF', 'LOUD', 'QUIET'], lab: 'SEL', name: 'Selective' },
    splice_base: { t: 'i', min: 0, max: 120, lab: 'BASE', name: 'Splice Pad Base' },
    organize_mode: { t: 'e', opts: ['END OF GENE', 'IMMEDIATE'], lab: 'ORGM', name: 'Organize Change' },
    bend_range:  { t: 'i', min: 0, max: 12, lab: 'BEND', name: 'Bend Range' },
    release_ms:  { t: 'f', min: 1, max: 2000, lab: 'REL', name: 'Release ms', step: 5 },
    splice_source: { t: 'e', opts: ['MARKERS', 'DIVIDE'], lab: 'SPL', name: 'Splice Source' },
    divide:      { t: 'i', min: 1, max: 32, lab: 'DIV', name: 'Divide Into' },
    mcr1:        { t: 'f', min: -16, max: 16, lab: 'CH2', name: 'Chord 2nd Gene', step: 0.05 },
    mcr2:        { t: 'f', min: -16, max: 16, lab: 'CH3', name: 'Chord 3rd Gene', step: 0.05 },
    mcr3:        { t: 'f', min: -16, max: 16, lab: 'CH4', name: 'Chord 4th Gene', step: 0.05 },
    save_48k:    { t: 'e', opts: ['OFF', 'ON'], lab: '48K', name: 'Save at 48 kHz' },
    mute:        { t: 'e', opts: ['OFF', 'ON'], lab: 'MUTE', name: 'Mute' },
    eq_low:      { t: 'f', min: -15, max: 15, lab: 'LOW', name: 'EQ Low', step: 0.25 },
    eq_mid:      { t: 'f', min: -15, max: 15, lab: 'MID', name: 'EQ Mid', step: 0.25 },
    eq_freq:     { t: 'f', min: 100, max: 8000, lab: 'FREQ', name: 'EQ Mid Freq', log: true },
    eq_high:     { t: 'f', min: -15, max: 15, lab: 'HIGH', name: 'EQ High', step: 0.25 },
    dly_time:    { t: 'f', min: 10, max: 2000, lab: 'TIME', name: 'Delay Time', log: true },
    dly_sync:    { t: 'e', opts: ['FREE', '1/16', '1/8T', '1/8', '1/8.', '1/4', '1/4.', '1/2'], lab: 'SYNC', name: 'Delay Sync' },
    dly_fdbk:    { t: 'f', min: 0, max: 0.95, lab: 'FDBK', name: 'Delay Feedback' },
    dly_tone:    { t: 'f', min: 0, max: 1, lab: 'TONE', name: 'Delay Tone' },
    dly_mix:     { t: 'f', min: 0, max: 1, lab: 'DLY', name: 'Delay Mix' },
    dly_ping:    { t: 'e', opts: ['OFF', 'ON'], lab: 'PING', name: 'Ping-Pong' },
    rev_decay:   { t: 'f', min: 0, max: 1, lab: 'DCAY', name: 'Reverb Decay' },
    rev_damp:    { t: 'f', min: 0, max: 1, lab: 'DAMP', name: 'Reverb Damping' },
    rev_mix:     { t: 'f', min: 0, max: 1, lab: 'REV', name: 'Reverb Mix' },
    fx_rec:      { t: 'e', opts: ['OFF', 'ON'], lab: 'RFX', name: 'Record Through FX' },
    hd_mix:      { t: 'f', min: 0, max: 1, lab: 'MIX', name: 'Heads Mix' },
    hd_time:     { t: 'e', opts: ['2BAR', '1BAR', '1/2.', '1/2', '1/4.', '1/4', '1/8.', '1/8', '1/8T', '1/16', '16T', '1/32', '1/64', '128'], lab: 'TIME', name: 'Heads Time' },
    hd_heads:    { t: 'i', min: 1, max: 4, lab: 'HEAD', name: 'Heads' },
    hd_spacing:  { t: 'f', min: 0, max: 1, lab: 'SPAC', name: 'Head Spacing' },
    hd_fdbk:     { t: 'f', min: 0, max: 1, lab: 'FDBK', name: 'Heads Feedback' },
    hd_reverse:  { t: 'f', min: 0, max: 1, lab: 'REV', name: 'Reverse Heads' },
    hd_wear:     { t: 'e', opts: ['DARK', 'THIN', 'CRSH', 'WARM', 'FOLD', 'FUZZ'], lab: 'WEAR', name: 'Wear' },
    hd_wear_amt: { t: 'f', min: 0, max: 1, lab: 'WAMT', name: 'Wear Amount' },
    hd_mode:     { t: 'e', opts: ['FADE', 'GLID', 'RISE', 'FALL'], lab: 'MODE', name: 'Heads Mode' },
    hd_route:    { t: 'e', opts: ['OWN', 'CROS', 'CHN', 'WNDR'], lab: 'ROUT', name: 'Heads Routing' },
    hd_freeze:   { t: 'e', opts: ['OFF', 'ON'], lab: 'FRZ', name: 'Freeze Heads' },
    spread_mode: { t: 'e', opts: ['FREE', 'SEMI', 'OCT', '5TH', 'MAJ', 'MIN'], lab: 'INTV', name: 'Spread Intervals' },
    gene_reverse: { t: 'f', min: 0, max: 1, lab: 'GREV', name: 'Gene Reverse' },
};
const DEFAULTS = {
    gene_size: 0, slide: 0, morph: 0.18, vari_speed: 0.5, organize: 0, volume: 0.8, sos: 1, in_gain: 1, rec_source: 0,
    rec_sync: 0, clock_source: 0, clock_div: 2, bpm: 120, clock_mode: 0, pad_mode: 0, play_mode: 1, root_note: 60,
    octave_transpose: 0, gene_window: 0, scatter: 0, spread: 0, width: 0, threshold: 0.5, thr_mode: 0, splice_base: 36, organize_mode: 0, bend_range: 2, release_ms: 15, splice_source: 0, divide: 8,
    mcr1: 2, mcr2: 3, mcr3: 4, save_48k: 0, mute: 0,
    eq_low: 0, eq_mid: 0, eq_freq: 1000, eq_high: 0, dly_time: 375, dly_sync: 0, dly_fdbk: 0.4, dly_tone: 0.6, dly_mix: 0,
    dly_ping: 0, rev_decay: 0.5, rev_damp: 0.5, rev_mix: 0, fx_rec: 0,
    hd_mix: 0, hd_time: 7, hd_heads: 2, hd_spacing: 0.3, hd_fdbk: 0.45, hd_reverse: 0, hd_wear: 0, hd_wear_amt: 0, hd_mode: 0, hd_route: 0, hd_freeze: 0,
    spread_mode: 0, gene_reverse: 0,
};

/* the eight cells of each page = the eight encoders */
const PAGES = {
    play:  ['gene_size', 'slide', 'morph', 'vari_speed', 'organize', 'sos', 'volume', 'clock_div'],
    reel:  ['@splice', '@start', '@end', '@zoom', '@sens', '@divide', 'gene_size', 'slide'],
    rec:   ['sos', 'in_gain', 'rec_source', 'rec_sync', 'clock_source', 'bpm', 'clock_div', 'clock_mode'],
    setup: [
        ['gene_window', 'scatter', 'spread', 'width', 'threshold', 'thr_mode', 'spread_mode', 'gene_reverse'],
        ['eq_low', 'eq_mid', 'eq_freq', 'eq_high', 'dly_time', 'dly_fdbk', 'dly_mix', 'rev_mix'],
        ['dly_sync', 'dly_tone', 'dly_ping', 'dly_fdbk', 'rev_decay', 'rev_damp', 'rev_mix', 'fx_rec'],
        ['hd_time', 'hd_heads', 'hd_spacing', 'hd_fdbk', 'hd_reverse', 'hd_wear_amt', 'hd_mix', 'hd_freeze'],
        ['hd_wear', 'hd_mode', 'hd_route', 'hd_time', 'hd_fdbk', 'hd_wear_amt', 'hd_mix', 'hd_freeze'],
        ['pad_mode', 'play_mode', 'root_note', 'octave_transpose', 'splice_base', 'organize_mode', 'bend_range', 'release_ms'],
        ['splice_source', 'divide', 'mcr1', 'mcr2', 'mcr3', 'save_48k', 'mute', 'volume'],
    ],
};
const VIRT = {
    '@splice': { lab: 'SPLC', name: 'Splice' }, '@start': { lab: 'STRT', name: 'Splice Start' },
    '@end': { lab: 'END', name: 'Splice End' }, '@zoom': { lab: 'ZOOM', name: 'Zoom' },
    '@sens': { lab: 'SENS', name: 'Detect Sensitivity' }, '@divide': { lab: 'DIV', name: 'Divide Equal' },
};
const PAGE_NAMES = { play: 'PLAY', reel: 'REEL', rec: 'REC', setup: 'SETUP' };
/* Track 4 cycles MICRO, FX, FX 2, HEADS, HEADS 2, SETUP 1, SETUP 2 */
const SETUP_NAMES = ['MICRO', 'FX', 'FX 2', 'HEADS', 'HEADS 2', 'SETUP', 'SETUP'];
const FX_PAGE = 1, HEADS_PAGE = 3, SETUP_FIRST = 5;
function pageName() { return page === 'setup' ? SETUP_NAMES[setupPage] : PAGE_NAMES[page]; }
function pageKeys() { return page === 'setup' ? PAGES.setup[setupPage] : PAGES[page]; }
const PAGE_ORDER = ['play', 'reel', 'rec', 'setup'];

/* ------------------------------------------------------------------ state */

const P = Object.assign({}, DEFAULTS);        /* mirror of the DSP parameters */
let S = {};                                   /* last ui_state from the DSP */
let markers = [];
let peaks = '';
let peaksKey = '';
let page = 'play', setupPage = 0;
let overlay = null;                           /* null | 'menu' | 'browser' | 'about' */
let menuCursor = 0;
let shiftHeld = false;
const touched = new Array(8).fill(false);
const focus = { idx: -1, until: 0 };           /* the encoder being turned or touched */
let message = { text: '', until: 0 };
let cursor = 0;                               /* edit cursor in the reel, frames */
let zoomLevel = 0;
let sens = 0.6;
let divN = 8;
let pendingDivide = null;
let browser = { path: SAMPLES_ROOT, items: [], cursor: 0 };
let heldPads = new Set();
let deleteDown = null;
let lastPoll = 0, lastDraw = 0, lastMv = -1, lastSv = 0, lastPg = -1, lastLocalEdit = 0;
let flashCapture = 0;
let ticks = 0;

/* --------------------------------------------------------------- DSP bridge */

function getParam(key) {
    try { const v = host_module_get_param(key); return v === undefined || v === null ? '' : String(v); } catch (e) { return ''; }
}
function setParam(key, value) {
    try { host_module_set_param(key, String(value)); } catch (e) { /* ignore */ }
}
function sendMidi(a, b, c) {
    try { host_module_send_midi([a, b, c], 'internal'); } catch (e) { /* ignore */ }
}
function setP(key, value) {
    const d = PD[key];
    if (!d) return;
    lastLocalEdit = Date.now();
    if (d.t === 'e') value = Math.max(0, Math.min(d.opts.length - 1, Math.round(value)));
    else if (d.t === 'i') value = Math.max(d.min, Math.min(d.max, Math.round(value)));
    else value = Math.max(d.min, Math.min(d.max, value));
    P[key] = value;
    setParam(key, d.t === 'f' ? value.toFixed(4) : String(value));
}
function say(text, ms = 1600) { message = { text, until: Date.now() + ms }; }

function readAllParams() {
    for (const key of Object.keys(PD)) {
        const raw = getParam(key);
        const v = parseFloat(raw);
        if (raw !== '' && Number.isFinite(v)) P[key] = v;
    }
}

function frames() { return S.fr || 0; }
function sr() { return S.sr || 44100; }
function fmtTime(f) {
    const s = f / sr();
    if (s >= 60) { const m = Math.floor(s / 60); return m + ':' + (s - m * 60).toFixed(1).padStart(4, '0'); }
    return s.toFixed(2) + 's';
}

function pollDsp(now) {
    const raw = getParam('ui_state');
    if (!raw) return;
    try { S = JSON.parse(raw); } catch (e) { return; }
    if (S.pg !== undefined && S.pg !== lastPg) {              /* the DSP changed parameters by itself (restored state, load) */
        lastPg = S.pg;
        if (now - lastLocalEdit > 600) readAllParams();
    }
    if (S.mv !== lastMv) {
        lastMv = S.mv;
        const csv = getParam('markers');
        markers = csv ? csv.split(',').map(Number).filter(Number.isFinite) : [];
    }
    if (S.sv !== lastSv) {
        const wasSaving = lastSv === 1;
        lastSv = S.sv;
        if (wasSaving) {
            const info = getParam('save_info');
            const bar = info.indexOf('|');
            if (info[0] === '2') say('SAVED ' + info.slice(bar + 1).split('/').pop(), 2600);
            else if (info[0] === '3') say('SAVE FAILED: ' + info.slice(bar + 1), 2600);
        }
    }
    refreshPeaks();
}

function viewRange() {
    if (page === 'reel' && frames() > 0) {
        const w = viewWidth();
        const a = Math.max(0, Math.min(frames() - w, Math.round(cursor - w / 2)));
        return [a, a + w];
    }
    return [0, 0];
}
function viewWidth() {
    const f = frames();
    return Math.max(Math.min(2048, f), Math.floor(f / Math.pow(2, zoomLevel)));
}
function refreshPeaks() {
    const [a, b] = viewRange();
    const key = a + ',' + b + ',' + (S.wv || 0);
    if (key === peaksKey) return;
    peaksKey = key;
    setParam('view', a + ',' + b);
    peaks = getParam('view_peaks');
}

/* ----------------------------------------------------------------- drawing */

function px(x, y, v = 1) { if (x >= 0 && y >= 0 && x < W && y < H) set_pixel(x | 0, y | 0, v); }
function hline(x0, x1, y, v = 1) { const a = Math.max(0, Math.min(x0, x1)), b = Math.min(W - 1, Math.max(x0, x1)); if (b >= a) fill_rect(a, y, b - a + 1, 1, v); }
function vline(x, y0, y1, v = 1) { const a = Math.max(0, Math.min(y0, y1)), b = Math.min(H - 1, Math.max(y0, y1)); if (x >= 0 && x < W && b >= a) fill_rect(x, a, 1, b - a + 1, v); }
function dotted(x, y0, y1) { for (let y = y0; y <= y1; y += 2) px(x, y); }
function txt(x, y, s, v = 1) { print(x, y, s, v); }
function tw(s) { return typeof text_width === 'function' ? text_width(s) : s.length * 6; }
function inverse(x, y, w, h, s, pad = 2) { fill_rect(x, y, w, h, 1); txt(x + pad, y + 1, s, 0); }
function circle(cx, cy, r, filled) {
    for (let a = 0; a < 24; a++) {
        const t = a / 24 * Math.PI * 2;
        px(Math.round(cx + Math.cos(t) * r), Math.round(cy + Math.sin(t) * r));
    }
    if (filled) for (let y = -r + 1; y < r; y++) hline(cx - r + 1, cx + r - 1, cy + y);
}
function meter(x, y, w, h, frac) {
    draw_rect(x, y, w, h, 1);
    const f = Math.max(0, Math.min(1, frac));
    if (f > 0) fill_rect(x + 1, y + 1, Math.max(1, Math.round((w - 2) * f)), h - 2, 1);
}

function fmtMs(ms) { return ms >= 1000 ? (ms / 1000).toFixed(2) + 's' : (ms >= 10 ? ms.toFixed(0) : ms.toFixed(1)) + 'ms'; }

function fmtParam(key) {
    if (key[0] === '@') return '';
    const d = PD[key], v = P[key];
    if (d.t === 'e') return d.opts[Math.max(0, Math.min(d.opts.length - 1, v | 0))];
    if (d.t === 'i') return String(v | 0);
    if (key === 'vari_speed') return (v >= 0 ? '+' : '') + v.toFixed(2);
    if (key === 'release_ms') return (v | 0) + 'ms';
    if (key === 'gene_size') return S.gms !== undefined ? fmtMs(S.gms) : v.toFixed(2);
    if (key === 'spread') return v.toFixed(1) + 'st';
    if (key === 'threshold') return Math.round(-60 + 60 * v) + 'dB';
    if (key === 'eq_low' || key === 'eq_mid' || key === 'eq_high') return (v > 0 ? '+' : '') + v.toFixed(1) + 'dB';
    if (key === 'eq_freq') return v >= 1000 ? (v / 1000).toFixed(v >= 5000 ? 1 : 2) + 'kHz' : Math.round(v) + 'Hz';
    if (key === 'dly_time') return (P.dly_sync | 0) > 0 ? PD.dly_sync.opts[P.dly_sync | 0] : fmtMs(v);
    if (key === 'rev_decay') return revDecaySec().toFixed(1) + 's';
    if (key === 'dly_fdbk' || key === 'dly_mix' || key === 'rev_mix' || key === 'dly_tone' || key === 'rev_damp') return Math.round(v * 100) + '%';
    if (key === 'gene_reverse') return Math.round(v * 100) + '%';
    if (key === 'hd_mix' || key === 'hd_spacing' || key === 'hd_fdbk' || key === 'hd_reverse' || key === 'hd_wear_amt') return Math.round(v * 100) + '%';
    return v.toFixed(2);
}
function revDecaySec() { return 0.3 * Math.pow(40, P.rev_decay); }     /* as in the DSP */
function norm(key) {
    const d = PD[key];
    if (!d) return 0;
    if (d.t === 'e') return d.opts.length > 1 ? P[key] / (d.opts.length - 1) : 0;
    if (d.log) return Math.log(Math.max(d.min, P[key]) / d.min) / Math.log(d.max / d.min);
    return (P[key] - d.min) / (d.max - d.min);
}

function morphGlyph() {
    const mm = S.mm | 0;
    return mm === 0 ? '-' : (mm === 1 ? '=' : String(Math.min(4, mm)));
}

/* header: page tab, reel / splice windows, state lamps */
function drawHeader() {
    inverse(0, 0, tw(pageName()) + 5, 9, pageName());
    const ns = S.ns || 0;
    const sp = ns > 0 ? 'S' + String((S.sel | 0) + 1).padStart(2, '0') + '/' + String(ns).padStart(2, '0') : 'S--/--';
    txt(tw(pageName()) + 9, 1, sp);
    /* the two Vari-Speed windows with the Morph state between them, as on the panel */
    const wx = tw(pageName()) + 9 + tw(sp) + 5, spd = S.spd || 0;
    draw_rect(wx, 1, 6, 7, 1); if (spd < -0.02) fill_rect(wx + 1, 2, 4, 5, 1);
    txt(wx + 8, 1, morphGlyph());
    draw_rect(wx + 15, 1, 6, 7, 1); if (spd > 0.02) fill_rect(wx + 16, 2, 4, 5, 1);
    let x = W - 2;
    const lamp = (s, on) => { x -= tw(s) + 3; if (on) { inverse(x - 1, 0, tw(s) + 3, 9, s, 1); } else txt(x, 1, s); };
    const blink = Math.floor(Date.now() / 250) % 2 === 0;
    if (S.rec) lamp('REC', blink || S.stp);
    else if (S.arm) lamp('REC', blink);
    if (S.sv === 1) lamp('SD', blink);
    if ((S.cr | 0) && (P.clock_source | 0)) lamp(S.act === 1 ? 'GS' : (S.act === 2 ? 'TS' : 'CK'), (S.ck | 0) % 2 === 1);
    if (P.mute) lamp('M', true);
    hline(0, W - 1, 9);
}

/* the waveform strip (full reel on PLAY, zoomed view on REEL) */
function drawWave(y0, y1, zoomed) {
    const cy = Math.round((y0 + y1) / 2), hh = Math.floor((y1 - y0) / 2);
    const f = frames();
    if (f < 2) {
        txt(14, cy - 7, 'EMPTY REEL', 1);
        txt(6, cy + 3, 'REC or MENU to load', 1);
        return;
    }
    const [va, vb] = zoomed ? viewRange() : [0, f];
    const span = Math.max(1, vb - va);
    const xOf = (fr) => Math.round((fr - va) / span * (W - 1));
    if (peaks.length >= W * 4) {
        for (let c = 0; c < W; c++) {
            const mx = parseInt(peaks.substr(c * 4, 2), 16) - 128, mn = parseInt(peaks.substr(c * 4 + 2, 2), 16) - 128;
            const top = cy - Math.round(mx / 127 * hh), bot = cy - Math.round(mn / 127 * hh);
            vline(c, top, Math.max(bot, top));
        }
    }
    /* splice markers; the selected splice gets a bar above and below, like the splice window lighting */
    const ns = S.ns | 0, sel = S.sel | 0;
    const bounds = [0].concat(markers, [f]);
    for (let i = 0; i < markers.length; i++) {
        const x = xOf(markers[i]);
        if (x >= 0 && x < W) dotted(x, y0, y1);
    }
    if (ns > 0 && sel < bounds.length - 1) {
        const a = Math.max(0, xOf(bounds[sel])), b = Math.min(W - 1, xOf(bounds[sel + 1]));
        if (b >= a) { hline(a, b, y0 - 1); hline(a, b, y1 + 1); }
    }
    /* playing genes */
    for (const g of (S.gp || [])) { const x = xOf(g); if (x >= 0 && x < W) { vline(x, y0, y1); px(x - 1, y1 + 2); px(x, y1 + 2); px(x + 1, y1 + 2); } }
    /* recording head */
    if (S.rec === 1 || S.rec === 2) { const x = xOf(S.rpos); if (x >= 0 && x < W) vline(x, y0, y1); }
}

/* Gene-Size / Slide bracket under the waveform of the selected splice */
function drawGeneBracket(y) {
    const f = frames(), ns = S.ns | 0;
    if (f < 2 || ns < 1 || P.gene_size < 0.005) return;
    const lo = S.lo || 0, len = Math.max(1, (S.hi || f) - lo);
    const tmax = len / sr(), tmin = 0.0005;
    const T = Math.min(tmax, tmax * Math.pow(tmin / tmax, (P.gene_size - 0.005) / 0.995));
    const span = T * sr();
    const start = lo + (P.slide * len) % len;
    const xa = Math.round(start / f * (W - 1));
    const xb = Math.round(Math.min(lo + len, start + span) / f * (W - 1));
    hline(xa, Math.max(xa + 1, xb), y);
    vline(xa, y - 1, y + 1);
    vline(Math.max(xa + 1, xb), y - 1, y + 1);
}

function cellValueGlyph(key) {
    if (key === 'vari_speed') {
        const v = S.spd || 0;
        return Math.abs(v) < 0.02 ? '|' : (v < 0 ? '<' : '>');
    }
    if (key === 'morph') return morphGlyph();
    return '';
}

/* two rows of four cells (y 41-63): label, state glyph, value bar - one per encoder */
const CELL_Y = 41, CELL_H = 11;
function stripBusy() {
    return (deleteDown && deleteDown.armed) || focusVisible() || (!!message.text && Date.now() <= message.until);
}
function drawCells() {
    if (stripBusy()) return;                       /* the strip below is showing a readout or a message */
    const keys = pageKeys();
    for (let i = 0; i < 8; i++) {
        const key = keys[i], col = i % 4, row = Math.floor(i / 4);
        const x = col * 32, y = CELL_Y + row * (CELL_H + 1);
        const virt = key[0] === '@';
        const lab = virt ? VIRT[key].lab : PD[key].lab;
        const hot = touched[i];
        if (hot) fill_rect(x, y, 31, CELL_H, 1);
        const ink = hot ? 0 : 1;
        txt(x + 2, y + 1, lab, ink);
        const g = virt ? '' : cellValueGlyph(key);
        if (g) txt(x + 24, y + 1, g, ink);
        const frac = virt ? virtualFrac(key) : norm(key);
        const by = y + CELL_H - 1;
        hline(x + 2, x + 28, by, ink);
        const filled = Math.round(27 * Math.max(0, Math.min(1, frac)));
        if (filled > 0) fill_rect(x + 2, by - 1, filled, 2, ink);
        if (!virt && PD[key].min < 0) vline(x + 15, by - 2, by, ink);
    }
    vline(127, CELL_Y, H - 1, 0);
}
function virtualFrac(key) {
    if (key === '@splice') return (S.ns | 0) > 1 ? (S.sel | 0) / ((S.ns | 0) - 1) : 0;
    if (key === '@zoom') return zoomLevel / 7;
    if (key === '@sens') return sens;
    if (key === '@divide') return (divN - 1) / 31;
    if (key === '@start' || key === '@end') {
        const bounds = [0].concat(markers, [frames()]), sel = S.sel | 0;
        const f = Math.max(1, frames());
        return (key === '@start' ? bounds[sel] : bounds[sel + 1] || f) / f;
    }
    return 0;
}

/* The cell area doubles as a transient readout: while an encoder is touched or
 * turned it shows that parameter's full name, value and a wide bar, then the
 * cells come back. Messages and the Delete-hold bar use the same strip, so
 * nothing is ever drawn over the waveform. */
const FOCUS_TURN_MS = 700, FOCUS_TOUCH_MS = 900;
function focusVisible() { return !overlay && focus.idx >= 0 && (touched[focus.idx] || Date.now() < focus.until); }
function virtValue(key) {
    if (key === '@splice') return String((S.sel | 0) + 1) + '/' + String(S.ns | 0);
    if (key === '@zoom') return 'x' + Math.pow(2, zoomLevel);
    if (key === '@sens') return sens.toFixed(2);
    if (key === '@divide') return String(divN);
    if (key === '@start' || key === '@end') return fmtTime(Math.round(virtualFrac(key) * frames()));
    return '';
}
function clearStrip() {
    fill_rect(0, CELL_Y - 1, W, H - CELL_Y + 1, 0);
    hline(0, W - 1, CELL_Y - 1);
}
function drawFocus() {
    const keys = pageKeys(), i = focus.idx, key = keys[i];
    if (!key) return;
    const virt = key[0] === '@';
    const name = virt ? VIRT[key].name : PD[key].name;
    const val = virt ? virtValue(key) : fmtParam(key);
    clearStrip();
    const room = Math.floor((W - 6 - tw(val)) / tw('M'));
    txt(2, CELL_Y + 2, name.slice(0, Math.max(4, room)));
    txt(W - 2 - tw(val), CELL_Y + 2, val);
    const by = CELL_Y + 12, bw = W - 4;
    draw_rect(2, by, bw, 5, 1);
    const frac = Math.max(0, Math.min(1, virt ? virtualFrac(key) : norm(key)));
    if (!virt && PD[key].min < 0) {                              /* bipolar: fill from the centre */
        const c = 2 + Math.floor(bw / 2), x = 2 + Math.round((bw - 1) * frac);
        vline(c, by - 1, by + 5);
        if (x !== c) fill_rect(Math.min(c, x), by + 1, Math.abs(x - c) + 1, 3, 1);
    } else if (frac > 0) fill_rect(3, by + 1, Math.max(1, Math.round((bw - 2) * frac)), 3, 1);
    for (let j = 0; j < 8; j++) {                                /* which of the eight encoders */
        const x = 2 + j * 16;
        if (j === i) fill_rect(x, H - 3, 12, 2, 1); else hline(x + 4, x + 7, H - 2);
    }
}
function drawMessage() {
    if (Date.now() > message.until || !message.text || focusVisible()) return;
    const s = message.text.slice(0, 21);
    clearStrip();
    txt(Math.floor((W - tw(s)) / 2), CELL_Y + 7, s);
}
function drawDeleteHold() {
    const frac = Math.min(1, (Date.now() - deleteDown.t) / deleteDown.hold);
    clearStrip();
    const s = deleteDown.shift ? 'HOLD: CLEAR REEL' : 'HOLD: CLEAR MARKERS';
    txt(Math.floor((W - tw(s)) / 2), CELL_Y + 2, s);
    meter(4, CELL_Y + 13, W - 8, 5, frac);
}

function drawPlay() {
    drawWave(12, 35, false);
    drawGeneBracket(38);
    drawCells();
}

function drawReel() {
    drawWave(12, 30, true);
    const f = frames();
    if (f > 1) {                                   /* the view is centred on the edit cursor */
        const [va, vb] = viewRange();
        const x = Math.round((cursor - va) / Math.max(1, vb - va) * (W - 1));
        vline(x, 11, 31);
        px(x - 1, 11); px(x + 1, 11); px(x - 1, 31); px(x + 1, 31);
    }
    const bounds = [0].concat(markers, [f]), sel = S.sel | 0;
    const lo = bounds[sel] || 0, hi = bounds[sel + 1] || f;
    txt(1, 32, 'S' + (sel + 1) + ' ' + fmtTime(Math.max(0, hi - lo)));
    const c = '@' + fmtTime(cursor);
    txt(W - 1 - tw(c), 32, c);
    drawCells();
}

function drawRec() {
    const f = frames(), cap = S.cap || 1;
    let state = 'READY';
    if (S.rec === 1) state = S.stp ? 'NEW: STOP ON TICK' : 'REC NEW SPLICE';
    else if (S.rec === 2) state = S.stp ? 'TLA: STOP ON TICK' : 'REC OVER SPLICE';
    else if (S.arm) state = 'WAIT FOR CLOCK';
    const blink = Math.floor(Date.now() / 300) % 2 === 0;
    circle(4, 15, 3, !!S.rec && blink);
    txt(10, 12, state);
    const t = fmtTime(f), tc = fmtTime(cap);
    txt(1, 21, t);
    txt(W - 1 - tw(tc), 21, tc);
    meter(1, 30, 126, 3, f / cap);
    txt(1, 34, 'I');
    meter(8, 35, 54, 4, S.in || 0);
    txt(66, 34, 'O');
    meter(73, 35, 54, 4, S.out || 0);
    drawCells();
}

/* the gene envelope shapes, as in the DSP (gene_env) */
function envShape(shape, u) {
    const sm = (t) => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };
    const edge = sm(Math.min(u, 1 - u) / 0.04);
    switch (shape) {
    case 1: return sm(Math.min(u, 1 - u) / 0.5);
    case 2: { const d = (u - 0.5) / 0.18; return (Math.exp(-0.5 * d * d) - 0.0211) / 0.9789 * edge; }
    case 3: return 1 - Math.abs(2 * u - 1);
    case 4: return Math.exp(-4.6 * u) * edge;
    case 5: return Math.exp(-4.6 * (1 - u)) * edge;
    default: return edge;
    }
}

/* where the cloud sits, after the perceptual thresholds of granular textures */
function durationTag(ms) { return ms < 2 ? 'CLICK' : (ms < 40 ? 'GRAIN' : 'SOUND'); }
function fillTag(ff) { return ff < 0.5 ? 'SPARSE' : (ff <= 1.1 ? 'COVERED' : 'PACKED'); }
function densityTag(d) { return d < 15 ? 'RHYTHM' : (d < 25 ? 'FLUTTER' : (d < 50 ? 'FLOW' : (d < 100 ? 'TEXTURE' : 'MASS'))); }

function drawMicro() {
    /* the envelope of one gene, drawn in a small window */
    const bx = 1, by = 12, bw = 38, bh = 26;
    draw_rect(bx, by, bw, bh, 1);
    let prev = -1;
    for (let x = 0; x < bw - 4; x++) {
        const v = envShape(P.gene_window | 0, x / (bw - 5));
        const y = by + bh - 3 - Math.round(v * (bh - 6));
        if (prev >= 0) vline(bx + 2 + x, Math.min(prev, y), Math.max(prev, y)); else px(bx + 2 + x, y);
        prev = y;
    }
    const ms = S.gms || 0, d = S.dens || 0, ff = S.ff || 0;
    const tx = 43;
    txt(tx, 12, fmtMs(ms) + ' ' + durationTag(ms));
    txt(tx, 21, d.toFixed(d < 10 ? 1 : 0) + '/s ' + densityTag(d));
    txt(tx, 30, 'FF' + ff.toFixed(2) + ' ' + fillTag(ff));
    drawCells();
}

/* ------------------------------------------------------------- FX pages */

/* approximate response of the three EQ bands in dB, for the picture only */
function eqDb(f) {
    const lo = P.eq_low / (1 + Math.pow(f / 180, 2));
    const o = Math.log2(f / Math.max(20, P.eq_freq));
    const mid = P.eq_mid * Math.exp(-(o * o) / (2 * 0.7 * 0.7));
    const hi = P.eq_high / (1 + Math.pow(5000 / f, 2));
    return lo + mid + hi;
}
function delayMs() {
    const sync = P.dly_sync | 0;
    if (!sync) return P.dly_time;
    const beats = [0, 0.25, 1 / 3, 0.5, 0.75, 1, 1.5, 2][sync];
    return beats * 60000 / Math.max(20, P.bpm || 120);
}
function drawFx() {
    /* EQ curve, 30 Hz - 16 kHz on a log axis, +-15 dB */
    const x0 = 0, x1 = 62, yc = 25, ky = 12 / 15;
    for (let x = x0; x <= x1; x += 3) px(x, yc);
    let prev = null;
    for (let x = x0; x <= x1; x++) {
        const f = 30 * Math.pow(16000 / 30, (x - x0) / (x1 - x0));
        const y = Math.max(12, Math.min(38, Math.round(yc - eqDb(f) * ky)));
        if (prev === null) px(x, y); else vline(x, prev, y);
        prev = y;
    }
    const fx = Math.round(x0 + (x1 - x0) * Math.log(Math.max(30, P.eq_freq) / 30) / Math.log(16000 / 30));
    vline(fx, 37, 38);
    vline(64, 12, 38, 1);
    txt(68, 12, 'DLY ' + fmtParam('dly_time'));
    txt(68, 21, 'FB ' + fmtParam('dly_fdbk') + (P.dly_ping ? ' PP' : ''));
    txt(68, 30, 'REV ' + fmtParam('rev_decay'));
    drawCells();
}
function drawFx2() {
    /* the echoes as they fall off, and the reverb's decay */
    txt(1, 12, 'ECHO ' + Math.round(delayMs()) + 'ms');
    const base = 38, top = 22;
    for (let k = 0; k < 7; k++) {                                /* the sound, then echoes at feedback^n */
        const a = k === 0 ? 1 : Math.pow(P.dly_fdbk, k - 1);
        const x = 2 + k * 9, h = Math.round((base - top) * a);
        if (h <= 0) continue;
        if (P.dly_ping && k > 0 && k % 2 === 0) draw_rect(x, base - h + 1, 4, h, 1);   /* right-hand echo */
        else fill_rect(x, base - h + 1, 4, h, 1);
    }
    vline(64, 12, 38, 1);
    txt(68, 12, 'VERB ' + fmtParam('rev_decay'));
    const T = revDecaySec(), span = 6;
    let prev = null;
    for (let x = 68; x <= 126; x++) {
        const t = (x - 68) / 58 * span;
        const y = Math.round(base - (base - top) * Math.pow(10, -3 * t / T / 1.2));
        if (prev === null) vline(x, y, base); else vline(x, Math.min(prev, y), Math.max(prev, y));
        prev = y;
    }
    hline(68, 126, base);
    drawCells();
}

/* ----------------------------------------------------------- HEADS pages */
const HD_BEATS = [8, 4, 3, 2, 1.5, 1, 0.75, 0.5, 1 / 3, 0.25, 1 / 6, 0.125, 0.0625, 0.03125];
/* where each head reads, as in the DSP: [{ t (s), rev, c }] */
function headTimes() {
    const route = P.hd_route | 0, n = Math.max(1, Math.min(4, P.hd_heads | 0)), lines = route >= 2 ? 4 : n;
    const T = HD_BEATS[Math.max(0, Math.min(13, P.hd_time | 0))] * 60 / Math.max(20, P.bpm || 120), sp = P.hd_spacing;
    const rc = Math.round(P.hd_reverse * 2 * lines), out = [];
    for (let c = 0; c < 2; c++) for (let k = 0; k < n; k++) {
        let t = route >= 2 ? T * (1 + 0.25 * k * sp) : T * (1 + k * (0.07 + 0.93 * sp));
        if (c) t *= 1 + 0.02 * sp;
        out.push({ t: Math.min(t, 5.9), rev: 2 * k + c < rc, c });
    }
    return out;
}
function drawHeads() {
    /* the tape, left over right: a tick where each head reads; backwards heads as arrows */
    const hs = headTimes(), span = Math.max(...hs.map((x) => x.t)) * 1.12;
    const st = P.hd_freeze ? 'FROZEN' : (P.hd_mix > 0 ? PD.hd_route.opts[P.hd_route | 0] : 'OFF');
    txt(1, 12, 'TAPE');
    txt(W - 1 - tw(st), 12, st);
    const yl = 25, yr = 35;
    txt(1, yl - 4, 'L'); txt(1, yr - 4, 'R');
    hline(9, 126, yl); hline(9, 126, yr);
    fill_rect(9, yl - 3, 2, 7, 1); fill_rect(9, yr - 3, 2, 7, 1);           /* the sound comes in here */
    for (const x of hs) {
        const px = 9 + Math.round((x.t / span) * 115), y = x.c ? yr : yl;
        if (x.rev) { hline(px - 3, px, y - 3); hline(px - 3, px, y + 3); vline(px - 4, y - 2, y + 2); set_pixel(px, y - 2, 1); set_pixel(px, y + 2, 1); }
        else fill_rect(px - 1, y - 3, 3, 7, 1);
    }
    drawCells();
}
function drawHeads2() {
    /* how the heads feed each other, and what each pass does to the sound */
    const route = P.hd_route | 0;
    const bx = (k) => 6 + k * 15;
    for (let c = 0; c < 2; c++) for (let k = 0; k < 4; k++) {
        const y = c ? 27 : 13, on = k < (route >= 2 ? 4 : (P.hd_heads | 0));
        if (on) draw_rect(bx(k), y, 10, 8, 1); else set_pixel(bx(k) + 5, y + 4, 1);
        if (route >= 2 && k < 3) hline(bx(k) + 10, bx(k) + 15, y + 4);       /* in series */
    }
    if (route === 1 || route === 3) { vline(60, 17, 31); vline(61, 17, 31); }          /* crossing sides */
    txt(66, 12, PD.hd_wear.opts[P.hd_wear | 0] + ' ' + Math.round(P.hd_wear_amt * 100) + '%');
    txt(66, 21, PD.hd_mode.opts[P.hd_mode | 0]);
    txt(66, 30, PD.hd_route.opts[route] + (P.hd_freeze ? ' FRZ' : ''));
    drawCells();
}

function drawSetup() {
    if (setupPage === 0) { drawMicro(); return; }
    if (setupPage === FX_PAGE) { drawFx(); return; }
    if (setupPage === FX_PAGE + 1) { drawFx2(); return; }
    if (setupPage === HEADS_PAGE) { drawHeads(); return; }
    if (setupPage === HEADS_PAGE + 1) { drawHeads2(); return; }
    const n = setupPage - SETUP_FIRST + 1;
    txt(1, 12, n === 1 ? 'PADS / PLAY' : 'SPLICE / CHORD');
    txt(W - 1 - tw(n + '/2'), 12, n + '/2');
    txt(1, 22, fmtParam('pad_mode') + ' ' + fmtParam('play_mode'));
    txt(1, 31, 'ROOT ' + fmtParam('root_note') + ' CLK ' + fmtParam('clock_source').slice(0, 4));
    drawCells();
}

const MENU = ['Load WAV...', 'New empty reel', 'Save reel', 'Auto-splice', 'Clear all markers', 'Undo', 'Clear heads', 'About / DSP load'];
const VERSION = '0.7.0';

/* version and the load meter, for beta reports: Heimat's own share of each
 * block's real time (average, recent peak) and the blocks that ran over */
function drawAbout() {
    fill_rect(0, 10, W, H - 10, 0);
    txt(1, 12, 'HEIMAT ' + VERSION + ' BETA');
    const avg = Math.round((S.cpu || 0) * 100), pk = Math.round((S.cpk || 0) * 100);
    txt(1, 22, 'DSP ' + avg + '%  PEAK ' + pk + '%');
    draw_rect(1, 31, 64, 6, 1);
    fill_rect(2, 32, Math.max(0, Math.min(62, Math.round(62 * (S.cpu || 0)))), 4, 1);
    const pkx = 2 + Math.max(0, Math.min(61, Math.round(62 * (S.cpk || 0))));
    vline(pkx, 30, 37);
    txt(70, 31, 'OVER ' + (S.ovr | 0));
    txt(1, 42, 'BACK OR JOG: CLOSE');
}

function drawMenu() {
    fill_rect(0, 10, W, H - 10, 0);
    const rows = 6, first = Math.max(0, Math.min(MENU.length - rows, menuCursor - rows + 1));   /* scrolls past six lines */
    for (let i = first; i < Math.min(MENU.length, first + rows); i++) {
        const y = 12 + (i - first) * 8;
        if (i === menuCursor) inverse(0, y - 1, W, 9, MENU[i], 4);
        else txt(4, y, MENU[i]);
    }
}
function drawBrowser() {
    fill_rect(0, 10, W, H - 10, 0);
    const short = browser.path.replace(SAMPLES_ROOT, '~');
    txt(2, 12, short.slice(-20));
    hline(0, W - 1, 21);
    const items = browser.items, per = 4;
    const first = Math.max(0, Math.min(items.length - per, browser.cursor - 1));
    for (let i = 0; i < per && first + i < items.length; i++) {
        const it = items[first + i], y = 24 + i * 10;
        const s = (it.dir ? '/' : '') + it.name.slice(0, 19);
        if (first + i === browser.cursor) inverse(0, y - 1, W, 10, s, 4);
        else txt(4, y, s);
    }
    if (!items.length) txt(4, 28, '(no WAV files here)');
}

function draw() {
    clear_screen();
    drawHeader();
    if (overlay === 'menu') drawMenu();
    else if (overlay === 'browser') drawBrowser();
    else if (overlay === 'about') drawAbout();
    else if (page === 'play') drawPlay();
    else if (page === 'reel') drawReel();
    else if (page === 'rec') drawRec();
    else drawSetup();
    if (deleteDown && deleteDown.armed) drawDeleteHold();
    else if (focusVisible()) drawFocus();
    else drawMessage();
}

/* ------------------------------------------------------------------- LEDs */

const ledWant = new Map(), ledSent = new Map();
function want(kind, id, color) { ledWant.set(kind + id, { kind, id, color }); }

function padNoteFor(i) {
    const row = Math.floor(i / 8), colm = i % 8;
    return (P.root_note | 0) + (row - 1) * 5 + colm;
}
function buildLeds() {
    ledWant.clear();
    const now = Date.now(), blink = Math.floor(now / 300) % 2 === 0;
    const ns = S.ns | 0, sel = S.sel | 0;
    const spliceCol = (i) => SPLICE_COLORS[i % SPLICE_COLORS.length];
    /* pads */
    const bank = Math.floor(sel / PAD_N);
    for (let i = 0; i < PAD_N; i++) {
        let c = COL.off;
        if (page === 'reel' || (P.pad_mode | 0) === 1) {
            const idx = bank * PAD_N + i;
            c = idx < ns ? (idx === sel ? COL.white : spliceCol(idx)) : COL.off;
        } else if ((P.pad_mode | 0) === 0) {
            const n = padNoteFor(i), r = ((n - (P.root_note | 0)) % 12 + 12) % 12;
            c = r === 0 ? COL.green : (r === 7 ? COL.azure : COL.dim);
        } else c = COL.dim;
        if (heldPads.has(i)) c = COL.white;
        want('n', PAD0 + i, c);
    }
    /* steps = splice windows */
    const sbank = Math.floor(sel / STEP_N);
    for (let i = 0; i < STEP_N; i++) {
        const idx = sbank * STEP_N + i;
        want('n', STEP0 + i, idx < ns ? (idx === sel ? COL.white : spliceCol(idx)) : COL.off);
    }
    /* page buttons */
    for (let i = 0; i < 4; i++) want('c', ROWS[i], PAGE_ORDER[i] === page && !overlay ? COL.white : COL.dim);
    /* transport and edit */
    want('c', MoveRec, S.rec ? (blink || S.stp ? COL.red : COL.off) : (S.arm ? (blink ? COL.orange : COL.off) : COL.dim));
    want('c', MoveSample, S.rec === 1 ? COL.red : COL.grey);
    want('c', MovePlay, S.run ? COL.green : COL.dim);
    want('c', MoveMute, P.mute ? COL.red : COL.off);
    want('c', MoveLoop, (P.clock_source | 0) ? ((S.ck | 0) % 2 ? COL.yellow : COL.orange) : COL.dim);
    want('c', MoveCapture, now < flashCapture ? COL.lime : COL.dim);
    want('c', MoveUndo, (S.up | 0) > 0 ? COL.white : COL.dim);
    want('c', MoveDelete, COL.dim);
    want('c', MoveCopy, COL.dim);
    want('c', MoveMenu, overlay ? COL.white : COL.grey);
    want('c', MoveBack, overlay ? COL.white : COL.dim);
    want('c', MoveUp, COL.dim);
    want('c', MoveDown, COL.dim);
    /* the arrows act as direction windows: left = reverse, right = forward */
    const spd = S.spd || 0;
    want('c', MoveLeft, spd < -0.02 ? COL.white : COL.dim);
    want('c', MoveRight, spd > 0.02 ? COL.white : COL.dim);
}
function drainLeds(max) {
    let n = 0;
    for (const [k, w] of ledWant) {
        if (ledSent.get(k) === w.color) continue;
        if (w.kind === 'n') moveInputFilter.setLED(w.id, w.color);
        else moveInputFilter.setButtonLED(w.id, w.color);
        ledSent.set(k, w.color);
        if (++n >= max) break;
    }
}

/* --------------------------------------------------------------- browsing */

function listDir(path) {
    let names = [];
    try {
        const r = os.readdir(path);
        names = Array.isArray(r) && Array.isArray(r[0]) ? r[0] : r;
    } catch (e) { names = []; }
    if (!Array.isArray(names)) names = [];
    const items = [];
    if (path !== SAMPLES_ROOT) items.push({ name: '..', dir: true, path: path.slice(0, path.lastIndexOf('/')) || SAMPLES_ROOT });
    const dirs = [], files = [];
    for (const name of names) {
        if (!name || name[0] === '.') continue;
        const full = path + '/' + name;
        let isDir = false;
        try { const st = os.stat(full); const info = Array.isArray(st) ? st[0] : st; isDir = !!info && (info.mode & 0o170000) === 0o040000; } catch (e) { /* not a dir */ }
        if (isDir) dirs.push({ name, dir: true, path: full });
        else if (/\.wav$/i.test(name)) files.push({ name, dir: false, path: full });
    }
    const byName = (a, b) => a.name.toLowerCase().localeCompare(b.name.toLowerCase());
    return items.concat(dirs.sort(byName), files.sort(byName));
}
function openBrowser(path) {
    browser.path = path;
    browser.items = listDir(path);
    browser.cursor = 0;
    overlay = 'browser';
}

/* ----------------------------------------------------------------- actions */

function selectSplice(i) {
    const ns = S.ns | 0;
    if (ns < 1) return;
    setParam('sel_splice', ((i % ns) + ns) % ns);
}
function dropMarker() {
    if (frames() < 2) { say('NO REEL YET'); return; }
    flashCapture = Date.now() + 160;
    if (page === 'reel') setParam('marker_add', Math.round(cursor));
    else setParam('marker_add', 'play');
    say('MARKER');
}
function nearestMarker() {
    let best = -1, bd = Infinity;
    const [va, vb] = viewRange(), perPx = Math.max(1, (vb - va) / W);
    for (let i = 0; i < markers.length; i++) {
        const d = Math.abs(markers[i] - cursor);
        if (d < bd) { bd = d; best = i; }
    }
    return best >= 0 && bd <= perPx * 4 ? best : -1;
}
function deleteMarker() {
    if (page === 'reel') {
        const i = nearestMarker();
        if (i >= 0) { setParam('marker_del', i); say('MARKER DELETED'); return; }
    }
    setParam('marker_del_cur', 1);
    say('SPLICES JOINED');
}
function nudgeMarker(which, delta) {
    const sel = S.sel | 0, idx = which === 'start' ? sel - 1 : sel;
    if (idx < 0 || idx >= markers.length) { say(which === 'start' ? 'REEL START' : 'REEL END'); return; }
    const step = Math.max(1, Math.round(viewWidth() / W)) * (shiftHeld ? 0.1 : 1);
    const pos = Math.max(1, Math.round(markers[idx] + delta * Math.max(1, step)));
    setParam('marker_move', idx + ',' + pos);
    cursor = pos;
}
function runMenu(i) {
    overlay = null;
    if (i === 0) openBrowser(SAMPLES_ROOT);
    else if (i === 1) { setParam('sample_path', ''); say('NEW EMPTY REEL'); }
    else if (i === 2) { setParam('save_reel', 'auto'); say('SAVING...'); }
    else if (i === 3) { setParam('detect_markers', sens.toFixed(2)); say('LISTENING FOR HITS'); }
    else if (i === 4) { setParam('markers_clear', 1); say('MARKERS CLEARED'); }
    else if (i === 5) { setParam('undo', 1); say('UNDO'); }
    else if (i === 6) { setParam('hd_clear', 1); say('HEADS CLEARED'); }
    else if (i === 7) overlay = 'about';
}

/* a virtual cell: knobs that are not plain DSP parameters */
function turnVirtual(key, delta) {
    if (key === '@splice') { selectSplice((S.sel | 0) + delta); cursor = [0].concat(markers)[Math.max(0, S.sel | 0)] || 0; }
    else if (key === '@start') nudgeMarker('start', delta);
    else if (key === '@end') nudgeMarker('end', delta);
    else if (key === '@zoom') zoomLevel = Math.max(0, Math.min(7, zoomLevel + delta));
    else if (key === '@sens') sens = Math.max(0, Math.min(1, sens + delta * 0.02));
    else if (key === '@divide') { divN = Math.max(1, Math.min(32, divN + delta)); pendingDivide = { n: divN, t: Date.now() }; }
}

function turnKnob(i, raw) {
    const key = pageKeys()[i];
    const accel = typeof moveInputFilter.decodeAcceleratedDelta === 'function'
        ? moveInputFilter.decodeAcceleratedDelta(raw, i) : moveInputFilter.decodeDelta(raw);
    const delta = accel === 0 || accel === undefined ? moveInputFilter.decodeDelta(raw) : accel;
    if (!delta) return;
    focus.idx = i; focus.until = Date.now() + FOCUS_TURN_MS;
    if (key[0] === '@') { turnVirtual(key, delta); return; }
    const d = PD[key];
    let v = P[key];
    if (d.log) v *= Math.pow(2, delta * 0.03 * (shiftHeld ? 0.2 : 1));          /* log knobs: 1/33 octave per step */
    else if (d.t === 'f') v += delta * (d.step || (d.max - d.min) * 0.005) * (shiftHeld ? 0.2 : 1);
    else v += delta > 0 ? 1 : -1;
    setP(key, v);
}

function showTouched(i) {
    focus.idx = i; focus.until = Date.now() + FOCUS_TOUCH_MS;
}

function pressButton(cc) {
    if (cc === MoveShift) return;
    const rowIdx = ROWS.indexOf(cc);
    if (rowIdx >= 0) {
        let target = PAGE_ORDER[rowIdx];
        if (target === 'rec' && shiftHeld) { target = 'setup'; setupPage = HEADS_PAGE; }   /* Shift + Track 3: HEADS */
        else if (target === 'setup' && shiftHeld) setupPage = FX_PAGE;                  /* Shift + Track 4: straight to FX */
        else if (target === 'setup' && page === 'setup' && !overlay) setupPage = (setupPage + 1) % PAGES.setup.length;
        else if (target === 'setup' && page !== 'setup') setupPage = 0;
        focus.idx = -1;
        page = target;
        overlay = null;
        peaksKey = '';
        return;
    }
    if (cc === MoveMenu) { overlay = overlay ? null : 'menu'; menuCursor = 0; return; }
    if (cc === MoveBack) {
        if (overlay === 'browser' && browser.path !== SAMPLES_ROOT) { openBrowser(browser.path.slice(0, browser.path.lastIndexOf('/')) || SAMPLES_ROOT); return; }
        if (overlay) { overlay = null; return; }
        if (shiftHeld && typeof host_exit_module === 'function') host_exit_module();
        return;
    }
    if (overlay) return;
    if (cc === MoveRec) setParam(shiftHeld ? 'rec_toggle_new' : 'rec_toggle', 1);
    else if (cc === MoveSample) setParam('rec_toggle_new', 1);
    else if (cc === MoveCapture) dropMarker();
    else if (cc === MoveUndo) { setParam('undo', 1); say('UNDO'); }
    else if (cc === MovePlay) { setParam('play_latch', S.run ? 0 : 1); }
    else if (cc === MoveMute) setP('mute', P.mute ? 0 : 1);
    else if (cc === MoveLoop) {
        const next = shiftHeld ? ((P.clock_source | 0) + 1) % 4 : ((P.clock_source | 0) ? 0 : 1);
        setP('clock_source', next);
        say('CLOCK ' + PD.clock_source.opts[next]);
    }
    else if (cc === MoveCopy) { setParam('detect_markers', sens.toFixed(2)); say('LISTENING FOR HITS'); }
    else if (cc === MoveLeft) { selectSplice((S.sel | 0) - 1); if (page === 'reel') cursor = [0].concat(markers)[Math.max(0, S.sel | 0)] || 0; }
    else if (cc === MoveRight) { selectSplice((S.sel | 0) + 1); if (page === 'reel') cursor = [0].concat(markers)[Math.max(0, S.sel | 0)] || 0; }
    else if (cc === MoveUp || cc === MoveDown) {
        const up = cc === MoveUp ? 1 : -1;
        if ((P.pad_mode | 0) === 1 || page === 'reel') selectSplice((S.sel | 0) + up * PAD_N);
        else setP('octave_transpose', (P.octave_transpose | 0) + up);
    }
    else if (cc === MoveDelete) { deleteDown = { t: Date.now(), hold: shiftHeld ? 2000 : 1200, shift: shiftHeld, armed: false, fired: false }; }
    else if (cc === MoveMainButton) {
        if (page === 'reel') dropMarker();
        else setParam('sel_splice', S.sel | 0);
    }
}
function releaseButton(cc) {
    if (cc === MoveDelete && deleteDown) {
        if (!deleteDown.fired && !overlay) deleteMarker();
        deleteDown = null;
    }
}

function jogTurn(raw) {
    const delta = moveInputFilter.decodeDelta(raw);
    if (!delta) return;
    if (overlay === 'menu') { menuCursor = Math.max(0, Math.min(MENU.length - 1, menuCursor + delta)); return; }
    if (overlay === 'browser') { browser.cursor = Math.max(0, Math.min(Math.max(0, browser.items.length - 1), browser.cursor + delta)); return; }
    if (page === 'reel' && frames() > 1) {
        const perPx = Math.max(1, viewWidth() / W);
        cursor = Math.max(0, Math.min(frames() - 1, Math.round(cursor + delta * perPx * (shiftHeld ? 0.1 : 1))));
        return;
    }
    selectSplice((S.sel | 0) + delta);
}
function jogClick() {
    if (overlay === 'about') { overlay = null; return; }
    if (overlay === 'menu') { runMenu(menuCursor); return; }
    if (overlay === 'browser') {
        const it = browser.items[browser.cursor];
        if (!it) return;
        if (it.dir) openBrowser(it.path);
        else { overlay = null; setParam('sample_path', it.path); say('LOADING ' + it.name.slice(0, 12)); }
        return;
    }
    pressButton(MoveMainButton);
}

/* a pad: the DSP interprets the note according to Pad Mode */
function padDown(i) {
    heldPads.add(i);
    if (page === 'reel') { selectSplice(Math.floor((S.sel | 0) / PAD_N) * PAD_N + i); setParam('play_latch', 1); return; }
    const pm = P.pad_mode | 0;
    let note;
    if (pm === 1) note = (P.splice_base !== undefined ? P.splice_base : 36) + Math.floor((S.sel | 0) / PAD_N) * PAD_N + i;
    else if (pm === 0) note = padNoteFor(i);
    else note = 36 + i;
    pads.set(i, note);
    sendMidi(0x90, Math.max(0, Math.min(127, note)), 100);
}
const pads = new Map();
function padUp(i) {
    heldPads.delete(i);
    if (page === 'reel') { setParam('play_latch', 0); return; }
    const note = pads.get(i);
    if (note !== undefined) { sendMidi(0x80, Math.max(0, Math.min(127, note)), 0); pads.delete(i); }
}

/* ------------------------------------------------------------- entry points */

globalThis.init = function () {
    if (typeof moveInputFilter.invalidateLedCache === 'function') moveInputFilter.invalidateLedCache();
    ledSent.clear();
    readAllParams();
    page = 'play'; setupPage = 0; overlay = null; shiftHeld = false; ticks = 0;
    touched.fill(false); heldPads.clear(); pads.clear(); deleteDown = null; focus.idx = -1; focus.until = 0;
    cursor = 0; zoomLevel = 0; peaksKey = ''; lastMv = -1; lastSv = 0; lastPg = -1; lastLocalEdit = 0; pendingDivide = null;
    pollDsp(Date.now());
    say('HEIMAT SOLO', 1200);
};

globalThis.tick = function () {
    const now = Date.now();
    ticks++;
    if (now - lastPoll >= 33) { lastPoll = now; pollDsp(now); }
    if (pendingDivide && now - pendingDivide.t > 450) { setParam('markers_divide', pendingDivide.n); pendingDivide = null; }
    if (deleteDown && !deleteDown.fired) {
        const held = now - deleteDown.t;
        deleteDown.armed = held > 350;
        if (held >= deleteDown.hold) {
            deleteDown.fired = true;
            if (deleteDown.shift) { setParam('sample_path', ''); say('NEW EMPTY REEL'); }
            else { setParam('markers_clear', 1); say('MARKERS CLEARED'); }
            deleteDown.armed = false;
        }
    }
    if (now - lastDraw >= 33) {
        lastDraw = now;
        draw();
        buildLeds();
    }
    drainLeds(8);
};

globalThis.onMidiMessageInternal = function (data) {
    if (!data || data.length < 3) return;
    const status = data[0] & 0xF0, d1 = data[1], d2 = data[2];
    if (status === 0x90 || status === 0x80) {
        const on = status === 0x90 && d2 > 0;
        if (d1 >= 0 && d1 <= 7) {
            touched[d1] = on;
            if (on) showTouched(d1);
            return;
        }
        if (d1 >= PAD0 && d1 < PAD0 + PAD_N) { if (on) padDown(d1 - PAD0); else padUp(d1 - PAD0); return; }
        if (d1 >= STEP0 && d1 < STEP0 + STEP_N) {
            if (on && !overlay) selectSplice(Math.floor((S.sel | 0) / STEP_N) * STEP_N + (d1 - STEP0));
            return;
        }
        return;
    }
    if (status !== 0xB0) return;
    if (d1 === MoveShift) { shiftHeld = d2 > 0; return; }
    if (d1 >= MoveKnob1 && d1 < MoveKnob1 + 8) { if (!overlay) turnKnob(d1 - MoveKnob1, d2); return; }
    if (d1 === MoveMainKnob) { jogTurn(d2); return; }
    if (d1 === MoveMainButton) { if (d2 > 0) jogClick(); return; }
    if (d2 > 0) pressButton(d1); else releaseButton(d1);
};

globalThis.onMidiMessageExternal = function () { /* the surface is driven by the Move itself */ };
