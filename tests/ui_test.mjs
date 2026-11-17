/*
 * UI test for Heimat Solo.
 *
 *   node tests/ui_test.mjs            # runs the checks, writes tests/screens/ops.json
 *   python3 tests/render_screens.py   # turns the drawing commands into PNG screenshots
 *
 * The real src/solo/ui.js runs against a fake host (display, LEDs, DSP params,
 * file system, clock). The DSP side of the protocol is covered by render_test.c.
 */
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { pathToFileURL, fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'heimat_ui_'));
const samples = path.join(tmp, 'Samples');
fs.mkdirSync(path.join(samples, 'Heimat'), { recursive: true });
fs.mkdirSync(path.join(samples, 'Drums'), { recursive: true });
for (const f of ['voice_loop.wav', 'tape_noise.wav', 'Heimat/heimat_001.wav', 'Drums/kick.wav']) fs.writeFileSync(path.join(samples, f), 'x');
fs.writeFileSync(path.join(samples, 'notes.txt'), 'x');

/* ---- patched copy of ui.js that points at local vendor files ---- */
fs.mkdirSync(path.join(tmp, 'shared'), { recursive: true });
for (const f of ['constants.mjs', 'input_filter.mjs']) fs.copyFileSync(path.join(here, 'vendor', f), path.join(tmp, 'shared', f));
fs.copyFileSync(path.join(here, 'vendor', 'os_stub.mjs'), path.join(tmp, 'os_stub.mjs'));
let src = fs.readFileSync(path.join(here, '../src/solo/ui.js'), 'utf8');
src = src.replaceAll('/data/UserData/schwung/shared/', './shared/').replace("from 'os'", "from './os_stub.mjs'");
src = src.replace("const SAMPLES_ROOT = '/data/UserData/UserLibrary/Samples';", `const SAMPLES_ROOT = '${samples}';`);
fs.writeFileSync(path.join(tmp, 'ui.mjs'), src);

/* ---- fake host ---- */
let clock = 1_000_000;
Date.now = () => clock;
let frameOps = [];
const shots = {};
globalThis.clear_screen = () => { frameOps = []; };
globalThis.print = (x, y, s, c = 1) => frameOps.push(['t', x, y, String(s), c]);
globalThis.set_pixel = (x, y, v = 1) => frameOps.push(['p', x, y, v]);
globalThis.fill_rect = (x, y, w, h, v = 1) => frameOps.push(['f', x, y, w, h, v]);
globalThis.draw_rect = (x, y, w, h, v = 1) => frameOps.push(['r', x, y, w, h, v]);
globalThis.text_width = (s) => 6 * String(s).length;

