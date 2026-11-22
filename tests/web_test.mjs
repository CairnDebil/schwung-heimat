// Heimat browser editor: tests in a real browser (Playwright + Chromium).
//
// The editor (src/web/web_ui.html) is loaded three ways:
//   demo   no Schwung around (as on the manual's site)
//   solo   as in schwung-manager's Tool tab, fed with the "state" that the real
//          DSP produced in tests/render_test.c (test 38), exploded into
//          overtake_dsp:<key> strings exactly as the manager does
// Screenshots go to docs/screens/ when SCREENS=1.
//
//   node tests/web_test.mjs
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');   /* CommonJS lookup, so a global install works too */
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, '..');
const url = pathToFileURL(path.join(root, 'src/web/web_ui.html')).href;
const shots = process.env.SCREENS === '1' ? path.join(root, 'docs/screens') : null;
const STATE = '/tmp/heimat_remote_state.json';

let fail = 0, pass = 0;
function check(ok, msg) { console.log(`  [${ok ? 'ok' : 'FAIL'}] ${msg}`); ok ? pass++ : fail++; }

/* what schwung-manager's fetchAllParams does with a "state" snapshot */
function explode(obj, prefix) {
  const out = {};
  for (const [k, v] of Object.entries(obj)) {
    if (typeof v === 'string') out[prefix + k] = v;
    else if (typeof v === 'number') out[prefix + k] = String(v);
    else if (typeof v === 'boolean') out[prefix + k] = v ? '1' : '0';
  }
  return out;
}

const MOCK = (component) => `
  window.__calls = [];
  window.schwungRemote = {
    component: ${JSON.stringify(component)},
    setParam(k, v) { window.__calls.push([k, String(v)]); },
    getParam(k) { return Promise.resolve(''); },
    onParamChange(cb) { window.__cb = cb; },
    getHierarchy() { return Promise.resolve(null); },
    getChainParams() { return Promise.resolve(null); },
    resubscribe() { window.__resub = (window.__resub || 0) + 1; },
  };`;

async function open(browser, opts = {}) {
  const ctx = await browser.newContext({
    viewport: opts.viewport || { width: 1280, height: 900 },
    deviceScaleFactor: 1,
    colorScheme: opts.scheme || 'light',
    reducedMotion: 'reduce',
  });
  const page = await ctx.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error' && !/schwung-remote-api|ERR_FILE_NOT_FOUND|fonts\.g|net::/.test(m.text())) errors.push(m.text()); });
  if (opts.component) await page.addInitScript(MOCK(opts.component));
  await page.goto(url + (opts.query || ''));
  await page.waitForTimeout(300);
  return { ctx, page, errors };
}
const txt = (page, sel) => page.$eval(sel, (e) => e.textContent.trim());
const calls = (page) => page.evaluate(() => window.__calls);
async function inkPixels(page, sel) {
  return page.$eval(sel, (cv) => {
    const g = cv.getContext('2d'), d = g.getImageData(0, 0, cv.width, cv.height).data;
    let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++;
    return n / (cv.width * cv.height);
  });
}
async function noHScroll(page) {
  return page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1);
}
async function snap(page, name, full = true) {
  if (!shots) return;
  /* the manual's own pages are only for looking at, so they stay out of the repo */
  const dir = name.startsWith('manual_') ? fs.mkdtempSync('/tmp/heimat-manual-') : shots;
  await page.screenshot({ path: path.join(dir, name + '.png'), fullPage: full });
}

const browser = await chromium.launch();
console.log('Heimat: editor nel browser\n');

