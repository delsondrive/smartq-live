#include "model/ActionNames.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace sqlive::model {

namespace {
ControlBinding key(int keyCode) { ControlBinding b; b.keyCode = keyCode; return b; }
}  // namespace

const std::vector<ActionInfo>& actionInfos() {
    // The titles are the mockup's rows; its "Next song: later / earlier" row is two here, each needing its own key and pedal,
    // and so are its section rows. "Go to the next song now" has no key of its own: holding Next song: later's key does it.
    static const std::vector<ActionInfo> infos = [] {
        std::vector<ActionInfo> all = {
            {"start", "Start the song", key(juce::KeyPress::spaceKey)},
            {"stop", "Stop", key(0)},
            {"nextSongLater", "Next song: later", key(juce::KeyPress::rightKey)},
            {"nextSongEarlier", "Next song: earlier", key(juce::KeyPress::leftKey)},
            {"nextSectionLater", "Next section: later", key(juce::KeyPress::downKey)},
            {"nextSectionEarlier", "Next section: earlier", key(juce::KeyPress::upKey)},
        };
        for (int i = 1; i <= 10; ++i)                               // keys 1 to 9, then 0
            all.push_back({"section" + juce::String(i), "Choose section " + juce::String(i), key(i < 10 ? '0' + i : '0')});
        all.push_back({"loopSection", "Loop this section", key('L')});
        all.push_back({"cancelSection", "Cancel the choice or loop", key(juce::KeyPress::backspaceKey)});
        all.push_back({"goToNextSongNow", "Go to the next song now", key(0)});
        all.push_back({"toggleView", juce::String::fromUTF8("Main view \xE2\x87\x84 stage view"), key(juce::KeyPress::tabKey)});
        return all;
    }();
    return infos;
}

const ActionInfo* actionInfo(const juce::String& name) {
    for (auto& info : actionInfos())
        if (info.name == name) return &info;
    return nullptr;
}

}  // namespace sqlive::model
