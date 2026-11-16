#include "ui/SongEditorPage.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "app/PartFiles.h"
#include "engine/Click.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SongEditorLanes.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

const juce::String kDash = juce::String::fromUTF8("\xe2\x80\x93");         // –

// The mockup's CSS: .songs (200 px), .ebar, .tl (a 150 px header column; rows of 26, 44 and 36 px), .insp (290 px), .foot.
constexpr int kSongsWidth = 200, kSongsPad = 10, kCapsHeight = 14, kCapsGap = 8;
constexpr int kSongItemHeight = 30, kSongGap = 4, kNewSongGap = 10, kSongPadX = 9;
constexpr int kBarHeight = 47, kBarPadX = 12, kBarGap = 8, kTitleGap = 6, kChipHeight = 24, kButtonHeight = 30;
constexpr int kFooterHeight = 32, kFooterPadX = 12, kFooterGap = 14;
constexpr int kInspectorWidth = 290, kInspectorPad = 12, kCardGap = 10, kCardPadX = 12, kCardPadY = 10, kCardRow = 30;
constexpr int kValueWidth = 96, kFieldHeight = 24, kChoiceWidth = 130;
constexpr int kEmptyCardWidth = 460, kEmptyCardHeight = 150;

const juce::StringArray kSignatures{"2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"};
const juce::StringArray kMajorKeys{"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
const juce::StringArray kMinorKeys{"Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"};
const int kCountIns[] = {0, 1, 2, 4};
const juce::StringArray kSoundNames{"wood", "beep", "rim"}, kSoundWords{"Wood block", "Beep", "Rim"};
const juce::StringArray kSubdivisionNames{"none", "eighths", "triplets", "sixteenths"}, kSubdivisionWords{"None", "8ths", "Triplets", "16ths"};

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font boldFont() { return theme::font(theme::sizeBody, true); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font smallBoldFont() { return theme::font(theme::sizeSmall, true); }

int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

juce::String countInWords(int bars) {
    if (bars <= 0) return "none";
    return juce::String(bars) + (bars == 1 ? " bar" : " bars");
}

// "1.5 s", "0 s": up to three decimals, no trailing zeros.
juce::String secondsText(double seconds) {
    auto text = juce::String(std::round(seconds * 1000.0) / 1000.0, 3);
    if (text.containsChar('.')) text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return text + " s";
}

// A positive number typed by the user ("128", "92.5", "1.5 s"); nullopt when it isn't one.
std::optional<double> typedNumber(juce::String text) {
    text = text.trim();
    if (text.endsWithIgnoreCase("s")) text = text.dropLastCharacters(1).trim();
    if (!text.containsAnyOf("0123456789") || !text.containsOnly("0123456789.")) return std::nullopt;
    const double value = text.getDoubleValue();
    return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

// "33.1" and "2:14": where the cursor is.
void footerOf(const model::SongDoc& song, double cursorSeconds, juce::String& bar, juce::String& time) {
    const auto at = model::tempoMapOf(song).barBeatAt(cursorSeconds);
    bar = juce::String(at.bar) + "." + juce::String(static_cast<int>(std::floor(at.beat + 1e-9)));
    time = formatTime(cursorSeconds);
}

// A chip's text as the Chip draws it: "128 BPM" is a value and a label, "Key E" and "Count-in 1 bar" a label and a value,
// "4/4" a value.
void setChip(Chip& chip, const juce::String& text) {
    if (text.startsWith("Key ") || text.startsWith("Count-in "))
        chip.setText(text.upToFirstOccurrenceOf(" ", false, false), text.fromFirstOccurrenceOf(" ", false, false), true);
    else if (text.containsChar(' '))
        chip.setText(text.fromFirstOccurrenceOf(" ", false, false), text.upToFirstOccurrenceOf(" ", false, false));
    else
        chip.setText({}, text);
}

// A body that scrolls in its viewport: as wide as the viewport leaves it, and at least as tall.
void fit(juce::Viewport& view, juce::Component& body, int idealHeight) {
    const bool scrolls = idealHeight > view.getHeight();
    const int width = std::max(0, view.getWidth() - (scrolls ? view.getScrollBarThickness() : 0));
    body.setSize(width, std::max(idealHeight, view.getHeight()));
}

}  // namespace

// ---- The state ---------------------------------------------------------------------------------------------------------

std::set<juce::String> missingParts(const app::Session& session, const juce::String& songId) {
    std::set<juce::String> missing;
    for (const auto& problem : app::partFileProblems(session, songId))
        if (!problem.inCloud) missing.insert(problem.partId);
    return missing;
}

SongEditorState makeSongEditorState(const app::Session& session, const juce::String& songId, double cursorSeconds,
                                    const std::set<juce::String>* missing) {
    SongEditorState state;
    const auto& model = session.model();
    for (const auto& id : model.songIds()) {
        state.songs.add(model.song(id)->title);
        state.songIds.add(id);
    }
    switch (session.saveState()) {
        case app::SaveState::Saved: state.saved = juce::String::fromUTF8("Saved \xe2\x9c\x93"); break;
        case app::SaveState::Saving: state.saved = "Saving" + kEllipsis; break;
        case app::SaveState::Waiting: state.saved = "Waiting for your answer"; break;
        case app::SaveState::Failed:
            state.saved = session.saveError().isNotEmpty() ? session.saveError() : juce::String("The band couldn't be saved.");
            state.saveFailed = true;
            break;
    }

    const auto* song = model.song(songId);
    if (song == nullptr) return state;
    state.songId = songId;
    state.title = song->title;
    state.selectedSong = state.songIds.indexOf(songId);

    const auto first = song->tempo.empty() ? model::TempoPointDoc{} : song->tempo.front();
    state.chips.add(formatBpm(first.bpm) + " BPM");
    state.chips.add(juce::String(first.numerator) + "/" + juce::String(first.denominator));
    state.chips.add("Key " + (song->key.isEmpty() ? kDash : song->key));
    state.chips.add("Count-in " + countInWords(song->countInBars));

    const auto looked = missing != nullptr ? std::set<juce::String>() : missingParts(session, songId);
    const auto& missingNow = missing != nullptr ? *missing : looked;
    for (const auto& part : song->parts) {
        const auto* role = model.role(part.roleId);
        state.parts.push_back({part.id, part.name, role != nullptr ? role->name : juce::String(),
                               role != nullptr ? role->colour : theme::dim, missingNow.count(part.id) > 0});
    }

    footerOf(*song, cursorSeconds, state.footerBar, state.footerTime);

    const auto loading = session.cache().state(songId);
    state.loading = loading == app::SongCache::State::Loading || loading == app::SongCache::State::NotLoaded;
    state.progress = state.loading ? session.cache().progress(songId) : 0.0;
    const auto now = session.now();
    state.playing = now.playing && now.playingSongId == songId;
    state.anyPlaying = now.playing;

    state.barOneSeconds = song->audioStartSeconds;
    state.clickOn = song->click.enabled;
    state.accentFirstBeat = song->click.accentDownbeat;
    state.clickSound = std::max(0, kSoundNames.indexOf(song->click.sound));
    state.subdivision = std::max(0, kSubdivisionNames.indexOf(song->click.subdivision));
    return state;
}

// ---- The songs, left ---------------------------------------------------------------------------------------------------

class SongEditorPage::SongsBody : public juce::Component {
public:
    explicit SongsBody(SongEditorPage& page) : page_(page) {}

    int idealHeight() const { return count() * (kSongItemHeight + kSongGap) - kSongGap + kNewSongGap + kSongItemHeight; }

    void paint(juce::Graphics& g) override {
        const auto& state = page_.state_;
        for (int i = 0; i < count(); ++i) {
            const auto area = itemArea(i).toFloat();
            const bool on = i == state.selectedSong;
            if (on || i == hovered_) {
                g.setColour(on ? theme::field : theme::field.withMultipliedAlpha(0.5f));
                g.fillRoundedRectangle(area, 7.0f);
            }
            if (on) {                                             // the inset 1 px mint outline
                g.setColour(theme::mint);
                g.drawRoundedRectangle(area.reduced(0.5f), 7.0f, 1.0f);
            }
            const auto text = area.reduced(static_cast<float>(kSongPadX), 0.0f);
            g.setColour(on ? theme::text : theme::mid);
            g.setFont(bodyFont());
            g.drawText(shorten(state.songs[i], bodyFont(), text.getWidth()), text, juce::Justification::centredLeft, false);
        }
        const auto add = newArea().toFloat().reduced(0.5f);
        if (hovered_ == count()) {
            g.setColour(theme::field.withMultipliedAlpha(0.5f));
            g.fillRoundedRectangle(add, 7.0f);
        }
        juce::Path outline, dashed;
        outline.addRoundedRectangle(add, 7.0f);
        const float dashes[] = {4.0f, 3.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(theme::mint.withAlpha(0.4f));
        g.fillPath(dashed);
        g.setColour(theme::mint);
        g.setFont(boldFont());
        g.drawText("+ New song", add, juce::Justification::centred, true);
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(itemAt(e.y)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const juce::MouseEvent& e) override {
        const int index = itemAt(e.y);
        if (index < 0 || e.mods.isPopupMenu()) return;
        if (index == count()) {
            if (page_.onNewSong) page_.onNewSong();
            return;
        }
        page_.openSong(page_.state_.songIds[index]);
        if (page_.isShowing()) page_.grabKeyboardFocus();
    }

private:
    int count() const { return page_.state_.songs.size(); }
    juce::Rectangle<int> itemArea(int i) const { return {0, i * (kSongItemHeight + kSongGap), getWidth(), kSongItemHeight}; }
    juce::Rectangle<int> newArea() const {
        return {0, count() * (kSongItemHeight + kSongGap) - kSongGap + kNewSongGap, getWidth(), kSongItemHeight};
    }

    // The item under y; count() for "+ New song"; -1 for none.
    int itemAt(int y) const {
        for (int i = 0; i < count(); ++i)
            if (itemArea(i).contains(0, y)) return i;
        return newArea().contains(0, y) ? count() : -1;
    }

    void setHovered(int index) {
        if (index == hovered_) return;
        hovered_ = index;
        repaint();
    }

    SongEditorPage& page_;
    int hovered_ = -1;
};

// ---- The lanes ---------------------------------------------------------------------------------------------------------

// One row per part (its header, then its waveform), then the Click row, each header in the 150 px column on the left.
// ---- The cursor --------------------------------------------------------------------------------------------------------

// The white line, with a soft glow, over the ruler and the lanes. Never takes the mouse.
class SongEditorPage::CursorLine : public juce::Component {
public:
    CursorLine() { setInterceptsMouseClicks(false, false); }

    void setX(std::optional<float> x) {
        if (x == x_) return;
        repaintAround(x_);
        x_ = x;
        repaintAround(x_);
    }

    void paint(juce::Graphics& g) override {
        if (!x_) return;
        const float h = static_cast<float>(getHeight());
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRect(*x_ - 5.0f, 0.0f, 10.0f, h);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.fillRect(*x_ - 3.0f, 0.0f, 6.0f, h);
        g.setColour(juce::Colours::white);
        g.fillRect(*x_ - 1.0f, 0.0f, 2.0f, h);
    }

private:
    void repaintAround(std::optional<float> x) {
        if (x) repaint(juce::roundToInt(*x) - 7, 0, 14, getHeight());
    }

    std::optional<float> x_;
};

// ---- The title and the chips -------------------------------------------------------------------------------------------

// The song's title, 18 px semibold, shortened with "…" to its width. A click renames the song.
class SongEditorPage::TitleLabel : public juce::Component {
public:
    std::function<void()> onClick;
    juce::String drawn;

    void setTitle(const juce::String& title) {
        title_ = title;
        resized();
        repaint();
    }
    int idealWidth() const { return ceilToInt(widthOf(titleFont(), title_)) + 1; }

    void resized() override { drawn = shorten(title_, titleFont(), static_cast<float>(getWidth())); }
    void paint(juce::Graphics& g) override {
        g.setColour(theme::text);
        g.setFont(titleFont());
        g.drawText(drawn, getLocalBounds(), juce::Justification::centredLeft, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (isEnabled() && !e.mods.isPopupMenu() && onClick) onClick();
    }

private:
    juce::String title_;
};

// A Chip that is pressed to edit it.
class SongEditorPage::EditChip : public Chip {
public:
    EditChip() {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    std::function<void()> onClick;
    void mouseDown(const juce::MouseEvent& e) override {
        if (isEnabled() && !e.mods.isPopupMenu() && onClick) onClick();
    }
};

// ---- The page ----------------------------------------------------------------------------------------------------------

SongEditorPage::SongEditorPage(app::Session& session)
    : session_(session),
      songs_(std::make_unique<SongsBody>(*this)),
      lanes_(std::make_unique<LanesBody>(*this)),
      cursorLine_(std::make_unique<CursorLine>()),
      title_(std::make_unique<TitleLabel>()),
      tap_("Tap"),
      play_(juce::String::fromUTF8("\xe2\x96\xb6 Play")),
      setToCursor_("Set to the cursor"),
      emptyNew_("+ New song", PillButton::Style::Primary) {
    setWantsKeyboardFocus(true);

    songsView_.setViewedComponent(songs_.get(), false);
    lanesView_.setViewedComponent(lanes_.get(), false);
    for (auto* view : {&songsView_, &lanesView_}) {
        view->setScrollBarsShown(true, false);
        view->setScrollBarThickness(10);
        addAndMakeVisible(*view);
    }

    title_->onClick = [this] { renameSong(); };
    addAndMakeVisible(*title_);
    for (auto& chip : chips_) {
        chip = std::make_unique<EditChip>();
        addAndMakeVisible(*chip);
    }
    chips_[0]->onClick = [this] { editBpm(); };
    chips_[1]->onClick = [this] { showSignatureMenu(); };
    chips_[2]->onClick = [this] { showKeyMenu(); };
    chips_[3]->onClick = [this] { showCountInMenu(); };

    tap_.onClick = [this] { tap(juce::Time::getMillisecondCounterHiRes() / 1000.0); };
    play_.onClick = [this] { togglePlay(); };
    setToCursor_.onClick = [this] { setBarOneAtCursor(); };
    emptyNew_.onClick = [this] {
        if (onNewSong) onNewSong();
    };
    for (auto* button : {&tap_, &play_, &setToCursor_, &emptyNew_}) {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }

    ruler_.onPointer = [this](float x, juce::ModifierKeys mods) { pointerAt(x, mods); };
    addAndMakeVisible(ruler_);

    for (auto* editor : {&bpmEditor_, &barOneEditor_}) {
        editor->setFont(smallFont());
        editor->setIndents(8, 3);
        editor->setJustification(juce::Justification::centredRight);
    }
    bpmEditor_.setJustification(juce::Justification::centredLeft);
    bpmEditor_.setInputRestrictions(7, "0123456789.");
    bpmEditor_.onReturnKey = [this] { finishBpm(true); };
    bpmEditor_.onEscapeKey = [this] { finishBpm(false); };
    bpmEditor_.onFocusLost = [this] { finishBpm(true); };
    addChildComponent(bpmEditor_);
    barOneEditor_.onReturnKey = [this] { finishBarOne(true); };
    barOneEditor_.onEscapeKey = [this] { finishBarOne(false); };
    barOneEditor_.onFocusLost = [this] { finishBarOne(true); };
    addAndMakeVisible(barOneEditor_);

    clickSound_.setItems(kSoundWords, 0);
    subdivision_.setItems(kSubdivisionWords, 0);
    clickOn_.onChange = [this](bool) { sendClick(); };
    accent_.onChange = [this](bool) { sendClick(); };
    clickSound_.onChange = [this](int) { sendClick(); };
    subdivision_.onChange = [this](int) { sendClick(); };
    for (juce::Component* child : std::initializer_list<juce::Component*>{&songsLabel_, &songLabel_, &clickLabel_, &clickOn_, &accent_,
                                                                          &clickSound_, &subdivision_})
        addAndMakeVisible(child);

    addAndMakeVisible(*cursorLine_);                              // last: over the ruler and the lanes

    session_.changes().addChangeListener(this);
    setVisible(true);                                             // AppShell hides it while another page shows
    refresh(true);
}

SongEditorPage::~SongEditorPage() { session_.changes().removeChangeListener(this); }

const juce::Viewport& SongEditorPage::lanesViewport() const { return lanesView_; }
const juce::Viewport& SongEditorPage::songsViewport() const { return songsView_; }
juce::String SongEditorPage::drawnTitle() const { return title_->drawn; }

juce::String SongEditorPage::songId() const { return songId_; }
double SongEditorPage::cursorSeconds() const { return cursor_; }

void SongEditorPage::openSong(const juce::String& songId) {
    const auto* song = session_.model().song(songId);
    songId_ = song != nullptr ? songId : juce::String();
    if (bpmEditor_.isVisible()) bpmEditor_.setVisible(false);
    tapTempo_ = {};
    cursor_ = song != nullptr ? model::songExtentSeconds(*song, model::tempoMapOf(*song)).first : 0.0;
    session_.setEditFocus(songId_);
    refresh(true);
}

void SongEditorPage::setCursorSeconds(double seconds) {
    if (!std::isfinite(seconds)) return;
    if (doc_) {
        const auto [start, end] = model::songExtentSeconds(*doc_, model::tempoMapOf(*doc_));
        seconds = juce::jlimit(start, end, seconds);
    }
    cursor_ = seconds;
    if (doc_) footerOf(*doc_, cursor_, state_.footerBar, state_.footerTime);
    moveCursorLine();
    repaint(footer_);
}

void SongEditorPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void SongEditorPage::visibilityChanged() {
    if (!isVisible()) return;
    if (!fresh_) refresh();
    if (isShowing()) grabKeyboardFocus();
}

void SongEditorPage::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!isVisible() && !force) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    const auto* doc = session_.model().song(songId_);
    if (doc == nullptr) songId_.clear();                          // removed from the band: the empty state

    const auto now = session_.now();
    if (doc != nullptr && now.playing && now.playingSongId == songId_) cursor_ = now.seconds;   // following the playhead
    auto loaded = session_.cache().loaded(songId_);
    const bool sameDoc = doc == nullptr ? !doc_.has_value() : doc_.has_value() && *doc_ == *doc;
    // Which audio files are missing is looked for on disk only when the song or its audio changed, never 30 times a second.
    if (!sameDoc || loaded != loaded_ || force || !missingKnown_) {
        missing_ = doc != nullptr ? missingParts(session_, songId_) : std::set<juce::String>();
        missingKnown_ = true;
    }
    auto next = makeSongEditorState(session_, songId_, cursor_, &missing_);

    // While a song plays the session changes 30 times a second, and usually only the cursor has moved.
    const auto withoutCursor = [](SongEditorState s) {
        s.footerBar.clear();
        s.footerTime.clear();
        return s;
    };
    const bool onlyCursor = sameDoc && loaded == loaded_ && editable() == shownEditable_ && withoutCursor(next) == withoutCursor(state_);
    const bool footerChanged = next.footerBar != state_.footerBar || next.footerTime != state_.footerTime;
    state_ = std::move(next);
    if (onlyCursor && !force) {
        moveCursorLine();
        if (footerChanged) repaint(footer_);
        return;
    }
    if (doc != nullptr) doc_ = *doc;
    else doc_.reset();
    loaded_ = std::move(loaded);
    shownEditable_ = editable();
    updateControls();
    resized();
    lanes_->invalidate();
    songs_->repaint();
    repaint();
}

void SongEditorPage::updateControls() {
    const bool open = state_.songId.isNotEmpty();
    const bool edit = editable();
    for (juce::Component* c : std::initializer_list<juce::Component*>{title_.get(), &tap_, &play_, &ruler_, &lanesView_, cursorLine_.get(),
                                                                      &setToCursor_, &barOneEditor_, &songLabel_, &clickLabel_,
                                                                      &clickOn_, &accent_, &clickSound_, &subdivision_})
        c->setVisible(open);
    for (auto& chip : chips_) chip->setVisible(open);
    emptyNew_.setVisible(!open);
    emptyNew_.setEnabled(session_.mode() == app::Mode::Edit);
    if (!open) {
        bpmEditor_.setVisible(false);
        return;
    }

    title_->setTitle(state_.title);
    title_->setEnabled(edit);
    title_->setMouseCursor(edit ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    for (int i = 0; i < static_cast<int>(chips_.size()); ++i) {
        setChip(*chips_[static_cast<std::size_t>(i)], state_.chips[i]);
        chips_[static_cast<std::size_t>(i)]->setEnabled(edit);
    }
    tap_.setEnabled(edit);
    // A press stops whatever plays, this song or another, so the button says Stop whenever anything does.
    play_.setButtonText(juce::String::fromUTF8(state_.anyPlaying ? "\xe2\x96\xa0 Stop" : "\xe2\x96\xb6 Play"));
    play_.setEnabled(session_.mode() == app::Mode::Edit && (state_.anyPlaying || !state_.loading));

    if (!barOneEditor_.hasKeyboardFocus(true)) barOneEditor_.setText(secondsText(state_.barOneSeconds), juce::dontSendNotification);
    barOneEditor_.setReadOnly(!edit);
    setToCursor_.setEnabled(edit);
    clickOn_.setOn(state_.clickOn);
    accent_.setOn(state_.accentFirstBeat);
    clickSound_.setSelected(state_.clickSound);
    subdivision_.setSelected(state_.subdivision);
    for (juce::Component* c : std::initializer_list<juce::Component*>{&clickOn_, &accent_, &clickSound_, &subdivision_}) c->setEnabled(edit);
}

void SongEditorPage::refreshScale() {
    if (!doc_ || ruler_.getWidth() <= 0) {
        scale_.reset();
        return;
    }
    const auto tempo = model::tempoMapOf(*doc_);
    const auto [start, end] = model::songExtentSeconds(*doc_, tempo);
    scale_.emplace(tempo, start, end, static_cast<float>(ruler_.getWidth()));
    ruler_.setScale(*scale_);
    lanes_->invalidate();
    moveCursorLine();
}

void SongEditorPage::moveCursorLine() {
    if (!scale_ || state_.songId.isEmpty()) {
        cursorLine_->setX(std::nullopt);
        return;
    }
    const float x = scale_->xAtSeconds(cursor_);
    cursorLine_->setX(x >= 0.0f && x <= scale_->width() ? std::optional<float>(x) : std::nullopt);
}

void SongEditorPage::resized() {
    auto area = getLocalBounds();
    songsColumn_ = area.removeFromLeft(std::min(kSongsWidth, area.getWidth()));
    auto songs = songsColumn_.reduced(kSongsPad);
    songs.removeFromRight(1);                                     // the column's border
    songsLabel_.setBounds(songs.removeFromTop(kCapsHeight));
    songs.removeFromTop(kCapsGap);
    songsView_.setBounds(songs);
    fit(songsView_, *songs_, songs_->idealHeight());

    rowLabels_.clear();
    if (state_.songId.isEmpty()) {
        bar_ = timeline_ = barsHead_ = footer_ = inspector_ = songCard_ = clickCard_ = {};
        // A card in the middle: the sentence, then "+ New song" under it.
        emptyCard_ = area.withSizeKeepingCentre(std::min(kEmptyCardWidth, std::max(0, area.getWidth() - 40)),
                                                std::min(kEmptyCardHeight, area.getHeight()));
        const int w = std::min(emptyNew_.idealWidth(), emptyCard_.getWidth());
        emptyNew_.setBounds(juce::Rectangle<int>(emptyCard_.getCentreX() - w / 2, emptyCard_.getBottom() - 26 - kButtonHeight, w, kButtonHeight)
                                .getIntersection(emptyCard_));
        scale_.reset();
        return;
    }

    emptyCard_ = {};
    inspector_ = area.removeFromRight(std::min(kInspectorWidth, area.getWidth()));
    auto edit = area;
    bar_ = edit.removeFromTop(kBarHeight);
    footer_ = edit.removeFromBottom(kFooterHeight);
    timeline_ = edit;

    // The bar under the top bar: the title, the chips (Tap after the BPM), a gap, then ▶ Play on the right.
    auto bar = bar_.withTrimmedBottom(1).reduced(kBarPadX, 0);
    // As wide as the wider of its two words, so it doesn't move when it changes (PillButton's font and padding).
    const int playWidth = std::max(play_.idealWidth(), ceilToInt(widthOf(boldFont(), juce::String::fromUTF8("\xe2\x96\xa0 Stop")) + 26.0f));
    const auto placeIn = [&bar](juce::Component& c, juce::Rectangle<int> r) { c.setBounds(r.getIntersection(bar)); };
    placeIn(play_, juce::Rectangle<int>(bar.getRight() - playWidth, bar.getCentreY() - kButtonHeight / 2, playWidth, kButtonHeight));
    auto row = bar.withTrimmedRight(playWidth + kBarGap);
    int chipsWidth = tap_.idealWidth();
    for (auto& chip : chips_) chipsWidth += chip->idealWidth() + kBarGap;
    const int titleRoom = std::max(0, row.getWidth() - chipsWidth - kTitleGap);
    const int titleWidth = std::min(title_->idealWidth(), titleRoom);
    placeIn(*title_, row.removeFromLeft(titleWidth).withSizeKeepingCentre(titleWidth, 28));
    row.removeFromLeft(kTitleGap);
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        row.removeFromLeft(kBarGap);
        const int w = std::min(chips_[i]->idealWidth(), std::max(0, row.getWidth()));
        placeIn(*chips_[i], row.removeFromLeft(w).withSizeKeepingCentre(w, kChipHeight));
        if (i == 0) {
            row.removeFromLeft(4);
            const int tw = std::min(tap_.idealWidth(), std::max(0, row.getWidth()));
            placeIn(tap_, row.removeFromLeft(tw).withSizeKeepingCentre(tw, kChipHeight + 2));
        }
    }
    if (bpmEditor_.isVisible()) {
        const auto chip = chips_[0]->getBounds();
        bpmEditor_.setBounds(chip.withWidth(std::max(chip.getWidth(), 64)).getIntersection(bar));
    }

    // The timeline: the ruler over the lane area, and the rows below it, which scroll.
    barsHead_ = timeline_.withWidth(std::min(LanesBody::headerColumn, timeline_.getWidth()))
                    .withHeight(std::min(BarRuler::height, timeline_.getHeight()));
    const int viewHeight = std::max(0, timeline_.getHeight() - BarRuler::height);
    const bool scrolls = lanes_->idealHeight() > viewHeight;
    const int laneWidth = std::max(0, timeline_.getWidth() - LanesBody::headerColumn - (scrolls ? lanesView_.getScrollBarThickness() : 0));
    ruler_.setBounds(timeline_.getX() + LanesBody::headerColumn, timeline_.getY(), laneWidth, barsHead_.getHeight());
    lanesView_.setBounds(timeline_.getX(), timeline_.getY() + barsHead_.getHeight(), timeline_.getWidth(), viewHeight);
    fit(lanesView_, *lanes_, lanes_->idealHeight());
    cursorLine_->setBounds(ruler_.getX(), ruler_.getY(), laneWidth, barsHead_.getHeight() + viewHeight);

    // The panel on the right: SONG, then CLICK, each a card of labelled rows.
    auto inspector = inspector_.withTrimmedLeft(1).reduced(kInspectorPad);
    const auto card = [&](int rows) {
        auto c = inspector.removeFromTop(std::min(inspector.getHeight(), 2 * kCardPadY + kCapsHeight + 6 + rows * kCardRow));
        inspector.removeFromTop(std::min(kCardGap, inspector.getHeight()));
        return c;
    };
    const auto rowOf = [this](juce::Rectangle<int>& inner, const juce::String& label, juce::Component* control, int width, int height) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        if (control != nullptr) {
            const int w = std::min(width, r.getWidth());
            control->setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight())));
            r.removeFromRight(std::min(10, r.getWidth()));
        }
        if (label.isNotEmpty()) rowLabels_.push_back({r, label});
    };
    songCard_ = card(2);
    auto songInner = songCard_.reduced(kCardPadX, kCardPadY);
    songLabel_.setBounds(songInner.removeFromTop(kCapsHeight));
    songInner.removeFromTop(6);
    rowOf(songInner, "Bar 1 starts at", &barOneEditor_, kValueWidth, kFieldHeight);
    rowOf(songInner, {}, &setToCursor_, setToCursor_.idealWidth(), 28);
    clickCard_ = card(4);
    auto clickInner = clickCard_.reduced(kCardPadX, kCardPadY);
    clickLabel_.setBounds(clickInner.removeFromTop(kCapsHeight));
    clickInner.removeFromTop(6);
    rowOf(clickInner, "On", &clickOn_, 34, 18);
    rowOf(clickInner, "Sound", &clickSound_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Subdivision", &subdivision_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Accent the first beat", &accent_, 34, 18);

    refreshScale();
}