/* ------------------------------------------------------------------ 1 */
console.log('1. Parametri allineati al DSP');
{
  const html = fs.readFileSync(path.join(root, 'src/web/web_ui.html'), 'utf8');
  const json = JSON.parse(html.split('<script id="heimat-params" type="application/json">')[1].split('</script>')[0]);
  const c = fs.readFileSync(path.join(root, 'src/dsp/heimat.c'), 'utf8');
  const keys = [...c.matchAll(/\[P_\w+\]\s*=\s*P[FIE]\("(\w+)"/g)].map((m) => m[1]);
  check(json.length === keys.length && keys.every((k, i) => json[i].key === k), `l'editor conosce gli stessi ${keys.length} parametri del DSP, nello stesso ordine`);
  const demo = fs.readFileSync(path.join(root, 'docs/editor.html'), 'utf8');
  check(demo === html, 'docs/editor.html (la demo del manuale) e\' identico all\'editor');
}

/* ------------------------------------------------------------------ 2 */
console.log('\n2. Demo (nessun Move collegato)');
{
  const { ctx, page, errors } = await open(browser);
  await page.waitForTimeout(500);
  check((await txt(page, '#whereTxt')).startsWith('Demo'), 'senza Schwung parte in demo e lo dice');
  check(await inkPixels(page, '#reel') > 0.08, 'il reel di prova e\' disegnato');
  check((await txt(page, '#roSplice')) === '1 of 8', 'otto splice, il primo selezionato');
  const m0 = await page.evaluate(() => window.heimatEditor.P.morph);
  await page.focus('[aria-label="Morph"]');
  for (let i = 0; i < 20; i++) await page.keyboard.press('ArrowUp');
  await page.waitForTimeout(100);                 /* the screen redraws on the next animation frame */
  const m1 = await page.evaluate(() => window.heimatEditor.P.morph);
  check(Math.abs(m1 - m0 - 0.2) < 0.001, `tastiera sul knob Morph: ${m0.toFixed(2)} -> ${m1.toFixed(2)}`);
  check((await txt(page, '#morphStage')) === 'Overlap ×1.6', `lo stadio di Morph si aggiorna (${await txt(page, '#morphStage')})`);
  await page.keyboard.press('End');
  check((await page.evaluate(() => window.heimatEditor.P.morph)) === 1, 'End porta al massimo');
  await page.dblclick('[aria-label="Morph"]');
  check((await page.evaluate(() => window.heimatEditor.P.morph)) === 0.18, 'doppio clic: torna al valore iniziale');
  const box = await page.$eval('[aria-label="Gene-Size"]', (e) => { const r = e.getBoundingClientRect(); return { x: r.x + r.width / 2, y: r.y + r.height / 2 }; });
  await page.mouse.move(box.x, box.y); await page.mouse.down(); await page.mouse.move(box.x, box.y - 60, { steps: 6 }); await page.mouse.up();
  const gs = await page.evaluate(() => window.heimatEditor.P.gene_size);
  check(gs > 0.25 && gs < 0.4, `trascinando in su Gene-Size sale (${gs.toFixed(3)})`);
  check(/ms/.test(await page.$eval('[aria-label="Gene-Size"]', (e) => e.getAttribute('aria-valuetext'))), 'Gene-Size si legge in millisecondi');
  await page.waitForTimeout(400);
  check(await inkPixels(page, '#train') > 0.02, 'il grafico dei gene nel tempo e\' disegnato');
  /* EQ: drag the mid point */
  const eq = await page.$eval('#eq', (e) => { e.scrollIntoView({ block: 'center' }); const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, width: r.width, height: r.height }; });
  const midX = eq.x + 8 + (eq.width - 16) * Math.log(1000 / 30) / Math.log(16000 / 30), midY = eq.y + eq.height / 2;
  await page.mouse.move(midX, midY); await page.mouse.down(); await page.mouse.move(midX + 40, midY + 25, { steps: 5 }); await page.mouse.up();
  const P = await page.evaluate(() => window.heimatEditor.P);
  check(P.eq_mid < -3 && P.eq_freq > 1300, `EQ: trascinando il punto centrale scendono i medi (${P.eq_mid.toFixed(1)} dB) e sale la frequenza (${Math.round(P.eq_freq)} Hz)`);
  /* click the fourth splice on the reel */
  await page.evaluate(() => window.scrollTo(0, 0));
  const reel = await page.$eval('#reel', (e) => { const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; });
  await page.mouse.click(reel.x + 6 + (reel.w - 12) * (4.4 / 11), reel.y + reel.h * 0.8);
  await page.waitForTimeout(100);
  check((await txt(page, '#roSplice')) === '4 of 8', `clic sul reel seleziona lo splice (${await txt(page, '#roSplice')})`);
  /* the reverb: its own panel; the top of Decay reads as forever, Depth by what it does */
  await page.evaluate(() => { const H = window.heimatEditor; H.send('rev_decay', 1); H.send('rev_depth', -0.5); H.send('rev_size', 0.85); });
  await page.waitForTimeout(150);
  check((await page.$eval('[aria-label="Decay"]', (e) => e.getAttribute('aria-valuetext'))) === 'Forever', 'Decay in cima: "Forever"');
  check((await page.$eval('[aria-label="Depth"]', (e) => e.getAttribute('aria-valuetext'))) === 'Chorus 50%' &&
        (await page.$eval('[aria-label="Size"]', (e) => e.getAttribute('aria-valuetext'))) === 'Cave', 'Depth e Size si leggono a parole (Chorus 50%, Cave)');
  await page.click('[aria-label="Reverse: the pre-delay backwards"] >> text=On');
  await page.waitForTimeout(100);
  check((await page.evaluate(() => window.heimatEditor.P.rev_reverse)) === 1, 'Reverse si accende dal pannello del riverbero');
  check(await inkPixels(page, '#rev') > 0.01 && (await page.$$('#dly')).length === 0, 'il riverbero e\' disegnato; il vecchio delay non c\'e\' piu\'');
  check(errors.length === 0, 'nessun errore JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await page.evaluate(() => { const H = window.heimatEditor; Object.assign(H.P, { morph: 0.62, gene_size: 0.3, gene_window: 1, scatter: 0.3, rev_mix: 0.3, rev_decay: 0.62, rev_size: 0.7, rev_depth: 0.4, hd_mix: 0.3, eq_low: 3, eq_mid: -5, eq_freq: 1800, eq_high: 2 }); H.send('morph', 0.62); });
  await page.waitForTimeout(600);
  await snap(page, 'editor_demo');
  await ctx.close();
}

/* ------------------------------------------------------------------ 3 */
console.log('\n3. Heimat nella scheda Tool (stato reale dal DSP)');
{
  const state = JSON.parse(fs.readFileSync(STATE, 'utf8'));
  const { ctx, page, errors } = await open(browser, { component: 'tool' });
  check((await txt(page, '#whereTxt')).startsWith('Waiting'), 'prima dei dati: in attesa di Heimat');
  await page.evaluate((p) => window.__cb(p), explode(state, 'overtake_dsp:'));
  await page.waitForTimeout(150);
  check((await txt(page, '#whereTxt')) === 'Heimat, live', 'arrivato lo stato: Heimat, live');
  check((await txt(page, '#roSplice')) === '1 of 4', `quattro splice dai marker del file (${await txt(page, '#roSplice')})`);
  check((await txt(page, '#roLen')) === '2.00 s', `lunghezza dello splice (${await txt(page, '#roLen')})`);
  const gene = await txt(page, '#roGene');
  check(gene.startsWith(state.v_gms.toFixed(0)) || gene.includes(String(Math.round(state.v_gms))), `durata del gene dal DSP (${gene})`);
  check(/GRAIN|SOUND|CLICK/.test(gene) && /RHYTHM|FLUTTER|FLOW|TEXTURE|MASS/.test(await txt(page, '#roDens')), 'etichette percettive del cloud');
  check(await inkPixels(page, '#reel') > 0.08, 'il reel arriva dalla panoramica v_pk');
  check((await page.$eval('[aria-label="Morph"]', (e) => +e.getAttribute('aria-valuenow'))) === state.morph, 'i knob partono dai valori del DSP');
  check(await page.$eval('#bPlay', (e) => e.getAttribute('aria-pressed')) === 'true', 'Play acceso: i gene stanno suonando');

  await page.focus('[aria-label="Morph"]'); await page.keyboard.press('ArrowUp');
  let c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:morph' && Math.abs(+v - (state.morph + 0.01)) < 1e-4), 'un knob scrive overtake_dsp:morph');
  await page.click('[aria-label="Pad Mode"] >> text=Splice');
  c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:pad_mode' && v === '1'), 'una scelta scrive l\'indice (pad_mode = 1)');
  await page.evaluate(() => window.scrollTo(0, 0));
  const reel = await page.$eval('#reel', (e) => { const r = e.getBoundingClientRect(); return { x: r.x, y: r.y, w: r.width, h: r.height }; });
  await page.mouse.click(reel.x + 6 + (reel.w - 12) * 0.62, reel.y + reel.h * 0.8);
  c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:sel_splice' && v === '2'), 'clic sul terzo splice: sel_splice = 2');
  /* drag the second marker (176400 of 352800) to 60% */
  const mx = reel.x + 6 + (reel.w - 12) * 0.5;
  await page.mouse.move(mx, reel.y + 60); await page.mouse.down(); await page.mouse.move(reel.x + 6 + (reel.w - 12) * 0.6, reel.y + 60, { steps: 6 }); await page.mouse.up();
  c = await calls(page);
  const mv = c.find(([k]) => k === 'overtake_dsp:marker_move');
  check(mv && mv[1].startsWith('1,') && Math.abs(+mv[1].split(',')[1] - 211680) < 900, `trascinare un marker: marker_move ${mv ? mv[1] : '(nessuno)'}`);
  await page.click('#bPlay');
  await page.click('#bRec');
  await page.click('#bSave');
  c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:play_latch' && v === '0') && c.some(([k]) => k === 'overtake_dsp:rec_toggle') &&
        c.some(([k, v]) => k === 'overtake_dsp:save_reel' && v === 'auto'), 'Play, Record e Save mandano play_latch, rec_toggle, save_reel');
  await page.click('#aClear');
  c = await calls(page);
  check(!c.some(([k]) => k === 'overtake_dsp:markers_clear'), 'Clear markers chiede conferma al primo clic');
  await page.click('#aClear');
  c = await calls(page);
  check(c.some(([k]) => k === 'overtake_dsp:markers_clear'), 'al secondo clic cancella');

  /* the Move turns a knob: the browser follows (after the local-edit quiet time) */
  await page.waitForTimeout(500);
  await page.evaluate(() => window.__cb({ 'overtake_dsp:rev_mix': '0.7', 'overtake_dsp:v_rec': '2', 'overtake_dsp:v_rpos': '200000', 'overtake_dsp:rev': '99' }));
  await page.waitForTimeout(120);
  check((await page.$eval('[aria-label="Reverb mix"]', (e) => e.getAttribute('aria-valuetext'))) === '70%', 'un knob girato sul Move si muove anche nel browser');
  check(await page.$eval('#bRec', (e) => e.getAttribute('aria-pressed')) === 'true', 'registrazione in corso: Record acceso');
  check(await page.$eval('[aria-labelledby="hRec"]', (e) => !e.hidden), 'il pannello Recording e\' visibile');
  check(errors.length === 0, 'nessun errore JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await page.evaluate(() => window.__cb({ 'overtake_dsp:v_rec': '0', 'overtake_dsp:v_save': '2', 'overtake_dsp:hd_mix': '0.4',
    'overtake_dsp:hd_heads': '3', 'overtake_dsp:hd_spacing': '0.55', 'overtake_dsp:hd_reverse': '0.35', 'overtake_dsp:hd_fdbk': '0.6',
    'overtake_dsp:hd_wear': '3', 'overtake_dsp:hd_wear_amt': '0.3', 'overtake_dsp:hd_route': '1' }));
  await page.waitForTimeout(250);
  await snap(page, 'editor_solo');
  await ctx.close();
}

