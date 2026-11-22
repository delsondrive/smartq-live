#include "ui/WindowFrame.h"

#include <utility>

namespace sqlive::ui {

namespace {

// Windows' WM_NCHITTEST answers (winuser.h), here so this part builds and is tested everywhere.
constexpr int kClient = 1, kCaption = 2, kMinButton = 8, kMaxButton = 9, kTop = 12, kClose = 20;

}  // namespace

int frameHitCode(WindowArea area, bool maximised, bool inTopResizeBand) {
    switch (area) {
        case WindowArea::Minimise: return kMinButton;
        case WindowArea::Maximise: return kMaxButton;
        case WindowArea::Close: return kClose;
        case WindowArea::Caption:
        case WindowArea::Client: break;
    }
    if (inTopResizeBand && !maximised) return kTop;
    return area == WindowArea::Caption ? kCaption : kClient;
}

WindowFrame::WindowFrame(juce::DocumentWindow& window, std::function<WindowArea(juce::Point<int>)> areaAt)
    : juce::ComponentMovementWatcher(&window), window_(window), areaAt_(std::move(areaAt)) {
    follow();
}

WindowFrame::~WindowFrame() { setHook(nullptr, false); }

bool WindowFrame::attached() const { return hwnd_ != nullptr; }

// JUCE calls this twice when it makes a new native window: first with none (the old one still there, about to go), then
// with the new one.
void WindowFrame::componentPeerChanged() { follow(); }

}  // namespace sqlive::ui
