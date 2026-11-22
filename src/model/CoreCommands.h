#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// Registers every command that edits the band: its name and level, roles, songs and their parts (their pans too), setlists,
// rigs, outputs, mixes and each role's own pan on an output (mix.setPan).
// A command that creates something returns the new id in CommandResult::value. Every refusal is a plain sentence.
void registerCoreCommands(CommandLayer&);

}  // namespace sqlive::model
