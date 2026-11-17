#include "ui/TopBar.h"

#include <algorithm>
#include <cmath>

#include "app/AudioHost.h"
#include "app/BuildInfo.h"
#include "app/MidiPorts.h"
#include "app/ShowController.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/Logo.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::Action;
using app::Mode;

namespace {

// The mockup's .top: 10 px above and below, 16 px either side, 14 px between its parts.
constexpr int kPadX = 16, kGap = 14;
constexpr float kLogoHeight = 26.0f;
constexpr double kHoldMs = app::ShowController::holdSeconds * 1000.0;

const juce::Colour kOnEmber{0xff160f10};                             // the mockup's text on an ember fill

const juce::String kCheckFiles = "Check files";
const juce::String kUndoKeys = juce::String::fromUTF8("Ctrl+Z undo \xc2\xb7 Ctrl+Y redo");

juce::Font tabFont() { return theme::font(theme::sizeBody, true); }
juce::Font switchFont() { return theme::font(theme::sizeSmall, true); }
juce::Font clockSubFont() { return theme::font(theme::sizeSmall); }
juce::Font clockFont() {                                              // the mockup's 30 px, weight 300
#if JUCE_WINDOWS
    return juce::Font(juce::FontOptions(theme::typefaceName(), "Light", 30.0f).withPointHeight(30.0f));
#else
    return theme::font(30.0f);
#endif
}

int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

const juce::StringArray& tabNames() {
    static const juce::StringArray names{"Setlists", "Song editor", "Mixer", "Setup"};
    return names;
}

// What an undo would take back, in the words the tip uses.
juce::String undoWords(const juce::String& command) {
    if (command == "band.rename") return "rename the band";
    if (command.startsWith("part.")) return "change a part";
    if (command == "mix.setLevel" || command.startsWith("output.")) return "change the routing";
    if (command.startsWith("setlist.")) return "change the setlist";
    if (command == "song.add") return "add a song";
    if (command == "song.remove") return "remove a song";
    if (command.startsWith("song.")) return "change the song";
    if (command.startsWith("rig.")) return "change the rig";
    if (command.startsWith("role.")) return "change the roles";
    if (command.startsWith("device.")) return "change the MIDI devices";
    if (command.startsWith("marker.")) return "change the pedal changes";
    if (command == "controls.set") return "change the controls";
    if (command == "snapshot.apply") return "go back to a snapshot";
    return "the last change";
}

// "02:14": minutes and seconds since bar 1, both two digits.
juce::String clockText(double seconds) {
    const int whole = static_cast<int>(std::floor(std::max(0.0, seconds)));
    return juce::String::formatted("%02d:%02d", whole / 60, whole % 60);
}

// A chip's text as the Chip draws it: "128 BPM" is a value and a label, "Key E" a label and a value, "4/4" a value.
void setChip(Chip& chip, const juce::String& text) {
    if (text.startsWith("Key ")) chip.setText("Key", text.fromFirstOccurrenceOf(" ", false, false), true);
    else if (text.containsChar(' ')) chip.setText(text.fromFirstOccurrenceOf(" ", false, false), text.upToFirstOccurrenceOf(" ", false, false));
    else chip.setText({}, text);
}

void centreOn(juce::Component& c, int x, int width, int height, int centreY) { c.setBounds(x, centreY - height / 2, width, height); }

}  // namespace

bool TopBarState::operator==(const TopBarState& o) const {
    if (pills.size() != o.pills.size()) return false;
    for (std::size_t i = 0; i < pills.size(); ++i)
        if (pills[i].text != o.pills[i].text || pills[i].level != o.pills[i].level || pills[i].editToo != o.pills[i].editToo
            || pills[i].shortText != o.pills[i].shortText || pills[i].tooltip != o.pills[i].tooltip)
            return false;
    return mode == o.mode && page == o.page && stageView == o.stageView && clock == o.clock && clockSub == o.clockSub
           && chips == o.chips && beatsInBar == o.beatsInBar && beatNow == o.beatNow && playing == o.playing
           && canUndo == o.canUndo && undoTip == o.undoTip && holdProgress == o.holdProgress;
}

juce::String pageName(Page page) { return tabNames()[static_cast<int>(page)]; }

