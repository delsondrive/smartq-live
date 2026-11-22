#include "ui/MixerPage.h"

#include <algorithm>
#include <cmath>

#include "app/LevelChecker.h"
#include "model/Units.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/OutputLabelPill.h"
#include "ui/StripParts.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::LevelTarget;

namespace {

// The mockup's CSS: .top (gap 14, padding 0 16), .tools, .dd, .content (padding 14 16, gap 14), .side (300 px here, as
// the plan has it), .box (padding 10 12), h4 (11 px bold, letter-spaced 0.8, margin below 6), .lbl-out (11 px bold,
// padding 1 7, 4 px corners), .row (13 px, mid, margin 6 0), .val (a field, 6 px corners, padding 3 8, at least 80 wide).
constexpr int kHeaderHeight = 46, kPadX = 16, kPadBottom = 14, kGap = 14, kControlHeight = 28;
constexpr int kSongsMinWidth = 160, kSongsMaxWidth = 280, kListMaxWidth = 240;
constexpr int kSideWidth = 300, kBoxPadX = 12, kBoxPadY = 10, kTitleHeight = 16, kTitleGap = 6, kRowGap = 10;
constexpr int kRowHeight = 24, kValueMinWidth = 80, kValuePadX = 8;
constexpr float kTrimStepDb = 0.1f, kFineTrimStepDb = 0.025f;   // a pixel of drag
// The level check's box (.side's gap 10, .row, .meter 80 x 6 with 3 px corners, the clip line .hint at 12 px) and the
// mockup's closing sentence in a box of its own.
constexpr int kSideGap = 10, kLineHeight = 24, kDetailHeight = 16, kMeterWidth = 80, kMeterHeight = 6, kMeterGap = 8;
// "Playing now" (margin-top 14, a 17 px line, the meter 8 below it: .ometer rows of 10 px, 3 apart, its L and R in 14 px,
// then the scale) and the keys box (.keys: a line every 24 px, keys 3 apart, the words 10 px after the widest keys).
constexpr int kPlayingTop = 14, kPlayingRow = 17, kPlayingGap = 8, kOmeterRow = 10, kOmeterGap = 3, kOmeterScale = 12;
constexpr float kOmeterLabel = 14.0f, kMeterFallDb = 1.5f, kKeyGap = 3.0f, kKeysToWords = 10.0f;
constexpr int kKeysLine = 24;
const juce::String kLevelsTitle = juce::String::fromUTF8("WHAT EACH OUTPUT GETS \xc2\xb7 LOUDEST MOMENT");
const juce::String kLevelsClosing = "Worked out from the whole song without playing it, so a problem shows up here and in the "
                                    "pre-show check, not on stage.";

juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font valueFont() { return theme::font(theme::sizeBody, true); }
juce::Font capsFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }

int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

// The sentence's height in the side panel at that width.
juce::TextLayout sentenceLayout(const juce::String& text, float width, const juce::Font& font = bodyFont(),
                                juce::Colour colour = theme::mid) {
    juce::AttributedString s;
    s.append(text, font, colour);
    s.setWordWrap(juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout(s, std::max(1.0f, width));
    return layout;
}

}  // namespace

// The mockup's .val with the output's trim in it: a drag up or down changes it, Shift for fine steps, a double-click
// makes it 0 dB. Caution text when it was changed tonight.
class MixerPage::TrimValue : public juce::Component {
public:
    explicit TrimValue(app::Session& session) : session_(session) { setMouseCursor(juce::MouseCursor::UpDownResizeCursor); }