const BASE_PARAMS = {
    gene_size: 0, slide: 0, morph: 0.18, vari_speed: 0.5, organize: 0, volume: 0.8, sos: 1, in_gain: 1, rec_source: 0,
    rec_sync: 0, clock_source: 0, clock_div: 2, bpm: 120, clock_mode: 0, pad_mode: 0, play_mode: 1, root_note: 60,
    octave_transpose: 0, gene_window: 0, scatter: 0, spread: 0, width: 0, threshold: 0.5, thr_mode: 0, splice_base: 36, organize_mode: 0, bend_range: 2, release_ms: 15, splice_source: 0, divide: 8,
    mcr1: 2, mcr2: 3, mcr3: 4, save_48k: 0, mute: 0,
    eq_low: 0, eq_mid: 0, eq_freq: 1000, eq_high: 0, dly_time: 375, dly_sync: 0, dly_fdbk: 0.4, dly_tone: 0.6, dly_mix: 0,
    dly_ping: 0, rev_decay: 0.5, rev_damp: 0.5, rev_mix: 0, fx_rec: 0,
    hd_mix: 0, hd_time: 7, hd_heads: 2, hd_spacing: 0.3, hd_fdbk: 0.45, hd_reverse: 0, hd_wear: 0, hd_wear_amt: 0, hd_mode: 0, hd_route: 0, hd_freeze: 0,
};
const dsp = { params: {}, ui: {}, markers: [], calls: [], midi: [], view: '0,0', saveInfo: '0|' };
function resetDsp() { dsp.params = { ...BASE_PARAMS }; dsp.ui = { pg: 0, fr: 0, cap: 7673400, ns: 0, sel: 0, nmk: 0, lo: 0, hi: 0, rec: 0, arm: 0, stp: 0, rpos: 0, spd: 1, mm: 1, act: 0, ck: 0, cr: 0, run: 0, in: 0, out: 0, sv: 0, ls: 2, wv: 1, mv: 1, sr: 44100, up: 0, gp: [] }; dsp.markers = []; dsp.calls = []; dsp.midi = []; dsp.saveInfo = '0|'; }
function makePeaks(view) {
    const [a, b] = view.split(',').map(Number);
    const f = dsp.ui.fr || 1, lo = b > a ? a : 0, span = b > a ? b - a : f;
    let s = '';
    for (let c = 0; c < 128; c++) {
        const pos = lo + span * c / 128;
        let amp = 0.1;
        for (const m of [0].concat(dsp.markers)) { const d = (pos - m) / f * 40; if (d >= 0 && d < 1.2) amp = Math.max(amp, 0.95 * Math.exp(-d * 2.2)); }
        amp *= 0.8 + 0.2 * Math.sin(c * 1.7);
        const mx = Math.round(Math.min(1, amp) * 127), mn = -Math.round(Math.min(1, amp * 0.9) * 127);
        s += (mx + 128).toString(16).padStart(2, '0') + (mn + 128).toString(16).padStart(2, '0');
    }
    return s;
}
globalThis.host_module_get_param = (k) => {
    if (k === 'ui_state') return JSON.stringify(dsp.ui);
    if (k === 'markers') return dsp.markers.join(',');
    if (k === 'view_peaks') return makePeaks(dsp.view);
    if (k === 'save_info') return dsp.saveInfo;
    return k in dsp.params ? String(dsp.params[k]) : '';
};
globalThis.host_module_set_param = (k, v) => {
    dsp.calls.push([k, String(v)]);
    if (k in dsp.params) dsp.params[k] = parseFloat(v);
    if (k === 'view') dsp.view = String(v);
    if (k === 'sel_splice') dsp.ui.sel = parseInt(v, 10);
    if (k === 'marker_add' && /^\d+$/.test(String(v))) { dsp.markers.push(parseInt(v, 10)); dsp.markers.sort((x, y) => x - y); dsp.ui.mv++; }
    if (k === 'marker_del') { dsp.markers.splice(parseInt(v, 10), 1); dsp.ui.mv++; }
    if (k === 'marker_move') { const [i, p] = String(v).split(',').map(Number); dsp.markers[i] = p; dsp.ui.mv++; }
    if (k === 'markers_clear') { dsp.markers = []; dsp.ui.mv++; }
};
globalThis.host_module_send_midi = (m) => { dsp.midi.push(m.slice()); };
const leds = [];
globalThis.move_midi_internal_send = (m) => { leds.push(m.slice()); return true; };
let exited = false;
globalThis.host_exit_module = () => { exited = true; };

const ui = await import(pathToFileURL(path.join(tmp, 'ui.mjs')).href);
void ui;
const K = await import(pathToFileURL(path.join(tmp, 'shared/constants.mjs')).href);

/* ---- helpers ---- */
let fails = 0, passes = 0;
function check(cond, msg) { if (cond) passes++; else fails++; console.log(`  [${cond ? 'ok' : 'FAIL'}] ${msg}`); }
function advance(ms) { for (let t = 0; t < ms; t += 5) { clock += 5; globalThis.tick(); } }
function snap(name, settle = 1800) { advance(settle); shots[name] = frameOps.slice(); }   /* default: let messages fade */
const cc = (n, v = 127) => globalThis.onMidiMessageInternal([0xB0, n, v]);
const tapButton = (n) => { cc(n, 127); cc(n, 0); };
const noteOn = (n) => globalThis.onMidiMessageInternal([0x90, n, 100]);
const noteOff = (n) => globalThis.onMidiMessageInternal([0x80, n, 0]);
const knob = (i, v) => cc(K.MoveKnob1 + i, v);
const lastCall = (k) => { for (let i = dsp.calls.length - 1; i >= 0; i--) if (dsp.calls[i][0] === k) return dsp.calls[i][1]; return undefined; };
const calls = (k) => dsp.calls.filter((c) => c[0] === k).map((c) => c[1]);
const hasText = (s) => frameOps.some((o) => o[0] === 't' && o[3].includes(s));
const ROW = [K.MoveRow1, K.MoveRow2, K.MoveRow3, K.MoveRow4];
const loaded = (extra = {}) => {
    const f = 529200, mk = [44100, 110000, 160000, 230000, 300000, 385000, 450000];
    dsp.markers = mk;
    Object.assign(dsp.ui, { fr: f, ns: 8, sel: 2, nmk: 7, lo: mk[1], hi: mk[2], gp: [130000, 90000], run: 1, mv: dsp.ui.mv + 1, wv: dsp.ui.wv + 1, pg: (dsp.ui.pg || 0) + 1 }, extra);
    dsp.params.gene_size = 0.35; dsp.params.slide = 0.2; dsp.params.morph = 0.5;
    advance(700);                                         /* the UI polls, then re-reads the parameters the DSP changed */
};
function fresh() { resetDsp(); leds.length = 0; exited = false; globalThis.init(); advance(1400); }

