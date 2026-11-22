#include "ui/WindowFrame.h"

namespace sqlive::ui {

// No frame of the app's own off Windows: the window keeps its native title bar, and the top bar shows no window buttons.
void WindowFrame::follow() {}
void WindowFrame::setHook(void*, bool) {}
bool WindowFrame::maximised() const { return false; }
void WindowFrame::paintNativeCaption(juce::DocumentWindow&) {}

}  // namespace sqlive::ui
