# Heimat — tape and microsound instrument for Schwung

Heimat is an open-source tape and microsound instrument for
[Schwung](https://github.com/charlesvestal/schwung) on Ableton Move. A sound — a
WAV file or something you record — becomes a **reel**; the reel is cut into
**splices**; a splice is played back as a stream of **genes**, grains whose size,
start, overlap, envelope, speed, timing and pitch you shape in real time. It moves
freely between the time scales of sound: the reel and the splice (phrases,
seconds), the sound object, and the micro scale of grains a few milliseconds long.

**Manual:** `docs/index.html`, published with GitHub Pages (see *Manual and
editor demo online* below). **Browser editor:** every parameter, the reel and
the genes live in schwung-manager's Remote UI.

One engine (`src/dsp/heimat.c`), two modules:

| Module | Type | What it is |
|---|---|---|
| **Heimat** | sound generator | Sits in a Signal Chain slot like any synth and plays WAV reels. |
| **Heimat Solo** | overtake | A standalone instrument with its own screen and pages that takes over the whole Move surface: records from the Move input, lets you place splice markers by hand, follows a clock and saves reels. |

## The engine

* **Reel**: a WAV file (PCM 8/16/24/32 or float32, mono or stereo, any sample
  rate, resampled to the Move rate on load), or an empty reel to record into. Up to
  2.9 minutes. Loading and saving run on a worker thread, so the audio never stops.
* **Splices**: the WAV's cue markers (files from samplers and editors that write
  markers load with their splices), equal divisions, transients detected
  automatically, or markers placed by hand. Undo covers marker edits and
  new-splice recordings.
* **Genes**:
  * **Gene-Size** — 0 plays the whole splice; turning up shrinks the gene down to
    half a millisecond. It is a time, so it does not change with speed.
  * **Slide** — where in the splice the genes start.
  * **Morph** — the overlap of successive genes: gaps of silence, then a seamless
    1/1 loop, then up to three overlapping genes, then a fourth with random
    pitch-up chords (ratios set on the Chords page) and pan.
  * **Vari-Speed** — speed and direction; the centre stops playback, ±0.5 is the
    original speed.
  * **Organize** — which splice plays, spread evenly over the knob; it changes at
    the end of the gene or at once.
* **Micro** (new in 0.3, all neutral at their defaults):
  * **Envelope** — the shape of every gene: *Edge* (flat with a short anti-click
    fade), *Tukey* (raised-cosine taper), *Gauss*, *Triangle*, *Expodec* (sharp
    attack, exponential decay) and *Rexpodec* (the reverse). Expodec and Rexpodec
    are not time-symmetric, so they sound different backwards. With the
    smooth shapes (Tukey, Gauss, Triangle) each gene fades in and out over
    half its length, so for the same Morph the genes come twice as often and
    up to eight sound at once.
  * **Scatter** — moves the cloud from synchronous to asynchronous: the interval
    between genes becomes irregular and their start points wander around Slide,
    from sequential reading to random reading of the splice.
  * **Spread** — a random transposition for each gene within ± the set number of
    semitones: the frequency band of the cloud.
  * **Spread Intervals** — *Free* detunes by any amount; *Semitones* keeps whole
    semitones; *Octaves*, *Fifths* (fourths and fifths), *Major* and *Minor* let each
    gene jump only to those intervals, up or down, or stay at unison. Spread sets
    the widest jump allowed, so Octaves needs Spread at 12 before anything moves.
  * **Gene Reverse** — the chance that each gene plays against the tape: at 0
    every gene follows Vari-Speed, at 50% forwards and backwards mix evenly, at
    100% all of them run the other way. It is decided when a gene starts.
  * **Width** — a random stereo position for each gene.
  * **Selective** + **Threshold** — play only the genes that are louder (*Loud*) or
    quieter (*Quiet*) than the threshold; the skipped genes keep their place in
    time as silence.
* **Effects** (new in 0.4, all off at their defaults): a three-band **EQ** (low
  shelf at 180 Hz, a mid band you can tune from 100 Hz to 8 kHz, high shelf at
  5 kHz, ±15 dB each), a stereo **Delay** (10 ms – 2 s, or synced to the tempo
  from 1/16 to 1/2 note; feedback with a tone control, optional ping-pong; the
  time glides when changed, like a tape echo) and a **Reverb** (diffused
  feedback-delay network, decay 0.3 – 12 s, damping). They run in series
  after the playback / input mix and before the volume. With *Record Through FX*
  (Solo) the effects are printed into what you record, which turns
  Sound-on-Sound into an echo and reverb accumulator; otherwise recordings stay
  dry and the effects are only in the output. A flat EQ and a mix at zero cost no
  CPU.