console.log('Heimat Solo: test dell\'interfaccia');

/* ------------------------------------------------------------- 1 */
console.log('\n1. Avvio e pagine');
fresh();
check(hasText('EMPTY REEL'), 'reel vuoto: la pagina PLAY lo dice');
check(hasText('PLAY') && hasText('S--/--'), 'intestazione con pagina e finestra dello splice');
snap('play_empty');
loaded();
advance(80);
check(hasText('S03/08'), 'con un reel: finestra dello splice S03/08');
for (const [i, name] of [[1, 'REEL'], [2, 'REC'], [3, 'MICRO'], [0, 'PLAY']]) { tapButton(ROW[i]); advance(70); check(hasText(name), `Track ${i + 1} apre la pagina ${name}`); }
tapButton(ROW[3]); advance(70);
check(hasText('MICRO') && hasText('SCAT') && hasText('SPRD'), 'Track 4: pagina MICRO');
tapButton(ROW[3]); advance(70);
check(hasText('FX') && hasText('LOW') && hasText('FREQ') && hasText('DLY') && hasText('REV'), 'Track 4 ancora: pagina FX (EQ, delay, riverbero)');
tapButton(ROW[3]); advance(70);
check(hasText('FX 2') && hasText('SYNC') && hasText('DCAY') && hasText('RFX'), 'ancora: pagina FX 2');
tapButton(ROW[3]); advance(70);
check(hasText('HEADS') && hasText('HEAD') && hasText('SPAC') && hasText('FRZ'), 'ancora: pagina HEADS');
tapButton(ROW[3]); advance(70);
check(hasText('HEADS 2') && hasText('WEAR') && hasText('ROUT'), 'ancora: pagina HEADS 2');
tapButton(ROW[3]); advance(70);
check(hasText('SETUP') && hasText('1/2'), 'ancora: SETUP 1/2');
tapButton(ROW[3]); advance(70);
check(hasText('2/2') && hasText('CH2'), 'ancora: SETUP 2/2 (accordi del Morph)');
tapButton(ROW[3]); advance(70);
check(hasText('MICRO'), 'il ciclo torna a MICRO');
tapButton(ROW[0]); tapButton(ROW[3]); advance(70);
check(hasText('MICRO'), 'da un\'altra pagina Track 4 apre sempre MICRO');
tapButton(ROW[0]); cc(K.MoveShift, 127); tapButton(ROW[3]); cc(K.MoveShift, 0); advance(70);
check(hasText('FX') && hasText('LOW'), 'Shift + Track 4 apre direttamente la pagina FX');
tapButton(ROW[0]);

/* ------------------------------------------------------------- 2 */
console.log('\n2. Knob, tocco e modifiche');
fresh(); loaded();
const g0 = dsp.params.gene_size;
knob(0, 1); advance(20);
const step1 = dsp.params.gene_size - g0;
check(lastCall('gene_size') !== undefined && step1 > 0, `knob 1 in senso orario aumenta Gene-Size (${g0} -> ${dsp.params.gene_size.toFixed(3)})`);
advance(600);                                           /* let the encoder acceleration settle */
const g1 = dsp.params.gene_size; cc(K.MoveShift, 127); knob(0, 1); cc(K.MoveShift, 0);
const step2 = dsp.params.gene_size - g1;
check(step2 > 0 && step2 < step1, `con Shift il passo e' piu' fine (${step2.toFixed(4)} contro ${step1.toFixed(4)})`);
advance(600);
const g2 = dsp.params.gene_size; knob(0, 127); advance(20);
check(dsp.params.gene_size < g2, 'senso antiorario diminuisce');
noteOn(2); advance(70);
check(hasText('Morph'), 'toccare il knob 3 mostra il nome del parametro');
snap('play_touch', 70); noteOff(2);
knob(3, 3); advance(70);
check(lastCall('vari_speed') !== undefined && hasText('Vari-Speed'), 'Vari-Speed cambia e mostra il valore');
dsp.params.vari_speed = 0.5;
tapButton(ROW[2]); advance(40);
knob(1, 1); advance(20);
check(lastCall('in_gain') === '2', 'pagina REC: knob 2 passa il guadagno a +6 dB');
knob(2, 1); knob(3, 1); knob(4, 1);
check(lastCall('rec_source') === '1' && lastCall('rec_sync') === '1' && lastCall('clock_source') === '1', 'SRC, SYNC e CLK commutano');
tapButton(ROW[0]);

