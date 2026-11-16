#pragma once

#include <cstdlib>
#include <initializer_list>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/LiveLookAndFeel.h"
#include "ui/Theme.h"

namespace sqlive::test {

// The component laid out at that size and painted, children included, into an image of its own size.
inline juce::Image snapshot(juce::Component& component, int w, int h) {
    component.setBounds(0, 0, w, h);
    return component.createComponentSnapshot(component.getLocalBounds());
}

inline bool near(juce::Colour a, juce::Colour b, int tolerance) {
    return std::abs(a.getRed() - b.getRed()) <= tolerance && std::abs(a.getGreen() - b.getGreen()) <= tolerance
           && std::abs(a.getBlue() - b.getBlue()) <= tolerance;
}

// Whether some pixel shows that colour: at least half opaque, and within the tolerance in every channel.
inline bool hasColour(const juce::Image& image, juce::Colour colour, int tolerance = 12) {
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto pixel = pixels.getPixelColour(x, y);
            if (pixel.getAlpha() >= 128 && near(pixel, colour, tolerance)) return true;
        }
    return false;
}

// The fraction of pixels that are drawn on: farther than 12 in some channel from every background colour (fully
// transparent, theme::bg1, bg2, glow and panel). A partly transparent pixel is judged as it shows over bg1.
inline double fractionNotBackground(const juce::Image& image) {
    const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
    if (pixels.width <= 0 || pixels.height <= 0) return 0.0;
    long long drawn = 0;
    for (int y = 0; y < pixels.height; ++y)
        for (int x = 0; x < pixels.width; ++x) {
            const auto shown = theme::bg1.overlaidWith(pixels.getPixelColour(x, y));
            bool background = false;
            for (const auto& b : {theme::bg1, theme::bg2, theme::glow, theme::panel}) background = background || near(shown, b, 12);
            if (!background) ++drawn;
        }
    return static_cast<double>(drawn) / (static_cast<double>(pixels.width) * pixels.height);
}

// Whether every visible child lies within its parent's bounds, down the whole tree. A juce::Viewport is checked like any
// child, but the component it shows may be larger than it, because it scrolls; that component's own children are checked.
inline bool childrenInside(const juce::Component& parent) {
    const auto* viewport = dynamic_cast<const juce::Viewport*>(parent.getParentComponent());
    for (const auto* child : parent.getChildren()) {
        if (!child->isVisible()) continue;
        const bool scrolls = viewport != nullptr && child == viewport->getViewedComponent();
        if (!scrolls && !parent.getLocalBounds().contains(child->getBounds())) return false;
        if (!childrenInside(*child)) return false;
    }
    return true;
}

// Makes Smart Q Live's look the default for as long as it lives. Declare it after the DemoSession or GUI initialiser,
// so the default is cleared even when a REQUIRE fails, and before the components it should draw.
struct ScopedLook {
    ui::LiveLookAndFeel look;
    ScopedLook() { juce::LookAndFeel::setDefaultLookAndFeel(&look); }
    ~ScopedLook() { juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
};

}  // namespace sqlive::test
