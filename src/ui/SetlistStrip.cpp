#include "ui/SetlistStrip.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: .setstrip (padding 12 16, gap 8, a hairline below), .song (168 px, 10 px corners, padding 8 10 10 12,
// a 1 px border), .n and .m (12 px, dim), .t (14 px semibold), .tag (right 8, top 6, 10 px bold letter-spaced 0.6,
// padding 1 6, 4 px corners), .song.playing::after (a 3 px bar), .song.done (45%).
constexpr int kPadX = 16, kPadY = 12, kGap = 8, kCardWidth = 168, kCardHeight = 74, kStep = kCardWidth + kGap;
constexpr int kInLeft = 12, kInRight = 10, kInTop = 8, kBorder = 1;
constexpr int kNumberHeight = 17, kTitleHeight = 20, kMetaHeight = 17;
constexpr float kCardRadius = 10.0f, kDoneAlpha = 0.45f, kBarHeight = 3.0f;
constexpr int kTagRight = 8, kTagTop = 6, kTagPadX = 6, kTagHeight = 15;
constexpr float kTagRadius = 4.0f;
constexpr float kWheelPixels = 600.0f;                            // a wheel delta of 1, in pixels

const juce::Colour kOnEmber{0xff160f10};                          // .tag.t-next text

juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font titleFont() { return theme::font(theme::sizeBase, true); }
juce::Font tagFont() { return theme::tracked(theme::font(theme::sizeTag, true), 0.6f); }

constexpr float kTitleWidth = static_cast<float>(kCardWidth - 2 * kBorder - kInLeft - kInRight);

juce::Path cardOutline(juce::Rectangle<float> r) {
    juce::Path p;
    p.addRoundedRectangle(r, kCardRadius);
    return p;
}

}  // namespace

SetlistStrip::SetlistStrip(app::Session& session) : session_(session) {
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

void SetlistStrip::set(const std::vector<ShowState::Card>& cards) {
    const bool countChanged = cards.size() != cards_.size();
    bool titlesChanged = countChanged;
    for (std::size_t i = 0; !titlesChanged && i < cards.size(); ++i) titlesChanged = cards[i].title != cards_[i].title;

    int playing = -1, next = -1;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (cards[i].kind == ShowState::Card::Kind::Playing) playing = static_cast<int>(i);
        if (cards[i].kind == ShowState::Card::Kind::Next) next = static_cast<int>(i);
    }

    if (titlesChanged) {
        cards_ = cards;
        fitTitles();
        repaint();
    } else {
        for (std::size_t i = 0; i < cards.size(); ++i)
            if (!(cards[i] == cards_[i])) repaint(cardBounds(static_cast<int>(i)));
        cards_ = cards;
    }
    if (countChanged || playing != playing_ || next != next_) {
        playing_ = playing;
        next_ = next;
        keepInView();
    }
}

juce::String SetlistStrip::drawnTitle(int card) const {
    return juce::isPositiveAndBelow(card, static_cast<int>(drawnTitles_.size())) ? drawnTitles_[static_cast<std::size_t>(card)] : juce::String();
}

juce::Rectangle<int> SetlistStrip::cardBounds(int card) const { return {kPadX + card * kStep - scroll_, kPadY, kCardWidth, kCardHeight}; }

int SetlistStrip::contentWidth() const {
    const int count = static_cast<int>(cards_.size());
    return count > 0 ? 2 * kPadX + count * kCardWidth + (count - 1) * kGap : 0;
}

void SetlistStrip::setScroll(int scroll) {
    scroll = std::clamp(scroll, 0, std::max(0, contentWidth() - getWidth()));
    if (scroll == scroll_) return;
    scroll_ = scroll;
    repaint();
}

void SetlistStrip::keepInView() {
    const int a = playing_ >= 0 ? playing_ : next_, b = next_ >= 0 ? next_ : playing_;
    if (a < 0) {
        setScroll(scroll_);                                       // only kept within the row
        return;
    }
    // Both cards with the strip's padding either side, or, when they don't both fit, the playing one.
    int left = std::min(a, b) * kStep, right = std::max(a, b) * kStep + kCardWidth + 2 * kPadX;
    if (right - left > getWidth()) {
        left = a * kStep;
        right = left + kCardWidth + 2 * kPadX;
    }
    int scroll = scroll_;
    if (left < scroll) scroll = left;
    else if (right > scroll + getWidth()) scroll = right - getWidth();
    setScroll(scroll);
}

void SetlistStrip::fitTitles() {
    ++titles_fitted_;
    drawnTitles_.clear();
    for (const auto& card : cards_) drawnTitles_.push_back(shorten(card.title, titleFont(), kTitleWidth));
}

void SetlistStrip::resized() { keepInView(); }

void SetlistStrip::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    const int x = e.x + scroll_ - kPadX;
    if (x < 0 || x % kStep >= kCardWidth || e.y < kPadY || e.y >= kPadY + kCardHeight) return;
    const int index = x / kStep;
    if (index < static_cast<int>(cards_.size())) session_.chooseNext(index);
}