void SongEditorPage::paint(juce::Graphics& g) {
    // The song list's column.
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.fillRect(songsColumn_);
    g.setColour(theme::hairline);
    g.fillRect(songsColumn_.getRight() - 1, songsColumn_.getY(), 1, songsColumn_.getHeight());

    if (state_.songId.isEmpty()) {
        if (emptyCard_.isEmpty()) return;
        theme::paintPanel(g, emptyCard_.toFloat());
        g.setColour(theme::mid);
        g.setFont(titleFont());
        g.drawFittedText("Choose a song on the left, or add one.", emptyCard_.reduced(20, 0).withTrimmedTop(24).withHeight(48),
                         juce::Justification::centred, 2);
        return;
    }

    // The bar, the BARS head and the footer's border.
    g.setColour(theme::hairline);
    g.fillRect(bar_.getX(), bar_.getBottom() - 1, bar_.getWidth(), 1);
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRect(barsHead_);
    g.setColour(theme::hairline);
    g.fillRect(barsHead_.getRight() - 1, barsHead_.getY(), 1, barsHead_.getHeight());
    g.fillRect(barsHead_.getX(), barsHead_.getBottom() - 1, barsHead_.getWidth(), 1);
    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeTag, true));
    g.drawText("BARS", barsHead_.reduced(8, 0), juce::Justification::centredLeft, true);

    // The footer: where the cursor is, the key hints, and how the save went on the right.
    g.setColour(theme::hairline);
    g.fillRect(footer_.getX(), footer_.getY(), footer_.getWidth(), 1);
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(footer_);
        auto foot = footer_.toFloat().reduced(static_cast<float>(kFooterPadX), 0.0f);
        const float cy = foot.getCentreY();
        const auto text = [&](const juce::String& words, juce::Colour colour, const juce::Font& font) {
            g.setColour(colour);
            g.setFont(font);
            const float w = widthOf(font, words);
            g.drawText(words, juce::Rectangle<float>(foot.getX(), foot.getY(), w + 1.0f, foot.getHeight()), juce::Justification::centredLeft, false);
            foot.removeFromLeft(w);
        };
        text("Bar ", theme::dim, smallFont());
        text(state_.footerBar, theme::mint, smallBoldFont());
        foot.removeFromLeft(static_cast<float>(kFooterGap));
        text(state_.footerTime, theme::dim, smallFont());
        foot.removeFromLeft(static_cast<float>(kFooterGap));
        foot.removeFromLeft(drawKey(g, "Space", foot.getX(), cy));
        text(" play from the cursor", theme::dim, smallFont());
        foot.removeFromLeft(static_cast<float>(kFooterGap));
        foot.removeFromLeft(drawKey(g, "Ctrl", foot.getX(), cy));
        text("+", theme::dim, smallFont());
        foot.removeFromLeft(drawKey(g, "Z", foot.getX(), cy));
        text(" undo", theme::dim, smallFont());
        foot.removeFromLeft(static_cast<float>(kFooterGap));
        const auto words = shorten(state_.saved, smallFont(), std::max(0.0f, foot.getWidth()));
        g.setColour(state_.saveFailed ? theme::danger : state_.saved.startsWith("Saved") ? theme::mint : theme::dim);
        g.setFont(smallFont());
        g.drawText(words, foot, juce::Justification::centredRight, false);
    }

    // The panel on the right, its cards and their row labels.
    g.setColour(juce::Colours::black.withAlpha(0.15f));
    g.fillRect(inspector_);
    g.setColour(theme::hairline);
    g.fillRect(inspector_.getX(), inspector_.getY(), 1, inspector_.getHeight());
    for (const auto& card : {songCard_, clickCard_})
        if (!card.isEmpty()) theme::paintPanel(g, card.toFloat());
    g.setColour(theme::mid);
    g.setFont(bodyFont());
    for (const auto& [area, label] : rowLabels_)
        g.drawText(shorten(label, bodyFont(), static_cast<float>(area.getWidth())), area, juce::Justification::centredLeft, false);
}