TopBarState makeTopBarState(const app::Session& session, const app::AudioHost* host, const app::ShowController* controller,
                            Page page, bool keyboardElsewhere, const app::RemoteStatus& remote) {
    TopBarState t;
    t.mode = session.mode();
    t.page = page;
    t.stageView = session.stageView();

    const auto now = session.now();
    t.playing = now.playing;
    // The playing song, or in Show mode the one that will start.
    const auto songId = now.playingSongId.isNotEmpty() ? now.playingSongId : (t.mode == Mode::Show ? now.nextSongId : juce::String());
    if (const auto* song = songId.isNotEmpty() ? session.model().song(songId) : nullptr) {
        const int beat = static_cast<int>(std::floor(now.beat));
        t.clock = clockText(now.seconds);
        t.clockSub = "of " + formatTime(now.lengthSeconds) + kDot
                     + (now.countIn ? juce::String("count-in") : "bar " + juce::String(now.bar) + "." + juce::String(beat));
        t.chips.add(formatBpm(now.bpm) + " BPM");
        t.chips.add(juce::String(now.numerator) + "/" + juce::String(now.denominator));
        if (song->key.isNotEmpty()) t.chips.add("Key " + song->key);
        t.beatsInBar = now.numerator;
        t.beatNow = now.playing ? beat : 0;
    }

    // Show mode never writes the band folder, so it offers no undo.
    t.canUndo = t.mode == Mode::Edit && session.commands().canUndo();
    t.undoTip = t.canUndo ? "Undo: " + undoWords(session.commands().undoName()) : juce::String();
    t.holdProgress = controller != nullptr ? controller->holdProgress() : 0.0;

    using Level = StatusPill::Level;
    if (keyboardElsewhere) t.pills.push_back({"Keyboard elsewhere", Level::Warning});
    if (host != nullptr && host->isLost()) t.pills.push_back({"Audio lost", Level::Bad});
    else if (host != nullptr && host->isOpen()) t.pills.push_back({"Audio", Level::Ok});
    else t.pills.push_back({"No audio", Level::Bad});
    // MIDI: how many devices are connected, which is missing, or that none has a port here; nothing without devices or
    // ports (tests, screenshots).
    if (const auto* ports = session.midi(); ports != nullptr && !session.model().band().midiDevices.empty()) {
        const auto missing = ports->missingDeviceNames();
        if (!missing.isEmpty()) {
            const auto who = missing.size() == 1 ? missing[0] : juce::String(missing.size());
            t.pills.push_back({"MIDI: " + who + " missing", Level::Warning, true, "MIDI missing"});
        } else if (const int connected = ports->connectedCount(); connected > 0) {
            t.pills.push_back({"MIDI " + juce::String(connected), Level::Ok});
        } else {
            t.pills.push_back({"MIDI: not connected", Level::Warning, true, "No MIDI"});   // no device has a port on this computer
        }
    }
    // The phone remote, while it runs: how many phones follow it. A phone waiting to be let in needs a look in Edit mode,
    // where it can be let in; in Show mode it can't be, so it isn't flagged there.
    if (remote.running) {
        const auto text = "Remote " + juce::String(remote.connected);
        if (remote.waiting && t.mode == Mode::Edit) t.pills.push_back({text, Level::Warning, true, {}, "A phone is waiting to be let in."});
        else t.pills.push_back({text, Level::Ok});
    } else if (remote.leftOn) {
        // Left on, yet not listening: phones can't follow the show, and that is never silent.
        t.pills.push_back({"Remote off", Level::Warning, true, {},
                           "The phone remote couldn't start. Setup, then Remote, says why."});
    }
    // The conflict copies are the list the session keeps from its last look at the band folder: nothing here scans.
    if (!session.loadReport().notes.isEmpty() || !session.conflictCopies().empty()) t.pills.push_back({kCheckFiles, Level::Warning});
    return t;
}

// ---- The transport buttons -------------------------------------------------------------------------------------------

// The mockup's .tbtn: 44 px squares on a field, and the 64 x 52 mint ▶ with its glow. The icons are drawn, so they never
// depend on a font having the symbols. While a song plays, the ▶ carries a ring that fills as a stop is held.
class TopBar::TransportButton : public juce::Component, public juce::SettableTooltipClient {
public:
    enum class Kind { Earlier, Play, Later };