    // True when what it shows changed.
    bool set(const juce::String& outputId) {
        if (outputId.isEmpty() && outputId_.isEmpty()) return false;   // "This song": not shown, nothing to read
        outputId_ = outputId;
        const LevelTarget target = this->target();
        const float db = session_.level(target).value_or(0.0f);
        const bool changed = session_.changedTonight(target);
        if (db == db_ && changed == changed_) return false;
        db_ = db;
        changed_ = changed;
        repaint();
        return true;
    }
    int idealWidth() const { return std::max(kValueMinWidth, ceilToInt(widthOf(valueFont(), formatLevelDb(db_))) + 2 * kValuePadX); }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(r, theme::radiusField);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(r.reduced(0.5f), theme::radiusField, 1.0f);
        g.setColour(changed_ ? theme::caution : theme::text);
        g.setFont(valueFont());
        g.drawText(formatLevelDb(db_), getLocalBounds().reduced(kValuePadX, 0), juce::Justification::centredRight, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) return;
        gesture_ = juce::Uuid().toString();
        startDb_ = db_;
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (gesture_.isEmpty()) return;
        const float step = e.mods.isShiftDown() ? kFineTrimStepDb : kTrimStepDb;
        const float db = toTenth(model::clampDb(startDb_ - step * static_cast<float>(e.getDistanceFromDragStartY())));
        if (db != db_) session_.setLevel(target(), db, gesture_);
    }
    void mouseUp(const juce::MouseEvent&) override { gesture_.clear(); }
    void mouseDoubleClick(const juce::MouseEvent&) override { session_.setLevel(target(), 0.0f, juce::Uuid().toString()); }

private:
    LevelTarget target() const { return {LevelTarget::Kind::OutputTrim, {}, {}, outputId_, {}}; }

    app::Session& session_;
    juce::String outputId_, gesture_;
    float db_ = 0.0f, startDb_ = 0.0f;
    bool changed_ = false;
};

// The mockup's "Playing now" under an output's overall level, and its .ometer: what the output's bus got, left and right,
// as two thin bars on the faders' travel (mint, blending to caution from -6 dB and danger from 0), the louder side's level
// in dB beside the words, and a scale under them. Its own MeterReader, 30 frames a second while shown; the bars jump up
// to a louder peak and otherwise fall 1.5 dB a frame, as the strips' meters do.
class MixerPage::PlayingNow : public juce::Component, private juce::Timer {
public:
    explicit PlayingNow(app::Session& session) : reader_(session.meters()) {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }
    ~PlayingNow() override { stopTimer(); }

    static constexpr int idealHeight = kPlayingTop + kPlayingRow + kPlayingGap + 2 * kOmeterRow + 2 * kOmeterGap + kOmeterScale;

    void setBus(int bus) {
        if (bus == bus_) return;
        bus_ = bus;
        level_[0] = level_[1] = ChannelStrip::kMinDb;
        repaint();
    }

    void tick() {
        const auto peak = bus_ >= 0 ? reader_.bus(bus_) : app::MeterReader::Peak{};
        bool changed = false;
        for (int side = 0; side < 2; ++side) {
            const float gain = side == 0 ? peak.left : peak.right;
            const float db = gain > 0.0f ? std::max(ChannelStrip::kMinDb, model::gainToDb(gain)) : ChannelStrip::kMinDb;
            const float level = std::max(db, std::max(ChannelStrip::kMinDb, level_[side] - kMeterFallDb));
            changed = changed || level != level_[side];
            level_[side] = level;
        }
        if (changed) repaint();
    }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat();
        area.removeFromTop(static_cast<float>(kPlayingTop));
        const auto row = area.removeFromTop(static_cast<float>(kPlayingRow));
        const float loudest = std::max(level_[0], level_[1]);
        g.setFont(hintFont());
        g.setColour(theme::mid);
        g.drawText("Playing now", row, juce::Justification::centredLeft, false);
        g.setColour(theme::text);
        g.drawText(loudest > ChannelStrip::kMinDb ? stripparts::withTrueMinus(formatStripDb(loudest)) + " dB"
                                                  : stripparts::withTrueMinus("-") + juce::String::fromUTF8("\xe2\x88\x9e"),
                   row, juce::Justification::centredRight, false);
        area.removeFromTop(static_cast<float>(kPlayingGap));

