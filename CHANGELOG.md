# Changelog

Heimat follows [semantic versioning](https://semver.org): from 1.0 on, a minor
version (1.1, 1.2) adds features without breaking saved sessions or patches,
and a patch version (1.0.1) only fixes things. Before 1.0 every version is a
development or beta release.

## [0.15.0] — after the first beta of 0.14

Everything here comes from the beta test of 0.14.0 on Schwung 1.9.4. The
Scenes planned for this version move to 0.16.

### Changed
- **Pages by family**: each Track button opens its first page and, pressed
  again, the next. Track 1 the genes (PLAY, MICRO, MICRO 2), Track 2 the reel
  and the instrument (REEL, SETUP 1, SETUP 2), Track 3 recording (REC, LOOP),
  Track 4 only the effects (EQ, HEADS, HEADS 2, REVERB, REVERB 2). Shift +
  Track 1 / 3 / 4 as before (MOD, HEADS, REVERB). The lit Track button is the
  one whose family is on screen. From an overlay (Menu, TRIM) a Track button
  opens its first page.
- **Modulators: LFO 1, LFO 2, Envelope** (held Track 1, 2, 3). LFO 2 is a
  second LFO with its own shape, rate, sync and restart (`mod_lfo2_shape`,
  `mod_lfo2_rate`, `mod_lfo2_sync`, `mod_lfo2_retrig`). Both LFOs gain two
  shapes: *Random* (a new value each cycle, held) and *Smooth Random* (the
  same, gliding) — `mod_wave_shape` goes from 5 to 7 options, the old values
  keep their meaning. Wave is now called LFO 1. MOD 1 holds the two LFOs,
  MOD 2 the Envelope and *Restart Mods On Clock* (which restarts both LFOs).
  Track 4 is no longer a modulator, so it opens its page on press.
- **Trim Key Noise counts from the release of the key**: the beta found
  100–200 ms not enough, because the loud part is letting go of Record or
  Sample. Recording started from a key now writes nothing until the key is
  released (`rec_key_up`), then skips the trim. If the release never comes,
  recording starts after 1.5 s anyway. Starts from a clock tick, from the
  editor or from MIDI are not affected. Engine: `rec_toggle`, `rec_toggle_new`
  and `rec_cur` / `rec_new` accept `key` as the value.
- SETUP 2, knob 8: Volume again (Restart Mods On Clock moved to MOD 2).
- LOOP: knob 3 is Trim Key Noise, knob 6 Record Source; the screen shows the
  key trim instead of the head.

### Added
- **Patches**: Menu > *Save patch* (16 slots; a full slot asks for a second
  click), *Load patch*. A patch is the sound: every control and the routes,
  but not the reel, the recording settings, the clock, Volume, Mute or
  Freeze. Files in the data folder, `patches/NN.json`. Engine: `patch_save`,
  `patch_load`, get `patches` (the full slots), `ui_state` carries `pv` and
  `pbusy`; the editor gets `v_pat`.
- **Init patch** (Menu, click twice): every control of the sound back to its
  start value, routes cleared. The sound before goes to slot 0, *Before Init*,
  at the top of Load patch. Engine: `init`.
- Editor: a patch row under Play (slot, Save, Load, Init with a second click).
- MOD 2 has empty cells (knobs 6–8 do nothing there).

### Fixed
- **Holding Delete cleared all markers**: Delete held alone (1.2 s) cleared
  the markers, so holding it to reset a knob could wipe them if the knob came
  late. Delete held alone now clears nothing; a tap still deletes one marker,
  but only a tap (under 0.6 s). Clear all markers stays in the Menu; Shift +
  hold Delete is still a new empty reel.
- **The route percentage was hidden while holding a Track**: touching the
  knob showed its reading over the message. Now the depth (`LFO1 > GENE
  +25%`) is what you see while you turn.
- **Editor: the LFO drew badly**: it was a trace of values polled a few times
  a second. Each LFO is now drawn as one cycle of its shape (two cycles, with
  a new value per cycle for the Random shapes) with a cursor at the phase
  the Move reports (`v_mph`), carried forward at its rate; the Envelope as
  its attack and decay with its level.

### Removed
- **Random** and **Follow** modulators, and the **Vari-Speed overdub head**
  (the beta asked for them to go). Retired keys, ignored in older sessions:
  `mod_rand_rate`, `mod_rand_smooth`, `mod_rand_sync`, `mod_rand_retrig`,
  `mod_follow_src`, `mod_follow_gain`, `mod_follow_attack`,
  `mod_follow_release`, `rec_head` (`tests/fixtures/retired_0.15.0.txt`).
  Routes are now saved as `mod_routes2`; an older `mod_routes` is read once:
  Wave routes become LFO 1, Envelope routes stay Envelope, Random and Follow
  routes are dropped.

## [0.14.0] — playing safely

The first of the steps planned after studying how a performance sampler
keeps the hands on the music: get back from any experiment, see what the
modulation is doing, and reach the most used control at once.

### Added
- **TEMP**: hold Undo and change anything (knobs, choices, pads, routes); let
  go and everything is back where it was. Shift + Undo keeps it on until
  Undo again (TMP in the header). A plain tap of Undo still undoes. Engine:
  `temp` = `begin` / `end` / `cancel`, `ui_state` carries `tmp`.
- **Back to the start value**: hold Delete and turn or touch a knob. Delete
  then neither deletes a marker nor clears them when let go.
- **Polarity of each route**: both ways around the knob, or one way only.
  On the Move hold the Track button and touch the knob without turning it; in
  the editor the ± / + button on each route. Wave and Random start both ways,
  Follow and Envelope one way (as before). Engine: `mod_pol` ("source key");
  routes are saved as `key:source:amount:polarity`, older ones without the
  fourth field take the source's.
- **Live value on modulated cells**: a tick on the bar where the modulation
  has taken the control right now. Engine: `mod_live` ("key:value,...").
- **DJ filter** (`filter`, PLAY knob 6, editor Play panel): low-pass to the
  left (20 kHz to 40 Hz), high-pass to the right (20 Hz to 10 kHz), nothing in
  the middle, last before the output.
- **Restart Mods On Clock** (`mod_restart`, SETUP 2 knob 8, editor
  Modulators panel): when the clock starts, Wave and Random start from the
  top.

### Changed
- PLAY page: knob 6 is the DJ filter; Sound on Sound stays on REC and LOOP.
- SETUP 2: knob 8 is Restart Mods On Clock (Volume stays on PLAY).
- Undo acts when released, so that holding it can start TEMP.
- Parameter keys frozen again in `tests/fixtures/params_0.14.0.txt`.

## [0.13.0] — quieter effects

Measured with a 440 Hz sine through each effect: the energy that is neither
the note nor its harmonics.

### Fixed
- **Reverb modulation noise.** To the right of Depth each line jumped to a new
  random length and slid there with a one-pole, so the pitch bent with a kink
  at every step; at high Speed the lengths moved by many samples per sample
  and the tail turned into noise (as loud as the note at Depth 0.6, Speed
  0.8). Now each line glides along a half cosine, and how far it may swing
  depends on the rate, so the pitch never bends by more than about 0.8%
  (Depth right) or 1% (Depth left): slow modulation is as wide as before,
  fast modulation gets shallow instead of noisy. Above 4 kHz the result is
  now 55 – 60 dB under the note. Moving lengths are read with a cubic
  interpolation.
- **Shimmer** feeds back only 120 Hz – 4.5 kHz of the octave above, so hiss
  is no longer raised an octave on every pass (non-harmonic energy from -6 to
  -15 dB, above 4 kHz from -24 to -41 dB).
- **Wear Warm, Fold and Fuzz** amplified quiet signals inside the heads'
  feedback (up to 30 times for Fuzz): the loop lifted its own noise and, with
  enough Feedback, played by itself from nothing. Now none of them is louder
  than its input near silence; repeats still saturate, fold and fuzz, and die
  away.

### Added
- **Input Gate** (`in_gate`, LOOP page knob 8, editor Recording panel; -60 dB
  to start with): below the threshold the input fades, 40 dB down 6 dB under
  it, after a 60 ms hold, so the hiss of the microphone or of a quiet line
  does not ring on in the reverb and the heads. Off, -70, -60, -50, -40 dB.

### Changed
- LOOP page: knob 8 is the Input Gate (Vari-Speed stays on PLAY).
- Parameter keys frozen again in `tests/fixtures/params_0.13.0.txt`.

## [0.12.1] — editor layout

### Changed
- Editor: the trim controls sit right under the reel, on one line (*Trim
  splice N*, From, To, Cut, Keep all), and two handles under the selected
  splice can be dragged on the waveform itself.
- Editor, Reverb panel: the scope across the whole panel, the eight knobs on
  one line in two groups (Mix, Size, Predelay, Decay | Absorb, Depth, Speed,
  Tilt) and the two switches below. Knob names stay on one line everywhere, so
  the dials of a row line up.

### Fixed
- REVERB pages on the Move: "STUDIO 1.8s" and "REC WITH FX" ran past the edge
  of the screen; long readings now shorten to fit. The UI tests check that no
  text leaves the screen.

## [0.12.0] — trimming

### Added
- **Trim splice** (Shift + Capture, or Menu → Trim splice): keep only part
  of the selected splice. Knob 1 moves the start, knob 2 the end, knob 3
  zooms on the last point moved, the jog nudges it; what will go is drawn
  dotted and the genes already play only what will stay. Jog click or Capture
  cuts, with a 3 ms fade at each cut; Back cancels; Undo brings the cut back
  (the reel from before is kept until the next edit or load). The rest of the
  reel moves up with its markers; in Divide mode the divisions become markers.
  The cut is built on the worker thread, not in the audio callback.
- **Trim Key Noise** (`rec_trim`, LOOP page, editor Recording panel): Off,
  50, 100, 200, 300, 500 ms. When you press to start, that much is not
  recorded; when you press to stop, as much is taken off a new splice, and an
  overdub gets back what its last frames replaced. Starts and stops on a clock
  tick or at the end of a Record Length are left alone.
- Editor: two sliders under Splices to trim the selected splice (*Cut*, *Keep
  all*), the part that will go greyed out on the reel.
- Engine commands `trim_set` ("a,b"), `trim_apply`, `trim_cancel`; `ui_state`
  carries `ton`, `ta`, `tb`, `tjb`.

### Changed
- LOOP page: knob 4 is now Trim Key Noise (Record Source stays on REC).
- Parameter keys frozen again in `tests/fixtures/params_0.12.0.txt`.

## [0.11.0] — modulators

### Added
- **Four modulators** that move any continuous control on top of its knob:
  - *Wave*: sine, triangle, ramp up, ramp down, square; 0.02 – 20 Hz or
    synced (4 bars to 1/16); can restart with each pad.
  - *Random*: a new value at each step, stepped or gliding (*Smooth*); free
    or synced; can take a new value with each pad.
  - *Follow*: the level of the input or of the output, with gain, rise and
    fall.
  - *Envelope*: attack and decay; *Hold* keeps it up while a pad is down,
    *Loop* repeats it; started by the pads, the clock or both.
- **Routing on the Move**: hold Track 1 – 4 (Wave, Random, Follow, Envelope)
  and turn a knob to set how much that source moves the knob's control
  (-100% to +100% of its range; back to 0 removes it). Hold a Track button and
  press Delete to remove all of its routes. Up to 16 routes. Modulated cells
  carry a flag on their left edge.
- **MOD 1 and MOD 2 pages** (Shift + Track 1), with each source drawn as it
  moves.
- **Modulators panel** in the browser editor: the four sources with live
  traces, the list of routes with a depth slider each, adding and clearing
  routes.
- The routes are saved with the session as `mod_routes`; a complete state
  without them (from an older version) starts with none.
- Engine commands `mod_set` ("source key amount") and `mod_clear` ("all", a
  source, or a key); `mod_routes` reads them back; `ui_state` carries `mrv` and
  `mo`, the editor state `v_mo`.

### Changed
- Track buttons change page when released, so that holding one can route a
  modulator. Shift + Track still acts at once.
- What a knob is set to and what the engine uses are kept apart: saved
  sessions and the displays always show the knob.
- Parameter keys frozen again in `tests/fixtures/params_0.11.0.txt` (16 new
  keys, nothing removed).

## [0.10.0] — Pattern and looper tape

### Added
- **Pattern** (MICRO 2 page, editor Micro panel): repeatable chance. The random
  choices of each gene (Scatter, Spread, Gene Reverse, Width, spacing, the Morph
  chord tones) repeat every 2, 3, 4, 5, 6, 8, 12, 16 or 32 genes; *Pattern Seed*
  (1 – 999) picks another phrase. A new attack starts the phrase from its first
  gene. Off (the default) works as before.
- **Looper tape** (LOOP page, Track 3 twice; editor Recording panel):
  - *Record Length*: a new-splice recording stops by itself after 1, 2, 4, 8
    or 16 bars at Heimat's tempo (Free: as before).
  - *After Recording*: *Play* the new splice (as before), or *Overdub*: the
    recording goes straight on over the loop just closed until you stop it.
  - *Overdub Head*: *Steady* (as before) or *Vari-Speed*, where the write
    head moves with the tape: slower, faster, backwards, or not at all.
- The MICRO 2 page, with one box per gene of the phrase.
- `ui_state` carries the recording target, the part already recorded and the
  position in the Pattern.

### Changed
- Track 4 now goes MICRO, MICRO 2, EQ, HEADS, HEADS 2, REVERB, REVERB 2,
  SETUP 1, SETUP 2.
- Parameter keys frozen again in `tests/fixtures/params_0.10.0.txt` (the five
  new keys added, nothing removed).

## [0.9.0] — one instrument

### Changed
- **Heimat Solo is now simply Heimat.** It keeps its module id `heimat-solo`
  and its package name `heimat-solo-module.tar.gz`, so it installs over Heimat
  Solo and keeps your settings and reels.
- One package, one build: the scripts, the tests, the continuous integration
  and the releases deal with Heimat alone.
- The browser editor has no track-slot mode any more; it is Heimat's editor in
  the Tool tab (and the demo on the manual's site).
- The manifest names CairnDebil as author.

### Removed
- **The track-slot module** (Heimat as a sound generator in a Signal Chain) is
  suspended. Its manifest, help, UI, the 0.8.0 engine with both builds and the
  0.8.0 editor are kept in `attic/track-module`, with notes on how to bring it
  back. The engine no longer answers `ui_hierarchy` and `chain_params`. An
  installed copy stays on the Move until you remove it.

## [0.8.0] — second beta

What the first days of beta testing on a Move asked for.

### Added
- **A new reverb**, last in the chain, with its own pages (REVERB, REVERB 2)
  and its own panel in the editor:
  - *Size* from a box to a sky on one knob; every distance scales at once, so
    turning it glides between spaces. Bigger spaces ring longer at the same
    Decay.
  - *Pre-Delay* 7 – 500 ms, independent of Size; *Reverse* plays it backwards
    in pieces of its own length.
  - *Decay* from about 0.1 s to a minute; at the top the tail holds forever.
  - *Absorb*: first diffusion, then damping inside the loop.
  - *Depth* and *Speed*: to the left a chorus of eight phases, to the right
    each line wanders on its own, at the far right shimmer (an octave above the
    tail fed back in); 0.05 – 200 Hz.
  - *Tilt*: dark to bright, after everything.
- Heimat Solo: Shift + Track 4 opens REVERB (again: EQ).

### Changed
- **Each effect on its own page.** Heimat Solo: MICRO, EQ, HEADS, HEADS 2,
  REVERB, REVERB 2, SETUP 1, SETUP 2. Editor: separate EQ, Heads and Reverb
  panels. Chain module: EQ, Heads and Reverb pages.
- **Calmer encoders** on Heimat Solo: 1/400 of the range per click (Shift
  1/2000), at most 4× when turned fast; choices and small whole numbers move
  one step every two clicks. Finer steps for the knobs of the chain module.
- **Gene-Size curve**: from the splice, at most 4 s, down to 0.5 ms; the first
  half of the knob covers genes you hear as sounds (down to about 130 ms), the
  second half the grains (10 ms at three quarters). Knob positions no longer
  depend so much on the length of the splice.
- *Reverb Damping* (`rev_damp`) is now *Absorb*: diffusion, then damping.

### Removed
- **The delay** (`dly_time`, `dly_sync`, `dly_fdbk`, `dly_tone`, `dly_mix`,
  `dly_ping`): it was noisy, and the heads already cover tape echo. Its keys are
  retired; in older sessions they are ignored. Parameter keys are frozen again
  from here (`tests/fixtures/params_0.8.0.txt`).

## [0.7.0] — first beta

The feature set for 1.0 is frozen here; what follows until 1.0 is fixing.

### Added
- **Gene Reverse** (Micro, knob GREV): the chance that each gene plays against
  the tape, decided when the gene starts. 0 = all follow Vari-Speed, 50% = an
  even mix, 100% = all backwards.
- **Spread Intervals** (Micro, knob INTV): Spread can now jump only to set
  intervals, up or down, or stay at unison. Free (as before), Semitones,
  Octaves, Fifths (fourths and fifths), Major, Minor. Spread sets the widest
  jump allowed.
- **About / DSP load** in the Heimat Solo menu: version, Heimat's own share of
  each audio block (average and recent peak) and the number of blocks that ran
  over time. Both modules answer `dsp_load` (`"average peak over"`).
- The saved state carries `"state_format": 1`.
- Test sheet for beta testers (`BETA.md`, `BETA.it.md`) and an issue form for
  reports.
- Continuous integration: every push runs the tests (also with
  AddressSanitizer and UBSan) and a real ARM64 build.

### Changed
- Heimat Solo MICRO page: knobs 7 and 8 are now Spread Intervals and Gene
  Reverse; Gene-Size and Morph stay on PLAY.
- A complete saved state (one with `sample_path`) now restores every
  parameter: controls it does not mention go back to their default instead of
  keeping the values of the patch before. A partial state still touches only
  the keys it names, and unknown keys are ignored.
- Parameter keys are frozen from this version on (`tests/fixtures/params_0.7.0.txt`).
- Releases tagged `v0.x` are published as pre-releases.

## [0.6.0]

### Added
- **HEADS**: a loop of tape with up to four playback heads per side, between
  the delay and the reverb. Time as a note value at Heimat's tempo, Spacing,
  Feedback, Reverse heads, six kinds of Wear (Dark, Thin, Crush, Warm, Fold,
  Fuzz), Mode (Fade, Glide, Rise, Fall), Routing (Own, Cross, Chain, Wander),
  Freeze and Clear. With Mix at 0 the heads stop and cost nothing.
- Heimat Solo: HEADS and HEADS 2 pages, Shift + Track 3 jumps to HEADS, *Clear
  heads* in the menu.
- Heimat Solo passes the input and the effect tails through even with an
  empty reel.
- The operation manual (GitHub Pages site in `docs/`) and the browser editor
  for Schwung's Remote UI, generated from the same parameter table.

## [0.5.0] and earlier

Development versions: the gene engine (Gene-Size, Slide, Morph, Vari-Speed,
Organize), splices from markers or equal division, Pitch / Splice / Drone pad
modes, the Micro tools (envelopes, Scatter, Spread, Width, selective
granulation), EQ, delay and reverb, clock from the internal BPM, the Move or
MIDI, and Heimat Solo with recording, Sound on Sound, marker editing and saving
reels. 0.5.0 fixed the spacing of overlapping genes.

[0.14.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.14.0
[0.13.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.13.0
[0.12.1]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.12.1
[0.12.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.12.0
[0.11.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.11.0
[0.10.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.10.0
[0.9.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.9.0
[0.8.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.8.0
[0.7.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.7.0
[0.6.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.6.0
[0.5.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.5.0