    explicit TransportButton(Kind kind) : kind_(kind) {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    std::function<void()> onPress, onRelease;

    void setRing(bool shown, double progress) {
        if (shown == ringShown_ && progress == ring_) return;
        ringShown_ = shown;
        ring_ = progress;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const auto area = getLocalBounds().toFloat();
        const bool play = kind_ == Kind::Play;
        if (play) {
            juce::Path shape;
            shape.addRoundedRectangle(face(), theme::radiusPanel);
            juce::DropShadow(theme::mint.withAlpha(0.35f), 24, {}).drawForPath(g, shape);
            LiveLookAndFeel::fillButton(g, face(), theme::mint, juce::Colours::transparentBlack, hot_, down_);
        } else {
            LiveLookAndFeel::fillButton(g, face(), theme::field, theme::hairline, hot_, down_);
        }

        const auto c = area.getCentre();
        const float s = play ? 9.0f : 6.0f;                      // half the icon's height: the mockup's 22 and 16 px glyphs
        g.setColour(play ? theme::onAccent : (hot_ ? theme::text : theme::mid));
        juce::Path icon;
        switch (kind_) {
            case Kind::Play: icon.addTriangle(c.x - 0.8f * s, c.y - s, c.x - 0.8f * s, c.y + s, c.x + 1.0f * s, c.y); break;
            case Kind::Earlier:
                icon.addRectangle(c.x - s, c.y - s, 2.0f, 2.0f * s);
                icon.addTriangle(c.x + s, c.y - s, c.x + s, c.y + s, c.x - s + 2.0f, c.y);
                break;
            case Kind::Later:
                icon.addRectangle(c.x + s - 2.0f, c.y - s, 2.0f, 2.0f * s);
                icon.addTriangle(c.x - s, c.y - s, c.x - s, c.y + s, c.x + s - 2.0f, c.y);
                break;
        }
        g.fillPath(icon);

        if (play && ringShown_) {
            const float r = 18.0f;
            g.setColour(theme::onAccent.withAlpha(0.2f));
            g.drawEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r, 2.5f);
            if (ring_ > 0.0) {
                juce::Path arc;
                arc.addCentredArc(c.x, c.y, r, r, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * static_cast<float>(std::min(1.0, ring_)), true);
                g.setColour(theme::onAccent);
                g.strokePath(arc, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }
    }

    bool hitTest(int x, int y) override { return face().contains(static_cast<float>(x), static_cast<float>(y)); }
    void mouseEnter(const juce::MouseEvent&) override { hot_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hot_ = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override {
        down_ = true;
        repaint();
        if (onPress) onPress();
    }
    void mouseUp(const juce::MouseEvent&) override {
        down_ = false;
        repaint();
        if (onRelease) onRelease();
    }

private:
    juce::Rectangle<float> face() const {
        return kind_ == Kind::Play ? getLocalBounds().toFloat().withSizeKeepingCentre(64.0f, 52.0f)
                                   : getLocalBounds().toFloat().withSizeKeepingCentre(44.0f, 44.0f);
    }

    Kind kind_;
    bool hot_ = false, down_ = false, ringShown_ = false;
    double ring_ = 0.0;
};

// ---- The About panel -------------------------------------------------------------------------------------------------

namespace {

// The logo, which build this is, and the band folder: opened from the logo in Edit mode.
class AboutPanel : public juce::Component {
public:
    AboutPanel(const juce::File& bandRoot, std::function<void()> openAnother)
        : root_(bandRoot), openAnother_(std::move(openAnother)) {
#if JUCE_MAC
        reveal_.setButtonText("Show in Finder");
#endif
        reveal_.onClick = [this] { root_.revealToUser(); };
        another_.onClick = [this] {
            auto openAnother = openAnother_;
            if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
            if (openAnother) openAnother();
        };
        for (auto* b : {&reveal_, &another_}) {
            b->setWantsKeyboardFocus(false);
            addAndMakeVisible(*b);
        }
        setSize(440, 172);
    }

    void paint(juce::Graphics& g) override {
        drawLogo(g, juce::Rectangle<float>(16.0f, 16.0f, 300.0f, kLogoHeight), true);
        g.setFont(theme::font(theme::sizeSmall));
        g.setColour(theme::dim);
        g.drawText(buildLine(buildInfo()), 16, 52, getWidth() - 32, 18, juce::Justification::centredLeft, true);
        g.setFont(theme::font(theme::sizeBody));
        g.setColour(theme::mid);
        g.drawText(shorten(root_.getFullPathName(), theme::font(theme::sizeBody), static_cast<float>(getWidth() - 32)), 16, 82,
                   getWidth() - 32, 20, juce::Justification::centredLeft, false);
    }

    void resized() override {
        reveal_.setBounds(16, 118, reveal_.idealWidth(), 30);
        another_.setBounds(reveal_.getRight() + 8, 118, another_.idealWidth(), 30);
    }

private:
    juce::File root_;
    std::function<void()> openAnother_;
    PillButton reveal_{"Show in Explorer"};
    PillButton another_{juce::String::fromUTF8("Open another band folder\xe2\x80\xa6")};
};

}  // namespace

// ---- TopBar ----------------------------------------------------------------------------------------------------------

TopBar::TopBar(app::Session& session)
    : session_(session),
      undo_(std::make_unique<PillButton>(juce::String::fromUTF8("\xe2\x86\xb6 Undo"))),
      snapshots_(std::make_unique<PillButton>("Snapshots")),
      earlier_(std::make_unique<TransportButton>(TransportButton::Kind::Earlier)),
      play_(std::make_unique<TransportButton>(TransportButton::Kind::Play)),
      later_(std::make_unique<TransportButton>(TransportButton::Kind::Later)) {
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    undo_->setWantsKeyboardFocus(false);
    undo_->onClick = [this] { if (onUndo) onUndo(); };
    addChildComponent(*undo_);
    snapshots_->setWantsKeyboardFocus(false);
    snapshots_->onClick = [this] { if (onSnapshots) onSnapshots(); };
    addChildComponent(*snapshots_);
    addChildComponent(beats_);

    earlier_->onPress = [this] { if (onAction) onAction(Action::NextSongEarlier); };
    later_->onPress = [this] { if (onAction) onAction(Action::NextSongLater); };
    // ▶ starts on the press. While a song plays it stops only when held for as long as Space must be.
    play_->onPress = [this] {
        if (!state_.playing) {
            if (onAction) onAction(Action::Start);
            return;
        }
        mouseHoldStart_ = juce::Time::getMillisecondCounterHiRes();
        mouseHoldDone_ = false;
        startTimerHz(30);
    };
    play_->onRelease = [this] {
        stopTimer();
        mouseHoldStart_ = -1.0;
        play_->setRing(state_.playing, state_.holdProgress);
    };
    for (auto* b : {earlier_.get(), play_.get(), later_.get()}) addChildComponent(*b);

    apply();
}

TopBar::~TopBar() { stopTimer(); }

void TopBar::setSnapshotsOpen(bool open) { snapshots_->setToggleState(open, juce::dontSendNotification); }

void TopBar::setState(const TopBarState& next) {
    if (next == state_) return;
    state_ = next;
    apply();
}

void TopBar::apply() {
    undo_->setEnabled(state_.canUndo);
    // The keys work on every Edit page (AppShell); there is no Redo button, so the tip is where they are told.
    undo_->setTooltip(state_.undoTip.isEmpty() ? kUndoKeys : state_.undoTip + " (" + kUndoKeys + ")");

    while (chips_.size() < static_cast<std::size_t>(state_.chips.size())) {
        chips_.push_back(std::make_unique<Chip>());
        addChildComponent(*chips_.back());
    }
    for (std::size_t i = 0; i < chips_.size(); ++i)
        if (i < static_cast<std::size_t>(state_.chips.size())) setChip(*chips_[i], state_.chips[static_cast<int>(i)]);

    while (pills_.size() < state_.pills.size()) {
        pills_.push_back(std::make_unique<StatusPill>());
        addChildComponent(*pills_.back());
    }
    for (std::size_t i = 0; i < pills_.size() && i < state_.pills.size(); ++i) {
        pills_[i]->set(state_.pills[i].text, state_.pills[i].level);
        pills_[i]->setTooltip(state_.pills[i].tooltip);
    }

    beats_.set(state_.beatsInBar, state_.beatNow, 12.0f);
    earlier_->setTooltip("Earlier song");
    later_->setTooltip("Later song");
    play_->setTooltip(state_.playing ? "Hold to stop" : "Start");
    if (!state_.playing && mouseHoldStart_ >= 0.0) {                 // the song ended under a held ▶
        stopTimer();
        mouseHoldStart_ = -1.0;
    }
    const double mouseHold = mouseHoldStart_ >= 0.0 ? (juce::Time::getMillisecondCounterHiRes() - mouseHoldStart_) / kHoldMs : 0.0;
    play_->setRing(state_.playing, std::max(state_.holdProgress, std::min(1.0, mouseHold)));

    layout();
    repaint();
}

void TopBar::timerCallback() {
    if (mouseHoldStart_ < 0.0) {
        stopTimer();
        return;
    }
    const double progress = (juce::Time::getMillisecondCounterHiRes() - mouseHoldStart_) / kHoldMs;
    play_->setRing(state_.playing, std::max(state_.holdProgress, std::min(1.0, progress)));
    if (progress >= 1.0 && !mouseHoldDone_) {
        mouseHoldDone_ = true;                                        // once; letting go after it starts nothing
        if (onAction) onAction(Action::Stop);
    }
}

void TopBar::resized() { layout(); }

void TopBar::layout() {
    const int w = getWidth(), centreY = height / 2;
    const bool edit = state_.mode == Mode::Edit, main = !edit && !state_.stageView;

    logo_ = juce::Rectangle<int>(kPadX, centreY - static_cast<int>(kLogoHeight) / 2, ceilToInt(logoWidth(kLogoHeight, true)),
                                 static_cast<int>(kLogoHeight));
    int left = logo_.getRight() + kGap;

    // The EDIT/SHOW switch, on the right.
    const auto sw = switchFont();
    const int switchWidth = ceilToInt(widthOf(sw, "EDIT") + widthOf(sw, "SHOW")) + 4 * 12 + 2;
    modeSwitch_ = juce::Rectangle<int>(w - kPadX - switchWidth, centreY - 14, switchWidth, 28);
    int right = modeSwitch_.getX() - kGap;

    // Edit mode: the tabs, Undo and Snapshots.
    tabs_.clear();
    if (edit) {
        int x = left;
        for (const auto& name : tabNames()) {
            const int tw = ceilToInt(widthOf(tabFont(), name)) + 2 * 12;
            tabs_.emplace_back(x, centreY - 15, tw, 30);
            x += tw + 4;
        }
        const int snapW = snapshots_->idealWidth(), uw = undo_->idealWidth();
        snapshots_->setBounds(right - snapW, centreY - 15, snapW, 30);
        undo_->setBounds(snapshots_->getX() - kGap - uw, centreY - 15, uw, 30);
    }
    undo_->setVisible(edit);
    snapshots_->setVisible(edit);
    // Show mode: the status pills, right to left before the switch. Edit mode: only what needs a look, right to left before
    // Undo: the audio pill when audio isn't OK, the MIDI pill when it is a Warning (editToo), and "Check files", where a
    // click opens the files dialog (in Show mode it only says so: nothing there may change the band folder).
    const bool showPills = !edit;
    checkFiles_ = {};
    std::vector<std::size_t> inEdit;                                  // the pills shown before Undo, left to right
    for (std::size_t i = 0; i < pills_.size(); ++i) {
        const bool inState = i < state_.pills.size();
        const bool shownInEdit = edit && inState
                                 && (state_.pills[i].text == kCheckFiles || state_.pills[i].editToo
                                     || state_.pills[i].level == StatusPill::Level::Bad);
        pills_[i]->setVisible(inState && (showPills || shownInEdit));
        if (inState) pills_[i]->set(state_.pills[i].text, state_.pills[i].level);   // every word, unless too narrow below
        if (shownInEdit) inEdit.push_back(i);
    }
    if (!inEdit.empty()) {
        // Between the tabs and Undo. When every word doesn't fit: the pills' short words, then the widest cut (with "…"),
        // so a pill never runs into the tabs.
        constexpr int kPillGap = 6, kPillMin = 28;
        const int room = undo_->getX() - kGap - (tabs_.back().getRight() + kGap);
        std::vector<int> widths;
        const auto measure = [&] {
            widths.clear();
            for (const auto i : inEdit) widths.push_back(pills_[i]->idealWidth());
        };
        const auto total = [&] {
            int sum = kPillGap * (static_cast<int>(widths.size()) - 1);
            for (const int w2 : widths) sum += w2;
            return sum;
        };
        measure();
        if (total() > room) {
            for (const auto i : inEdit)
                if (state_.pills[i].shortText.isNotEmpty()) pills_[i]->set(state_.pills[i].shortText, state_.pills[i].level);
            measure();
        }
        while (total() > room) {
            const auto widest = std::max_element(widths.begin(), widths.end());
            if (*widest <= kPillMin) break;
            *widest = std::max(kPillMin, *widest - (total() - room));
        }
        int editX = undo_->getX() - kGap;
        for (std::size_t k = inEdit.size(); k-- > 0;) {
            auto& pill = *pills_[inEdit[k]];
            editX -= widths[k];
            centreOn(pill, editX, widths[k], 24, centreY);
            editX -= kPillGap;
            if (state_.pills[inEdit[k]].text == kCheckFiles) checkFiles_ = pill.getBounds();
        }
    }
    if (showPills) {
        int x = right;
        for (std::size_t i = state_.pills.size(); i-- > 0;) {
            const int pw = pills_[i]->idealWidth();
            x -= pw;
            centreOn(*pills_[i], x, pw, 24, centreY);
            x -= 6;
        }
        right = x + 6 - kGap;
    }

    // Show mode, main view: the clock, the chips, the beats, then the transport centred in what is left.
    const bool hasSong = main && state_.clock.isNotEmpty();
    const int transportWidth = 44 + 10 + 64 + 10 + 44;
    int x = left;
    clock_ = {};
    if (hasSong) {
        const int cw = ceilToInt(std::max(widthOf(clockFont(), state_.clock), widthOf(clockSubFont(), state_.clockSub)));
        clock_ = juce::Rectangle<int>(x, centreY - 23, cw, 46);
        x = clock_.getRight() + kGap;
    }
    // The chips go first when the bar is too narrow for everything.
    int chipsWidth = 0;
    for (int i = 0; i < state_.chips.size(); ++i) chipsWidth += chips_[static_cast<std::size_t>(i)]->idealWidth() + (i > 0 ? 6 : 0);
    const int beatsWidth = beats_.idealWidth() + 20;                  // room for the current light's glow
    const int needed = (chipsWidth > 0 ? chipsWidth + kGap : 0) + beatsWidth + kGap + transportWidth;
    const bool showChips = hasSong && x + needed <= right;
    for (std::size_t i = 0; i < chips_.size(); ++i) {
        const bool shown = showChips && i < static_cast<std::size_t>(state_.chips.size());
        chips_[i]->setVisible(shown);
        if (shown) {
            const int cw = chips_[i]->idealWidth();
            centreOn(*chips_[i], x, cw, 28, centreY);
            x += cw + (i + 1 < static_cast<std::size_t>(state_.chips.size()) ? 6 : kGap);
        }
    }
    beats_.setVisible(hasSong);
    if (hasSong) {
        centreOn(beats_, x - 10, beatsWidth, 40, centreY);
        x += beatsWidth - 20 + kGap;
    }

    for (auto* b : {earlier_.get(), play_.get(), later_.get()}) b->setVisible(main);
    if (main) {
        const int free = right - x;
        int tx = x + std::max(0, (free - transportWidth) / 2);       // the mockup's margin: 0 auto
        // centred on the whole bar when that fits, as the mockup's flex row lands it
        const int ideal = (w - transportWidth) / 2;
        if (ideal >= x && ideal + transportWidth <= right) tx = ideal;
        centreOn(*earlier_, tx, 44, 44, centreY);
        play_->setBounds(tx + 44, 0, 84, height);                     // 64 x 52, with room for its glow either side
        centreOn(*later_, tx + 44 + 84, 44, 44, centreY);
    }
}

void TopBar::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.fillRect(bounds);
    g.setColour(theme::hairline);
    g.fillRect(bounds.withTop(bounds.getBottom() - 1));

    drawLogo(g, logo_.toFloat(), true);

    for (std::size_t i = 0; i < tabs_.size(); ++i) {
        const auto r = tabs_[i].toFloat();
        const bool on = static_cast<int>(i) == static_cast<int>(state_.page);
        if (on) {
            juce::Path shape;
            shape.addRoundedRectangle(r, theme::radiusButton);
            g.setColour(theme::field);
            g.fillPath(shape);
            g.saveState();
            g.reduceClipRegion(shape);
            g.setColour(theme::mint);
            g.fillRect(r.withTop(r.getBottom() - 2.0f));
            g.restoreState();
        }
        g.setColour(on ? theme::text : (static_cast<int>(i) == hoveredTab_ ? theme::mid : theme::dim));
        g.setFont(tabFont());
        g.drawText(tabNames()[static_cast<int>(i)], tabs_[i], juce::Justification::centred, false);
    }

    if (!clock_.isEmpty()) {
        g.setColour(theme::text);
        g.setFont(clockFont());
        g.drawText(state_.clock, clock_.withHeight(30), juce::Justification::centredLeft, false);
        g.setColour(theme::dim);
        g.setFont(clockSubFont());
        g.drawText(state_.clockSub, clock_.withTrimmedTop(30), juce::Justification::centredLeft, false);
    }

    // The EDIT/SHOW switch: the mode's segment filled, mint for EDIT and ember for SHOW.
    const auto sw = modeSwitch_.toFloat();
    juce::Path outline;
    outline.addRoundedRectangle(sw, theme::radiusButton);
    const float editWidth = widthOf(switchFont(), "EDIT") + 24.0f + 1.0f;
    const auto editPart = sw.withWidth(editWidth), showPart = sw.withTrimmedLeft(editWidth);
    const bool edit = state_.mode == Mode::Edit;
    g.saveState();
    g.reduceClipRegion(outline);
    g.setColour(edit ? theme::mint : theme::ember);
    g.fillRect(edit ? editPart : showPart);
    g.restoreState();
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(sw.reduced(0.5f), theme::radiusButton, 1.0f);
    g.setFont(switchFont());
    g.setColour(edit ? theme::onAccent : theme::dim);
    g.drawText("EDIT", editPart, juce::Justification::centred, false);
    g.setColour(edit ? theme::dim : kOnEmber);
    g.drawText("SHOW", showPart, juce::Justification::centred, false);
}

juce::Rectangle<int> TopBar::tabArea(Page page) const {
    const auto i = static_cast<std::size_t>(page);
    return i < tabs_.size() ? tabs_[i] : juce::Rectangle<int>();
}

int TopBar::tabAt(juce::Point<int> p) const {
    for (std::size_t i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].contains(p)) return static_cast<int>(i);
    return -1;
}

int TopBar::modeSegmentAt(juce::Point<int> p) const {
    if (!modeSwitch_.contains(p)) return -1;
    const float editWidth = widthOf(switchFont(), "EDIT") + 24.0f + 1.0f;
    return static_cast<float>(p.x - modeSwitch_.getX()) < editWidth ? 0 : 1;
}

void TopBar::mouseDown(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    if (const int tab = tabAt(p); tab >= 0) {
        if (onPage) onPage(static_cast<Page>(tab));
        return;
    }
    if (const int segment = modeSegmentAt(p); segment >= 0) {
        const auto wanted = segment == 0 ? Mode::Edit : Mode::Show;
        if (wanted != state_.mode && onMode) onMode(wanted);
        return;
    }
    if (checkFiles_.contains(p)) {
        if (onCheckFiles) onCheckFiles();
        return;
    }
    if (state_.mode == Mode::Edit && logo_.contains(p)) showAbout();
}

void TopBar::mouseMove(const juce::MouseEvent& e) {
    const auto p = e.getPosition();
    const int tab = tabAt(p);
    if (tab != hoveredTab_) {
        hoveredTab_ = tab;
        repaint();
    }
    const bool clickable = tab >= 0 || modeSwitch_.contains(p) || checkFiles_.contains(p)
                           || (state_.mode == Mode::Edit && logo_.contains(p));
    setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TopBar::mouseExit(const juce::MouseEvent&) {
    if (hoveredTab_ >= 0) {
        hoveredTab_ = -1;
        repaint();
    }
}

void TopBar::showAbout() {
    auto panel = std::make_unique<AboutPanel>(session_.folder().root(), [safe = juce::Component::SafePointer<TopBar>(this)] {
        if (safe != nullptr && safe->onOpenAnotherBand) safe->onOpenAnotherBand();
    });
    juce::CallOutBox::launchAsynchronously(std::move(panel), localAreaToGlobal(logo_), nullptr);
}

}  // namespace sqlive::ui
