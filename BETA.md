# Heimat 0.15.0 — beta test sheet

Thank you for testing. Heimat has passed its offline tests, but it has not
yet spent hours on real Moves. That is what this sheet is for. Each section
takes 5–15 minutes; do the ones you can, in any order, and tick what works.
When something does not, open a **Beta report** issue (the form asks for
everything below).

Use headphones or a line-in cable for anything that records: with the built-in
speakers the microphone hears the speakers.

## 0. Before you start

- [ ] Note your **Move firmware** version and **Schwung** version.
- [ ] Install `heimat-solo-module.tar.gz` (Schwung Manager → *Modules* →
      custom module, or `scripts/install.sh`) and restart the Move.
- [ ] Open Heimat → **Menu** → **About / DSP load**: it reads
      `HEIMAT 0.15.0 BETA`. Keep this screen in mind: you will use it for the
      load figures.

**How to read About / DSP load.** `DSP` is the average share of each audio
block that Heimat itself takes, `PEAK` the highest recent block, `OVER` how
many blocks took longer than real time since Heimat started. Only Heimat is
measured, not the rest of the Move. `OVER` above 0 means a click or a gap was
likely: always report it, with what was running.

## 1. Loading and playing

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
- [ ] **Trim Key Noise** (LOOP, KEYS) at 100 and 200 ms — **again, 0.15**:
      the trim now counts from the **release** of the key. Record a new
      splice pressing Record / Sample and holding it a moment: the click of
      the press and of the release are gone, at the start and at the end.
      Same on an overdub over a splice. With Record Sync on, nothing is cut.
      If 100 ms is enough, note it: that was the value that fell short.
- [ ] **TRIM** (Shift + Capture) — "OK, not perfect" in 0.14: write in the
      report **what** was not perfect (the points, the zoom, the listening,
      the cut?). Move start and end, zoom, nudge with the
      jog; while trimming you hear only what will stay. Cut with the jog
      click: no click at the cut, the next splices are intact. Undo: all back.
      Back without cutting: nothing changes. Try it on a 2-minute reel too:
      no gap in the sound while it cuts. Tap a Track button while trimming:
      the trim is cancelled and that Track's first page opens.
- [ ] **LOOP page** (Track 3 twice): Record Length 1 bar with the clock
      running: the recording stops by itself after one bar, the bar fills on
      screen. With 2 and 4 bars, also with Record Sync on.
- [ ] After Recording = Overdub: at the end of the bar the loop plays and
      recording goes on over it (OVERDUB on screen); Record stops it. Also when
      you stop a Free recording yourself.
- [ ] The Vari-Speed head is gone: the LOOP page has KEYS on knob 3 and SRC
      (Record Source) on knob 6; the screen shows KEY TRIM instead of HEAD.

## 3. Genes and Micro

- [ ] PLAY page: Gene-Size, Slide, Morph, Vari-Speed (also backwards and
      stopped), Organize.
- [ ] MICRO page: each envelope, Scatter, Spread, Width, Selective and
      Threshold.
- [ ] **Gene Reverse (GREV)** (not tested in 0.14): at 50% you hear forwards and backwards genes
      mixed; at 100% all go backwards, also with Vari-Speed backwards (then
      they play forwards).
- [ ] **Spread Intervals (INTV)**: Spread at 12, Octaves → three clear
      octaves; Fifths, Major, Minor → recognisable chords; Free → a blur.
- [ ] **Pattern (MICRO 2)**: Scatter and Spread up, Pattern 4: the cloud
      repeats a four-gene phrase; another SEED, another phrase; a new pad
      starts it from the top; the boxes on screen follow the genes. Off: free
      chance again.

## 3a. Pages by family (new, 0.15)

- [ ] Track 1: PLAY, again MICRO, again MICRO 2, again PLAY.
- [ ] Track 2: REEL, again SETUP 1, again SETUP 2, again REEL.
- [ ] Track 3: REC, again LOOP, again REC.
- [ ] Track 4: EQ, HEADS, HEADS 2, REVERB, REVERB 2, back to EQ (no MICRO
      or SETUP).
