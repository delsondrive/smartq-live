#pragma once

#include "model/CommandLayer.h"

namespace sqlive::model {

// Registers every command that edits the band: its name and level, roles, songs and their parts, setlists, rigs, outputs and mixes.
// A command that creates something returns the new id in CommandResult::value. Every refusal is a plain sentence.
void registerCoreCommands(CommandLayer&);

}  // namespace sqlive::model
