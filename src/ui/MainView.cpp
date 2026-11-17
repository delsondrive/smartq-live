#include "ui/MainView.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: .map (padding 12 16 6), .bottom (padding 8 16 14, gap 12), .side (250 px, gap 10), .box (padding
// 10 12), h4 (11 px bold, letter-spaced 0.8, 6 px below), the next song's title (18 px semibold), .hint (12 px, dim;
// line-height 1.9 in the hints box), .mixer .cap2 (left 12, top 8, 10 px bold, letter-spaced 0.8).
constexpr int kPadX = 16, kMapTop = 12, kMapBottom = 6, kBottomTop = 8, kBottomBottom = 14, kBottomGap = 12;
constexpr int kSideWidth = 250, kSideGap = 10, kBoxPadX = 12, kBoxPadY = 10;
constexpr int kCapsHeight = 15, kCapsGap = 6, kTitleHeight = 25, kHintHeight = 17, kHintLine = 23;
constexpr int kCaptionLeft = 12, kCaptionTop = 7, kCaptionHeight = 16, kChoiceRight = 10, kChoiceTop = 5, kChoiceHeight = 20;
constexpr int kChoiceMaxWidth = 220, kCaptionChoiceGap = 8;
constexpr int kMinBottomRow = 220;                                // the next song's box and the hints, with the gaps around them

juce::Font capsFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font captionFont() { return theme::tracked(theme::font(theme::sizeTag, true), 0.8f); }
juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

}  // namespace

// The mockup's .cap2: "LEVELS ARE LIVE · " in dim, then "AMBER = CHANGED TONIGHT" in caution.
class MainView::Caption : public juce::Component {
public:
    Caption() { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        const juce::String first = "LEVELS ARE LIVE" + kDot, second = "AMBER = CHANGED TONIGHT";
        const auto font = captionFont();
        const float w = static_cast<float>(getWidth()), firstWidth = std::min(w, widthOf(font, first));
        g.setFont(font);
        g.setColour(theme::dim);
        g.drawText(shorten(first, font, w), juce::Rectangle<float>(0.0f, 0.0f, firstWidth + 1.0f, static_cast<float>(getHeight())),
                   juce::Justification::centredLeft, false);
        g.setColour(theme::caution);
        g.drawText(shorten(second, font, w - firstWidth),
                   juce::Rectangle<float>(firstWidth, 0.0f, std::max(0.0f, w - firstWidth), static_cast<float>(getHeight())),
                   juce::Justification::centredLeft, false);
    }
};

MainView::MainView(app::Session& session)
    : session_(session), strip_(session), map_(session), mixer_(session), caption_(std::make_unique<Caption>()) {
    addAndMakeVisible(strip_);
    addAndMakeVisible(map_);
    addAndMakeVisible(mixer_);
    addAndMakeVisible(*caption_);
    addAndMakeVisible(levelsFor_);
    mixer_.setCompact(true);
    mixer_.onStateChanged = [this] { mixerStateChanged(); };
    levelsFor_.onChange = [this](int index) {
        const auto& options = mixer_.state().options;
        if (!juce::isPositiveAndBelow(index, static_cast<int>(options.size()))) return;
        outputId_ = options[static_cast<std::size_t>(index)].id;
        mixer_.show(map_.songId(), outputId_);
    };
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh();
}

MainView::~MainView() {
    stopTimer();
    session_.changes().removeChangeListener(this);
}

void MainView::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

// While a song plays the session's changes come 30 times a second as well; a tick just after one of them has nothing new.
void MainView::timerCallback() {
    if (juce::Time::getMillisecondCounterHiRes() - lastRefreshMs_ >= 20.0) refresh();
}

void MainView::visibilityChanged() {
    if (isVisible() && !fresh_) refresh();
}