bool SongEditorPage::keyPressed(const juce::KeyPress& key) {
    if (session_.mode() != app::Mode::Edit) return false;
    if (key == juce::KeyPress::spaceKey) {
        togglePlay();
        return true;
    }
    return false;                                                // Ctrl+Z and the redo keys: AppShell, for every Edit page
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

// The open song is looked up every time: an undo can take it away before the page has heard (songId_ is cleared at the
// next refresh), and then every gesture does nothing.
bool SongEditorPage::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

bool SongEditorPage::run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params, coalesceKey);
    if (!result.ok) {
        tell(result.message);
        // What the controls show goes back to what the band says, once the gesture that changed them has finished.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SongEditorPage>(this)] {
            if (safe != nullptr) safe->refresh(true);
        });
    }
    return result.ok;
}

void SongEditorPage::tell(const juce::String& message) {
    dialog_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                     .withIconType(juce::MessageBoxIconType::NoIcon)
                                                     .withAssociatedComponent(this)
                                                     .withTitle(message)
                                                     .withButton("OK"),
                                                 nullptr);
}

void SongEditorPage::askText(const juce::String& title, const juce::String& text, const juce::String& button,
                             std::function<void(const juce::String&)> then) {
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("text", text);
    window->addButton(button, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<SongEditorPage>(this), window, then = std::move(then)](int result) {
                                    if (safe != nullptr && result == 1) then(window->getTextEditorContents("text").trim());
                                }),
                            true);
}