        const auto tag = theme::font(theme::sizeTag);
        const float barX = area.getX() + kOmeterLabel;
        const float barWidth = std::max(1.0f, area.getWidth() - kOmeterLabel);
        for (int side = 0; side < 2; ++side) {
            const auto line = area.removeFromTop(static_cast<float>(kOmeterRow));
            area.removeFromTop(static_cast<float>(kOmeterGap));
            g.setFont(tag);
            g.setColour(theme::dim);
            g.drawText(side == 0 ? "L" : "R", line.withWidth(kOmeterLabel), juce::Justification::centredLeft, false);
            const auto bar = juce::Rectangle<float>(barX, line.getCentreY() - 3.5f, barWidth, 7.0f);
            g.setColour(juce::Colour(0xff08070a));
            g.fillRoundedRectangle(bar, 2.0f);
            if (level_[side] <= ChannelStrip::kMinDb) continue;
            juce::ColourGradient fill(theme::mint, bar.getX(), 0.0f, theme::danger, bar.getRight(), 0.0f, false);
            fill.addColour(ChannelStrip::fractionForDb(-6.0f), theme::mint);
            fill.addColour(ChannelStrip::fractionForDb(-1.0f), theme::caution);
            fill.addColour(ChannelStrip::fractionForDb(0.0f), theme::danger);
            g.setGradientFill(fill);
            g.fillRoundedRectangle(bar.withWidth(ChannelStrip::fractionForDb(level_[side]) * bar.getWidth()), 2.0f);
        }
        // The scale under the bars, each mark at its place on the travel.
        const auto scale = area.removeFromTop(static_cast<float>(kOmeterScale));
        g.setFont(tag);
        g.setColour(theme::dim);
        for (const float mark : {-60.0f, -30.0f, -12.0f, 0.0f, 6.0f}) {
            const auto words = mark > 0.0f ? "+" + juce::String(juce::roundToInt(mark)) : stripparts::withTrueMinus(juce::String(juce::roundToInt(mark)));
            const float w = widthOf(tag, words) + 1.0f;
            const float x = juce::jlimit(barX, barX + barWidth - w, barX + ChannelStrip::fractionForDb(mark) * barWidth - 0.5f * w);
            g.drawText(words, juce::Rectangle<float>(x, scale.getY(), w, scale.getHeight()), juce::Justification::centred, false);
        }
    }

private:
    void timerCallback() override {
        if (bus_ >= 0 && shownWithParents(*this)) tick();
    }

    app::MeterReader reader_;
    int bus_ = -1;
    float level_[2] = {ChannelStrip::kMinDb, ChannelStrip::kMinDb};
};

// The mockup's "KEYS · SELECTED STRIP" box: each line the keys, drawn as keys, then what they do, a key ("Shift") among
// the words where they name one.
class MixerPage::KeysCard : public juce::Component {
public:
    KeysCard() { setInterceptsMouseClicks(false, false); }

    static int idealHeight() { return kBoxPadY + kTitleHeight + kTitleGap + static_cast<int>(lines().size()) * kKeysLine + kBoxPadY - 4; }

    void paint(juce::Graphics& g) override {
        theme::paintPanel(g, getLocalBounds().toFloat());
        auto inner = getLocalBounds().reduced(kBoxPadX, kBoxPadY);
        g.setFont(capsFont());
        g.setColour(theme::dim);
        g.drawText(shorten(juce::String("KEYS") + kDot + "SELECTED STRIP", capsFont(), static_cast<float>(inner.getWidth())),
                   inner.removeFromTop(kTitleHeight), juce::Justification::centredLeft, false);
        inner.removeFromTop(kTitleGap);
        float keysWidth = 0.0f;
        for (const auto& line : lines()) keysWidth = std::max(keysWidth, widthOfKeys(line.keys));
        const auto words = hintFont();
        for (const auto& line : lines()) {
            const auto row = inner.removeFromTop(kKeysLine).toFloat();
            const float centreY = row.getCentreY();
            float x = row.getX();
            for (const auto& key : line.keys) x += drawKey(g, key, x, centreY) + kKeyGap;
            x = row.getX() + keysWidth + kKeysToWords;
            for (const auto& piece : line.words) {
                if (piece.startsWith("[")) {                      // a key among the words
                    x += drawKey(g, piece.substring(1, piece.length() - 1), x, centreY) + kKeyGap;
                    continue;
                }
                g.setFont(words);
                g.setColour(theme::mid);
                const float w = widthOf(words, piece);
                g.drawText(piece, juce::Rectangle<float>(x, row.getY(), std::max(0.0f, std::min(w + 1.0f, row.getRight() - x)), row.getHeight()),
                           juce::Justification::centredLeft, false);
                x += w + kKeyGap;
            }
        }
    }

private:
    struct Line {
        juce::StringArray keys, words;                            // a word in [brackets] is drawn as a key
    };
    static const std::vector<Line>& lines() {
        static const std::vector<Line> all = {
            {{utf8("\xe2\x86\x91"), utf8("\xe2\x86\x93")}, {utf8("level \xc2\xb1" "0.5 dB,"), "[Shift]", utf8("\xc2\xb1" "0.1")}},
            {{utf8("\xe2\x86\x90"), utf8("\xe2\x86\x92")}, {utf8("pan \xc2\xb1" "5,"), "[Shift]", utf8("\xc2\xb1" "1")}},
            {{"Tab"}, {"next strip"}},
            {{"M", "S"}, {"mute, solo"}},
            {{"Enter"}, {"type a level"}},
        };
        return all;
    }
    static float widthOfKeys(const juce::StringArray& keys) {
        float w = 0.0f;
        for (const auto& key : keys) w += keyWidth(key) + kKeyGap;
        return std::max(0.0f, w - kKeyGap);
    }
};

