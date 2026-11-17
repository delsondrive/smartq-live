#pragma once

#include <vector>

#include "model/BandModel.h"
#include "model/CommandLayer.h"

namespace sqlive::model {

// Registers the commands for sections, arrangements, cues and automation: section.add, section.set, section.remove,
// arrangement.add, arrangement.set, arrangement.remove, song.setJumpTiming, cue.add, cue.set, cue.remove, automation.set,
// automation.fadeIn, automation.muteIn, marker.copyToType, sectionType.add, sectionType.set, sectionType.remove,
// band.setCueLead and setlist.setArrangement. A command that creates something returns the new id in CommandResult::value.
// Every refusal is a plain sentence. The section commands work on the song's sections as tileSections lays them out and store
// that, so a song's sections are tidy once edited; a section that is gone leaves every arrangement (see below).
void registerSectionCommands(CommandLayer&);

// What song.setSections, song.importMidi and the section commands share.
//
// The incoming sections (already in bar order, no two at one bar) take the id of the stored section that starts at the same
// bar and has the same name (a section renamed or swapped for another is a new one), and its type unless they name another;
// one still without a type gets guessSectionTypeId(name). An incoming id that would now be used twice is replaced by a new
// one.
void keepSectionIdentities(const SongDoc& stored, const BandDoc&, std::vector<SectionDoc>& incoming);

// Takes out of the song's arrangements every step whose section the song no longer stores. An arrangement left with no steps
// is removed, and the setlist entries using it go back to "" (the sections in order).
void dropGoneArrangementSteps(BandModel&, const juce::String& songId);

}  // namespace sqlive::model