* **Heads** (new in 0.6, off at its default): a loop of tape with up to four
  playback heads per side, between the delay and the reverb. *Time* is a note
  value from two bars to 1/128 at Heimat's tempo; *Heads* sets one to four per
  side; *Spacing* spreads them from a tight, strummed cluster to wide intervals
  (and detunes right against left); *Feedback* goes from one repeat to endless;
  *Reverse* turns heads around one by one (1L, 1R, 2L ...). *Wear* is what each
  pass through the feedback does to the sound, with its amount: Dark (4-pole
  low-pass), Thin (4-pole high-pass), Crush (bits and sample rate), Warm
  (saturation), Fold (wavefolder), Fuzz (hard clipping). *Mode*: Fade
  (cross-fades to a new time), Glide (the time bends like tape), Rise and Fall
  (every repeat an octave up or down). *Routing*: Own, Cross (left/right),
  Chain (the heads of a side in series), Wander (in series, crossing sides).
  *Freeze* loops what is on the tape; *Clear heads* silences it at once (it
  marks the tape as erased instead of wiping memory on the audio thread). Each
  head has 11.9 s of tape; head times stop at 5.9 s, so that backwards heads
  have room. Solo: the HEADS and HEADS 2 pages (Shift + Track 3), *Clear heads*
  in the menu; chain module: the *Heads* page.
* **Input through the effects** (Solo, 0.6): with an empty reel the input and
  the effect tails now go on through the effects, so you can play into Heimat
  Solo before recording anything (with Sound on Sound below the top).
* **Recording** (Solo): *into a new splice* at the end of the reel, or *over the
  current splice* (time-lag accumulation: the write point wraps round the splice).
  The source is the Sound-on-Sound mix of input and playback, or the input only,
  with four input gain steps. Recording is independent of playback speed.
* **Clock**: internal BPM, Move's transport, or MIDI clock, divided from 1/16 to a
  bar. A tick steps to the next gene (*Gene Shift*) or makes the start point glide
  through the splice (*Time Stretch*); *Auto* uses Gene Shift up to Morph 2/1 and
  Time Stretch above. *Record Sync* starts and stops recording on the next tick.
* **Save**: the reel is written as a 32-bit float stereo WAV with cue markers into
  `UserLibrary/Samples/Heimat/`, at the Move rate or at 48 kHz, atomically (temp
  file, then rename).
* **Solo remembers its settings** (and the last reel file) between sessions, in
  `/data/UserData/schwung/heimat/state.json`, two seconds after the last change.

## Reading the cloud

The MICRO page shows what the engine is producing, so you can steer by ear and
by number:

* **Gene duration** in milliseconds, tagged *CLICK* below 2 ms (a click whose
  colour still changes with the material), *GRAIN* up to 40 ms (the source loses
  its identity) and *SOUND* above (the source stays recognisable).
* **Density** in genes per second, tagged *RHYTHM* (below 15), *FLUTTER* (15–25),
  *FLOW* (25–50), *TEXTURE* (50–100) and *MASS* (above 100).
* **Fill factor** — density × duration: *SPARSE* below 0.5 (more silence than
  sound), *COVERED* around 1, *PACKED* above (overlapping genes). Morph is, in
  effect, a fill-factor control.

These thresholds follow Curtis Roads' observations on granular textures (see
References); they are guides for listening, not hard limits.

## Heimat Solo: the surface

The screen is laid out like a hardware panel: the splice window (`S03/08`), two
direction windows with the Morph state between them, and one cell per encoder
underneath. The 16 step LEDs show the splices in colour (the selected one white)
and the left / right arrows light as reverse / forward windows.