MixerPage::MixerPage(app::Session& session)
    : session_(session), panel_(session), trim_(std::make_unique<TrimValue>(session)),
      playingNow_(std::make_unique<PlayingNow>(session)), keys_(std::make_unique<KeysCard>()) {
    addAndMakeVisible(panel_);
    addAndMakeVisible(levelsFor_);
    addChildComponent(levelsForList_);
    addAndMakeVisible(songs_);
    addChildComponent(*trim_);
    addChildComponent(*playingNow_);
    addChildComponent(*keys_);
    levelsFor_.onChange = [this](int index) {
        const auto& options = panel_.state().options;
        if (juce::isPositiveAndBelow(index, static_cast<int>(options.size()))) chooseOutput(options[static_cast<std::size_t>(index)].id);
    };
    levelsForList_.onChange = levelsFor_.onChange;
    songs_.onChange = [this](int index) {
        if (juce::isPositiveAndBelow(index, songIds_.size())) openSong(songIds_[index]);
    };
    panel_.onStateChanged = [this] { applyPanelState(); };
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
}

MixerPage::~MixerPage() {
    session_.changes().removeChangeListener(this);
    session_.levelChecker().alsoMeasure({});
}

void MixerPage::openSong(const juce::String& songId) {
    songId_ = songId;
    refresh(true);
}

void MixerPage::chooseOutput(const juce::String& outputId) {
    outputId_ = outputId;
    refresh(true);
}

void MixerPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void MixerPage::visibilityChanged() {
    if (!isVisible()) session_.levelChecker().alsoMeasure({});   // closed: its song needn't be measured any more
    else if (!fresh_) refresh();
    else session_.levelChecker().alsoMeasure(songId_);
}

void MixerPage::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays. Shown, a tick
    // with nothing new in it lays nothing out: the panel follows the session by itself (one makeMixerState a tick) and
    // tells the page when its state changes; the page itself only looks at the songs and the trim.
    if (!isVisible() && !force) {
        fresh_ = false;
        return;
    }
    const bool wasFresh = fresh_;
    fresh_ = true;
    const auto& model = session_.model();

    juce::StringArray ids, titles;
    for (const auto& id : model.songIds()) {
        ids.add(id);
        titles.add(model.song(id)->title);
    }
    if (model.song(songId_) == nullptr) {                         // none chosen yet, or removed from the band
        const auto now = session_.now();
        songId_ = model.song(now.playingSongId) != nullptr ? now.playingSongId
                  : model.song(now.nextSongId) != nullptr  ? now.nextSongId
                  : !ids.isEmpty()                         ? ids[0]
                                                           : juce::String();
    }
    // The song shown is measured too when it isn't in the setlist; a hidden page asks for nothing.
    if (isVisible()) session_.levelChecker().alsoMeasure(songId_);

    bool layOut = false;
    if (ids != songIds_ || titles != songTitles_) {
        songIds_ = ids;
        songTitles_ = titles;
        songs_.setItems(titles, ids.indexOf(songId_));
        layOut = true;                                            // the dropdown's width follows the titles
    } else {
        songs_.setSelected(ids.indexOf(songId_));
    }
    // The panel is told only what it should show: a new song or output, or after the page was hidden. Its state, if that
    // changes, comes back through applyPanelState.
    if (force || !wasFresh || songId_ != panel_.songId() || outputId_ != panel_.outputId()) panel_.show(songId_, outputId_);
    if (trim_->set(outputId_)) layOut = true;
    if (layOut) layOutAgain();
}

