#include "ui/Veil.h"

#include <algorithm>

#include "ui/Theme.h"

namespace sqlive::ui {

namespace {
constexpr int kVeilMargin = 80;
}

Veil::Veil(std::unique_ptr<juce::Component> content, int width, std::function<int()> idealHeight)
    : content_(std::move(content)), width_(width), idealHeight_(std::move(idealHeight)) {
    setWantsKeyboardFocus(true);
    addAndMakeVisible(*content_);
}

Veil::~Veil() {
    if (host_ != nullptr) host_->removeComponentListener(this);
}

void Veil::coverAndFollow(juce::Component& host) {
    host_ = &host;
    host.addComponentListener(this);
    host.addAndMakeVisible(this);
    setBounds(host.getLocalBounds());
    if (content_->isShowing()) content_->grabKeyboardFocus();
}

void Veil::paint(juce::Graphics& g) {
    g.fillAll(theme::veil);
    juce::DropShadow(theme::veilShadow, 80, {0, 30}).drawForRectangle(g, content_->getBounds());
}

void Veil::resized() {
    const int w = std::max(0, std::min(width_, getWidth() - kVeilMargin));
    content_->setSize(w, std::max(1, content_->getHeight()));   // so the height it asks for is for this width
    const int h = std::max(0, std::min(idealHeight_(), getHeight() - kVeilMargin));
    content_->setBounds(getLocalBounds().withSizeKeepingCentre(w, h));
}

void Veil::componentMovedOrResized(juce::Component& component, bool, bool wasResized) {
    if (&component == host_ && wasResized) setBounds(host_->getLocalBounds());
}

}  // namespace sqlive::ui
