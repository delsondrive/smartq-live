#include "ui/fx/HoldButton.h"

#include <utility>

#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kCorner = 8.0f;

}  // namespace

// Hears every parent's visibility, which the button's own visibilityChanged doesn't, and follows a change of parents.
class HoldButton::ParentWatcher final : public juce::ComponentMovementWatcher {
public:
    explicit ParentWatcher(HoldButton& owner) : juce::ComponentMovementWatcher(&owner), owner_(owner) {}

    using juce::ComponentMovementWatcher::componentMovedOrResized;
    void componentMovedOrResized(bool, bool) override {}
    void componentPeerChanged() override {}
    void componentVisibilityChanged() override {}
    void componentVisibilityChanged(juce::Component&) override { owner_.letGoIfHidden(); }

private:
    HoldButton& owner_;
};

HoldButton::HoldButton(juce::String text) : text_(std::move(text)), accent_(theme::totalGlow) {
    setName(text_);
    setTitle(text_);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    watcher_ = std::make_unique<ParentWatcher>(*this);
}

HoldButton::~HoldButton() {
    watcher_.reset();
    setHeld(false);
}

void HoldButton::setAccent(juce::Colour accent) {
    if (accent == accent_) return;
    accent_ = accent;
    repaint();
}

// The window at the top is shown and hidden by the host, and a hidden window takes no press, so its own flag isn't asked.
bool HoldButton::canBeHeld() const {
    if (getParentComponent() == nullptr || !isEnabled()) return false;
    const juce::Component* component = this;
    for (auto* parent = getParentComponent(); parent != nullptr; component = parent, parent = parent->getParentComponent())
        if (!component->isVisible()) return false;
    return true;
}

void HoldButton::setHeld(bool held) {
    if (held == held_) return;
    held_ = held;
    repaint();
    if (onHeld) onHeld(held);
}

void HoldButton::letGoIfHidden() {
    if (held_ && !canBeHeld()) setHeld(false);
}

void HoldButton::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isLeftButtonDown() && canBeHeld()) setHeld(true);
}

// JUCE gives the release to the component that took the press, wherever the pointer has gone.
void HoldButton::mouseUp(const juce::MouseEvent&) { setHeld(false); }

void HoldButton::visibilityChanged() { letGoIfHidden(); }
void HoldButton::enablementChanged() {
    letGoIfHidden();
    repaint();
}
void HoldButton::parentHierarchyChanged() { letGoIfHidden(); }

void HoldButton::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat().reduced(0.5f);
    const float dim = isEnabled() ? 1.0f : 0.4f;

    g.setColour(accent_.withAlpha((held_ ? 0.28f : 0.10f) * dim));
    g.fillRoundedRectangle(area, kCorner);

    // Pressed in while held: a shade along the inside of its top edge.
    if (held_) {
        juce::Graphics::ScopedSaveState inside(g);
        juce::Path key;
        key.addRoundedRectangle(area, kCorner);
        g.reduceClipRegion(key);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.45f), 0.0f, area.getY(), juce::Colours::black.withAlpha(0.0f), 0.0f,
                                               area.getY() + 7.0f, false));
        g.fillRect(area.withHeight(7.0f));
    }

    g.setColour(accent_.withAlpha((held_ ? 1.0f : 0.7f) * dim));
    g.drawRoundedRectangle(area, kCorner, 1.0f);

    g.setColour(theme::textBright.withAlpha(dim));
    g.setFont(theme::font(theme::kSizeBody, true));
    g.drawText(text_, getLocalBounds(), juce::Justification::centred, false);
}

// A screen reader hears it as a button by its name. It can't hold it down, and so has nothing to press.
std::unique_ptr<juce::AccessibilityHandler> HoldButton::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button);
}

}  // namespace smartq