// The first tempo point with what is given changed (0 = keep), the others sent back as they are.
void SongEditorPage::setFirstTempoPoint(std::optional<double> bpm, int numerator, int denominator, const juce::String& coalesceKey) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable()) return;
    auto tempo = model::toVar(*song).getProperty("tempo", juce::var());
    auto* points = tempo.getArray();
    if (points == nullptr || points->isEmpty()) return;
    auto* first = points->getReference(0).getDynamicObject();
    if (first == nullptr) return;
    if (bpm) first->setProperty("bpm", *bpm);
    if (numerator > 0) first->setProperty("numerator", numerator);
    if (denominator > 0) first->setProperty("denominator", denominator);
    run("song.setTempo", jsonObject({{"songId", songId_}, {"tempo", tempo}}), coalesceKey);
}

void SongEditorPage::setBpm(double bpm) {
    if (std::isfinite(bpm) && bpm > 0.0) setFirstTempoPoint(bpm, 0, 0);
}

void SongEditorPage::setSignature(int numerator, int denominator) {
    if (numerator > 0 && denominator > 0) setFirstTempoPoint(std::nullopt, numerator, denominator);
}

void SongEditorPage::setKey(const juce::String& key) { run("song.setInfo", jsonObject({{"songId", songId_}, {"key", key}})); }

