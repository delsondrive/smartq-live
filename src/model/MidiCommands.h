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
//
// song.setSections {songId, sections}: the sections are put in bar order, each ending where the next starts at the latest,
// and refused when two start at the same bar: "Two sections can't start at the same bar." A section that starts at the same
// bar as a stored one of the same name keeps its id, and its type unless the new one names another; one with no type gets
// the type its name suggests (guessSectionTypeId). Steps whose section is gone leave every arrangement, as in
// section.remove. song.importMidi does the same with the file's sections.
void registerMidiCommands(CommandLayer&);

namespace commands {
// Puts a lane's markers in position order (bar, then beat), keeping the order of two at one place.
void sortLane(std::vector<MarkerDoc>&);
}  // namespace commands

}  // namespace sqlive::model
