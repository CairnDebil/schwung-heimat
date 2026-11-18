# The track module (suspended)

Up to version 0.8.0 Heimat came as two modules built from one engine:

* **Heimat**, a sound generator for a Signal Chain slot (`id: heimat`,
  `HM_SOLO=0`), driven by Schwung's shared sound-generator UI;
* **Heimat Solo**, the standalone instrument that takes over the Move
  (`id: heimat-solo`, `HM_SOLO=1`).

From 0.9.0 only the standalone instrument is developed, and it is simply called
Heimat (its id stays `heimat-solo`, so installed copies update in place and
keep their settings). The track module is suspended, not abandoned. This folder
keeps what is needed to bring it back:

| File | What it is |
|---|---|
| `heimat-0.8.0.c` | the engine as of 0.8.0, with both builds (`-DHM_SOLO=0` for the track module) |
| `module.json` | the track module's manifest (`component_type: sound_generator`, chainable) |
| `help.json` | its on-screen help |
| `ui.js` | its UI: Schwung's shared sound-generator UI, pages from `ui_hierarchy` |
| `web_ui-0.8.0.html` | the browser editor as of 0.8.0, with the track-slot mode (keys prefixed `synth:`) |
| `build-0.8.0.sh` | the build script that produced both modules |

Nothing here is compiled, tested or released.

## Bringing it back

The engine in `src/dsp/heimat.c` moved on after 0.8.0, so the simplest path is
to start from the current engine and put back the few parts that only the track
module needs. Compare with `heimat-0.8.0.c`:

1. the `HM_SOLO` switch (default 0) and the places that test it: no audio input,
   no settings file, Pitch / Momentary pads at start, no empty reel at start,
   no Remote UI Tool-tab keys (`module_id`, `rui_poll`, flat `state`);
2. `get_chain_params` and `get_ui_hierarchy`, answered from `v2_get_param` as
   `chain_params` and `ui_hierarchy` (the pages the shared UI shows: Heimat,
   Reel / Splices, Play / Pads, Micro, EQ, Heads, Reverb, Morph Chords);
3. a second compile in `scripts/build.sh` (`-DHM_SOLO=0`) packaged with this
   folder's `module.json`, `help.json` and `ui.js` into
   `dist/heimat-module.tar.gz`, installed to
   `modules/sound_generators/heimat`;
4. the track-slot mode of the editor (`mode === "chain"` in
   `web_ui-0.8.0.html`) and the `HM_SOLO=0` run of the DSP tests.

The parameter keys are shared, so sets saved with the 0.8.0 track module open
again once it is back.