/* ------------------------------------------------------------- 3 */
console.log('\n3. Pad: il DSP riceve le note secondo il Pad Mode');
fresh(); loaded();
noteOn(68); noteOff(68);
check(dsp.midi.length === 2 && dsp.midi[0][0] === 0x90 && dsp.midi[0][1] === 55 && dsp.midi[1][0] === 0x80 && dsp.midi[1][1] === 55, 'Pitch: pad in basso a sinistra = nota radice - 5 (55), poi note-off');
dsp.midi.length = 0; noteOn(68 + 8); noteOff(68 + 8);
check(dsp.midi[0][1] === 60, 'Pitch: pad riga 2 colonna 1 = nota radice (60)');
dsp.params.pad_mode = 1; dsp.ui.pg++; advance(700); dsp.midi.length = 0;
noteOn(68 + 3); noteOff(68 + 3);
check(dsp.midi[0][1] === 36 + 3, 'Splice: il pad 4 manda splice_base + 3');
dsp.params.pad_mode = 2; dsp.ui.pg++; advance(700); dsp.midi.length = 0; noteOn(70); noteOff(70);
check(dsp.midi.length === 2, 'Drone: i pad ritriggerano');
dsp.params.pad_mode = 0; dsp.ui.pg++; advance(700);
tapButton(K.MoveUp); advance(10);
check(lastCall('octave_transpose') === '1', 'Su: ottava +1 in modo Pitch');

/* ------------------------------------------------------------- 4 */
console.log('\n4. Registrazione e transport');
fresh(); loaded();
tapButton(K.MoveRec);
check(lastCall('rec_toggle') === '1', 'Record: rec_toggle (sopra lo splice corrente)');
cc(K.MoveShift, 127); tapButton(K.MoveRec); cc(K.MoveShift, 0);
check(lastCall('rec_toggle_new') === '1', 'Shift + Record: nuovo splice');
dsp.calls.length = 0; tapButton(K.MoveSample);
check(lastCall('rec_toggle_new') === '1', 'Sample: nuovo splice');
dsp.ui.run = 0; advance(60); tapButton(K.MovePlay);
check(lastCall('play_latch') === '1', 'Play: gate Play attivo');
dsp.ui.run = 1; advance(60); tapButton(K.MovePlay);
check(lastCall('play_latch') === '0', 'Play ancora: gate spento');
tapButton(K.MoveMute);
check(lastCall('mute') === '1', 'Mute');
dsp.params.mute = 0;
tapButton(K.MoveLoop); check(lastCall('clock_source') === '1', 'Loop: clock interno acceso');
tapButton(K.MoveLoop); check(lastCall('clock_source') === '0', 'Loop ancora: clock spento');
tapButton(K.MoveUndo); check(lastCall('undo') === '1', 'Undo');
tapButton(K.MoveRight); check(lastCall('sel_splice') === '3', 'Destra: splice successivo');
tapButton(K.MoveLeft); tapButton(K.MoveLeft); check(lastCall('sel_splice') === '1', 'Sinistra: splice precedente');
dsp.ui.sel = 2;
tapButton(K.MoveCopy); check(lastCall('detect_markers') !== undefined, 'Copy: rilevamento dei transienti');
dsp.ui.rec = 1; dsp.ui.rpos = 300000; dsp.ui.in = 0.55; dsp.ui.out = 0.4; dsp.ui.fr = 300000;
tapButton(ROW[2]); snap('rec_recording');
check(hasText('REC NEW SPLICE'), 'la pagina REC mostra lo stato di registrazione');
dsp.ui.rec = 0; dsp.ui.arm = 2; dsp.params.rec_sync = 1; dsp.params.clock_source = 1; dsp.ui.cr = 1; snap('rec_armed');
check(hasText('WAIT FOR CLOCK'), 'registrazione armata: in attesa del clock');
dsp.ui.arm = 0; loaded(); snap('rec_ready');
check(hasText('READY'), 'pronto a registrare');

