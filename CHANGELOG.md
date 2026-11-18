# Changelog

Heimat follows [semantic versioning](https://semver.org): from 1.0 on, a minor
version (1.1, 1.2) adds features without breaking saved sessions or patches,
and a patch version (1.0.1) only fixes things. Before 1.0 every version is a
development or beta release.

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

[0.12.1]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.12.1
[0.12.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.12.0
[0.11.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.11.0
[0.10.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.10.0
[0.9.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.9.0
[0.8.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.8.0
[0.7.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.7.0
[0.6.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.6.0
[0.5.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.5.0
