# Heimat 0.7.0 — beta test sheet

Thank you for testing. Heimat has passed its offline tests, but it has not
yet spent hours on real Moves. That is what this sheet is for. Each section
takes 5–15 minutes; do the ones you can, in any order, and tick what works.
When something does not, open a **Beta report** issue (the form asks for
everything below).

Use headphones or a line-in cable for anything that records: with the built-in
speakers the microphone hears the speakers.

## 0. Before you start

- [ ] Note your **Move firmware** version and **Schwung** version.
- [ ] Install both tarballs (Schwung Manager → *Modules* → custom module, or
      `scripts/install.sh`) and restart the Move.
- [ ] Open Heimat Solo → **Menu** → **About / DSP load**: it reads
      `HEIMAT 0.7.0 BETA`. Keep this screen in mind: you will use it for the
      load figures.

**How to read About / DSP load.** `DSP` is the average share of each audio
block that Heimat itself takes, `PEAK` the highest recent block, `OVER` how
many blocks took longer than real time since Heimat started. Only Heimat is
measured, not the rest of the Move. `OVER` above 0 means a click or a gap was
likely: always report it, with what was running.

## 1. Loading and playing (Heimat Solo)

- [ ] Load WAVs of different kinds: 16 and 24 bit, 32-bit float, mono and
      stereo, 44.1 and 48 kHz, one with cue markers. Each plays at the right
      pitch and speed; markers become splices.
- [ ] Load a long file (2–3 minutes) **while something is playing**: no click
      or gap during loading. Files longer than 2.9 minutes are cut there.
- [ ] Pads in Pitch, Splice and Drone mode; steps 1–16 select splices; Left /
      Right and Up / Down do what the README says.
- [ ] The screen is readable: no text cut off or overlapping, the knob readout
      never covers the waveform.

## 2. Recording

- [ ] Record into a new splice (Shift + Record or Sample), from the
      microphone and from line in; Input Gain behaves.
- [ ] Record over a splice; Sound on Sound from 0 (input only) to 1.
- [ ] With the clock running and Record Sync on, recording waits for the next
      tick to start and to stop.
- [ ] Save the reel (Menu → Save reel). It appears in
      `UserLibrary/Samples/Heimat` with its markers; load it back.
- [ ] Undo after a new-splice recording and after a marker edit.

## 3. Genes and Micro

- [ ] PLAY page: Gene-Size, Slide, Morph, Vari-Speed (also backwards and
      stopped), Organize.
- [ ] MICRO page: each envelope, Scatter, Spread, Width, Selective and
      Threshold.
- [ ] **Gene Reverse (GREV)**: at 50% you hear forwards and backwards genes
      mixed; at 100% all go backwards, also with Vari-Speed backwards (then
      they play forwards).
- [ ] **Spread Intervals (INTV)**: Spread at 12, Octaves → three clear
      octaves; Fifths, Major, Minor → recognisable chords; Free → a blur.

## 4. Effects and HEADS

- [ ] EQ, delay (free and synced), ping-pong, reverb: everything at maximum
      for a minute — loud, maybe wild, but no runaway, no silence afterwards.
- [ ] HEADS: every Routing and Mode, every Wear at full feedback; Freeze holds
      the tape; *Clear heads* silences them at once.
- [ ] Heads follow the tempo: change the internal BPM and the Move's tempo.

## 5. Load (the most important section)

Set up the heaviest patch you can and read **About / DSP load** after a
minute of each step. Write down `DSP`, `PEAK` and `OVER`.

1. A reel playing, Gene-Size about 0.3, Morph at maximum, Scatter 0.5.
2. Add delay and reverb mix 0.5.
3. Add HEADS: Mix 1, 4 heads, Wander, Rise, Reverse heads 100%, Wear Fold 50%.
4. Record into a new splice while all of that runs.
5. Save the reel while all of that runs.

- [ ] `OVER` stays at 0 through all five steps.
- [ ] No clicks or gaps you can hear.

## 6. Heimat in a track slot (chain module)

- [ ] Heimat as the sound generator on each of the four tracks, played from
      pads and from the Move's sequencer.
- [ ] Two or more Heimat instances at once, together with other Schwung
      modules: no clicks.
- [ ] Save a set, power the Move off and on, open the set: every Heimat comes
      back with its sample and its settings.
- [ ] If you have a set saved with Heimat 0.6 or older: it opens, the old
      settings are kept, the new controls sit at their default.

## 7. Browser editor

- [ ] Schwung Manager → *Remote UI*: Heimat Solo under the **Tool** tab, Heimat
      in a slot in the slot's view.
- [ ] Turning a knob in the browser moves it on the Move and the other way
      round; the reel, genes and levels move while it plays (Solo).
- [ ] On a phone: everything fits, nothing scrolls sideways.

## 8. Long run and leaving

- [ ] Leave Heimat Solo playing for an hour with a moving patch: no crash,
      `OVER` still 0, the Move still responsive.
- [ ] Shift + Back leaves Heimat Solo; open it again: the settings and the
      last loaded or saved reel come back. Same after a power cycle. (A
      recording you have not saved is not kept: that is expected.)

## What to put in a report

- Heimat version (About), Schwung and Move firmware versions.
- Which module (Solo or track slot) and which section of this sheet.
- What you did, what happened, what you expected.
- The `DSP / PEAK / OVER` figures if sound or timing is involved.
- If it is about settings: the file `/data/UserData/schwung/heimat/state.json`
  (Heimat Solo's last session). If it is about a sample: the WAV, if you can
  share it.