void MainView::refresh() {
    if (!shownWithParents(*this)) {                               // hidden: it catches up when shown
        fresh_ = false;
        stopTimer();
        return;
    }
    fresh_ = true;
    lastRefreshMs_ = juce::Time::getMillisecondCounterHiRes();
    const auto state = makeShowState(session_);

    strip_.set(state.cards);
    // The playing block reads "Chorus · 6 bars left". A lane more or less, or the blocks coming or going: the map's height
    // follows them.
    const auto playingWords = state.sectionName.isEmpty() ? juce::String()
                                                          : state.sectionName + kDot + state.barsLeft + " " + state.barsLeftLabel;
    const bool sectionsReshaped = map_.setSections(state.tiles, playingWords);
    if (map_.setLanes(state.lanes) || sectionsReshaped) resized();
    map_.refresh();
    if (map_.songId() != mixer_.songId()) mixer_.show(map_.songId(), outputId_);   // the mixer follows the song by itself

    Side side;
    side.endOfSet = state.endOfSet;
    side.title = state.nextTitle;
    side.follow = state.nextInfo.fromFirstOccurrenceOf("\n", false, false);
    for (const auto& card : state.cards)
        if (card.kind == ShowState::Card::Kind::Next) {
            side.note = card.note;
            side.noteGood = card.noteGood;
            side.noteBad = card.noteBad;
        }
    side.hints = state.hints;
    if (!(side == shownSide_)) {
        const bool reshaped = side.endOfSet != shownSide_.endOfSet || side.hints != shownSide_.hints;
        shownSide_ = side;
        if (reshaped) layoutSide();                               // the boxes' heights follow what is in them
        repaint(side_);
    }

    if (state.playing && !isTimerRunning()) startTimerHz(30);
    else if (!state.playing && isTimerRunning()) stopTimer();
}

void MainView::mixerStateChanged() {
    const auto& state = mixer_.state();
    if (state.chosen == 0) outputId_.clear();                     // an output no longer in the rig: "This song"
    juce::StringArray options;
    for (const auto& option : state.options) options.add(option.name);
    if (options != options_) {
        options_ = options;
        levelsFor_.setItems(options_, state.chosen);
        layoutCaption();                                          // the dropdown's width follows its items
    } else {
        levelsFor_.setSelected(state.chosen);
    }
}

void MainView::resized() {
    ++layouts_done_;
    auto area = getLocalBounds();
    strip_.setBounds(area.removeFromTop(SetlistStrip::idealHeight));
    area.removeFromTop(kMapTop - (map_.hasSections() ? SongMapView::sectionsTop : 0));   // the blocks 12 px down, as .map's
    // The map's lanes take what the bottom row can spare: a lane that doesn't fit whole is left out, never the next song's box.
    const int mapHeight = map_.heightWithin(area.getHeight() - kMinBottomRow);
    map_.setBounds(area.removeFromTop(mapHeight).reduced(kPadX - SongMapView::sideBleed, 0));   // its outlines reach past it
    area.removeFromTop(kMapBottom + kBottomTop);
    area.removeFromBottom(kBottomBottom);
    area.reduce(kPadX, 0);
    area.setHeight(std::max(0, area.getHeight()));
    side_ = area.removeFromRight(std::min(kSideWidth, area.getWidth()));
    area.removeFromRight(std::min(kBottomGap, area.getWidth()));
    mixer_.setBounds(area);
    layoutCaption();
    layoutSide();
}

void MainView::layoutCaption() {
    ++layouts_done_;
    const auto mixer = mixer_.getBounds();
    const int choiceWidth = std::max(0, std::min({levelsFor_.idealWidth(), kChoiceMaxWidth, mixer.getWidth() - 2 * kChoiceRight}));
    levelsFor_.setBounds(mixer.getRight() - kChoiceRight - choiceWidth, mixer.getY() + kChoiceTop, choiceWidth,
                         std::min(kChoiceHeight, mixer.getHeight()));
    const int captionX = mixer.getX() + kCaptionLeft;
    caption_->setBounds(captionX, mixer.getY() + kCaptionTop, std::max(0, levelsFor_.getX() - kCaptionChoiceGap - captionX),
                        std::min(kCaptionHeight, std::max(0, mixer.getHeight() - kCaptionTop)));
}