void MixerPage::applyPanelState() {
    const auto& state = panel_.state();
    if (state.chosen == 0) outputId_.clear();                     // an output no longer in the rig: "This song"

    juce::StringArray options;
    for (const auto& option : state.options) options.add(option.name);
    const bool sideChanged = state.panelTitle != shownSide_.panelTitle || state.panelLabel != shownSide_.panelLabel
                             || state.panelLabelKind != shownSide_.panelLabelKind || state.panelChannels != shownSide_.panelChannels
                             || state.panelText != shownSide_.panelText
                             || state.outputLines != shownSide_.outputLines || state.outputLinesNote != shownSide_.outputLinesNote
                             || state.bus != shownSide_.bus || keysWanted() != keysWanted_;
    if (options == options_ && state.chosen == shownChosen_ && !sideChanged) return;   // only the strips changed: the panel's own

    if (options != options_) {                                    // setItems forgets the hovered segment: only when they change
        options_ = options;
        levelsFor_.setItems(options, state.chosen);
        levelsForList_.setItems(options, state.chosen);
    } else {
        levelsFor_.setSelected(state.chosen);
        levelsForList_.setSelected(state.chosen);
    }
    shownChosen_ = state.chosen;
    shownSide_.panelTitle = state.panelTitle;
    shownSide_.panelLabel = state.panelLabel;
    shownSide_.panelLabelKind = state.panelLabelKind;
    shownSide_.panelChannels = state.panelChannels;
    shownSide_.panelText = state.panelText;
    shownSide_.outputLines = state.outputLines;
    shownSide_.outputLinesNote = state.outputLinesNote;
    shownSide_.bus = state.bus;
    keysWanted_ = keysWanted();
    trim_->set(outputId_);
    layOutAgain();
}

// The keys box is for "This song" in Edit mode: in Show mode the mixer never takes the keyboard.
bool MixerPage::keysWanted() const { return panel_.state().chosen == 0 && session_.mode() == app::Mode::Edit; }

void MixerPage::tickMeters(double nowMs) {
    panel_.tickMeters(nowMs);
    if (playingNow_->isVisible()) playingNow_->tick();
}

void MixerPage::layOutAgain() {
    resized();
    repaint();
}

void MixerPage::resized() {
    ++layouts_done_;
    layoutHeader();
    auto content = getLocalBounds().withTrimmedTop(kHeaderHeight).withTrimmedBottom(kPadBottom).reduced(kPadX, 0);
    side_ = content.removeFromRight(std::min(kSideWidth, std::max(0, content.getWidth() / 2)));
    content.removeFromRight(kGap);
    panel_.setBounds(content);
    layoutSide();
}

void MixerPage::layoutHeader() {
    header_ = getLocalBounds().withHeight(kHeaderHeight);
    auto row = header_.reduced(kPadX, 0);
    const int centreY = header_.getCentreY();
    const auto place = [centreY](juce::Component& c, int x, int w) { c.setBounds(x, centreY - kControlHeight / 2, w, kControlHeight); };

    const int labelWidth = ceilToInt(widthOf(hintFont(), "Levels for"));
    levelsForLabel_ = row.removeFromLeft(labelWidth);
    row.removeFromLeft(kGap);

    // The songs' dropdown, for "This song" only, at the right of what the switch leaves.
    const bool thisSong = outputId_.isEmpty();
    songs_.setVisible(thisSong);
    const int songsWidth = thisSong ? juce::jlimit(kSongsMinWidth, kSongsMaxWidth, songs_.idealWidth()) : 0;
    const int forSwitch = std::max(0, row.getWidth() - (thisSong ? songsWidth + kGap : 0));

    // The switch when it fits, else a dropdown of the same options.
    const bool fits = levelsFor_.idealWidth() <= forSwitch;
    levelsFor_.setVisible(fits);
    levelsForList_.setVisible(!fits);
    int x = row.getX();
    if (fits) {
        place(levelsFor_, x, levelsFor_.idealWidth());
        x = levelsFor_.getRight() + kGap;
    } else {
        const int w = std::min({levelsForList_.idealWidth(), kListMaxWidth, forSwitch});
        place(levelsForList_, x, w);
        x = levelsForList_.getRight() + kGap;
    }
    if (thisSong) place(songs_, x, std::max(0, std::min(songsWidth, row.getRight() - x)));
}