void SongEditorPage::setCountIn(int bars) { run("song.setInfo", jsonObject({{"songId", songId_}, {"countInBars", bars}})); }

void SongEditorPage::tap(double nowSeconds) {
    if (!editable()) return;
    const auto bpm = tapTempo_.tap(nowSeconds);
    if (!bpm) {
        ++tapRun_;                                                // a new run of taps: its tempo is one undo step
        return;
    }
    setFirstTempoPoint(std::round(*bpm), 0, 0, "tap:" + songId_ + ":" + juce::String(tapRun_));
}

void SongEditorPage::setBarOneSeconds(double seconds) {
    if (std::isfinite(seconds)) run("song.setAudioStart", jsonObject({{"songId", songId_}, {"seconds", seconds}}));
}

void SongEditorPage::setBarOneAtCursor() {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr || !editable()) return;
    const double before = song->audioStartSeconds;
    const double after = juce::jlimit(0.0, model::kMaxAudioStartSeconds, before + cursor_);
    if (after == before) return;
    if (!run("song.setAudioStart", jsonObject({{"songId", songId_}, {"seconds", after}}))) return;
    cursor_ -= after - before;                                    // the same place in the audio, now counted from the new bar 1
    if (std::abs(cursor_) < 1e-9) cursor_ = 0.0;
    moveCursorLine();
}

