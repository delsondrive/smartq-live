#include "ui/MixerPage.h"

#include <algorithm>
#include <cmath>

#include "model/Units.h"
#include "ui/Format.h"
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
constexpr int kRowHeight = 24, kValueMinWidth = 80, kValuePadX = 8, kPillPadX = 7, kPillHeight = 16;
constexpr float kTrimStepDb = 0.1f, kFineTrimStepDb = 0.025f;   // a pixel of drag

juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font valueFont() { return theme::font(theme::sizeBody, true); }
juce::Font capsFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }

float widthOf(const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); }
int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

void keepOffKeyboard(juce::Component& component) {
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) keepOffKeyboard(*child);
}

float toTenth(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
}

juce::String trimText(float db) { return db <= model::kMinDb ? juce::String("off") : formatDb(db) + " dB"; }

// The .lbl-out pill's colours for each label: fill, text, border.
struct PillLook { juce::Colour fill, text, border; bool dashed; };
PillLook pillLook(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Foh: return {juce::Colour(110, 168, 255).withAlpha(0.15f), juce::Colour(0xff8fbcff), juce::Colour(110, 168, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Timecode: return {juce::Colour(182, 157, 255).withAlpha(0.15f), juce::Colour(0xffc4b1ff), juce::Colour(182, 157, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Unused: return {juce::Colours::transparentBlack, theme::dim, theme::hairline, true};
    case model::OutputLabel::Monitor: break;
    }
    return {juce::Colours::white.withAlpha(0.1f), theme::text, theme::hairline, false};
}

// The sentence's height in the side panel at that width.
juce::TextLayout sentenceLayout(const juce::String& text, float width) {
    juce::AttributedString s;
    s.append(text, bodyFont(), theme::mid);
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
    int idealWidth() const { return std::max(kValueMinWidth, ceilToInt(widthOf(valueFont(), trimText(db_))) + 2 * kValuePadX); }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(r, theme::radiusField);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(r.reduced(0.5f), theme::radiusField, 1.0f);
        g.setColour(changed_ ? theme::caution : theme::text);
        g.setFont(valueFont());
        g.drawText(trimText(db_), getLocalBounds().reduced(kValuePadX, 0), juce::Justification::centredRight, false);
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

MixerPage::MixerPage(app::Session& session)
    : session_(session), panel_(session), trim_(std::make_unique<TrimValue>(session)) {
    addAndMakeVisible(panel_);
    addAndMakeVisible(levelsFor_);
    addChildComponent(levelsForList_);
    addAndMakeVisible(songs_);
    addChildComponent(*trim_);
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

MixerPage::~MixerPage() { session_.changes().removeChangeListener(this); }

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
    if (isVisible() && !fresh_) refresh();
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
                             || state.panelText != shownSide_.panelText;
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
    trim_->set(outputId_);
    layOutAgain();
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
    if (output) height += kRowGap + kRowHeight;
    side_ = side_.withHeight(std::min(height, side_.getHeight()));

    auto inner = side_.reduced(kBoxPadX, kBoxPadY);
    sideTitle_ = inner.removeFromTop(kTitleHeight);
    inner.removeFromTop(kTitleGap);
    sideText_ = inner.removeFromTop(textHeight);
    trim_->setVisible(output);
    if (output) {
        inner.removeFromTop(kRowGap);
        trimLabel_ = inner.removeFromTop(kRowHeight);
        const int w = std::min(trim_->idealWidth(), trimLabel_.getWidth());
        trim_->setBounds(trimLabel_.removeFromRight(w));
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
    const float pillWidth = hasPill ? widthOf(pillFont(), state.panelLabel) + 2.0f * kPillPadX : 0.0f;
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
        const auto look = pillLook(state.panelLabelKind);
        const auto pill = juce::Rectangle<float>(x, y + 0.5f * (h - kPillHeight), pillWidth, kPillHeight);
        g.setColour(look.fill);
        g.fillRoundedRectangle(pill, 4.0f);
        g.setColour(look.border);
        if (look.dashed) {
            juce::Path outline, dashedOutline;
            outline.addRoundedRectangle(pill.reduced(0.5f), 4.0f);
            const float dashes[] = {3.0f, 2.0f};
            juce::PathStrokeType(1.0f).createDashedStroke(dashedOutline, outline, dashes, 2);
            g.fillPath(dashedOutline);
        } else {
            g.drawRoundedRectangle(pill.reduced(0.5f), 4.0f, 1.0f);
        }
        g.setColour(look.text);
        g.setFont(pillFont());
        g.drawText(state.panelLabel, pill, juce::Justification::centred, false);
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
}

}  // namespace sqlive::ui