void MixerPage::layoutSide() {
    const auto& state = panel_.state();
    const bool output = state.chosen > 0;
    const int innerWidth = std::max(0, side_.getWidth() - 2 * kBoxPadX);
    const int textHeight = ceilToInt(sentenceLayout(state.panelText, static_cast<float>(innerWidth)).getHeight());

    int height = kBoxPadY + kTitleHeight + kTitleGap + textHeight + kBoxPadY;
    if (output) height += kRowGap + kRowHeight + PlayingNow::idealHeight;
    const auto column = side_;
    side_ = side_.withHeight(std::min(height, side_.getHeight()));

    // Under it, the level check's box, the keys and the closing sentence, as far as the column reaches.
    levelsBox_ = closingBox_ = {};
    auto below = column.withTrimmedTop(std::min(column.getHeight(), side_.getHeight() + kSideGap));
    const bool levels = !state.outputLines.empty() || state.outputLinesNote.isNotEmpty();
    if (levels) {
        int lines = state.outputLines.empty() ? kLineHeight : 0;
        for (const auto& line : state.outputLines) lines += kLineHeight + (line.detail.isNotEmpty() ? kDetailHeight : 0);
        levelsBox_ = below.removeFromTop(std::min(below.getHeight(), kBoxPadY + kTitleHeight + kTitleGap + lines + kBoxPadY));
        below.removeFromTop(std::min(below.getHeight(), kSideGap));
    }
    keys_->setVisible(keysWanted_ && below.getHeight() >= KeysCard::idealHeight());
    if (keys_->isVisible()) {
        keys_->setBounds(below.removeFromTop(KeysCard::idealHeight()));
        below.removeFromTop(std::min(below.getHeight(), kSideGap));
    }
    if (levels) {
        const auto closing = sentenceLayout(kLevelsClosing, static_cast<float>(innerWidth), hintFont(), theme::dim);
        closingBox_ = below.removeFromTop(std::min(below.getHeight(), ceilToInt(closing.getHeight()) + 2 * kBoxPadY));
    }

    auto inner = side_.reduced(kBoxPadX, kBoxPadY);
    sideTitle_ = inner.removeFromTop(kTitleHeight);
    inner.removeFromTop(kTitleGap);
    sideText_ = inner.removeFromTop(textHeight);
    trim_->setVisible(output);
    playingNow_->setVisible(output);
    if (output) {
        inner.removeFromTop(kRowGap);
        trimLabel_ = inner.removeFromTop(kRowHeight);
        const int w = std::min(trim_->idealWidth(), trimLabel_.getWidth());
        trim_->setBounds(trimLabel_.removeFromRight(w));
        playingNow_->setBounds(inner.removeFromTop(std::min(inner.getHeight(), static_cast<int>(PlayingNow::idealHeight))));
        playingNow_->setBus(state.bus);
    } else {
        trimLabel_ = {};
    }
}

void MixerPage::paint(juce::Graphics& g) {
    g.setColour(theme::dim);
    g.setFont(hintFont());
    g.drawText("Levels for", levelsForLabel_, juce::Justification::centredLeft, false);

    if (side_.isEmpty()) return;
    const auto& state = panel_.state();
    theme::paintPanel(g, side_.toFloat());

    // The heading: "HEADPHONES · [In-ears] OUT 1/2", or "THIS SONG".
    const auto caps = capsFont();
    const float y = static_cast<float>(sideTitle_.getY()), h = static_cast<float>(sideTitle_.getHeight());
    float x = static_cast<float>(sideTitle_.getX());
    const float right = static_cast<float>(sideTitle_.getRight());
    const juce::String dot = juce::String::fromUTF8(" \xc2\xb7 ");   // " · "
    const bool hasPill = state.panelLabel.isNotEmpty();
    const float pillWidth = hasPill ? outputLabelPillWidth(state.panelLabelKind) : 0.0f;
    const juce::String channels = state.panelChannels.isNotEmpty() ? " " + state.panelChannels : juce::String();
    const float tail = (hasPill ? widthOf(caps, dot) + pillWidth : 0.0f) + widthOf(caps, channels);
    const auto title = shorten(state.panelTitle, caps, std::max(0.0f, right - x - tail));
    g.setFont(caps);
    g.setColour(theme::dim);
    const auto drawCaps = [&](const juce::String& text) {
        const float w = widthOf(caps, text);
        g.drawText(text, juce::Rectangle<float>(x, y, w + 1.0f, h), juce::Justification::centredLeft, false);
        x += w;
    };
    drawCaps(title);
    if (hasPill) {
        drawCaps(dot);
        drawOutputLabelPill(g, state.panelLabelKind,
                            juce::Rectangle<float>(x, y + 0.5f * (h - kOutputLabelPillHeight), pillWidth, kOutputLabelPillHeight));
        x += pillWidth;
        g.setFont(caps);
        g.setColour(theme::dim);
    }
    if (channels.isNotEmpty()) drawCaps(channels);

    sentenceLayout(state.panelText, static_cast<float>(sideText_.getWidth())).draw(g, sideText_.toFloat());

    if (!trimLabel_.isEmpty()) {
        g.setColour(theme::mid);
        g.setFont(bodyFont());
        g.drawText("Overall level", trimLabel_, juce::Justification::centredLeft, true);
    }
    paintLevels(g);
}

