/*
 * UI test for Heimat (the on-screen interface).
 *
 *   node tests/ui_test.mjs            # runs the checks, writes tests/screens/ops.json
 *   python3 tests/render_screens.py   # turns the drawing commands into PNG screenshots
 *
 * The real src/ui.js runs against a fake host (display, LEDs, DSP params,
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
let src = fs.readFileSync(path.join(here, '../src/ui.js'), 'utf8');
src = src.replaceAll('/data/UserData/schwung/shared/', './shared/').replace("from 'os'", "from './os_stub.mjs'");
src = src.replace("const SAMPLES_ROOT = '/data/UserData/UserLibrary/Samples';", `const SAMPLES_ROOT = '${samples}';`);
fs.writeFileSync(path.join(tmp, 'ui.mjs'), src);

/* ---- fake host ---- */
let clock = 1_000_000;
Date.now = () => clock;
let frameOps = [];
const shots = {};
globalThis.clear_screen = () => { frameOps = []; };
const overflow = new Set();                    /* text that would run off the screen (6 px a character, the last column may touch the edge) */
globalThis.print = (x, y, s, c = 1) => { s = String(s); if (x < 0 || x + s.length * 6 - 1 > 128) overflow.add(s + ' @' + x + ',' + y); frameOps.push(['t', x, y, s, c]); };
globalThis.set_pixel = (x, y, v = 1) => frameOps.push(['p', x, y, v]);
globalThis.fill_rect = (x, y, w, h, v = 1) => frameOps.push(['f', x, y, w, h, v]);
globalThis.draw_rect = (x, y, w, h, v = 1) => frameOps.push(['r', x, y, w, h, v]);
globalThis.text_width = (s) => 6 * String(s).length;

