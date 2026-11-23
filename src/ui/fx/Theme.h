#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>

#include "ui/fx/params/Slots.h"

namespace smartq::theme {

// Smart Q's own character: a warm near-black, the sound drawn as a thin ember line, and the EQ as a cool,
// bright curve over it. Everything else stays quiet, so that those and the band colours are what the eye
// goes to.
inline const juce::Colour backgroundTop{0xff0e0c11};
inline const juce::Colour backgroundBottom{0xff17131b};
inline const juce::Colour backgroundGlow{0xff241b2c};
inline const juce::Colour panelBottom{0xff100d13};

inline const juce::Colour field{0x12ffffff};
inline const juce::Colour fieldHover{0x20ffffff};
inline const juce::Colour hairline{0x1cffffff};
inline const juce::Colour popup{0xff211b27};
inline const juce::Colour labelPlate{0x8c0e0c11};

inline const juce::Colour gridMinor{0x07ffffff};
inline const juce::Colour gridMajor{0x0effffff};
inline const juce::Colour gridZero{0x30ffffff};

inline const juce::Colour textBright{0xfff5f2f6};
inline const juce::Colour textMid{0xffc9c2cf};
inline const juce::Colour textDim{0xff9a90a2};
inline const juce::Colour danger{0xffff6f7d};
inline const juce::Colour caution{0xffffb547};

inline const juce::Colour spectrumBefore{0xff8d7f96};
inline const juce::Colour spectrumAfter{0xfff29a78};
inline const juce::Colour totalCurve{0xffdcfff3};
inline const juce::Colour totalGlow{0xff6ff0c8};

// A Dynamic's page: its panes, and the bright middle of its curve, whose glow is the module's accent.
inline const juce::Colour pane{0xff121016};
inline const juce::Colour dynamicCurve{0xfffff0d6};

// Twelve hues of even brightness, chosen to stay apart on the dark background. The second twelve bands
// repeat them lighter, and every handle carries its number.
inline juce::Colour bandColour(int band) {
    static const std::array<juce::Colour, 12> hues{
        juce::Colour(0xff6ea8ff),  // blue
        juce::Colour(0xffff7d93),  // rose
        juce::Colour(0xff5ed8a2),  // mint
        juce::Colour(0xffb69dff),  // violet
        juce::Colour(0xffffc163),  // amber
        juce::Colour(0xff58d3e8),  // cyan
        juce::Colour(0xffff8fd2),  // pink
        juce::Colour(0xffa8dc68),  // lime
        juce::Colour(0xffff9a68),  // coral
        juce::Colour(0xff8491ff),  // indigo
        juce::Colour(0xffe9d568),  // sand
        juce::Colour(0xff4fcabd),  // teal
    };
    const auto hue = hues[static_cast<size_t>(band) % hues.size()];
    return band < static_cast<int>(hues.size()) ? hue : hue.interpolatedWith(juce::Colours::white, 0.3f);
}

// The colour a kind of module is known by in the chain column, and on its page.
inline juce::Colour moduleAccent(ModuleType type) {
    switch (type) {
        case ModuleType::Eq: break;
        case ModuleType::Dynamic: return juce::Colour(0xffffc163);
        case ModuleType::Gate: return juce::Colour(0xff6ea8ff);
        case ModuleType::DeEss: return juce::Colour(0xffff8fd2);
        case ModuleType::Drive: return juce::Colour(0xffff9a68);
        case ModuleType::Limit: return juce::Colour(0xffff7d93);
        case ModuleType::Width: return juce::Colour(0xffb69dff);      // lavender
        case ModuleType::Multiband: return juce::Colour(0xffc6f06a);  // lime
        case ModuleType::Transient: return juce::Colour(0xff5ee6f0);  // cyan
        case ModuleType::Resonance: return juce::Colour(0xffe070ff);  // orchid, ruled with the mockup
    }
    return totalGlow;
}

// One platform face in a few sizes. Segoe UI's figures are tabular by default, so values don't jitter
// sideways as they change.
inline constexpr float kSizeCaption = 10.5f, kSizeSmall = 11.5f, kSizeBody = 13.0f, kSizeHint = 15.0f, kSizeChip = 26.0f;

inline juce::Font font(float height, bool semibold = false) {
    return juce::Font(juce::FontOptions("Segoe UI", semibold ? "Semibold" : "Regular", height));
}

}  // namespace smartq::theme