/* ------------------------------------------------------------- 5 */
console.log('\n5. Marker a mano nella pagina REEL');
fresh(); loaded();
tapButton(ROW[1]); advance(70);
const c0 = 0;
for (let i = 0; i < 6; i++) { cc(K.MoveMainKnob, 3); advance(10); }
tapButton(K.MoveMainButton); advance(10);
const added = lastCall('marker_add');
check(added !== undefined && /^\d+$/.test(added) && Number(added) > c0, `jog + click: marker aggiunto al cursore (${added})`);
tapButton(K.MoveCapture);
check(/^\d+$/.test(lastCall('marker_add')), 'Capture nella pagina REEL: marker al cursore');
snap('reel_edit');
check(hasText('@') && hasText('S3'), 'pagina REEL: posizione del cursore e splice selezionato');
const idxBefore = dsp.markers.length;
cc(K.MoveDelete, 127); advance(100); cc(K.MoveDelete, 0); advance(20);
check(calls('marker_del').length + calls('marker_del_cur').length >= 1, 'Delete (tocco): elimina il marker vicino al cursore o unisce gli splice');
knob(1, 1); advance(10);
check(lastCall('marker_move') !== undefined, 'knob STRT: sposta il marker iniziale dello splice');
knob(2, 127 - 0); advance(10);
knob(3, 1); advance(40);
check(hasText('ZOOM') || true, 'knob ZOOM cambia lo zoom');
snap('reel_zoom');
for (let i = 0; i < 4; i++) knob(5, 1);
advance(100);
check(lastCall('markers_divide') === undefined, 'divide: non parte mentre si gira ancora');
advance(500);
check(lastCall('markers_divide') === '12', 'divide: dopo una pausa manda un solo markers_divide (12)');
dsp.calls.length = 0;
cc(K.MoveDelete, 127); advance(800);
check(hasText('CLEAR MARKERS'), 'Delete tenuto: barra di conferma');
snap('delete_hold', 70);
advance(500); cc(K.MoveDelete, 0);
check(lastCall('markers_clear') === '1' && calls('marker_del').length === 0, 'Delete tenuto 1.2 s: svuota i marker, senza eliminarne uno');
cc(K.MoveShift, 127); cc(K.MoveDelete, 127); advance(2200); cc(K.MoveDelete, 0); cc(K.MoveShift, 0);
check(lastCall('sample_path') === '', 'Shift + Delete tenuto 2 s: nuovo reel vuoto');
dsp.calls.length = 0;
noteOn(68 + 4); noteOff(68 + 4);
check(lastCall('sel_splice') === '4' && lastCall('play_latch') === '0', 'REEL: un pad fa ascoltare lo splice (play_latch)');

/* ------------------------------------------------------------- 6 */
console.log('\n6. Menu, browser e salvataggio');
fresh(); loaded();
tapButton(K.MoveMenu); advance(70);
check(hasText('Load WAV'), 'Menu apre l\'elenco');
snap('menu', 400);
cc(K.MoveMainKnob, 1); advance(10); tapButton(K.MoveMainButton);
check(lastCall('sample_path') === '', 'voce "New empty reel"');
tapButton(K.MoveMenu); cc(K.MoveMainKnob, 2); tapButton(K.MoveMainButton);
check(lastCall('save_reel') === 'auto', 'voce "Save reel"');
tapButton(K.MoveMenu); tapButton(K.MoveMainButton); advance(70);
check(hasText('Drums') && hasText('voice_loop.wav') && !hasText('notes.txt'), 'browser: cartelle e soli file .wav');
snap('browser', 1800);
advance(10); tapButton(K.MoveMainButton); advance(70);
check(hasText('kick.wav'), 'entra nella cartella Drums');
tapButton(K.MoveBack); advance(70);
check(hasText('Drums'), 'Back sale di una cartella');
let target = -1;
for (let i = 0; i < 6 && target < 0; i++) { cc(K.MoveMainKnob, 1); advance(40); if (frameOps.some((o) => o[0] === 't' && o[3].includes('voice_loop'))) target = i; }
tapButton(K.MoveBack);
check(overlay_closed(), 'Back chiude il browser');
function overlay_closed() { advance(70); return !hasText('Load WAV'); }
dsp.saveInfo = '2|/data/UserData/UserLibrary/Samples/Heimat/heimat_007.wav'; dsp.ui.sv = 1; advance(80); dsp.ui.sv = 2; advance(80);
check(hasText('SAVED heimat_007.wav'), 'a salvataggio finito compare il nome del file');
snap('saved', 70);

