#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// Registers the commands for MIDI: device.add, device.set, device.setPatches, device.remove, rig.setMidi, marker.add,
// marker.set, marker.remove, controls.set and song.setSections. A command that creates something returns the new id in
// CommandResult::value. Every refusal is a plain sentence.
void registerMidiCommands(CommandLayer&);

}  // namespace sqlive::model