- [ ] From another family each Track button opens its first page; the lit
      Track button is the family on screen (on MICRO, Track 1).
- [ ] Shift + Track 1 (MOD), Shift + Track 3 (HEADS), Shift + Track 4
      (REVERB, again EQ) as before.
- [ ] SETUP 2, knob 8: Volume again.

## 3b. Modulators (LFO 1, LFO 2, Envelope — new, 0.15)

- [ ] Shift + Track 1: MOD 1 (LFO 1 and LFO 2, four knobs each), again MOD 2
      (Envelope and MRST; knobs 6–8 do nothing); the traces move.
- [ ] **Recheck (0.14: the percentage was not shown)**: on PLAY, hold Track 1
      and turn Gene-Size: LFO1 > GENE with a percentage that stays in view
      while you turn, finger on the knob too; Gene-Size breathes, its cell has
      a flag on the left. Let go: the page has not changed. Tap Track 2 alone:
      REEL opens as before.
- [ ] LFO 1 and LFO 2 at different rates on the same control (Morph, say): a
      movement that does not repeat soon.
- [ ] RAND shape at 1/4 with the clock running, onto Morph or Vari-Speed:
      steps on the beat; SMTH turns them into glides.
- [ ] Track 2 held: LFO 2. Track 3 held: Envelope. Track 4 held is no longer
      a modulator: it opens EQ at once.
- [ ] Envelope from the pads onto Scatter or Gene-Size, AD, Hold and Loop.
- [ ] MOD 2, MRST on: when the clock starts both LFOs start from the top.
- [ ] Hold Track 1 + Delete: LFO 1's routes are gone. Sixteen routes at most.
- [ ] Leave Heimat and open it again: the routes are back. A 0.14 session
      with routes on Wave: they are now on LFO 1 (Random and Follow routes
      are gone).
- [ ] **Editor**: the Modulators panel has three columns; each LFO is drawn
      as its shape (two cycles) with a moving cursor, and changing the shape
      changes the drawing; the Envelope shows attack and decay. Routes,
      sliders, Add route and the crosses work both ways with the Move.
- [ ] Load (section 5) with eight routes active: `OVER` stays 0.

## 3c. Playing safely (0.14)

- [ ] Hold Undo, turn several knobs and play pads, let go: everything is
      back. Tap Undo alone: it undoes as before. Shift + Undo: TMP stays in the
      header until Undo again.
- [ ] **Check first**: Undo, Delete and the Track buttons react when
      released (needed for holding them). If holding Undo does nothing, report
      it.
- [ ] **Recheck (0.14: markers were cleared)**: hold Delete for a while,
      wait, then turn a knob: back to its start value, no marker deleted; same
      with a touch. Hold Delete alone for 3 seconds and let go: nothing
      happens. A short tap of Delete still deletes the marker under the
      cursor. Shift + Delete held 2 s: new reel.
- [ ] Hold Track 1 and touch a modulated knob without turning: ONE WAY /
      BOTH WAYS; the tick on the bar moves accordingly.
- [ ] PLAY knob 6: DJ filter, smooth from both sides, no click through the
      middle.

## 3d. Patches and Init (new, 0.15)

- [ ] Menu → Save patch, slot 1: saved. Change the sound, Menu → Load patch,
      slot 1: it is back. The reel, the tempo, SOS and the volume do not change.
- [ ] Save patch onto a full slot: CLICK AGAIN: REPLACE; the second click
      replaces it.
- [ ] Load patch on an empty slot: SLOT N IS EMPTY, nothing loads.
- [ ] Menu → Init patch: asks to confirm; on the second click the whole sound
      goes back to the start and the routes are gone. Load patch → BEFORE
      INIT: the sound from before is back.
- [ ] Leave, reopen, power off and on: the patches are still there.
- [ ] Editor, Play panel: slot, Save / Load / Init work and the full slots
      show (without "(empty)").
