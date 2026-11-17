# Changelog

Heimat follows [semantic versioning](https://semver.org): from 1.0 on, a minor
version (1.1, 1.2) adds features without breaking saved sessions or patches,
and a patch version (1.0.1) only fixes things. Before 1.0 every version is a
development or beta release.

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

[0.7.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.7.0
[0.6.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.6.0
[0.5.0]: https://github.com/CairnDebil/schwung-heimat/releases/tag/v0.5.0