void SongEditorPage::setClick(bool on, int sound, int subdivision, bool accentFirstBeat) {
    run("song.setClick", jsonObject({{"songId", songId_},
                                 {"enabled", on},
                                 {"sound", kSoundNames[juce::jlimit(0, kSoundNames.size() - 1, sound)]},
                                 {"subdivision", kSubdivisionNames[juce::jlimit(0, kSubdivisionNames.size() - 1, subdivision)]},
                                 {"accentDownbeat", accentFirstBeat}}));
}

void SongEditorPage::sendClick() { setClick(clickOn_.isOn(), clickSound_.selected(), subdivision_.selected(), accent_.isOn()); }

void SongEditorPage::togglePlay() {
    if (session_.mode() != app::Mode::Edit) return;
    if (session_.now().playing) {
        session_.perform(app::Action::Stop);
        return;
    }
    if (session_.model().song(songId_) != nullptr) session_.playSong(songId_, cursor_);
}

void SongEditorPage::pointerAt(float laneX, juce::ModifierKeys mods) {
    if (!scale_ || songId_.isEmpty()) return;
    if (isShowing()) grabKeyboardFocus();
    if (state_.playing) return;                                   // while it plays, the cursor follows the playhead
    const float x = juce::jlimit(0.0f, scale_->width(), laneX);
    setCursorSeconds(mods.isAltDown() ? scale_->secondsAtX(x) : scale_->tempo().secondsAt(scale_->snap(x, 1)));
}