- [ ] Load a patch while playing: no click or gap (note `OVER`).

## 4. Effects and HEADS

- [ ] Each effect on its own page: Track 4 goes EQ, HEADS, HEADS 2, REVERB,
      REVERB 2; Shift + Track 4 opens REVERB.
- [ ] Encoders: one slow click moves a little, a fast turn sweeps; choices
      (envelope, routing, on/off) move one step every two clicks and never skip.
- [ ] Gene-Size: the first half of the knob for long genes, the second half
      for grains; it should feel even from end to end.
- [ ] REVERB: sweep Size from a box to a sky while a sound plays (a pitch bend
      on the way is expected); Pre-Delay with and without Reverse; Decay at the
      top holds forever; Absorb from dense to dark; Depth left (chorus), right
      (wandering), far right (shimmer) at slow and fast Speed; Tilt dark and
      bright.
- [ ] **Noise** (0.13; in 0.14 "now and then more noise": if it happens, note
      which effect and which values): reverb Depth right and left with Speed slow and fast,
      shimmer, heads with Warm / Fold / Fuzz at high feedback: no hiss or
      crackle that was not in the sound; with nothing playing, silence.
- [ ] **Input Gate** (LOOP, GATE): with SOS below 1 and the reverb on, the
      microphone's hiss no longer rings on between phrases; playing, nothing
      is cut. Try -70 and -50 dB too.
- [ ] EQ and reverb at maximum, Decay forever, Depth at shimmer, for a minute —
      loud, maybe wild, but no runaway and no silence afterwards.
- [ ] HEADS: every Routing and Mode, every Wear at full feedback; Freeze holds
      the tape; *Clear heads* silences them at once.
- [ ] Heads follow the tempo: change the internal BPM and the Move's tempo.

## 5. Load (the most important section)

Set up the heaviest patch you can and read **About / DSP load** after a
minute of each step. Write down `DSP`, `PEAK` and `OVER`.

1. A reel playing, Gene-Size about 0.3, Morph at maximum, Scatter 0.5.
2. Add the reverb: mix 0.5, Size 0.9, Decay 0.8, Depth all the way right.
3. Add HEADS: Mix 1, 4 heads, Wander, Rise, Reverse heads 100%, Wear Fold 50%.
4. Record into a new splice while all of that runs.
5. Save the reel while all of that runs.

- [ ] `OVER` stays at 0 through all five steps.
- [ ] No clicks or gaps you can hear.

## 6. Updating from Heimat Solo

- [ ] Install 0.15.0 over an installed Heimat Solo: Schwung's module menu now
      shows **Heimat**, once, and no Heimat Solo.
- [ ] Open it: the settings and the last reel from before the update are
      there; saved reels are still in `Samples/Heimat`.
- [ ] If the old track-slot Heimat was installed: it is still among the sound
      generators until you remove it, and it does not get in the way.

## 7. Browser editor

- [ ] Schwung Manager → *Remote UI*: Heimat under the **Tool** tab while it
      runs.
- [ ] Turning a knob in the browser moves it on the Move and the other way
      round; the reel, genes and levels move while it plays.
- [ ] On a phone: everything fits, nothing scrolls sideways.

## 8. Long run and leaving

- [ ] Leave Heimat playing for an hour with a moving patch: no crash,
      `OVER` still 0, the Move still responsive.
- [ ] Shift + Back leaves Heimat; open it again: the settings and the
      last loaded or saved reel come back. Same after a power cycle. (A
      recording you have not saved is not kept: that is expected.)

## What to put in a report

- Heimat version (About), Schwung and Move firmware versions.
- Which section of this sheet.
- What you did, what happened, what you expected.
- The `DSP / PEAK / OVER` figures if sound or timing is involved.
- If it is about settings: the file `/data/UserData/schwung/heimat/state.json`
  (Heimat's last session). If it is about a sample: the WAV, if you can
  share it.