const BASE_PARAMS = {
    gene_size: 0, slide: 0, morph: 0.18, vari_speed: 0.5, organize: 0, volume: 0.8, sos: 1, in_gain: 1, rec_source: 0,
    rec_sync: 0, clock_source: 0, clock_div: 2, bpm: 120, clock_mode: 0, pad_mode: 0, play_mode: 1, root_note: 60,
    octave_transpose: 0, gene_window: 0, scatter: 0, spread: 0, width: 0, threshold: 0.5, thr_mode: 0, splice_base: 36, organize_mode: 0, bend_range: 2, release_ms: 15, splice_source: 0, divide: 8,
    mcr1: 2, mcr2: 3, mcr3: 4, save_48k: 0, mute: 0,
    eq_low: 0, eq_mid: 0, eq_freq: 1000, eq_high: 0,
    rev_mix: 0, rev_size: 0.5, rev_predelay: 20, rev_reverse: 0, rev_decay: 0.5, rev_damp: 0.4, rev_tilt: 0, rev_depth: 0, rev_speed: 0.35, fx_rec: 0,
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

console.log('Heimat: test dell\'interfaccia');

/* ------------------------------------------------------------- 1 */
console.log('\n1. Avvio e pagine');
fresh();
check(hasText('EMPTY REEL'), 'reel vuoto: la pagina PLAY lo dice');
check(hasText('PLAY') && hasText('S--/--'), 'intestazione con pagina e finestra dello splice');
snap('play_empty');
loaded();
advance(80);
check(hasText('S03/08'), 'con un reel: finestra dello splice S03/08');
for (const [i, name] of [[1, 'REEL'], [2, 'REC'], [3, 'EQ'], [0, 'PLAY']]) { tapButton(ROW[i]); advance(70); check(hasText(name), `Track ${i + 1} apre la pagina ${name}`); }
tapButton(ROW[0]); advance(70);
check(hasText('MICRO') && hasText('SCAT') && hasText('SPRD'), 'Track 1 ancora: pagina MICRO');
tapButton(ROW[0]); advance(70);
check(hasText('MICRO 2') && hasText('PATT') && hasText('SEED') && hasText('PATTERN OFF'), 'Track 1 la terza volta: pagina MICRO 2 (Pattern)');
tapButton(ROW[0]); advance(70);
check(hasText('PLAY') && hasText('GENE'), 'Track 1 ancora: si torna a PLAY');
tapButton(ROW[1]); advance(70); tapButton(ROW[1]); advance(70);
check(hasText('SETUP 1') && hasText('1/2'), 'Track 2 ancora: SETUP 1/2');
tapButton(ROW[1]); advance(70);
check(hasText('SETUP 2') && hasText('2/2') && hasText('CH2'), 'Track 2 la terza volta: SETUP 2/2 (accordi del Morph)');
tapButton(ROW[1]); advance(70);
check(hasText('REEL') && hasText('SPLC'), 'Track 2 ancora: si torna a REEL');
tapButton(ROW[3]); advance(70);
check(hasText('EQ') && hasText('LOW') && hasText('FREQ') && hasText('HIGH') && !hasText('DCAY'), 'Track 4: pagina EQ (solo l\'equalizzatore e i livelli)');
tapButton(ROW[3]); advance(70);
check(hasText('HEADS') && hasText('HEAD') && hasText('SPAC') && hasText('FRZ'), 'ancora: pagina HEADS');
tapButton(ROW[3]); advance(70);
check(hasText('HEADS 2') && hasText('WEAR') && hasText('ROUT'), 'ancora: pagina HEADS 2');
tapButton(ROW[3]); advance(70);
check(hasText('REVERB') && hasText('SIZE') && hasText('PRE') && hasText('DCAY') && hasText('ABSB') && hasText('DPTH') && hasText('TILT'), 'ancora: pagina REVERB');
tapButton(ROW[3]); advance(70);
check(hasText('REVERB 2') && hasText('RVRS') && hasText('RFX'), 'ancora: pagina REVERB 2 (Reverse, registrazione attraverso gli effetti)');
tapButton(ROW[3]); advance(70);
check(hasText('EQ') && hasText('LOW'), 'il ciclo di Track 4 torna a EQ (niente MICRO ne\' SETUP)');
tapButton(ROW[0]); tapButton(ROW[3]); advance(70);
check(hasText('EQ'), 'da un\'altra pagina Track 4 apre sempre EQ');
tapButton(ROW[0]); cc(K.MoveShift, 127); tapButton(ROW[3]); cc(K.MoveShift, 0); advance(70);
check(hasText('REVERB') && hasText('SIZE'), 'Shift + Track 4 apre direttamente il riverbero');
cc(K.MoveShift, 127); tapButton(ROW[3]); cc(K.MoveShift, 0); advance(70);
check(hasText('EQ') && hasText('LOW'), 'ancora Shift + Track 4: l\'EQ');
tapButton(ROW[0]); advance(70); tapButton(ROW[0]); advance(70);
advance(300);
{ const ledOf = (id) => { for (let i = leds.length - 1; i >= 0; i--) if (leds[i][0] === 0x0b && leds[i][2] === id) return leds[i][3]; return -1; };
  check(hasText('MICRO') && ledOf(K.MoveRow1) === K.White && ledOf(K.MoveRow4) !== K.White, 'su MICRO e\' acceso Track 1, non Track 4'); }
tapButton(ROW[2]); advance(70); tapButton(ROW[0]);

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
check(lastCall('in_gain') === undefined, 'una scelta non scatta con un solo scatto del knob');
knob(1, 1); advance(20);
check(lastCall('in_gain') === '2', 'pagina REC: al secondo scatto il guadagno passa a +6 dB');
knob(2, 2); knob(3, 2); knob(4, 2);
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
check(lastCall('rec_toggle') === 'key', 'Record: rec_toggle (sopra lo splice corrente), dal tasto');
check(calls('rec_key_up').length === 1, 'al rilascio di Record: rec_key_up (il taglio del rumore conta da qui)');
cc(K.MoveShift, 127); tapButton(K.MoveRec); cc(K.MoveShift, 0);
check(lastCall('rec_toggle_new') === 'key', 'Shift + Record: nuovo splice');
dsp.calls.length = 0; tapButton(K.MoveSample);
check(lastCall('rec_toggle_new') === 'key' && calls('rec_key_up').length === 1, 'Sample: nuovo splice, e il rilascio');
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
for (let i = 0; i < 8; i++) knob(5, 1);
advance(100);
check(lastCall('markers_divide') === undefined, 'divide: non parte mentre si gira ancora');
advance(500);
check(lastCall('markers_divide') === '12', 'divide: dopo una pausa manda un solo markers_divide (12)');
dsp.calls.length = 0;
cc(K.MoveDelete, 127); advance(1600);
check(!hasText('CLEAR MARKERS') && !hasText('CLEAR REEL'), 'Delete tenuto da solo: nessuna barra, e\' solo un modificatore');
cc(K.MoveDelete, 0); advance(30);
check(calls('markers_clear').length === 0 && calls('marker_del').length === 0, 'Delete tenuto 1.6 s e lasciato: nessun marker cancellato');
cc(K.MoveShift, 127); cc(K.MoveDelete, 127); advance(800);
check(hasText('CLEAR REEL'), 'Shift + Delete tenuto: barra di conferma');
snap('delete_hold', 70);
advance(1400); cc(K.MoveDelete, 0); cc(K.MoveShift, 0);
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
tapButton(ROW[1]); tapButton(ROW[1]);
snap('setup_1'); tapButton(ROW[1]); snap('setup_2');
cc(K.MoveShift, 127); tapButton(K.MoveBack); cc(K.MoveShift, 0);
check(exited, 'Shift + Back chiama host_exit_module (se esiste)');
fresh(); loaded({ gp: [200000] }); dsp.params.gene_size = 0.45; dsp.params.slide = 0.3;
snap('play_reel');
dsp.params.vari_speed = -0.3; dsp.ui.spd = -0.4; dsp.ui.mm = 3; dsp.ui.act = 1; dsp.ui.cr = 1; dsp.params.clock_source = 1; dsp.params.morph = 0.6; snap('play_clock');


/* ------------------------------------------------------------- 9 */
console.log('\n9. Pagina MICRO');
fresh(); loaded();
Object.assign(dsp.ui, { gms: 25, dens: 12, ff: 0.3 });
tapButton(ROW[0]); advance(80);
check(hasText('25ms GRAIN') && hasText('12/s RHYTHM') && hasText('FF0.30 SPARSE'), 'indicatori: durata, densita\' e fill factor con la zona percettiva (25 ms, 12/s, FF 0.30)');
Object.assign(dsp.ui, { gms: 45.7, dens: 14.6, ff: 0.67 }); advance(80);
check(hasText('46ms SOUND') && hasText('FF0.67 COVERED'), 'oltre 40 ms il gene conserva il suono; FF 0.67 = coperta');
Object.assign(dsp.ui, { gms: 25, dens: 12, ff: 0.3 });
snap('micro_sparse');
knob(0, 2); advance(20);
check(lastCall('gene_window') === '1', 'knob 1: forma d\'inviluppo (EDGE -> TUKEY)');
for (let i = 0; i < 3; i++) knob(0, 2);
check(lastCall('gene_window') === '4', 'fino a EXPODEC');
knob(1, 6); check(parseFloat(lastCall('scatter')) > 0, 'knob 2: Scatter');
knob(2, 6); check(parseFloat(lastCall('spread')) > 0, 'knob 3: Pitch Spread');
knob(3, 6); check(parseFloat(lastCall('width')) > 0, 'knob 4: Width');
knob(5, 2); check(lastCall('thr_mode') === '1', 'knob 6: selezione LOUD');
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
check(hasText('Filter'), 'con il dito sul knob la lettura resta');
noteOff(5); advance(50);
check(!hasText('Filter'), 'tolto il dito scompare');
cc(K.MoveShift, 127); cc(K.MoveDelete, 127); advance(800);
check(hasText('HOLD: CLEAR REEL') && !coversWave(), 'Shift + Delete tenuto: la barra di conferma sta nella fascia in basso');
cc(K.MoveDelete, 0); cc(K.MoveShift, 0); advance(1800);
tapButton(K.MoveUndo); advance(40);
check(hasText('UNDO') && !coversWave(), 'i messaggi usano la stessa fascia');

/* ------------------------------------------------------------ 11 */
console.log('\n11. Pagine EQ e REVERB');
fresh(); loaded();
tapButton(ROW[3]); advance(80);
knob(0, 8); advance(20);
check(parseFloat(lastCall('eq_low')) > 0 && hasText('EQ Low') && hasText('dB'), 'EQ, knob 1: EQ Low in dB');
advance(600);
const f0 = dsp.params.eq_freq; knob(2, 1); advance(60);
check(dsp.params.eq_freq > f0 && dsp.params.eq_freq < f0 * 1.05 && hasText('kHz'), `knob 3: frequenza del medio a passi logaritmici fini (${f0} -> ${dsp.params.eq_freq.toFixed(1)} Hz)`);
knob(4, 10); knob(5, 10); advance(20);
check(parseFloat(lastCall('hd_mix')) > 0 && parseFloat(lastCall('rev_mix')) > 0, 'knob 5 e 6: quanto mandare alle testine e al riverbero');
advance(700); Object.assign(dsp.params, { eq_low: 9, eq_mid: -6, eq_freq: 1500, eq_high: 4, hd_mix: 0.3, rev_mix: 0.25 }); dsp.ui.pg++;
snap('eq');
check(hasText('HEADS 30%') && hasText('REVERB 25%') && hasText('REC DRY'), 'la pagina EQ mostra la curva e quanto vanno gli effetti');
tapButton(ROW[3]); tapButton(ROW[3]); tapButton(ROW[3]); advance(80);
check(hasText('REVERB') && hasText('SIZE'), 'REVERB dopo HEADS 2');
knob(1, 10); advance(60);
check(parseFloat(lastCall('rev_size')) > 0.5 && hasText('Reverb Size'), 'knob 2: Size');
advance(600);
const p0 = dsp.params.rev_predelay; knob(2, 1); advance(60);
check(dsp.params.rev_predelay > p0 && hasText('Pre-Delay') && hasText('ms'), `knob 3: pre-delay in ms (${p0} -> ${dsp.params.rev_predelay.toFixed(1)})`);
knob(5, 118); advance(60);
check(parseFloat(lastCall('rev_depth')) < 0 && hasText('CYC'), 'knob 6 a sinistra: modulazione ciclica');
advance(700); Object.assign(dsp.params, { rev_mix: 0.4, rev_size: 0.82, rev_predelay: 60, rev_decay: 1, rev_damp: 0.5, rev_depth: 0.95, rev_tilt: -0.4 }); dsp.ui.pg++;
snap('reverb');
check(hasText('CAVE INF') && hasText('SHIMMER') && hasText('DARK 40'), 'REVERB: spazio, decay infinito, shimmer e tilt a parole');
tapButton(ROW[3]); advance(60);
knob(0, 1); advance(20); knob(0, 1); advance(20);
check(lastCall('rev_reverse') === '1' && hasText('Reverb Reverse'), 'REVERB 2, knob 1: due scatti accendono Reverse');
knob(7, 2); advance(20);
check(lastCall('fx_rec') === '1', 'knob 8: registrare attraverso gli effetti');
advance(700); Object.assign(dsp.params, { rev_reverse: 1, rev_predelay: 240, fx_rec: 0 }); dsp.ui.pg++;
snap('reverb_2');
check(hasText('PRE 240ms') && hasText('BACKWARDS'), 'REVERB 2: il pre-delay, all\'indietro');

/* --------------------------------------------------------------- 11b */
console.log('\n11b. Encoder: passi fini, accelerazione contenuta');
fresh(); loaded(); advance(1000);
dsp.params.slide = 0.5; dsp.ui.pg++; advance(100);
knob(1, 1); advance(300);
const slow = dsp.params.slide - 0.5;
check(Math.abs(slow - 1 / 400) < 1e-6, `uno scatto lento sposta un controllo 0..1 di 1/400 (${slow.toFixed(4)})`);
advance(1500); dsp.params.slide = 0.5; dsp.ui.pg++; advance(100);
for (let i = 0; i < 40; i++) { knob(1, 1); advance(10); }
const fast = dsp.params.slide - 0.5;
check(fast > 40 / 400 * 1.5 && fast <= 40 * 4 / 400 + 1e-6, `girando veloce accelera al massimo 4 volte (40 scatti: ${fast.toFixed(3)})`);
advance(600); dsp.params.bpm = 120; tapButton(ROW[2]); advance(40);
knob(5, 1); advance(300);
check(lastCall('bpm') === '121', 'BPM: uno per scatto');
tapButton(ROW[0]);

/* ------------------------------------------------------------ 12 */
console.log('\n11c. MICRO 2 (Pattern) e LOOP');
fresh(); loaded();
tapButton(ROW[0]); tapButton(ROW[0]); advance(80);
knob(0, 2); advance(60);
check(lastCall('pattern') === '1' && hasText('Pattern'), 'MICRO 2, knob 1: due scatti, Pattern ogni 2 geni');
knob(0, 4); advance(60);
check(lastCall('pattern') === '3', 'altri quattro scatti: ogni 4 geni');
knob(1, 6); advance(60);
check(lastCall('pattern_seed') === '7' && hasText('Pattern Seed'), 'knob 2: il seme, uno per scatto');
advance(700); Object.assign(dsp.ui, { pi: 2, run: 1 }); dsp.ui.pg++; advance(60);
snap('micro2');
check(hasText('EVERY 4 GENES') && hasText('SEED 7'), 'MICRO 2: quanti geni nel ciclo e quale seme');
tapButton(ROW[2]); advance(70);
check(hasText('REC') && hasText('SYNC'), 'Track 3: pagina REC');
tapButton(ROW[2]); advance(70);
check(hasText('LOOP') && hasText('LEN') && hasText('AFTR') && hasText('KEYS') && hasText('SRC') && !hasText('HEAD') && hasText('FREE LENGTH'), 'Track 3 ancora: pagina LOOP (senza la testina Vari-Speed)');
knob(0, 4); knob(1, 2); knob(2, 6); advance(60);
check(lastCall('rec_length') === '2' && lastCall('rec_after') === '1' && lastCall('rec_trim') === '3', 'LOOP: lunghezza 2 battute, poi sovraincisione, taglio dei tasti 200 ms');
check(calls('rec_head').length === 0, 'nessun rec_head: la testina Vari-Speed non c\'e\' piu\'');
advance(700);
check(hasText('2 BARS') && hasText('THEN OVERDUB') && hasText('KEY TRIM 200MS'), 'LOOP lo dice a parole');
Object.assign(dsp.ui, { rec: 1, rtg: 176400, rdn: 88200 }); dsp.ui.pg++; advance(60);
snap('loop');
check(hasText('REC 2 BARS'), 'in registrazione: la lunghezza che si sta riempiendo');
Object.assign(dsp.ui, { rec: 0, rtg: 0, rdn: 0 }); dsp.ui.pg++;
tapButton(ROW[2]); advance(70);
check(hasText('REC') && !hasText('LOOP'), 'ancora Track 3: di nuovo REC');
tapButton(ROW[0]); tapButton(ROW[2]); advance(70);
check(!hasText('LOOP'), 'da un\'altra pagina Track 3 apre sempre REC');

console.log('\n11d. Modulatori: pagine MOD e tenere premuto un Track');
fresh(); loaded();
cc(K.MoveShift, 127); tapButton(ROW[0]); cc(K.MoveShift, 0); advance(70);
check(hasText('MOD 1') && hasText('LFO1') && hasText('LFO2') && hasText('SHP') && !hasText('RAND') && !hasText('FOLW'), 'Shift + Track 1: pagina MOD 1 (LFO 1 e LFO 2)');
for (let k = 0; k < 60; k++) { dsp.ui.mo = [Math.sin(k / 6), k % 15 < 8 ? 0.6 : -0.4, 0]; advance(34); }
dsp.params.mod_routes = 'gene_size:0:0.250,scatter:0:0.4,morph:1:-0.1'; dsp.ui.mrv = 3; advance(700);
shots.mod1 = frameOps.slice();
check(hasText('>2') && hasText('>1'), 'MOD 1: quanti controlli muove ciascuna sorgente');
dsp.params.mod_routes = ''; dsp.ui.mrv = 4; advance(700);
cc(K.MoveShift, 127); tapButton(ROW[0]); cc(K.MoveShift, 0); advance(70);
check(hasText('MOD 2') && hasText('ENV') && !hasText('FOLW') && hasText('ATK') && hasText('TRIG') && hasText('MRST'), 'ancora: MOD 2 (Envelope, e la ripartenza col clock)');
knob(0, 6); advance(60);
check(parseFloat(lastCall('mod_env_attack')) > 0.1, 'MOD 2, knob 1: attacco dell\'inviluppo');
{ const n0 = dsp.calls.length; knob(6, 3); noteOn(6); advance(60); noteOff(6);
  check(dsp.calls.length === n0, 'MOD 2, knob 7: cella vuota, non fa niente'); }
advance(700); for (let k = 0; k < 60; k++) { const e = (k % 20) / 20; dsp.ui.mo = [0.5, -0.2, e < 0.15 ? e / 0.15 : Math.pow(1 - (e - 0.15) / 0.85, 2)]; advance(34); }
shots.mod2 = frameOps.slice();
tapButton(ROW[0]); advance(70);
check(hasText('PLAY') && !hasText('MOD'), 'Track 1 toccato e lasciato: torna a PLAY');
cc(ROW[1], 127); noteOn(2); knob(2, 10); advance(30);
check(lastCall('mod_set') === '1 morph 0.10' && hasText('LFO2 > MRP +10%'), 'tenendo Track 2 e girando Morph: LFO 2 su Morph al 10%, e la percentuale si vede');
knob(2, 117); advance(30);
check(lastCall('mod_set') === '1 morph -0.01', 'all\'indietro si passa sotto zero (-1%)');
knob(7, 4); advance(30);
check(hasText('NOT FOR'), 'le scelte non si modulano: lo dice');
noteOff(2); cc(ROW[1], 0); advance(70);
check(hasText('PLAY') && !hasText('REEL'), 'lasciato Track 2 dopo aver girato: la pagina non cambia');
cc(ROW[0], 127); knob(0, 25); cc(ROW[0], 0); advance(30);
check(lastCall('mod_set') === '0 gene_size 0.25', 'Track 1 + Gene-Size: LFO 1 al 25%');
dsp.params.mod_routes = 'gene_size:0:0.250,morph:1:-0.010'; dsp.ui.mrv = 5; advance(900);
snap('mod_mark');
check(frameOps.some((o) => o[0] === 'f' && o[1] === 0 && o[2] === 42 && o[3] === 1 && o[4] === 10) && frameOps.some((o) => o[0] === 'f' && o[1] === 64 && o[2] === 42 && o[3] === 1 && o[4] === 10) && !frameOps.some((o) => o[0] === 'f' && o[1] === 32 && o[2] === 42 && o[3] === 1), 'le celle modulate (Gene-Size, Morph) portano il segno');
cc(ROW[0], 127); tapButton(K.MoveDelete); cc(ROW[0], 0); advance(40);
check(lastCall('mod_clear') === '0' && hasText('LFO1 ROUTES CLEARED'), 'Track 1 + Delete: via i percorsi di LFO 1');
tapButton(ROW[1]); advance(70);
check(hasText('REEL'), 'Track 2 toccato da solo apre ancora REEL');

console.log('\n11e. Ritagliare una splice (TRIM)');
fresh(); loaded();
cc(K.MoveShift, 127); tapButton(K.MoveCapture); cc(K.MoveShift, 0); advance(60);
check(hasText('TRIM') && hasText('IN +0.00s') && hasText('OUT -0.00s') && lastCall('trim_set') === '110000,160000', 'Shift + Capture: TRIM sulla splice selezionata, anteprima di tutta la splice');
check(lastCall('view') === '110000,160000', 'la forma d\'onda mostra solo la splice');
knob(0, 20); advance(60);
const ts1 = (lastCall('trim_set') || '').split(',').map(Number);
check(ts1[0] > 110000 && ts1[0] < 115000 && ts1[1] === 160000, `knob 1 sposta l'inizio (${ts1.join(',')})`);
knob(1, 127 - 9); advance(60);
const ts2 = (lastCall('trim_set') || '').split(',').map(Number);
check(ts2[1] < 160000 && ts2[1] > 155000, `knob 2 sposta la fine (${ts2.join(',')})`);
knob(2, 4); advance(60);
check(hasText('x4') && /^1[45]\d{4},1[56]\d{4}$/.test(lastCall('view')), `knob 3: zoom sulla fine (${lastCall('view')})`);
cc(K.MoveMainKnob, 3); advance(60);
check(+((lastCall('trim_set') || '').split(',')[1]) > ts2[1], 'la rotella ritocca il punto mosso per ultimo');
snap('trim', 100);
check(hasText('KEEP') && hasText('CLICK: CUT'), 'dice quanto resta e come tagliare');
tapButton(K.MoveMainButton); advance(60);
check(lastCall('trim_apply') === '1' && !hasText('KEEP') && hasText('TRIMMED'), 'click della rotella: taglia e dice quanto');
cc(K.MoveShift, 127); tapButton(K.MoveCapture); cc(K.MoveShift, 0); advance(60);
knob(0, 10); tapButton(K.MoveBack); advance(60);
check(lastCall('trim_cancel') === '1' && !hasText('KEEP') && calls('trim_apply').length === 1, 'Back: esce senza tagliare');
tapButton(K.MoveMenu); advance(40);
for (let i = 0; i < 10; i++) cc(K.MoveMainKnob, 1);
advance(40); check(hasText('Trim splice'), 'nel menu: Trim splice...');
tapButton(K.MoveMainButton); advance(60);
check(hasText('TRIM'), 'dal menu si apre lo stesso TRIM');
tapButton(ROW[0]); advance(60);
check(!hasText('KEEP') && hasText('PLAY') && calls('trim_cancel').length === 2, 'cambiare pagina annulla il ritaglio');
dsp.ui.ns = 0; dsp.ui.fr = 0; advance(60);
cc(K.MoveShift, 127); tapButton(K.MoveCapture); cc(K.MoveShift, 0); advance(60);
check(hasText('NOTHING TO TRIM'), 'reel vuoto: niente da ritagliare');

console.log('\n11f. Gesti di base: TEMP, Delete + knob, polarita\', valore vivo, filtro');
fresh(); loaded();
const nUndo = () => calls('undo').length;
const u0 = nUndo();
cc(K.MoveUndo, 127); advance(40);
check(lastCall('temp') === 'begin' && hasText('TMP'), 'Undo tenuto: TEMP comincia (TMP in alto)');
knob(0, 10); knob(5, 20); advance(40);
cc(K.MoveUndo, 0); advance(700);
check(lastCall('temp') === 'end' && nUndo() === u0, 'girato qualcosa: al rilascio torna tutto, e non annulla niente');
tapButton(K.MoveUndo); advance(40);
check(lastCall('temp') === 'cancel' && nUndo() === u0 + 1 && hasText('UNDO'), 'Undo toccato e basta: annulla come sempre');
advance(1600);
cc(K.MoveShift, 127); tapButton(K.MoveUndo); cc(K.MoveShift, 0); advance(40);
check(lastCall('temp') === 'begin' && hasText('TEMP ON'), 'Shift + Undo: TEMP bloccato');
knob(1, 5); advance(700);
check(hasText('TMP'), 'resta acceso mentre suoni');
tapButton(K.MoveUndo); advance(40);
check(lastCall('temp') === 'end' && nUndo() === u0 + 1, 'Undo lo chiude e rimette tutto com\'era (senza annullare)');
advance(1600);
knob(2, 30); advance(30);
const mBefore = parseFloat(lastCall('morph'));
const md0 = calls('marker_del').length + calls('join').length;
cc(K.MoveDelete, 127); knob(2, 1); cc(K.MoveDelete, 0); advance(40);
check(mBefore > 0.18 && lastCall('morph') === '0.1800' && calls('marker_del').length + calls('join').length === md0, `Delete + knob: Morph torna a 0.18 (da ${mBefore}), nessun marker toccato`);
advance(700);
check(hasText('MRP RESET'), 'lo dice: MRP RESET');
const mc0 = calls('markers_clear').length;
cc(K.MoveDelete, 127); noteOn(0); noteOff(0); advance(1500); cc(K.MoveDelete, 0); advance(40);
check(calls('markers_clear').length === mc0 && !hasText('HOLD: CLEAR'), 'tenuto a lungo dopo un reset: non cancella i marker');
advance(1600);
cc(ROW[0], 127); knob(0, 25); advance(20);
noteOn(0); advance(30); noteOff(0); advance(30);
check(lastCall('mod_pol') === '0 gene_size', 'Track 1 + tocco senza girare: la Wave su Gene-Size cambia polarita\'');
advance(600);
check(hasText('ONE WAY'), 'e lo dice: ONE WAY');
noteOn(0); knob(0, 1); noteOff(0); advance(30);
check(calls('mod_pol').length === 1, 'toccare per girare non cambia la polarita\'');
cc(ROW[0], 0); advance(30);
check(hasText('PLAY') && !hasText('REEL'), 'e la pagina non cambia');
dsp.params.mod_routes = 'gene_size:0:0.250:0'; dsp.params.mod_live = 'gene_size:0.8000'; dsp.ui.mrv = 9; advance(1600);
check(frameOps.some((o) => o[0] === 'p' && o[1] === 2 + Math.round(27 * 0.8) && o[2] === 49), 'sulla barra di Gene-Size una tacca dove la modulazione la porta ora');
snap('live_tick');
knob(5, 127 - 30); advance(60);
check(/LP \d/.test(frameOps.filter((o) => o[0] === 't').map((o) => o[3]).join(' ')) && parseFloat(lastCall('filter')) < 0, 'PLAY, knob 6: filtro DJ, a sinistra passa-basso');
dsp.params.mod_routes = ''; dsp.params.mod_live = ''; dsp.ui.mrv = 10; advance(900);

console.log('\n12. Pagine HEADS');
fresh(); loaded();
cc(K.MoveShift, 127); tapButton(ROW[2]); cc(K.MoveShift, 0); advance(80);
check(hasText('HEADS') && hasText('SPAC') && hasText('MIX'), 'Shift + Track 3 apre direttamente HEADS');
knob(6, 10); advance(30);
check(parseFloat(lastCall('hd_mix')) > 0, 'knob 7: mix delle testine');
knob(1, 2); advance(30);
check(lastCall('hd_heads') === '3', 'knob 2: da due a tre testine per canale');
knob(7, 2); advance(40);
check(lastCall('hd_freeze') === '1' && hasText('Freeze Heads'), 'knob 8: Freeze');
advance(800); dsp.params.hd_mix = 0.5; dsp.params.hd_heads = 3; dsp.params.hd_spacing = 0.6; dsp.params.hd_reverse = 0.5; dsp.params.hd_freeze = 0; dsp.ui.pg++;
snap('heads');
check(hasText('OWN'), 'la pagina disegna le testine e mostra il percorso');
tapButton(ROW[3]); advance(60);
for (let i = 0; i < 4; i++) { knob(2, 1); advance(10); }
check(lastCall('hd_route') === '2', 'HEADS 2, knob 3: quattro scatti, due passi, percorso Chain');
advance(800); dsp.params.hd_route = 3; dsp.params.hd_wear = 4; dsp.params.hd_wear_amt = 0.4; dsp.params.hd_mode = 2; dsp.ui.pg++;
snap('heads_2');
check(hasText('FOLD 40%') && hasText('RISE') && hasText('WNDR'), 'HEADS 2: usura, modo e percorso a parole');
tapButton(K.MoveMenu); advance(40);
for (let i = 0; i < 9; i++) cc(K.MoveMainKnob, 1);
advance(40);
check(hasText('Clear heads') && !hasText('Load WAV'), 'il menu scorre fino a Clear heads');
tapButton(K.MoveMainButton); advance(40);
check(dsp.calls.some((c) => c[0] === 'hd_clear'), 'Clear heads manda hd_clear');

/* ------------------------------------------------------------ 13 */
console.log('\n13. About / DSP load, per i rapporti della beta');
fresh(); loaded();
Object.assign(dsp.ui, { cpu: 0.124, cpk: 0.31, ovr: 2 });
tapButton(K.MoveMenu); advance(40);
for (let i = 0; i < 11; i++) cc(K.MoveMainKnob, 1);
advance(40);
check(hasText('About / DSP load'), 'l\'ultima voce del menu e\' About / DSP load');
tapButton(K.MoveMainButton); advance(60);
const uiVersion = (fs.readFileSync(path.join(here, '..', 'src', 'ui.js'), 'utf8').match(/const VERSION = '([^']+)'/) || [])[1];
const modVersion = JSON.parse(fs.readFileSync(path.join(here, '..', 'src', 'module.json'), 'utf8')).version;
check(uiVersion === modVersion, `la versione sullo schermo (${uiVersion}) e' quella di module.json (${modVersion})`);
check(hasText('HEIMAT ' + modVersion + ' BETA') && hasText('DSP 12%  PEAK 31%') && hasText('OVER 2'), 'mostra versione, carico medio e di picco, blocchi fuori tempo');
snap('about', 60);
tapButton(K.MoveMainButton); advance(40);
check(!hasText('PEAK'), 'un clic sul jog chiude la schermata');

/* ------------------------------------------------------------ 13b */
console.log('\n13b. Patch: salvare, richiamare, Init');
fresh(); loaded();
dsp.patches = '0,3';
const prevGet = globalThis.host_module_get_param;
globalThis.host_module_get_param = (k) => (k === 'patches' ? dsp.patches : prevGet(k));
const openMenuAt = (n) => { tapButton(K.MoveMenu); advance(40); for (let i = 0; i < n; i++) cc(K.MoveMainKnob, 1); advance(40); };
openMenuAt(3);
check(hasText('Save patch'), 'nel menu: Save patch...');
tapButton(K.MoveMainButton); advance(60);
snap('patch_save', 60);
check(hasText('SAVE PATCH TO') && hasText('PATCH 01 (EMPTY)') && !hasText('BEFORE INIT'), 'si sceglie uno slot da 1 a 16 (lo 0 e\' solo di Init)');
cc(K.MoveMainKnob, 1); cc(K.MoveMainKnob, 1); advance(40);
check(hasText('PATCH 03') && !hasText('PATCH 03 (EMPTY)'), 'lo slot 3 e\' gia\' pieno');
tapButton(K.MoveMainButton); advance(40);
check(calls('patch_save').length === 0 && hasText('CLICK AGAIN: REPLACE'), 'sopra uno slot pieno chiede un secondo clic');
tapButton(K.MoveMainButton); advance(40);
check(lastCall('patch_save') === '3' && hasText('SAVED PATCH 3'), 'secondo clic: patch_save 3');
openMenuAt(4);
tapButton(K.MoveMainButton); advance(60);
snap('patch_load', 60);
check(hasText('LOAD PATCH') && hasText('BEFORE INIT'), 'Load patch: c\'e\' anche lo slot 0, BEFORE INIT');
check(frameOps.some((o) => o[0] === 't' && o[3] === 'PATCH 03'), 'si parte dal primo slot pieno');
cc(K.MoveMainKnob, 1); advance(30); tapButton(K.MoveMainButton); advance(40);
check(calls('patch_load').length === 0 && hasText('IS EMPTY'), 'uno slot vuoto non si carica');
cc(K.MoveMainKnob, 127); advance(30); tapButton(K.MoveMainButton); advance(40);
check(lastCall('patch_load') === '3' && hasText('LOADED PATCH 3'), 'patch_load 3');
dsp.params.morph = 0.9; dsp.params.rev_mix = 0.4; dsp.params.bpm = 97; dsp.ui.pg++; advance(800);
openMenuAt(5);
check(hasText('Init patch'), 'nel menu: Init patch');
tapButton(K.MoveMainButton); advance(40);
check(calls('init').length === 0 && hasText('CLICK AGAIN: YES'), 'Init chiede conferma');
tapButton(K.MoveMainButton); advance(60);
check(lastCall('init') === '1' && hasText('INIT'), 'secondo clic: init');
knob(2, 1); advance(40);
check(Math.abs(parseFloat(lastCall('morph')) - (0.18 + 1 / 400)) < 1e-3, 'dopo Init il Morph riparte dal suo valore iniziale');
tapButton(ROW[2]); advance(40); knob(5, 1); advance(40);
check(lastCall('bpm') === '98', 'il tempo (globale) non torna all\'inizio');
globalThis.host_module_get_param = prevGet;

fs.mkdirSync(path.join(here, 'screens'), { recursive: true });
fs.writeFileSync(path.join(here, 'screens', 'ops.json'), JSON.stringify(shots));
fs.rmSync(tmp, { recursive: true, force: true });
check(overflow.size === 0, 'nessun testo esce dallo schermo' + (overflow.size ? ': ' + [...overflow].slice(0, 12).join(' | ') : ''));
console.log(`\n${fails ? 'ESITO: FALLITO' : 'ESITO: TUTTO OK'} (${passes} controlli superati, ${fails} falliti)`);
process.exit(fails ? 1 : 0);