| Control | Action |
|---|---|
| **Track 1–4** | Pages: PLAY · REEL (marker editing) · REC / CLOCK · MICRO, then FX, FX 2, HEADS, HEADS 2, SETUP 1 and SETUP 2 on further presses of Track 4 |
| **Shift + Track 4** | Straight to the FX page |
| **Shift + Track 3** | Straight to the HEADS page |
| **Knobs 1–8** | The eight cells of the page. Turn or touch: name and value appear in place of the cells; Shift for fine steps |
| **Record** | Record over the current splice; again to stop |
| **Shift + Record**, **Sample** | Record into a new splice |
| **Capture** | Drop a splice marker at the playhead (PLAY) or at the cursor (REEL) |
| **Jog** | REEL: move the cursor (Shift = fine); click adds a marker. Elsewhere: choose the splice |
| **Delete** | Tap: delete the marker under the cursor, or join the current splice with the next. Hold 1.2 s: clear all markers. Shift + hold 2 s: new empty reel |
| **Undo** | Undo the last marker edit or new-splice recording |
| **Copy** | Auto-splice on transients (sensitivity on the REEL page) |
| **Steps 1–16** | Select a splice (banks of 16 follow the selection) |
| **Pads** | Follow Pad Mode (Pitch / Splice / Drone). On the REEL page a pad plays its splice while held |
| **Left / Right** | Previous / next splice. **Up / Down**: octave (Pitch) or splice bank |
| **Play** | Play gate on / off |
| **Loop** | Clock on / off. **Shift + Loop** cycles the source: Internal, Move, MIDI |
| **Mute** | Mute the output |
| **Menu** | Load WAV · New empty reel · Save reel · Auto-splice · Clear all markers · Undo · Clear heads · About / DSP load (version, Heimat's share of each audio block, blocks over time) |
| **Shift + Back** | Leave Heimat Solo |

REEL page knobs: **SPLC** (splice), **STRT / END** (move the markers around the
selected splice), **ZOOM**, **SENS** (transient sensitivity), **DIV** (divide into
N equal splices, applied when you stop turning), plus Gene-Size and Slide.

MICRO page knobs: **ENV**, **SCAT**, **SPRD**, **WIDE**, **THR**, **SEL**, **INTV**
(Spread Intervals) and **GREV** (Gene Reverse); Gene-Size and Morph stay on PLAY.
In the chain module the same controls are on the *Micro* parameter page.

FX page knobs (Track 4 twice, or **Shift + Track 4**): **LOW**, **MID**, **FREQ**,
**HIGH**, delay **TIME**, **FDBK**, **DLY** (delay mix), **REV** (reverb mix). The
screen shows the EQ curve. FX 2 page: **SYNC**, **TONE**, **PING**, **FDBK**,
**DCAY**, **DAMP**, **REV**, **RFX** (record through FX), with the echoes and the
reverb decay drawn above. In the chain module these are on the *Effects* page.

Turning or touching a knob replaces the cells at the bottom of the screen with
that parameter's name, value and a wide bar for as long as you turn (or keep
the finger on it), then the cells come back. The waveform is never covered;
messages and the Delete-hold confirmation use the same strip.

### Schwung's own effects

Heimat's effects are built in, but you can also use the audio effects already
installed in Schwung:

* **Heimat Solo**: its output joins the Move mix before **Master FX**, so the
  effects you load in Schwung's Master FX (up to eight in a row) process Heimat
  too, together with the rest of the Move.
* **Heimat** in a Signal Chain: put any Schwung audio effect in the slot's FX
  positions after the sound generator, as with any other synth.

Heimat does not load other modules' effects inside itself. Schwung already hosts
effects in Master FX and in the chain slots, and a module that loaded other
modules' code would depend on their internals and on host details that can
change between Schwung versions; using Schwung's own FX positions is the stable
way.

## Pad modes

* **Pitch** (default): notes set the playback speed at 1 V/octave around *Root
  Note*; the key is the Play gate. Monophonic, last note wins, legato.
* **Splice**: each pad selects and plays one splice.
* **Drone**: plays continuously; any pad restarts it from the Slide point.

**Play Gate**: *Classic* lets the gene in progress finish after the key is
released, *Momentary* stops with the key, *Trigger Loop* restarts at every key and
never stops.

## Browser editor

Both modules ship a `web_ui.html` that schwung-manager shows in its Remote UI
(`http://move.local:7700`, *Remote UI*): Heimat in a track slot shows it in
that slot's view, Heimat Solo under the **Tool** tab while it runs. The pop-out
button opens it in a window of its own, also on a phone.

* Changes in the browser reach the Move at once, and knobs turned on the Move
  move in the browser.
