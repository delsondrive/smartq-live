#include "ui/SongEditorPage.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include "app/MidiPorts.h"
#include "app/PartFiles.h"
#include "engine/Click.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/MidiImportDialog.h"
#include "ui/MidiLane.h"
#include "ui/PartLanes.h"
#include "ui/Theme.h"
#include "ui/Veil.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

const juce::String kDash = juce::String::fromUTF8("\xe2\x80\x93");         // –

// The mockup's CSS: .songs (200 px), .ebar, .insp (290 px), .foot. The timeline (.tl) is the Timeline's.
constexpr int kSongsWidth = 200, kSongsPad = 10, kCapsHeight = 14, kCapsGap = 8;
constexpr int kSongItemHeight = 30, kSongGap = 4, kNewSongGap = 10, kSongPadX = 9;
constexpr int kBarHeight = 47, kBarPadX = 12, kBarGap = 8, kTitleGap = 6, kChipHeight = 24, kButtonHeight = 30, kToolsRow = 38;
constexpr int kMinTitleWidth = 60;                                  // narrower than this, the tools go to a second row
constexpr int kFooterHeight = 32, kFooterPadX = 12, kFooterGap = 14;
constexpr int kInspectorWidth = 290, kInspectorPad = 12, kCardGap = 10, kCardPadX = 12, kCardPadY = 10, kCardRow = 30;
constexpr int kValueWidth = 96, kFieldHeight = 24, kChoiceWidth = 130;
constexpr int kEmptyCardWidth = 460, kEmptyCardHeight = 150;

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

// ---- The title and the chips -------------------------------------------------------------------------------------------

// The song's title, 18 px semibold, shortened with "…" to its width. A click opens the title menu.
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
      timeline_(session),
      markerInspector_(session),
      title_(std::make_unique<TitleLabel>()),
      tap_("Tap"),
      play_(juce::String::fromUTF8("\xe2\x96\xb6 Play")),
      setToCursor_("Set to the cursor"),
      emptyNew_("+ New song", PillButton::Style::Primary) {
    setWantsKeyboardFocus(true);

    songsView_.setViewedComponent(songs_.get(), false);
    songsView_.setScrollBarsShown(true, false);
    songsView_.setScrollBarThickness(10);
    addAndMakeVisible(songsView_);

    timeline_.onPressed = [this] {
        if (isShowing()) grabKeyboardFocus();
    };
    timeline_.onCursorMoved = [this] {
        if (doc_) footerOf(*doc_, timeline_.cursorSeconds(), state_.footerBar, state_.footerTime);
        repaint(footer_);
    };
    timeline_.onSelectionChanged = [this] {
        showInspector();
        updateControls();                                         // the song panels go while a change is inspected
        resized();
        repaint();
    };
    addAndMakeVisible(timeline_);
    addChildComponent(tempoCard_);
    markerInspector_.onSelect = [this](EditorSelection picked) { timeline_.select(std::move(picked)); };
    markerView_.setViewedComponent(&markerInspector_, false);
    markerView_.setScrollBarsShown(true, false);
    markerView_.setScrollBarThickness(10);
    addChildComponent(markerView_);

    title_->onClick = [this] { showTitleMenu(); };
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
    tools_.setItems({"Select", "Draw"}, 0);
    snapTo_.setItems({"Bar", "Beat", "1/8"}, 1);
    tools_.onChange = [this](int item) { timeline_.setTool(item == 1 ? EditTool::Draw : EditTool::Select); };
    snapTo_.onChange = [this](int item) { timeline_.setSnap(item == 0 ? SnapTo::Bar : item == 2 ? SnapTo::Eighth : SnapTo::Beat); };
    for (juce::Component* child : std::initializer_list<juce::Component*>{&songsLabel_, &songLabel_, &clickLabel_, &clickOn_, &accent_,
                                                                          &clickSound_, &subdivision_, &tools_, &snapTo_})
        addAndMakeVisible(child);

    session_.changes().addChangeListener(this);
    setVisible(true);                                             // AppShell hides it while another page shows
    refresh(true);
}

SongEditorPage::~SongEditorPage() { session_.changes().removeChangeListener(this); }

const juce::Viewport& SongEditorPage::lanesViewport() const { return timeline_.viewport(); }
const juce::Viewport& SongEditorPage::songsViewport() const { return songsView_; }
juce::String SongEditorPage::drawnTitle() const { return title_->drawn; }