void MixerPage::paintLevels(juce::Graphics& g) {
    if (levelsBox_.getHeight() <= 2 * kBoxPadY) return;
    const auto& state = panel_.state();
    const juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(levelsBox_.getUnion(closingBox_));
    theme::paintPanel(g, levelsBox_.toFloat());
    auto inner = levelsBox_.reduced(kBoxPadX, kBoxPadY);
    const auto caps = capsFont();
    g.setFont(caps);
    g.setColour(theme::dim);
    g.drawText(shorten(kLevelsTitle, caps, static_cast<float>(inner.getWidth())), inner.removeFromTop(kTitleHeight),
               juce::Justification::centredLeft, false);
    inner.removeFromTop(kTitleGap);

    if (state.outputLines.empty()) {
        g.setFont(bodyFont());
        g.setColour(theme::mid);
        g.drawText(state.outputLinesNote, inner.removeFromTop(kLineHeight), juce::Justification::centredLeft, true);
    }
    for (const auto& line : state.outputLines) {
        auto row = inner.removeFromTop(kLineHeight);
        // On the right: the meter, then the peak ("clips" in danger, bold).
        const auto valueFont = line.clips ? theme::font(theme::sizeBody, true) : bodyFont();
        const int valueWidth = ceilToInt(widthOf(valueFont, line.value));
        g.setFont(valueFont);
        g.setColour(line.clips ? theme::danger : theme::mid);
        g.drawText(line.value, row.removeFromRight(valueWidth), juce::Justification::centredRight, false);
        if (line.used) {
            row.removeFromRight(kMeterGap);
            const int meterWidth = std::min(kMeterWidth, std::max(0, row.getWidth() / 2));
            const auto meter = row.removeFromRight(meterWidth).withSizeKeepingCentre(meterWidth, kMeterHeight).toFloat();
            g.setColour(theme::field);
            g.fillRoundedRectangle(meter, 0.5f * kMeterHeight);
            if (line.fill01 > 0.0f) {
                g.setColour(line.clips ? theme::danger : theme::mint);
                g.fillRoundedRectangle(meter.withWidth(line.fill01 * meter.getWidth()), 0.5f * kMeterHeight);
            }
        }
        row.removeFromRight(kMeterGap);
        g.setFont(bodyFont());
        g.setColour(theme::mid);
        g.drawText(shorten(line.name, bodyFont(), static_cast<float>(row.getWidth())), row, juce::Justification::centredLeft, false);
        if (line.detail.isNotEmpty()) {
            const auto detail = inner.removeFromTop(kDetailHeight);
            g.setFont(hintFont());
            g.setColour(theme::danger);
            g.drawText(shorten(line.detail, hintFont(), static_cast<float>(detail.getWidth())), detail, juce::Justification::centredLeft, false);
        }
    }

    if (closingBox_.getHeight() <= 2 * kBoxPadY) return;
    theme::paintPanel(g, closingBox_.toFloat());
    const auto text = closingBox_.reduced(kBoxPadX, kBoxPadY);
    sentenceLayout(kLevelsClosing, static_cast<float>(text.getWidth()), hintFont(), theme::dim).draw(g, text.toFloat());
}

}  // namespace sqlive::ui