void SongEditorPage::renameSong() {
    if (!editable()) return;
    askText("Rename the song", state_.title, "Rename", [this](const juce::String& title) {
        if (title.isNotEmpty()) run("song.setInfo", jsonObject({{"songId", songId_}, {"title", title}}));
    });
}

void SongEditorPage::editBpm() {
    if (!editable() || state_.chips.isEmpty()) return;
    bpmEditor_.setText(state_.chips[0].upToFirstOccurrenceOf(" ", false, false), juce::dontSendNotification);
    bpmEditor_.setVisible(true);
    resized();
    if (bpmEditor_.isShowing()) bpmEditor_.grabKeyboardFocus();
    bpmEditor_.selectAll();
}

void SongEditorPage::finishBpm(bool keep) {
    if (!bpmEditor_.isVisible()) return;
    const auto text = bpmEditor_.getText();
    bpmEditor_.setVisible(false);
    if (keep)
        if (const auto bpm = typedNumber(text)) setBpm(*bpm);
    if (isShowing()) grabKeyboardFocus();
}

void SongEditorPage::finishBarOne(bool keep) {
    if (keep)
        if (const auto seconds = typedNumber(barOneEditor_.getText()); seconds && *seconds != state_.barOneSeconds) setBarOneSeconds(*seconds);
    barOneEditor_.setText(secondsText(state_.barOneSeconds), juce::dontSendNotification);
    if (barOneEditor_.hasKeyboardFocus(false) && isShowing()) grabKeyboardFocus();
}