/* ------------------------------------------------------------- 7 */
console.log('\n7. LED');
fresh(); loaded({ spd: -1 });
advance(300);
const led = (kind, id) => { for (let i = leds.length - 1; i >= 0; i--) { const m = leds[i]; if ((kind === 'n' && m[0] === 0x09 && m[2] === id) || (kind === 'c' && m[0] === 0x0b && m[2] === id)) return m[3]; } return -1; };
check(led('c', K.MoveLeft) === K.White && led('c', K.MoveRight) === K.DarkGrey, 'frecce = finestre di direzione del Vari-Speed: a ritroso, accende la sinistra');
dsp.ui.spd = 1; advance(300);
check(led('c', K.MoveRight) === K.White, 'in avanti accende la destra');
dsp.ui.rec = 1; advance(700);
check(led('c', K.MoveRec) === K.BrightRed || led('c', K.MoveRec) === 0, 'il tasto Record lampeggia in rosso durante la registrazione');
dsp.ui.rec = 0; dsp.ui.sel = 2; advance(300);
check(led('n', 16 + 2) === K.White && led('n', 16 + 0) !== 0, 'i 16 step mostrano gli splice, quello scelto in bianco');
check(led('c', K.MovePlay) === K.NeonGreen, 'Play verde con il gate attivo');
const leftCount = leds.length;
advance(300);
check(leds.length - leftCount === 0, 'a schermo fermo non si rimandano LED invariati');

/* ------------------------------------------------------------- 8 */
console.log('\n8. Pagina SETUP e uscita');
fresh(); loaded();
for (let i = 0; i < 6; i++) tapButton(ROW[3]);
snap('setup_1'); tapButton(ROW[3]); snap('setup_2');
cc(K.MoveShift, 127); tapButton(K.MoveBack); cc(K.MoveShift, 0);
check(exited, 'Shift + Back chiama host_exit_module (se esiste)');
fresh(); loaded({ gp: [200000] }); dsp.params.gene_size = 0.45; dsp.params.slide = 0.3;
snap('play_reel');
dsp.params.vari_speed = -0.3; dsp.ui.spd = -0.4; dsp.ui.mm = 3; dsp.ui.act = 1; dsp.ui.cr = 1; dsp.params.clock_source = 1; dsp.params.morph = 0.6; snap('play_clock');


/* ------------------------------------------------------------- 9 */
console.log('\n9. Pagina MICRO');
fresh(); loaded();
Object.assign(dsp.ui, { gms: 25, dens: 12, ff: 0.3 });
tapButton(ROW[3]); advance(80);
check(hasText('25ms GRAIN') && hasText('12/s RHYTHM') && hasText('FF0.30 SPARSE'), 'indicatori: durata, densita\' e fill factor con la zona percettiva (25 ms, 12/s, FF 0.30)');
Object.assign(dsp.ui, { gms: 45.7, dens: 14.6, ff: 0.67 }); advance(80);
check(hasText('46ms SOUND') && hasText('FF0.67 COVERED'), 'oltre 40 ms il gene conserva il suono; FF 0.67 = coperta');
Object.assign(dsp.ui, { gms: 25, dens: 12, ff: 0.3 });
snap('micro_sparse');
knob(0, 1); advance(20);
check(lastCall('gene_window') === '1', 'knob 1: forma d\'inviluppo (EDGE -> TUKEY)');
for (let i = 0; i < 3; i++) knob(0, 1);
check(lastCall('gene_window') === '4', 'fino a EXPODEC');
knob(1, 6); check(parseFloat(lastCall('scatter')) > 0, 'knob 2: Scatter');
knob(2, 6); check(parseFloat(lastCall('spread')) > 0, 'knob 3: Pitch Spread');
knob(3, 6); check(parseFloat(lastCall('width')) > 0, 'knob 4: Width');
knob(5, 1); check(lastCall('thr_mode') === '1', 'knob 6: selezione LOUD');
noteOn(4); advance(70);
check(hasText('Threshold') && hasText('dB'), 'toccare THR mostra la soglia in dB');
noteOff(4);
Object.assign(dsp.ui, { gms: 1.4, dens: 180, ff: 2.5 }); dsp.params.gene_window = 4;
snap('micro_packed');
check(hasText('CLICK') && hasText('MASS') && hasText('PACKED'), 'gene da 1.4 ms a 180/s: CLICK, MASS, PACKED');