* Heimat Solo also sends the live view: the whole reel with its splice marks,
  the genes as they read, the record head, the in and out levels and the
  cloud readings (gene length, density, fill factor). Click a splice to select
  it, drag a mark to move it, double-click to add or remove one. Play, Record,
  New splice, Undo, Save, Auto-splice, Divide now and Clear markers are there
  too.
* The EQ curve is the exact response of the three filters; drag its points.
  The delay and reverb show their echoes and decay.
* Knobs: drag up and down (Shift for fine), mouse wheel, arrow keys, Home and
  End; double-click resets.
* The look: a black panel with cream silkscreen and line drawings, typed notes
  taped on it, the reel drawn as a spiral. Acid green is what is alive (genes,
  values), magenta the cuts and recording. The name HEIMAT is set in Monoton
  (SIL Open Font License), whose letters are made of parallel lines like the
  tracks of a tape; labels in Syne, notes in Courier Prime. The fonts load from
  Google Fonts; without a connection the page falls back to system fonts.
* In a track slot the editor shows the parameters only: the reel, the markers
  and recording are in Heimat Solo.

How it talks to the Move: the page uses Schwung's `schwung-remote-api.js`. In
a slot it reads and writes `synth:<key>` (or whichever component the manager
names). For the Tool tab Heimat Solo answers `module_id`, `rui_poll`
(`rev:on:tick:bpm`, the rev moving about 25 times a second while something
sounds, still otherwise) and `state`: a flat JSON object with the parameters,
then `rev` and the live keys starting with `v_` (`v_pk` is a 256-column
overview of the reel, `v_mk` and `v_gp` are comma lists of markers and gene
positions). The editor needs a recent Schwung with Remote UI module panels and
the Tool tab; it was built against Schwung 1.9.