void SetlistStrip::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    const float delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    setScroll(scroll_ - juce::roundToInt(delta * kWheelPixels));
}

void SetlistStrip::paint(juce::Graphics& g) {
    g.setColour(theme::hairline);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    if (cards_.empty()) {
        g.setColour(theme::dim);
        g.setFont(theme::font(theme::sizeBody));
        g.drawText("No songs yet", getLocalBounds().withTrimmedBottom(1), juce::Justification::centred, false);
        return;
    }
    const auto clip = g.getClipBounds();
    for (int i = 0; i < static_cast<int>(cards_.size()); ++i) {
        const auto bounds = cardBounds(i);
        if (bounds.getX() >= clip.getRight()) break;
        if (bounds.intersects(clip)) paintCard(g, cards_[static_cast<std::size_t>(i)], i);
    }
}

void SetlistStrip::paintCard(juce::Graphics& g, const ShowState::Card& card, int index) const {
    using Kind = ShowState::Card::Kind;
    const auto bounds = cardBounds(index);
    const auto r = bounds.toFloat();
    const bool done = card.kind == Kind::Done;
    if (done) g.beginTransparencyLayer(kDoneAlpha);

    // The card: .song, .song.playing (a mint tint and a 2 px mint outline), .song.upnext (a dashed ember outline).
    g.setColour(card.kind == Kind::Playing ? theme::mint.withAlpha(0.08f) : theme::field);
    g.fillRoundedRectangle(r, kCardRadius);
    if (card.kind == Kind::Playing) {
        g.setColour(theme::mint);
        g.drawRoundedRectangle(r.reduced(1.0f), kCardRadius - 1.0f, 2.0f);
        const float progress = static_cast<float>(std::clamp(card.progress, 0.0, 1.0));
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(cardOutline(r));
        g.fillRect(r.getX(), r.getBottom() - kBarHeight, r.getWidth() * progress, kBarHeight);
    } else if (card.kind == Kind::Next) {
        juce::Path dashed;
        const float dashes[] = {4.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, cardOutline(r.reduced(0.5f)), dashes, 2);
        g.setColour(theme::ember);
        g.fillPath(dashed);
        if (card.preparing) {                                     // its effects being prepared: the playing card's bar, grey
            const float progress = static_cast<float>(std::clamp(card.progress, 0.0, 1.0));
            juce::Graphics::ScopedSaveState state(g);
            g.reduceClipRegion(cardOutline(r));
            g.setColour(theme::dim);
            g.fillRect(r.getX(), r.getBottom() - kBarHeight, r.getWidth() * progress, kBarHeight);
        }
    } else {
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(r.reduced(0.5f), kCardRadius, 1.0f);
    }

    // .n, .t and .m.
    auto inner = bounds.reduced(kBorder).withTrimmedLeft(kInLeft).withTrimmedRight(kInRight).withTrimmedTop(kInTop);
    g.setFont(smallFont());
    g.setColour(theme::dim);
    g.drawText(juce::String(card.number), inner.removeFromTop(kNumberHeight), juce::Justification::centredLeft, false);
    g.setFont(titleFont());
    g.setColour(theme::text);
    g.drawText(drawnTitle(index), inner.removeFromTop(kTitleHeight), juce::Justification::centredLeft, false);
    const auto meta = inner.removeFromTop(kMetaHeight);
    g.setFont(smallFont());
    const float noteWidth = juce::GlyphArrangement::getStringWidth(smallFont(), card.note);
    const auto noteArea = meta.withTrimmedLeft(std::max(0, meta.getWidth() - static_cast<int>(std::ceil(noteWidth))));
    g.setColour(card.noteGood ? theme::mint : card.noteBad ? theme::danger : theme::dim);
    g.drawText(shorten(card.note, smallFont(), static_cast<float>(meta.getWidth())), noteArea.getWidth() > 0 ? noteArea : meta,
               juce::Justification::centredRight, false);
    g.setColour(theme::dim);
    g.drawText(card.length, meta.withTrimmedRight(noteArea.getWidth() + 4), juce::Justification::centredLeft, false);

    // The tag: "NOW" on mint, "NEXT" on ember.
    if (card.kind == Kind::Playing || card.kind == Kind::Next) {
        const juce::String tag = card.kind == Kind::Playing ? "NOW" : "NEXT";
        const float w = juce::GlyphArrangement::getStringWidth(tagFont(), tag) + 2.0f * kTagPadX;
        const juce::Rectangle<float> box(r.getRight() - kBorder - kTagRight - w, r.getY() + kBorder + kTagTop, w, kTagHeight);
        g.setColour(card.kind == Kind::Playing ? theme::mint : theme::ember);
        g.fillRoundedRectangle(box, kTagRadius);
        g.setColour(card.kind == Kind::Playing ? theme::onAccent : kOnEmber);
        g.setFont(tagFont());
        g.drawText(tag, box, juce::Justification::centred, false);
    }
    if (done) g.endTransparencyLayer();
}

}  // namespace sqlive::ui
