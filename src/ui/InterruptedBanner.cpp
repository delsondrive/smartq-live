#include "ui/InterruptedBanner.h"

#include <cmath>

#include "app/AudioHost.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

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
    if (session_.mode() != app::Mode::Show) {
        // Edit mode: nothing; the top bar's pill says the audio is lost, and the pages stay uncovered.
    } else if (lost) {
        heading = warningSign() + "Audio interface disconnected";
        sentence = now.interrupted ? "Paused at bar " + juce::String(now.bar) + ". Waiting for it to come back."
                                   : juce::String("No song was playing. Waiting for it to come back.");
    } else if (now.interrupted) {
        heading = "The audio interface is back.";
        sentence = "Press Space to start the song again from the top.";
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

// ---- When it comes back ------------------------------------------------------------------------------------------------

namespace {

// The mockup's .box (panel, hairline, 10 px corners, 10 px by 12 px of padding), its h4 (11 px bold, 0.8 px apart, dim,
// 6 px under it) and the hint (13 px in mid, its keys as kbd: 3 px between keys, 4 px between other words).
constexpr int kBoxPadX = 12, kBoxPadY = 10, kCaptionHeight = 15, kCaptionGap = 6, kHintLine = 20;
constexpr float kKeyGap = 3.0f, kWordGap = 4.0f;

bool isKeyName(const juce::String& word) {
    return word == "Space" || word == juce::String::fromUTF8("\xe2\x86\x90") || word == juce::String::fromUTF8("\xe2\x86\x92");
}

}  // namespace

ComesBackBox::ComesBackBox() {
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);
}

juce::String ComesBackBox::title() const { return "WHEN IT COMES BACK"; }

juce::String ComesBackBox::hint() const {
    return juce::String::fromUTF8("Space restarts the song from the top \xc2\xb7 \xe2\x86\x90 \xe2\x86\x92 choose another song. "
                                  "Nothing starts by itself.");
}

void ComesBackBox::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    auto area = getLocalBounds().reduced(kBoxPadX, kBoxPadY);

    g.setColour(theme::dim);
    g.setFont(theme::tracked(theme::font(theme::sizeCaption, true), 0.8f));
    g.drawText(title(), area.removeFromTop(kCaptionHeight), juce::Justification::centredLeft, false);
    area.removeFromTop(kCaptionGap);

    const auto line = area.removeFromTop(kHintLine).toFloat();
    const auto font = theme::font(theme::sizeBody);
    const auto words = juce::StringArray::fromTokens(hint(), " ", "");
    float x = line.getX();
    for (int i = 0; i < words.size(); ++i) {
        if (i > 0) x += isKeyName(words[i]) && isKeyName(words[i - 1]) ? kKeyGap : kWordGap;
        if (isKeyName(words[i])) {
            x += drawKey(g, words[i], x, line.getCentreY());
            continue;
        }
        const float w = juce::GlyphArrangement::getStringWidth(font, words[i]);
        g.setColour(theme::mid);
        g.setFont(font);
        g.drawText(words[i], juce::Rectangle<float>(x, line.getY(), w + 1.0f, line.getHeight()), juce::Justification::centredLeft, false);
        x += w;
    }
}

}  // namespace sqlive::ui