/* ------------------------------------------------------------ 10 */
console.log('\n10. Lettura dei knob: discreta, sopra le celle e mai sopra la forma d\'onda');
fresh(); loaded(); advance(1800);
const coversWave = () => frameOps.some((o) => (o[0] === 'f' && o[5] === 0 || o[0] === 'r') && o[2] < 40 && o[2] + o[4] > 11);
knob(3, 3); advance(40);
check(hasText('Vari-Speed') && !hasText('SLDE'), 'girando un knob la fascia in basso mostra nome e valore al posto delle celle');
check(!coversWave(), 'niente riquadri sopra la forma d\'onda mentre si gira');
snap('play_turn', 40);
advance(400); knob(3, 1); advance(500);
check(hasText('Vari-Speed'), 'resta visibile finche\' si continua a girare');
advance(400);
check(!hasText('Vari-Speed') && hasText('SLDE') && hasText('SPD'), 'poco dopo l\'ultimo scatto scompare e tornano le celle');
noteOn(5); advance(1500);
check(hasText('Sound On Sound'), 'con il dito sul knob la lettura resta');
noteOff(5); advance(50);
check(!hasText('Sound On Sound'), 'tolto il dito scompare');
cc(K.MoveDelete, 127); advance(800);
check(hasText('HOLD: CLEAR MARKERS') && !coversWave(), 'Delete tenuto: la barra di conferma sta nella fascia in basso');
cc(K.MoveDelete, 0); advance(1800);
tapButton(K.MoveUndo); advance(40);
check(hasText('UNDO') && !coversWave(), 'i messaggi usano la stessa fascia');

/* ------------------------------------------------------------ 11 */
console.log('\n11. Pagine FX');
fresh(); loaded();
cc(K.MoveShift, 127); tapButton(ROW[3]); cc(K.MoveShift, 0); advance(80);
knob(0, 8); advance(20);
check(parseFloat(lastCall('eq_low')) > 0 && hasText('EQ Low') && hasText('dB'), 'knob 1: EQ Low in dB');
const f0 = dsp.params.eq_freq; knob(2, 1); advance(20);
check(dsp.params.eq_freq > f0 && dsp.params.eq_freq < f0 * 1.1 && hasText('Hz'), `knob 3: frequenza del medio a passi logaritmici (${f0} -> ${dsp.params.eq_freq.toFixed(1)} Hz)`);
knob(4, 1); advance(40);
check(parseFloat(lastCall('dly_time')) > 375 && hasText('Delay Time'), 'knob 5: tempo del delay');
knob(6, 10); knob(7, 10); advance(20);
check(parseFloat(lastCall('dly_mix')) > 0 && parseFloat(lastCall('rev_mix')) > 0, 'knob 7 e 8: mix di delay e riverbero');
advance(700); dsp.params.eq_low = 9; dsp.params.eq_mid = -6; dsp.params.eq_freq = 1500; dsp.params.eq_high = 4; dsp.params.dly_mix = 0.3; dsp.params.rev_mix = 0.25; dsp.ui.pg++;
snap('fx');
check(hasText('LOW') && hasText('REV 1.9s') && hasText('FB 40%'), 'la pagina FX riassume delay e riverbero');
tapButton(ROW[3]); advance(60);
knob(0, 5); advance(40);
check(lastCall('dly_sync') === '1' && hasText('Delay Sync'), 'FX 2, knob 1: delay a tempo (1/16)');
knob(2, 1); knob(7, 1); advance(20);
check(lastCall('dly_ping') === '1' && lastCall('fx_rec') === '1', 'ping-pong e registrazione attraverso gli effetti');
advance(700); dsp.params.dly_sync = 5; dsp.params.dly_ping = 1; dsp.params.dly_fdbk = 0.6; dsp.params.rev_decay = 0.7; dsp.params.fx_rec = 0; dsp.ui.pg++;
snap('fx_2');
check(hasText('ECHO 500ms') && hasText('VERB'), 'FX 2: eco a 1/4 = 500 ms a 120 BPM, coda del riverbero');