juce::String SongEditorPage::songId() const { return songId_; }
double SongEditorPage::cursorSeconds() const { return timeline_.cursorSeconds(); }

void SongEditorPage::openSong(const juce::String& songId) {
    const auto* song = session_.model().song(songId);
    songId_ = song != nullptr ? songId : juce::String();
    if (bpmEditor_.isVisible()) bpmEditor_.setVisible(false);
    tapTempo_ = {};
    timeline_.setSong(songId_);                                   // the cursor at the song's start
    session_.setEditFocus(songId_);
    refresh(true);
}

void SongEditorPage::setCursorSeconds(double seconds) {
    timeline_.setCursorSeconds(seconds);                          // kept within the song
    if (doc_) footerOf(*doc_, timeline_.cursorSeconds(), state_.footerBar, state_.footerTime);
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

    timeline_.followPlayhead();                                   // while this song plays, the cursor is the playhead
    auto loaded = session_.cache().loaded(songId_);
    const bool sameDoc = doc == nullptr ? !doc_.has_value() : doc_.has_value() && *doc_ == *doc;
    // Which audio files are missing is looked for on disk only when the song or its audio changed, never 30 times a second.
    if (!sameDoc || loaded != loaded_ || force || !missingKnown_) {
        missing_ = doc != nullptr ? missingParts(session_, songId_) : std::set<juce::String>();
        missingKnown_ = true;
    }
    auto next = makeSongEditorState(session_, songId_, timeline_.cursorSeconds(), &missing_);

    // While a song plays the session changes 30 times a second, and usually only the cursor has moved.
    const auto withoutCursor = [](SongEditorState s) {
        s.footerBar.clear();
        s.footerTime.clear();
        return s;
    };
    // The MIDI lanes' headers and the inspector's Send now follow the band's devices and whether their ports are open.
    const auto& devices = session_.model().band().midiDevices;
    std::vector<int> ports;
    for (const auto& device : devices) ports.push_back(session_.midi() != nullptr ? static_cast<int>(session_.midi()->status(device.id)) : -1);
    const bool sameMidi = devices == devicesShown_ && ports == portsShown_;
    const bool onlyCursor = sameDoc && sameMidi && loaded == loaded_ && editable() == shownEditable_ && withoutCursor(next) == withoutCursor(state_);
    const bool footerChanged = next.footerBar != state_.footerBar || next.footerTime != state_.footerTime;
    state_ = std::move(next);
    if (onlyCursor && !force) {
        if (footerChanged) repaint(footer_);
        return;
    }
    if (doc != nullptr) doc_ = *doc;
    else doc_.reset();
    loaded_ = std::move(loaded);
    devicesShown_ = devices;
    portsShown_ = std::move(ports);
    shownEditable_ = editable();
    buildLanes();
    timeline_.refresh();
    showInspector();
    updateControls();
    resized();
    songs_->repaint();
    repaint();
}

void SongEditorPage::buildLanes() {
    std::vector<juce::String> parts;
    for (const auto& part : state_.parts) parts.push_back(part.partId);
    auto devices = songId_.isNotEmpty() ? midiLaneDevices(session_, songId_) : std::vector<juce::String>();
    if (songId_ == laneSong_ && parts == laneParts_ && devices == laneDevices_) {
        for (std::size_t i = 0; i < partLanes_.size() && i < state_.parts.size(); ++i) partLanes_[i]->setMissing(state_.parts[i].missing);
        return;
    }
    if (songId_ != laneSong_) timeline_.setSong(songId_);
    laneSong_ = songId_;
    laneParts_ = std::move(parts);
    laneDevices_ = std::move(devices);
    tempoCard_.show(nullptr, -1, false);                          // before the lane it points at goes
    tempoLane_ = nullptr;
    partLanes_.clear();
    std::vector<std::unique_ptr<Lane>> lanes;
    if (songId_.isNotEmpty()) {
        auto tempo = std::make_unique<TempoLane>(session_, songId_);
        tempoLane_ = tempo.get();
        lanes.push_back(std::move(tempo));
        for (const auto& device : laneDevices_) lanes.push_back(std::make_unique<MidiLane>(session_, songId_, device));
        for (const auto& part : state_.parts) {
            auto lane = std::make_unique<PartLane>(session_, songId_, part.partId);
            lane->setMissing(part.missing);
            partLanes_.push_back(lane.get());
            lanes.push_back(std::move(lane));
        }
        lanes.push_back(std::make_unique<ClickLane>(session_, songId_));
    }
    timeline_.setLanes(std::move(lanes));
}

