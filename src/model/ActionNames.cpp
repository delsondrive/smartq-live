#include "model/ActionNames.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::model {

namespace {
ControlBinding key(int keyCode) { ControlBinding b; b.keyCode = keyCode; return b; }
}  // namespace

const std::vector<ActionInfo>& actionInfos() {
    // The titles are the mockup's rows; its "Next song: later / earlier" row is two here, each needing its own key and pedal.
    static const std::vector<ActionInfo> infos = {
        {"start", "Start the song", key(juce::KeyPress::spaceKey)},
        {"stop", "Stop", key(0)},
        {"nextSongLater", "Next song: later", key(juce::KeyPress::rightKey)},
        {"nextSongEarlier", "Next song: earlier", key(juce::KeyPress::leftKey)},
        {"toggleView", juce::String::fromUTF8("Main view \xE2\x87\x84 stage view"), key(juce::KeyPress::tabKey)},
    };
    return infos;
}

const ActionInfo* actionInfo(const juce::String& name) {
    for (auto& info : actionInfos())
        if (info.name == name) return &info;
    return nullptr;
}

}  // namespace sqlive::model