void MainView::layoutSide() {
    // The hints, as many to a line as fit, " · " between them.
    hintLines_.clear();
    const float lineWidth = static_cast<float>(std::max(0, side_.getWidth() - 2 * kBoxPadX));
    float used = 0.0f;
    for (int i = 0; i < static_cast<int>(shownSide_.hints.size()); ++i) {
        const float w = hintWidth(shownSide_.hints[static_cast<std::size_t>(i)], hintFont());
        const float withDot = w + widthOf(hintFont(), kDot);
        if (hintLines_.empty() || used + withDot > lineWidth) {
            hintLines_.push_back({i});
            used = w;
        } else {
            hintLines_.back().push_back(i);
            used += withDot;
        }
    }

    auto column = side_;
    const int nextHeight = 2 * kBoxPadY + kCapsHeight + (shownSide_.endOfSet ? 0 : kCapsGap + kTitleHeight + kHintHeight);
    nextBox_ = column.removeFromTop(std::min(nextHeight, column.getHeight()));
    column.removeFromTop(std::min(kSideGap, column.getHeight()));
    const int hintsHeight = 2 * kBoxPadY + static_cast<int>(hintLines_.size()) * kHintLine;
    hintsBox_ = hintLines_.empty() ? juce::Rectangle<int>() : column.removeFromTop(std::min(hintsHeight, column.getHeight()));
}

void MainView::paint(juce::Graphics& g) {
    if (!g.getClipBounds().intersects(side_)) return;
    juce::Graphics::ScopedSaveState clipped(g);
    g.reduceClipRegion(side_);

    // The next song: "NEXT SONG", its title, then "✓ Ready · after a 4 s gap".
    if (!nextBox_.isEmpty()) {
        theme::paintPanel(g, nextBox_.toFloat());
        auto inner = nextBox_.reduced(kBoxPadX, kBoxPadY);
        const float width = static_cast<float>(inner.getWidth());
        g.setFont(capsFont());
        g.setColour(theme::dim);
        g.drawText(shorten(shownSide_.endOfSet ? "END OF THE SET" : "NEXT SONG", capsFont(), width), inner.removeFromTop(kCapsHeight),
                   juce::Justification::centredLeft, false);
        if (!shownSide_.endOfSet) {
            inner.removeFromTop(kCapsGap);
            g.setFont(titleFont());
            g.setColour(theme::text);
            g.drawText(shorten(shownSide_.title, titleFont(), width), inner.removeFromTop(kTitleHeight), juce::Justification::centredLeft, false);
            const auto line = inner.removeFromTop(kHintHeight).toFloat();
            const auto note = shorten(shownSide_.note, hintFont(), width);
            const float noteWidth = widthOf(hintFont(), note);
            g.setFont(hintFont());
            g.setColour(shownSide_.noteGood ? theme::mint : shownSide_.noteBad ? theme::danger : theme::dim);
            g.drawText(note, line.withWidth(noteWidth + 1.0f), juce::Justification::centredLeft, false);
            if (shownSide_.follow.isNotEmpty()) {
                g.setColour(theme::dim);
                g.drawText(shorten(kDot + shownSide_.follow, hintFont(), std::max(0.0f, width - noteWidth)), line.withTrimmedLeft(noteWidth),
                           juce::Justification::centredLeft, false);
            }
        }
    }

    // The key hints.
    if (!hintsBox_.isEmpty()) {
        theme::paintPanel(g, hintsBox_.toFloat());
        const auto inner = hintsBox_.reduced(kBoxPadX, kBoxPadY).toFloat();
        float centreY = inner.getY() + 0.5f * kHintLine;
        for (const auto& line : hintLines_) {
            float x = inner.getX();
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (i > 0) {
                    g.setFont(hintFont());
                    g.setColour(theme::dim);
                    const float w = widthOf(hintFont(), kDot);
                    g.drawText(kDot, juce::Rectangle<float>(x, centreY - 0.5f * kHintHeight, w + 1.0f, kHintHeight), juce::Justification::centredLeft, false);
                    x += w;
                }
                const auto& hint = shownSide_.hints[static_cast<std::size_t>(line[i])];
                drawHint(g, hint, hintFont(), x, centreY);
                x += hintWidth(hint, hintFont());
            }
            centreY += kHintLine;
        }
    }
}

}  // namespace sqlive::ui
