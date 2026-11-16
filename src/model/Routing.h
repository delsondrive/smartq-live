#pragma once

#include <memory>
#include <vector>

#include <juce_core/juce_core.h>

#include "engine/MixSetup.h"
#include "model/Documents.h"

namespace sqlive::model {

// Click and cues may only go to in-ear (monitor) outputs; nothing goes to a timecode or unused output.
bool mayRoute(RoleKind, OutputLabel) noexcept;
juce::String whyNot(RoleKind, OutputLabel);                              // the sentence to show when mayRoute is false
int roleIndex(const BandDoc&, const juce::String& roleId) noexcept;     // its place in band.roles, -1 if absent
int firstSharedChannel(const OutputDoc&, const OutputDoc&) noexcept;    // the lowest device channel (1-based) both use; 0 = none

struct RoutingProblem {
    enum class Kind { ForbiddenSend, ChannelsOverlap, BeyondDevice, NothingHeard, ClickNotHeard } kind;
    juce::String outputId, roleId, message;
    bool blocksShow = false;
};

// deviceOutputChannels: 0 = not known.
std::vector<RoutingProblem> checkRig(const BandDoc&, const RigDoc&, int deviceOutputChannels);
std::shared_ptr<const engine::MixSetup> buildMixSetup(const BandDoc&, const RigDoc&, int deviceOutputChannels);

}  // namespace sqlive::model