Opened outside Schwung (for example from the manual's site) the editor runs
as a demo on a made-up reel.

## Beta

Version 0.7.0 is the first beta: the features are frozen and it now needs
hours on real Moves. [BETA.md](BETA.md) is the test sheet (in Italian:
[BETA.it.md](BETA.it.md)); report what you find with the *Beta report*
issue form. [CHANGELOG.md](CHANGELOG.md) lists what changed in each version.

From 0.7.0 on, parameter keys are frozen: none is renamed or removed and no
choice loses options, so saved sessions and track-slot patches keep working
(`tests/fixtures/params_0.7.0.txt`, checked by the tests). A saved state from
an older version opens with its own values, and controls it does not know start
at their default. The saved state carries `"state_format": 1`.

## Install

The ready-made packages are on the
[releases page](https://github.com/CairnDebil/schwung-heimat/releases):
download `heimat-module.tar.gz` and `heimat-solo-module.tar.gz` and add them
in Schwung Manager as below. To build them yourself instead:

Requirements: a Move with Schwung (1.7.3 or later), and Docker (or an
`aarch64-linux-gnu-` toolchain) to build.

```bash
bash scripts/build.sh      # -> dist/heimat-module.tar.gz and dist/heimat-solo-module.tar.gz
bash scripts/install.sh    # SSH to move.local, installs both
```

Or install the two tarballs from Schwung Manager (`http://move.local:7700`,
*Modules*, custom module). Restart the Move afterwards.

* **Heimat** goes in a Signal Chain slot as a sound generator (hold `Track 1–4`).
* **Heimat Solo** opens from Schwung's module menu like other takeover modules.
  For the input, use headphones or a line-in cable: with the built-in speakers on
  the microphone hears the speakers, and Schwung may mute the input to avoid
  feedback.

## Design notes and estimates

Choices made by ear rather than measured: the knob positions of the Morph stages
and the length of the gaps, the Vari-Speed curve below the original speed, the
envelope constants (Gauss width, the −40 dB end of Expodec), the overlap level
compensation (1/√n), the transient detector, how far Time Stretch moves per tick,
the range of Scatter (± half the splice, ± the spacing), the effect ranges and
curves (shelf frequencies, delay tone range, reverb line lengths, decay and
damping curves), the EQ picture (an approximation, not the exact response), and the screen font (the
layout assumes 8-pixel text rows, as in other takeover modules).

Not included: control-voltage style outputs, audio-rate phase modulation, and
recording in the chain module (a chain sound generator cannot hear the other
tracks; use Heimat Solo for that).

## Manual and editor demo online

`docs/` is a static site: `index.html` is the manual, `editor.html` a copy of
the editor that runs as a demo. On GitHub: *Settings*, *Pages*, *Deploy from a
branch*, branch `main`, folder `/docs`. The manual is then online at
<https://cairndebil.github.io/schwung-heimat/> and the editor demo at
<https://cairndebil.github.io/schwung-heimat/editor.html>.

`scripts/gen_web.py` keeps both in step with the engine: it reads the parameter
table from `src/dsp/heimat.c`, writes it into the editor and into the manual's
parameter reference, and copies the editor to `docs/editor.html`. The build
runs it; the tests check that nothing is out of date.

## Test

```bash
bash scripts/test.sh               # SAN=1 for AddressSanitizer + UBSan, SCREENS=1 for PNG screenshots
```

On GitHub every push runs all of this, also with the sanitizers, and builds the
two modules for ARM64 (`.github/workflows/ci.yml`); the tarballs are kept as a
download of the run. A tag `vX.Y.Z` publishes a release with the notes from
CHANGELOG.md, as a pre-release while the version starts with 0.

* **DSP**, built twice (chain and Solo): loader formats and markers, resampling,
  pitch, Vari-Speed, Organize, Splice mode, Slide, Gene-Size, Morph stages, play
  modes, reel swaps under load, loading without stalling the audio, recording into
  a new splice and undo, recording over a splice from the input, empty-reel
  recording and input gain, Sound-on-Sound, clock-synced recording, manual markers,
  waveform peaks, saving and re-reading WAVs with markers (also at 48 kHz),
  transient detection, Gene Shift and Time Stretch from all three clocks, the
  2.9-minute limit, settings restored in a new session, and the Micro tools:
  envelope shapes and their symmetry, Scatter in time and position, Spread,
  Width, selective granulation, and the duration / density / fill-factor figures
  checked against the audio; the effects: bit-exact output when they are back at
  zero, the three EQ bands measured on noise, delay timing, feedback, ping-pong
  and tempo sync (internal BPM and Move's tempo), reverb tail length, damping
  and stereo, stability with everything at maximum, and recording through the
  effects; the browser link of Heimat Solo (flat `state`, `rui_poll` still
  when idle and moving while it plays, live keys ignored when a state is
  applied); the heads: timing of two heads, Chain against Own, backwards
  heads (an attack comes back as a swell), Rise and Fall measured an octave
  away, Freeze holding the tape and ignoring new input, Clear, every wear and
  routing at full feedback without NaN or runaway, cost at full load, and
  stopping at Mix 0. The EQ test now runs fixed noise straight through the
  effects, so it is exact. Gene Reverse (the share of backward genes at 0, 50
  and 100%, and that they really read backwards) and Spread Intervals (only the
  allowed intervals, and by ear: a 440 Hz reel in Octaves sounds at 220, 440
  and 880 Hz with nothing in between). Compatibility: the frozen keys of 0.7.0,
  sessions saved by 0.5.0, 0.6.0 and 0.7.0 (`tests/fixtures/`), states from a
  later version with unknown keys. The load meter.
  Checks on wall-clock time only report, without failing, on CI machines (`CI`
  set), with sanitizers, or with `HEIMAT_LAX_TIMING=1`.
* **Browser editor and manual** (Playwright with Chromium; skipped without
  it): the editor as a demo, as Heimat Solo in the Tool tab fed with the `state`
  that the real DSP produced, and as a track-slot panel; knobs by mouse and
  keyboard, choices, the EQ points, splice selection and marker dragging on the
  reel, transport and the two-click Clear markers, updates coming back from
  the Move, a phone-sized screen with no sideways scrolling, the light theme;
  the manual's generated table, page tabs, control map and genes lab.
* **UI** (Node 18+): the real `src/solo/ui.js` against a fake host — pages, knobs,
  touch, pads in each mode, transport, markers, Delete hold, menu, file browser,
  save message, LEDs, the MICRO page, the FX pages, the knob readout (never over
  the waveform, gone shortly after the last turn), the About / DSP load screen
  and its version against `module.json`, exit. `docs/screens/` holds the screenshots.

## References

* Curtis Roads, *Microsound*, MIT Press, 2001 — time scales of music, grain
  envelopes, synchronous and asynchronous granulation, density and fill factor,
  micromontage, granulation of sampled sound.

## Licence

MIT — see `LICENSE` and `NOTICE`. Heimat is and will stay fully open source.