void SongEditorPage::showInspector() {
    const auto& picked = timeline_.selection();
    const bool tempo = picked.kind == EditorSelection::Kind::Tempo && tempoLane_ != nullptr && picked.index >= 0
                       && picked.index < static_cast<int>(tempoLane_->state().points.size());
    const bool change = picked.kind == EditorSelection::Kind::Marker && findMarker(session_, songId_, picked.deviceId, picked.id) != nullptr;
    if ((picked.kind == EditorSelection::Kind::Tempo && !tempo) || (picked.kind == EditorSelection::Kind::Marker && !change)) {
        timeline_.select({});                                     // undone away: nothing is selected (and this runs again)
        return;
    }
    tempoCard_.show(tempo ? tempoLane_ : nullptr, picked.index, editable());
    tempoCard_.setVisible(tempo && state_.songId.isNotEmpty());
    markerInspector_.show(songId_, change ? picked : EditorSelection{});
    markerView_.setVisible(change && state_.songId.isNotEmpty());
}

bool SongEditorPage::changeShown() const { return markerView_.isVisible(); }

void SongEditorPage::updateControls() {
    const bool open = state_.songId.isNotEmpty();
    const bool edit = editable();
    for (juce::Component* c : std::initializer_list<juce::Component*>{title_.get(), &tap_, &play_, &timeline_, &tools_, &snapTo_})
        c->setVisible(open);
    // The song panels give the column to the change inspector.
    for (juce::Component* c : std::initializer_list<juce::Component*>{&setToCursor_, &barOneEditor_, &songLabel_, &clickLabel_, &clickOn_,
                                                                      &accent_, &clickSound_, &subdivision_})
        c->setVisible(open && !changeShown());
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
    tools_.setEnabled(edit);
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
        bar_ = footer_ = inspector_ = songCard_ = clickCard_ = {};
        // A card in the middle: the sentence, then "+ New song" under it.
        emptyCard_ = area.withSizeKeepingCentre(std::min(kEmptyCardWidth, std::max(0, area.getWidth() - 40)),
                                                std::min(kEmptyCardHeight, area.getHeight()));
        const int w = std::min(emptyNew_.idealWidth(), emptyCard_.getWidth());
        emptyNew_.setBounds(juce::Rectangle<int>(emptyCard_.getCentreX() - w / 2, emptyCard_.getBottom() - 26 - kButtonHeight, w, kButtonHeight)
                                .getIntersection(emptyCard_));
        return;
    }

    emptyCard_ = {};
    inspector_ = area.removeFromRight(std::min(kInspectorWidth, area.getWidth()));
    auto edit = area;

    // The bar under the top bar: the title, the chips (Tap after the BPM), a gap, the tools, then ▶ Play on the right. When
    // they would leave the title too little room, the tools go to a second row. Play is as wide as the wider of its two
    // words, so it doesn't move when it changes (PillButton's font and padding).
    const int playWidth = std::max(play_.idealWidth(), ceilToInt(widthOf(boldFont(), juce::String::fromUTF8("\xe2\x96\xa0 Stop")) + 26.0f));
    int chipsWidth = tap_.idealWidth();
    for (auto& chip : chips_) chipsWidth += chip->idealWidth() + kBarGap;
    const int toolsWidth = kBarGap + tools_.idealWidth() + kBarGap + snapTo_.idealWidth();
    const int rowWidth = edit.getWidth() - 2 * kBarPadX - playWidth - kBarGap;
    const bool twoRows = rowWidth - chipsWidth - kTitleGap - toolsWidth < kMinTitleWidth;
    bar_ = edit.removeFromTop(kBarHeight + (twoRows ? kToolsRow : 0));
    footer_ = edit.removeFromBottom(kFooterHeight);
    timeline_.setBounds(edit);

    auto bar = bar_.withTrimmedBottom(1).reduced(kBarPadX, 0);
    auto toolsRow = twoRows ? bar.removeFromBottom(kToolsRow).withTrimmedBottom(8) : juce::Rectangle<int>();
    const auto placeIn = [this](juce::Component& c, juce::Rectangle<int> r) { c.setBounds(r.getIntersection(bar_)); };
    placeIn(play_, juce::Rectangle<int>(bar.getRight() - playWidth, bar.getCentreY() - kButtonHeight / 2, playWidth, kButtonHeight));
    auto row = bar.withTrimmedRight(playWidth + kBarGap);
    if (!twoRows) toolsRow = row.removeFromRight(std::min(toolsWidth, std::max(0, row.getWidth()))).withTrimmedLeft(kBarGap);
    for (auto* tools : {&tools_, &snapTo_}) {                     // left to right, in the second row or on the right
        const int w = std::min(tools->idealWidth(), std::max(0, toolsRow.getWidth()));
        placeIn(*tools, toolsRow.removeFromLeft(w).withSizeKeepingCentre(w, kChipHeight + 2));
        toolsRow.removeFromLeft(std::min(kBarGap, toolsRow.getWidth()));
    }
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

    // The panel on the right: the change inspector alone while a change is selected; else TEMPO while a tempo point is
    // selected, SONG, then CLICK, each a card of labelled rows.
    auto inspector = inspector_.withTrimmedLeft(1).reduced(kInspectorPad);
    if (changeShown()) {
        songCard_ = clickCard_ = {};
        markerView_.setBounds(inspector);
        const int ideal = markerInspector_.idealHeight();
        const int width = std::max(0, inspector.getWidth() - (ideal > inspector.getHeight() ? markerView_.getScrollBarThickness() : 0));
        markerInspector_.setSize(width, ideal);
        return;
    }
    const auto card = [&](int height) {
        auto c = inspector.removeFromTop(std::min(inspector.getHeight(), height));
        inspector.removeFromTop(std::min(kCardGap, inspector.getHeight()));
        return c;
    };
    const auto rowsCard = [&](int rows) { return card(2 * kCardPadY + kCapsHeight + 6 + rows * kCardRow); };
    if (tempoCard_.isVisible()) tempoCard_.setBounds(card(tempoCard_.idealHeight()));
    const auto rowOf = [this](juce::Rectangle<int>& inner, const juce::String& label, juce::Component* control, int width, int height) {
        auto r = inner.removeFromTop(std::min(kCardRow, inner.getHeight()));
        if (control != nullptr) {
            const int w = std::min(width, r.getWidth());
            control->setBounds(r.removeFromRight(w).withSizeKeepingCentre(w, std::min(height, r.getHeight())));
            r.removeFromRight(std::min(10, r.getWidth()));
        }
        if (label.isNotEmpty()) rowLabels_.push_back({r, label});
    };
    songCard_ = rowsCard(2);
    auto songInner = songCard_.reduced(kCardPadX, kCardPadY);
    songLabel_.setBounds(songInner.removeFromTop(kCapsHeight));
    songInner.removeFromTop(6);
    rowOf(songInner, "Bar 1 starts at", &barOneEditor_, kValueWidth, kFieldHeight);
    rowOf(songInner, {}, &setToCursor_, setToCursor_.idealWidth(), 28);
    clickCard_ = rowsCard(4);
    auto clickInner = clickCard_.reduced(kCardPadX, kCardPadY);
    clickLabel_.setBounds(clickInner.removeFromTop(kCapsHeight));
    clickInner.removeFromTop(6);
    rowOf(clickInner, "On", &clickOn_, 34, 18);
    rowOf(clickInner, "Sound", &clickSound_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Subdivision", &subdivision_, kChoiceWidth, kFieldHeight);
    rowOf(clickInner, "Accent the first beat", &accent_, 34, 18);
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

    // The bar's and the footer's borders.
    g.setColour(theme::hairline);
    g.fillRect(bar_.getX(), bar_.getBottom() - 1, bar_.getWidth(), 1);
    g.fillRect(footer_.getX(), footer_.getY(), footer_.getWidth(), 1);

    // The footer: where the cursor is, the key hints, and how the save went on the right.
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
        const auto gap = [&] { foot.removeFromLeft(static_cast<float>(kFooterGap)); };
        text("Bar ", theme::dim, smallFont());
        text(state_.footerBar, theme::mint, smallBoldFont());
        gap();
        text(state_.footerTime, theme::dim, smallFont());
        gap();
        foot.removeFromLeft(drawKey(g, "Space", foot.getX(), cy));
        text(" play from the cursor", theme::dim, smallFont());
        gap();
        foot.removeFromLeft(drawKey(g, "Del", foot.getX(), cy));
        text(" delete", theme::dim, smallFont());
        gap();
        foot.removeFromLeft(drawKey(g, "Ctrl", foot.getX(), cy));
        text("+", theme::dim, smallFont());
        foot.removeFromLeft(drawKey(g, "Z", foot.getX(), cy));
        text(" undo", theme::dim, smallFont());
        gap();
        const auto words = shorten(state_.saved, smallFont(), std::max(0.0f, foot.getWidth()));
        g.setColour(state_.saveFailed ? theme::danger : state_.saved.startsWith("Saved") ? theme::mint : theme::dim);
        g.setFont(smallFont());
        g.drawText(words, foot, juce::Justification::centredRight, false);
    }

    // The panel on the right, its cards and their row labels (the TEMPO card draws its own).
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
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) return timeline_.removeSelected();
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
        tellUser(dialog_, *this, result.message);
        // What the controls show goes back to what the band says, once the gesture that changed them has finished.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SongEditorPage>(this)] {
            if (safe != nullptr) safe->refresh(true);
        });
    }
    return result.ok;
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
    const double at = timeline_.cursorSeconds();
    const double after = juce::jlimit(0.0, model::kMaxAudioStartSeconds, before + at);
    if (after == before) return;
    if (!run("song.setAudioStart", jsonObject({{"songId", songId_}, {"seconds", after}}))) return;
    timeline_.refresh();                                          // the song's new extent, then the same place in the audio,
    const double moved = at - (after - before);               // now counted from the new bar 1
    setCursorSeconds(std::abs(moved) < 1e-9 ? 0.0 : moved);
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
    if (session_.model().song(songId_) != nullptr) session_.playSong(songId_, timeline_.cursorSeconds());
}