/* ------------------------------------------------------------------ 3b */
console.log('\n3b. Pannello Heads');
{
  const { ctx, page, errors } = await open(browser, { component: 'tool' });
  await page.evaluate(() => window.__cb({ 'overtake_dsp:hd_mix': '0.5', 'overtake_dsp:hd_heads': '3', 'overtake_dsp:hd_reverse': '0.5',
    'overtake_dsp:hd_route': '2', 'overtake_dsp:v_fr': '0' }));
  await page.waitForTimeout(150);
  check(/Chain routing/.test(await txt(page, '#headsState')), `lo stato delle testine si legge (${await txt(page, '#headsState')})`);
  check(await inkPixels(page, '#headsCv') > 0.01, 'il nastro con le testine e\' disegnato');
  await page.click('[aria-label="Raise Heads per side"]');
  await page.click('[aria-label="Wear: what each pass does"] >> text=Fold');
  await page.click('[aria-label="Freeze"] >> text=On');
  await page.click('#aHdClear');
  await page.focus('[aria-label="Spacing"]'); await page.keyboard.press('ArrowUp');
  const c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:hd_heads' && v === '4') && c.some(([k, v]) => k === 'overtake_dsp:hd_wear' && v === '4') &&
        c.some(([k, v]) => k === 'overtake_dsp:hd_freeze' && v === '1') && c.some(([k]) => k === 'overtake_dsp:hd_clear') &&
        c.some(([k]) => k === 'overtake_dsp:hd_spacing'), 'Heads, Wear, Freeze, Clear e Spacing arrivano al DSP');
  await page.click('[aria-label="Raise Time"]');
  check((await page.evaluate(() => window.heimatEditor.P.hd_time)) === 8, 'Time avanza di una nota (1/8 -> 1/8T)');
  check(errors.length === 0, 'nessun errore JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await ctx.close();
}

