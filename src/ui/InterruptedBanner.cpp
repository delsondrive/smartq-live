#include "ui/InterruptedBanner.h"

#include <cmath>

#include "app/AudioHost.h"
#include "ui/Helpers.h"
#include "ui/ShowState.h"
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
        sentence = "Press " + showKeysOf(session_.model().band()).startWords() + " to start the song again from the top.";
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
        g.drawText(warningSign(), area.removeFromLeft(widthInPixels(sign, warningSign())), juce::Justification::centredLeft, false);
        heading = heading.substring(warningSign().length());
    }
    const auto bold = theme::font(18.0f, true);
    g.setFont(bold);
    g.drawText(heading, area.removeFromLeft(widthInPixels(bold, heading)), juce::Justification::centredLeft, false);
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

}  // namespace

ComesBackBox::ComesBackBox(app::Session& session) : session_(session) {
    setWantsKeyboardFocus(false);
    setInterceptsMouseClicks(false, false);
    refresh();
}

juce::String ComesBackBox::title() const { return "WHEN IT COMES BACK"; }

void ComesBackBox::refresh() {
    const auto& band = session_.model().band();
    const auto keys = showKeysOf(band);
    std::vector<Piece> pieces;
    if (keys.startKey.isNotEmpty())
        pieces.push_back({keys.startKey, true});
    else
        pieces.push_back({keys.startWords(true), false});
    juce::String rest = "restarts the song from the top";
    if (!keys.songKeys.isEmpty()) {
        pieces.push_back({rest + juce::String::fromUTF8(" \xc2\xb7"), false});
        for (const auto& key : keys.songKeys) pieces.push_back({key, true});
        rest = "choose another song";
    }
    rest << ". Nothing starts by itself.";
    if (!band.midiDevices.empty()) rest << " The pedalboards are sent their patches again.";   // a restart sends them first
    pieces.push_back({rest, false});
    if (pieces == pieces_) return;
    pieces_ = std::move(pieces);
    repaint();
}

juce::String ComesBackBox::hint() const {
    juce::StringArray words;
    for (const auto& piece : pieces_) words.add(piece.text);
    return words.joinIntoString(" ");
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
    float x = line.getX();
    for (std::size_t i = 0; i < pieces_.size(); ++i) {
        const auto& piece = pieces_[i];
        if (i > 0) x += piece.key && pieces_[i - 1].key ? kKeyGap : kWordGap;
        if (piece.key) {
            x += drawKey(g, piece.text, x, line.getCentreY());
            continue;
        }
        const float w = juce::GlyphArrangement::getStringWidth(font, piece.text);
        g.setColour(theme::mid);
        g.setFont(font);
        g.drawText(piece.text, juce::Rectangle<float>(x, line.getY(), w + 1.0f, line.getHeight()), juce::Justification::centredLeft, false);
        x += w;
    }
}

}  // namespace sqlive::ui