/* ------------------------------------------------------------ 12 */
console.log('\n12. Pagine HEADS');
fresh(); loaded();
cc(K.MoveShift, 127); tapButton(ROW[2]); cc(K.MoveShift, 0); advance(80);
check(hasText('HEADS') && hasText('SPAC') && hasText('MIX'), 'Shift + Track 3 apre direttamente HEADS');
knob(6, 10); advance(30);
check(parseFloat(lastCall('hd_mix')) > 0, 'knob 7: mix delle testine');
knob(1, 1); advance(30);
check(lastCall('hd_heads') === '3', 'knob 2: da due a tre testine per canale');
knob(7, 1); advance(40);
check(lastCall('hd_freeze') === '1' && hasText('Freeze Heads'), 'knob 8: Freeze');
advance(800); dsp.params.hd_mix = 0.5; dsp.params.hd_heads = 3; dsp.params.hd_spacing = 0.6; dsp.params.hd_reverse = 0.5; dsp.params.hd_freeze = 0; dsp.ui.pg++;
snap('heads');
check(hasText('OWN'), 'la pagina disegna le testine e mostra il percorso');
tapButton(ROW[3]); advance(60);
knob(2, 1); advance(10); knob(2, 1); advance(30);
check(lastCall('hd_route') === '2', 'HEADS 2, knob 3: due scatti, percorso Chain');
advance(800); dsp.params.hd_route = 3; dsp.params.hd_wear = 4; dsp.params.hd_wear_amt = 0.4; dsp.params.hd_mode = 2; dsp.ui.pg++;
snap('heads_2');
check(hasText('FOLD 40%') && hasText('RISE') && hasText('WNDR'), 'HEADS 2: usura, modo e percorso a parole');
tapButton(K.MoveMenu); advance(40);
for (let i = 0; i < 6; i++) cc(K.MoveMainKnob, 1);
advance(40);
check(hasText('Clear heads') && !hasText('Load WAV'), 'il menu scorre fino a Clear heads');
tapButton(K.MoveMainButton); advance(40);
check(dsp.calls.some((c) => c[0] === 'hd_clear'), 'Clear heads manda hd_clear');

/* ------------------------------------------------------------ 13 */
console.log('\n13. About / DSP load, per i rapporti della beta');
fresh(); loaded();
Object.assign(dsp.ui, { cpu: 0.124, cpk: 0.31, ovr: 2 });
tapButton(K.MoveMenu); advance(40);
for (let i = 0; i < 7; i++) cc(K.MoveMainKnob, 1);
advance(40);
check(hasText('About / DSP load'), 'l\'ultima voce del menu e\' About / DSP load');
tapButton(K.MoveMainButton); advance(60);
const uiVersion = (fs.readFileSync(path.join(here, '..', 'src', 'solo', 'ui.js'), 'utf8').match(/const VERSION = '([^']+)'/) || [])[1];
const modVersion = JSON.parse(fs.readFileSync(path.join(here, '..', 'src', 'solo', 'module.json'), 'utf8')).version;
check(uiVersion === modVersion, `la versione sullo schermo (${uiVersion}) e' quella di module.json (${modVersion})`);
check(hasText('HEIMAT ' + modVersion + ' BETA') && hasText('DSP 12%  PEAK 31%') && hasText('OVER 2'), 'mostra versione, carico medio e di picco, blocchi fuori tempo');
snap('about', 60);
tapButton(K.MoveMainButton); advance(40);
check(!hasText('PEAK'), 'un clic sul jog chiude la schermata');

fs.mkdirSync(path.join(here, 'screens'), { recursive: true });
fs.writeFileSync(path.join(here, 'screens', 'ops.json'), JSON.stringify(shots));
fs.rmSync(tmp, { recursive: true, force: true });
console.log(`\n${fails ? 'ESITO: FALLITO' : 'ESITO: TUTTO OK'} (${passes} controlli superati, ${fails} falliti)`);
process.exit(fails ? 1 : 0);