void SongEditorPage::showTitleMenu() {
    if (!editable()) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis);
    menu.addItem(2, "Import a MIDI file" + kEllipsis);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(title_.get()),
                       [safe = juce::Component::SafePointer<SongEditorPage>(this)](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->renameSong();
                           else if (result == 2) safe->chooseMidiFile();
                       });
}

void SongEditorPage::renameSong() {
    if (!editable()) return;
    askUserText(*this, "Rename the song", state_.title, "Rename", [this](const juce::String& title) {
        if (title.isNotEmpty()) run("song.setInfo", jsonObject({{"songId", songId_}, {"title", title}}));
    });
}

void SongEditorPage::chooseMidiFile() {
    if (!editable()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Import a MIDI file", juce::File(), "*.mid;*.midi");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<SongEditorPage>(this)](const juce::FileChooser& chooser) {
                              if (safe != nullptr && chooser.getResult() != juce::File()) safe->importMidiFile(chooser.getResult());
                          });
}

void SongEditorPage::importMidiFile(const juce::File& file) {
    if (!editable() || midiImport_ != nullptr) return;
    auto dialog = std::make_unique<MidiImportDialog>(session_, songId_, file);
    auto* d = dialog.get();
    // Often asked from inside the dialog's own button: hidden at once, deleted once that call has returned.
    d->onClose = [safe = juce::Component::SafePointer<SongEditorPage>(this)] {
        if (safe == nullptr || safe->midiImport_ == nullptr) return;
        safe->midiImport_->setVisible(false);
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->midiImport_.reset();
        });
        if (safe->isShowing()) safe->grabKeyboardFocus();
    };
    midiImport_ = std::make_unique<Veil>(std::move(dialog), MidiImportDialog::width, [d] { return d->idealHeight(); });
    auto* host = getParentComponent();                            // the app shell, so the veil covers the top bar too
    midiImport_->coverAndFollow(host != nullptr ? *host : *this);
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
    for (int i = 0; i < kTimeSignatures.size(); ++i) menu.addItem(i + 1, kTimeSignatures[i], true, kTimeSignatures[i] == state_.chips[1]);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(chips_[1].get()),
                       [safe = juce::Component::SafePointer<SongEditorPage>(this)](int result) {
                           if (safe == nullptr || result <= 0) return;
                           const auto& signature = kTimeSignatures[result - 1];
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

}  // namespace sqlive::ui
