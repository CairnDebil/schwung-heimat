/*
 * Heimat UI for Schwung
 *
 * Uses the shared sound-generator UI. Parameter pages and the file browser
 * come from the DSP (get_param "ui_hierarchy" / "chain_params").
 *
 * MIT License
 */

import { createSoundGeneratorUI } from '/data/UserData/schwung/shared/sound_generator_ui.mjs';

const ui = createSoundGeneratorUI({
    moduleName: 'Heimat',

    onPresetChange: () => {
        host_module_set_param('all_notes_off', '1');
    },

    showPolyphony: false,
    showOctave: true,
});

globalThis.init = ui.init;
globalThis.tick = ui.tick;
globalThis.onMidiMessageInternal = ui.onMidiMessageInternal;
globalThis.onMidiMessageExternal = ui.onMidiMessageExternal;