/* ------------------------------------------------------------------ 3c */
console.log('\n3c. Modulatori');
{
  const { ctx, page, errors } = await open(browser, { component: 'tool' });
  await page.evaluate(() => window.__cb({ 'overtake_dsp:mod_routes': 'gene_size:0:0.250,rev_mix:2:-0.500', 'overtake_dsp:v_mo': '0.8,-0.3,0.5', 'overtake_dsp:v_mph': '0.25,0.6', 'overtake_dsp:v_fr': '0' }));
  await page.waitForTimeout(300);
  const rt = await txt(page, '#routes');
  check(/LFO 1 → Gene-Size/.test(rt) && /\+25%/.test(rt) && /Envelope → Reverb Mix/.test(rt) && /-50%/.test(rt), 'i percorsi arrivano dal DSP e si leggono');
  check(await page.$$eval('.modcols > div', (d) => d.length) === 3 && !(await page.$('#mod3')), 'tre modulatori: LFO 1, LFO 2, Envelope');
  check(await inkPixels(page, '#mod0') > 0.005 && await inkPixels(page, '#mod1') > 0.005 && await inkPixels(page, '#mod2') > 0.005, 'i tre modulatori si vedono');
  /* the LFO is drawn as its shape: a sine has its top in the first quarter of the
   * canvas, a ramp up has it at the end of each cycle */
  const peakX = (sel) => page.$eval(sel, (cv) => {
    const g = cv.getContext('2d'), d = g.getImageData(0, 0, cv.width, cv.height).data;
    for (let y = 0; y < cv.height; y++) for (let x = 0; x < cv.width; x++) { const i = (y * cv.width + x) * 4; if (d[i + 3] > 0 && d[i] > 150 && Math.abs(d[i] - d[i + 2]) < 200) return x / cv.width; }
    return -1;
  });
  const sineTop = await peakX('#mod0');
  await page.click('[aria-label="LFO 1 shape"] >> text=Up'); await page.waitForTimeout(200);
  const rampTop = await peakX('#mod0');
  check(sineTop >= 0 && sineTop < 0.3 && rampTop > 0.35, `l'LFO e' disegnato come la sua forma (seno in cima a ${sineTop.toFixed(2)}, rampa a ${rampTop.toFixed(2)})`);
  await page.click('[aria-label="LFO 1 shape"] >> text=Sine');
  await page.$eval('[aria-label="Depth of LFO 1 → Gene-Size"]', (e) => { e.value = '60'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.click('[aria-label="Remove Envelope → Reverb Mix"]');
  await page.selectOption('#rSrc', '1'); await page.selectOption('#rTgt', 'scatter'); await page.click('#rAdd');
  check((await page.$$eval('#rSrc option', (o) => o.map((x) => x.textContent))).join() === 'LFO 1,LFO 2,Envelope', 'le sorgenti: LFO 1, LFO 2, Envelope');
  await page.click('[aria-label="LFO 1 sync"] >> text=1/4');
  await page.click('[aria-label="LFO 2 shape"] >> text=Glide');
  let c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_set' && v === '0 gene_size 0.600'), 'il cursore cambia la profondita\' (mod_set 0 gene_size 0.600)');
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_set' && v === '2 rev_mix 0.000'), 'la croce toglie il percorso');
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_set' && v === '1 scatter 0.250'), 'Add route: LFO 2 su Scatter al 25%');
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_wave_sync' && v === '5'), 'LFO 1 sync a 1/4');
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_lfo2_shape' && v === '6'), 'LFO 2: forma Glide (casuale morbida)');
  check(/LFO 2 → Scatter/.test(await txt(page, '#routes')) && !/Envelope/.test(await txt(page, '#routes')), 'la lista segue le modifiche');
  await page.click('[aria-label^="Polarity of LFO 1 → Gene-Size"]');
  c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:mod_pol' && v === '0 gene_size') && /one way/.test(await page.$eval('[aria-label^="Polarity of LFO 1"]', (e) => e.getAttribute('aria-label'))), 'il pulsante ± / + cambia la polarita\' del percorso');
  await page.waitForTimeout(500);
  await page.evaluate(() => window.__cb({ 'overtake_dsp:mod_routes': 'gene_size:0:0.600:0,scatter:1:0.250:1' }));
  await page.waitForTimeout(600);
  check(/one way/.test(await page.$eval('[aria-label^="Polarity of LFO 1"]', (e) => e.getAttribute('aria-label'))) && /both ways/.test(await page.$eval('[aria-label^="Polarity of LFO 2"]', (e) => e.getAttribute('aria-label'))), 'la polarita\' arriva dal DSP (quarto campo)');
  /* patches */
  await page.evaluate(() => window.__cb({ 'overtake_dsp:v_pat': '0,3' }));
  await page.waitForTimeout(150);
  check(/Slot 3$/.test((await page.$$eval('#pSlot option', (o) => o.map((x) => x.textContent)))[3]) && /Slot 2 \(empty\)/.test(await txt(page, '#pSlot')), 'gli slot pieni e vuoti si vedono');
  await page.selectOption('#pSlot', '3'); await page.click('#pSave');
  c = await calls(page);
  check(!c.some(([k]) => k === 'overtake_dsp:patch_save'), 'sopra uno slot pieno non salva al primo clic');
  await page.click('#pSave'); await page.click('#pLoad');
  await page.selectOption('#pSlot', '2'); await page.click('#pLoad');
  await page.click('#pInit'); c = await calls(page);
  check(!c.some(([k]) => k === 'overtake_dsp:init'), 'Init chiede un secondo clic');
  await page.click('#pInit'); c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:patch_save' && v === '3') && c.filter(([k]) => k === 'overtake_dsp:patch_load').length === 1 && c.some(([k]) => k === 'overtake_dsp:init'), 'Save, Load (non da uno slot vuoto) e Init arrivano al DSP');
  check(errors.length === 0, 'nessun errore JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await ctx.close();
}

/* ------------------------------------------------------------------ 3d */
console.log('\n3d. Ritagliare dal browser');
{
  const state = JSON.parse(fs.readFileSync(STATE, 'utf8'));
  const { ctx, page, errors } = await open(browser, { component: 'tool' });
  await page.evaluate((p) => window.__cb(p), explode(state, 'overtake_dsp:'));
  await page.waitForTimeout(200);
  check(await page.$eval('#tApply', (e) => e.disabled), 'Cut spento finche\' non si sposta niente');
  await page.$eval('#tIn', (e) => { e.value = '100'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.waitForTimeout(80);
  await page.$eval('#tOut', (e) => { e.value = '800'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.waitForTimeout(120);
  let c = await calls(page);
  const last = c.filter(([k]) => k === 'overtake_dsp:trim_set').pop();
  const [ta, tb] = last ? last[1].split(',').map(Number) : [0, 0];
  check(Math.abs(ta - 8820) < 2 && Math.abs(tb - 70560) < 2, `trim_set dalla splice selezionata (${last ? last[1] : 'nessuno'})`);
  check(/\+0\.20 s/.test(await txt(page, '#oIn')) && /0\.40 s/.test(await txt(page, '#oOut')), 'quanto si toglie, in secondi');
  await page.click('#tApply');
  c = await calls(page);
  check(c.some(([k]) => k === 'overtake_dsp:trim_apply') && await page.$eval('#tApply', (e) => e.disabled), 'Cut manda trim_apply e torna a riposo');
  await page.$eval('#tIn', (e) => { e.value = '300'; e.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.click('#tCancel');
  c = await calls(page);
  check(c.some(([k]) => k === 'overtake_dsp:trim_cancel') && (await page.$eval('#tIn', (e) => e.value)) === '0', 'Keep all annulla');
  /* the handles under the selected splice, on the reel */
  const box = await page.$eval('#reel', (e) => { const r = e.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; });
  const fr = 352800, xLo = box.x + 6 + (box.w - 12) * 0 / fr, xHi = box.x + 6 + (box.w - 12) * 88200 / fr;
  await page.mouse.move(xHi - 2, box.y + box.h - 8); await page.mouse.down();
  await page.mouse.move(xHi - (xHi - xLo) * 0.5, box.y + box.h - 8, { steps: 4 }); await page.mouse.up();
  await page.waitForTimeout(120);
  c = await calls(page);
  const dr = c.filter(([k]) => k === 'overtake_dsp:trim_set').pop();
  const [da, db] = dr ? dr[1].split(',').map(Number) : [0, 0];
  check(da === 0 && db > 40000 && db < 48000 && !(await page.$eval('#tApply', (e) => e.disabled)), `trascinando la maniglia di destra sotto la splice si accorcia la fine (${dr ? dr[1] : '-'})`);
  check(+(await page.$eval('#tOut', (e) => e.value)) < 560 && /Trim splice 1/.test(await txt(page, '#tLbl')), 'il cursore To segue la maniglia; le cifre stanno sotto la forma d\'onda');
  const near = await page.evaluate(() => { const a = document.getElementById('reel').getBoundingClientRect(), b = document.getElementById('trimBox').getBoundingClientRect(); return b.top - a.bottom; });
  check(near >= 0 && near < 30, `i controlli del ritaglio sono subito sotto la forma d'onda (${Math.round(near)} px)`);
  await page.click('#tCancel');
  await page.click('[aria-label="Trim key noise (start and end of a recording)"] >> text=100 ms');
  c = await calls(page);
  check(c.some(([k, v]) => k === 'overtake_dsp:rec_trim' && v === '2'), 'Trim key noise a 100 ms');
  check(errors.length === 0, 'nessun errore JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
  await ctx.close();
}

/* ------------------------------------------------------------------ 5 */
console.log('\n5. Schermo piccolo e stampa in negativo');
{
  const { ctx, page, errors } = await open(browser, { viewport: { width: 390, height: 844 } });
  await page.waitForTimeout(400);
  check(await noHScroll(page), 'a 390 px niente scorrimento orizzontale');
  await snap(page, 'editor_phone');
  await ctx.close();
  const l = await open(browser, { scheme: 'dark' });
  await l.page.waitForTimeout(400);
  const bg = await l.page.evaluate(() => getComputedStyle(document.body).backgroundColor);
  check(bg === 'rgb(16, 16, 16)', `pannello nero anche con il sistema scuro (${bg})`);
  check(errors.length === 0 && l.errors.length === 0, 'nessun errore JavaScript');
  
  await l.ctx.close();
}

/* ------------------------------------------------------------------ 6 */
console.log('\n6. Il manuale (docs/index.html)');
{
  const murl = pathToFileURL(path.join(root, 'docs/index.html')).href;
  for (const vp of [{ width: 1280, height: 900 }, { width: 390, height: 844 }]) {
    const ctx = await browser.newContext({ viewport: vp, reducedMotion: 'reduce' });
    const page = await ctx.newPage();
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    await page.goto(murl);
    await page.waitForTimeout(300);
    if (vp.width > 800) {
      const rows = await page.$$eval('table.ref tbody tr:not(.grp)', (r) => r.length);
      const nParams = [...fs.readFileSync(path.join(root, 'src/dsp/heimat.c'), 'utf8').matchAll(/\[P_\w+\]\s*=\s*P[FIE]\("/g)].length;
      check(rows === nParams, `tabella dei parametri generata dal sorgente: ${rows} righe per ${nParams} parametri`);
      await page.click('#tabs >> text=REVERB 2');
      check((await page.$eval('#pgImg', (e) => e.getAttribute('src'))) === 'screens/reverb_2.png' && /RVRS/.test(await txt(page, '#pgCells')), 'le schede delle pagine cambiano schermata e celle');
      const missing = await page.$$eval('img', (im) => im.filter((i) => i.complete && i.naturalWidth === 0).map((i) => i.getAttribute('src')));
      check(missing.length === 0, 'tutte le immagini esistono' + (missing.length ? ': ' + missing.join(', ') : ''));
      await page.click('button[aria-label="Pad 5"]');
      check(/Pad Mode/.test(await txt(page, '#explain')), 'la mappa della superficie spiega il controllo scelto');
      await page.fill('#lGs', '0.8'); await page.dispatchEvent('#lGs', 'input');
      check(/ms/.test(await txt(page, '#rGene')) && (await txt(page, '#tGene')).length > 0, `il laboratorio dei gene calcola durata ed etichetta (${await txt(page, '#rGene')} ${await txt(page, '#tGene')})`);
      await page.click('#tabs >> text=PLAY');
      await snap(page, 'manual_desktop');
    } else {
      check(await noHScroll(page), 'manuale a 390 px senza scorrimento orizzontale');
      await snap(page, 'manual_phone');
    }
    check(errors.length === 0, `nessun errore JavaScript (${vp.width} px)` + (errors.length ? ': ' + errors.join(' | ') : ''));
    await ctx.close();
  }
}

await browser.close();
console.log(`\n${fail ? 'ESITO: FALLITO' : 'ESITO: TUTTO OK'} (${pass} controlli superati, ${fail} falliti)`);
process.exit(fail ? 1 : 0);
