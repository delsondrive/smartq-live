#pragma once

#include <vector>

#include "model/Documents.h"

namespace sqlive::model {

// The things a key or a pedal can do. The model knows them so that controls.set can check a name and say which action
// already had a key; the app says what each one does.
struct ActionInfo { juce::String name, title; ControlBinding defaultBinding; };

const std::vector<ActionInfo>& actionInfos();
const ActionInfo* actionInfo(const juce::String& name);            // nullptr when there is no such action

}  // namespace sqlive::model