void SongEditorPage::showSignatureMenu() {
    if (!editable()) return;
    juce::PopupMenu menu;
    for (int i = 0; i < kSignatures.size(); ++i) menu.addItem(i + 1, kSignatures[i], true, kSignatures[i] == state_.chips[1]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chips_[1].get()),
                       [safe = juce::Component::SafePointer<SongEditorPage>(this)](int result) {
                           if (safe == nullptr || result <= 0) return;
                           const auto& signature = kSignatures[result - 1];
                           safe->setSignature(signature.upToFirstOccurrenceOf("/", false, false).getIntValue(),
                                              signature.fromFirstOccurrenceOf("/", false, false).getIntValue());
                       });
}

void SongEditorPage::showKeyMenu() {
    if (!editable()) return;
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    const auto current = song->key;
    juce::PopupMenu menu;
    menu.addItem(1, kDash, true, current.isEmpty());
    for (int i = 0; i < kMajorKeys.size(); ++i) menu.addItem(100 + i, kMajorKeys[i], true, current == kMajorKeys[i]);
    menu.addColumnBreak();
    for (int i = 0; i < kMinorKeys.size(); ++i) menu.addItem(200 + i, kMinorKeys[i], true, current == kMinorKeys[i]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chips_[2].get()),
                       [safe = juce::Component::SafePointer<SongEditorPage>(this)](int result) {
                           if (safe == nullptr || result <= 0) return;
                           if (result == 1) safe->setKey({});
                           else if (result >= 200) safe->setKey(kMinorKeys[result - 200]);
                           else safe->setKey(kMajorKeys[result - 100]);
                       });
}

void SongEditorPage::showCountInMenu() {
    if (!editable()) return;
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    const int current = song->countInBars;
    juce::PopupMenu menu;
    for (const int bars : kCountIns)
        menu.addItem(bars + 1, bars == 0 ? juce::String("None") : countInWords(bars), true, bars == current);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chips_[3].get()),
                       [safe = juce::Component::SafePointer<SongEditorPage>(this)](int result) {
                           if (safe != nullptr && result > 0) safe->setCountIn(result - 1);
                       });
}

void SongEditorPage::showPartMenu(const juce::String& partId) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    const auto part = std::find_if(song->parts.begin(), song->parts.end(), [&](const auto& p) { return p.id == partId; });
    if (part == song->parts.end()) return;
    const bool edit = editable();
    juce::PopupMenu roles;
    juce::StringArray roleIds;
    for (const auto& role : session_.model().band().roles) {
        if (role.kind != model::RoleKind::Part) continue;
        roleIds.add(role.id);
        roles.addItem(100 + roleIds.size() - 1, role.name, edit, role.id == part->roleId);
    }
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    menu.addSubMenu("Role", roles, edit);
    menu.addItem(2, "Remove" + kEllipsis, edit);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(lanes_.get()).withMousePosition(),
        [safe = juce::Component::SafePointer<SongEditorPage>(this), partId, name = part->name, songTitle = song->title, roleIds](int result) {
            if (safe == nullptr || !safe->editable()) return;
            const auto songId = safe->songId_;
            if (result == 1) {
                safe->askText("Rename the part", name, "Rename", [safe, songId, partId](const juce::String& newName) {
                    if (safe != nullptr && newName.isNotEmpty())
                        safe->run("part.set", jsonObject({{"songId", songId}, {"partId", partId}, {"name", newName}}));
                });
            } else if (result == 2) {
                safe->dialog_ = juce::AlertWindow::showScopedAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::NoIcon)
                        .withAssociatedComponent(safe.getComponent())
                        .withTitle("Remove " + name + " from " + songTitle + "?")
                        .withMessage("Undo brings it back.")
                        .withButton("Remove")
                        .withButton("Cancel"),
                    [safe, songId, partId](int answer) {
                        if (safe != nullptr && answer == 1) safe->run("part.remove", jsonObject({{"songId", songId}, {"partId", partId}}));
                    });
            } else if (result >= 100 && result - 100 < roleIds.size()) {
                safe->run("part.set", jsonObject({{"songId", songId}, {"partId", partId}, {"roleId", roleIds[result - 100]}}));
            }
        });
}

}  // namespace sqlive::ui
