#include "ui/InterruptedBanner.h"

#include <cmath>

#include "app/AudioHost.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .banner.red: danger at 96%, text #160f10 at weight 600, 12 px by 18 px of padding, 14 px between the
// bold 18 px heading and the sentence.
const juce::Colour kInk{0xff160f10};
constexpr int kPadX = 18, kGap = 14;

const juce::String& warningSign() {
    static const auto text = juce::String::fromUTF8("\xe2\x9a\xa0 ");
    return text;
}

// The ⚠ in a font that has it as a plain glyph, so it takes the ink colour instead of falling back to a colour emoji.
juce::Font signFont() {
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI Symbol", "Regular", 18.0f).withPointHeight(18.0f));
#else
    return theme::font(18.0f, true);
#endif
}

int widthOf(const juce::Font& font, const juce::String& text) {
    return static_cast<int>(std::ceil(juce::GlyphArrangement::getStringWidth(font, text)));
}

}  // namespace

InterruptedBanner::InterruptedBanner(app::Session& session, const app::AudioHost* host) : session_(session), host_(host) {
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);
    refresh();
}

void InterruptedBanner::refresh() {
    const auto now = session_.now();
    const bool lost = host_ != nullptr && host_->isLost();
    juce::String heading, sentence;
    if (lost) {
        heading = warningSign() + "Audio interface disconnected";
        sentence = now.interrupted ? "Paused at bar " + juce::String(now.bar) + ". Waiting for it to come back."
                                   : juce::String("No song was playing. Waiting for it to come back.");
    } else if (now.interrupted) {
        heading = "The audio interface is back.";
        sentence = session_.mode() == app::Mode::Show ? "Press Space to start the song again from the top."
                                                      : "Play the song again when you're ready.";
    }
    const bool wanted = heading.isNotEmpty();
    if (wanted == wanted_ && heading == heading_ && sentence == sentence_) return;
    wanted_ = wanted;
    heading_ = heading;
    sentence_ = sentence;
    repaint();
}

void InterruptedBanner::paint(juce::Graphics& g) {
    g.fillAll(theme::danger.withAlpha(0.96f));
    auto area = getLocalBounds().reduced(kPadX, 0);

    g.setColour(kInk);
    auto heading = heading_;
    if (heading.startsWith(warningSign())) {
        const auto sign = signFont();
        g.setFont(sign);
        g.drawText(warningSign(), area.removeFromLeft(widthOf(sign, warningSign())), juce::Justification::centredLeft, false);
        heading = heading.substring(warningSign().length());
    }
    const auto bold = theme::font(18.0f, true);
    g.setFont(bold);
    g.drawText(heading, area.removeFromLeft(widthOf(bold, heading)), juce::Justification::centredLeft, false);
    area.removeFromLeft(kGap);
    g.setFont(theme::font(theme::sizeBase, true));
    g.drawFittedText(sentence_, area, juce::Justification::centredLeft, 2, 1.0f);
}

}  // namespace sqlive::ui
