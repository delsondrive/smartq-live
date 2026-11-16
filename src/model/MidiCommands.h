#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// Registers the commands for MIDI: device.add, device.set, device.setPatches, device.remove, rig.setMidi, marker.add,
// marker.set, marker.remove, controls.set, song.setSections and song.importMidi. A command that creates something returns
// the new id in CommandResult::value. Every refusal is a plain sentence.
//
// song.importMidi {songId, content, choice} (MidiFileContent and MidiImportChoice in their var forms) is one step: with
// choice.tempo the song's tempo becomes the file's, with choice.sections its sections become the file's (new ids), and each
// chosen track's changes replace its device's lane (new ids; two tracks to one device are merged in position order).
void registerMidiCommands(CommandLayer&);

}  // namespace sqlive::model
