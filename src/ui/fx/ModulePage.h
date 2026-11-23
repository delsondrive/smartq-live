#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace smartq {

// A module's page in the window, whatever the module. The editor makes it when the module is first shown and keeps it for as
// long as the module is in the chain, stepping it only while it is on show. Destroying it closes whatever host gesture a drag
// on it has open, as closing the window does.
class ModulePage : public juce::Component {
public:
    // One step of the display: reads what the page shows. dtSeconds is the time since the last step.
    virtual void update(double dtSeconds) = 0;

    // Whether a layer's picture went with the graphics card, and has not been drawn again.
    virtual bool anyLayerLost() const = 0;

    // It has just come on show again: what it was showing when it went is out of date.
    virtual void pageShown() {}

    // The clock on any gesture held open across a pause, which runs at every step of the window's timer, on show or not.
    virtual void advanceGestures(double /*dtSeconds*/) {}

    // Something else has taken over: closes any such gesture at once.
    virtual void endGestures() {}
};

}  // namespace smartq
