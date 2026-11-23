#include "ui/EditorMixerDock.h"

#include <algorithm>

#include "ui/BarRuler.h"
#include "ui/Helpers.h"
#include "ui/PartLanes.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: .split (18 px, hairlines above and under, black at 30 %), .split .lbl (12 px in), .split .grip (44 x 4,
// white at 25 %), .split .fold (10 px in from the right, 15 px high, 8 px either side of its words), and .dock's padding
// (8 px at the top, 10 at the sides, 6 at the foot).
constexpr int kLabelX = 12, kGripWidth = 44, kGripHeight = 4, kFoldRight = 10, kFoldHeight = 15, kFoldPadX = 8;
constexpr int kDockTop = 8, kDockSide = 10, kDockFoot = 6;

const juce::String kWords = juce::String::fromUTF8("MIXER \xc2\xb7 THIS SONG");
const juce::String kHide = juce::String::fromUTF8("\xe2\x96\xbe Hide"), kShow = juce::String::fromUTF8("\xe2\x96\xb4 Mixer");

juce::Font foldFont() { return theme::font(theme::sizeCaption, true); }

}  // namespace

// "▾ Hide" / "▴ Mixer": the mockup's .fold, a small field with its words in mid.
class EditorMixerDock::FoldButton : public juce::Button {
public:
    FoldButton() : juce::Button(kHide) {
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    int idealWidth() const { return static_cast<int>(std::ceil(widthOf(foldFont(), getButtonText()))) + 2 * kFoldPadX + 2; }

    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(over || down ? theme::fieldHover : theme::field);
        g.fillRoundedRectangle(area, 5.0f);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(area, 5.0f, 1.0f);
        g.setColour(theme::mid);
        g.setFont(foldFont());
        g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, false);
    }
};

// The bar between the tracks and the panel: its words, its grip, and the fold button. A drag on it (not on the button)
// resizes the panel; folded, it doesn't drag.
class EditorMixerDock::Splitter : public juce::Component {
public:
    explicit Splitter(EditorMixerDock& dock) : dock_(dock) {
        label_.setText(kWords);
        addAndMakeVisible(label_);
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }

    void resized() override {
        label_.setBounds(kLabelX, 0, std::min(label_.idealWidth() + 2, std::max(0, getWidth() - kLabelX)), getHeight());
    }

    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
        g.fillRect(getLocalBounds());
        g.setColour(theme::hairline);
        g.fillRect(0, 0, getWidth(), 1);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(kGripWidth), static_cast<float>(kGripHeight))
                                   .withCentre(getLocalBounds().toFloat().getCentre()),
                               2.0f);
    }

    void mouseDown(const juce::MouseEvent&) override { start_ = dock_.panelHeight(); }
    void mouseDrag(const juce::MouseEvent& e) override { dock_.dragged(start_, e.getDistanceFromDragStartY(), false); }
    void mouseUp(const juce::MouseEvent& e) override {
        if (e.mouseWasDraggedSinceMouseDown()) dock_.dragged(start_, e.getDistanceFromDragStartY(), true);
    }

    juce::String words() const { return kWords; }

private:
    EditorMixerDock& dock_;
    CapsLabel label_;
    int start_ = 0;
};

EditorMixerDock::EditorMixerDock(app::Session& session, Timeline& timeline)
    : session_(session), timeline_(timeline), mixer_(session), splitter_(std::make_unique<Splitter>(*this)),
      fold_(std::make_unique<FoldButton>()) {
    folded_ = session_.settings().editorMixerFolded();
    chosen_ = session_.settings().editorMixerHeight();
    fold_->onClick = [this] { setFolded(!folded_); };
    splitter_->addAndMakeVisible(*fold_);
    addAndMakeVisible(*splitter_);
    mixer_.onStripSelected = [this](int index) { stripChosen(index); };
    mixer_.onStateChanged = [this] { stripsMadeAgain(); };
    mixer_.onKeysChanged = [this] { if (onKeysChanged) onKeysChanged(); };
    addChildComponent(mixer_);
    mixer_.setVisible(!folded_);                                  // hidden only while folded, whatever its size
}

EditorMixerDock::~EditorMixerDock() {
    mixer_.onStripSelected = nullptr;
    mixer_.onStateChanged = nullptr;
    mixer_.onKeysChanged = nullptr;
}

void EditorMixerDock::setSong(const juce::String& songId) {
    if (songId != mixer_.songId() || mixer_.outputId().isNotEmpty()) mixer_.show(songId, {});
}

void EditorMixerDock::setKeysHome(juce::Component* home) { mixer_.setKeysHome(home); }

juce::Component& EditorMixerDock::splitter() { return *splitter_; }
juce::Button& EditorMixerDock::foldButton() { return *fold_; }
juce::String EditorMixerDock::splitterWords() const { return splitter_->words(); }
juce::String EditorMixerDock::foldWords() const { return fold_->getButtonText(); }

// ---- The height ------------------------------------------------------------------------------------------------------

int EditorMixerDock::minimumTracksHeight() { return BarRuler::height + 2 * PartLane::height; }

int EditorMixerDock::minimumPanelHeight() const { return mixer_.minimumHeight() + kDockTop + kDockFoot; }

int EditorMixerDock::maximumPanelHeight() const {
    return std::max(minimumPanelHeight(), room_ - splitterHeight - minimumTracksHeight());
}

int EditorMixerDock::clampPanel(int height) const { return juce::jlimit(minimumPanelHeight(), maximumPanelHeight(), height); }

int EditorMixerDock::heightFor(int room) {
    room_ = std::max(0, room);
    if (folded_) return splitterHeight;
    // Never sized by hand: 440 px or half the room, but tall enough for the strips' Effects box at least as its slim line
    // when the room allows it (clampPanel: the tracks keep their ruler and two lanes).
    const int effects = mixer_.minimumHeightWithEffects() + kDockTop + kDockFoot;
    const int wanted = chosen_ > 0 ? chosen_ : std::max(std::min(defaultPanelHeight, (room_ - splitterHeight) / 2), effects);
    return splitterHeight + clampPanel(wanted);
}

int EditorMixerDock::panelHeight() const { return folded_ ? 0 : std::max(0, getHeight() - splitterHeight); }

void EditorMixerDock::dragged(int startHeight, int dy, bool end) {
    if (folded_) return;
    chosen_ = clampPanel(startHeight - dy);
    if (onLayoutChanged) onLayoutChanged();
    if (!end) return;
    session_.settings().setEditorMixerHeight(chosen_);
    session_.noteSettingsChanged();
}

void EditorMixerDock::setFolded(bool folded) {
    if (folded == folded_) return;
    if (!folded_ && chosen_ <= 0) chosen_ = panelHeight();       // the height it had, so unfolding brings it back
    folded_ = folded;
    mixer_.setVisible(!folded);
    session_.settings().setEditorMixerFolded(folded);
    if (chosen_ > 0) session_.settings().setEditorMixerHeight(chosen_);
    session_.noteSettingsChanged();
    if (onLayoutChanged) onLayoutChanged();
    resized();
}

void EditorMixerDock::resized() {
    auto area = getLocalBounds();
    splitter_->setBounds(area.removeFromTop(std::min(splitterHeight, area.getHeight())));
    fold_->setButtonText(folded_ ? kShow : kHide);
    const int foldWidth = fold_->idealWidth();
    fold_->setBounds(splitter_->getWidth() - kFoldRight - foldWidth, (splitterHeight - kFoldHeight) / 2, foldWidth, kFoldHeight);
    mixer_.setBounds(area.withTrimmedTop(kDockTop).withTrimmedBottom(kDockFoot).reduced(kDockSide, 0));
}

// ---- The selection, linked with the tracks ----------------------------------------------------------------------------

int EditorMixerDock::stripOf(const juce::String& partId) const {
    const auto& strips = mixer_.state().strips;
    for (std::size_t i = 0; i < strips.size(); ++i)
        if (strips[i].target.kind == app::LevelTarget::Kind::Part && strips[i].target.partId == partId) return static_cast<int>(i);
    return -1;
}

// A strip chosen in the mixer: its track selected and shown (none for Click and Cues, and for no strip).
void EditorMixerDock::stripChosen(int index) {
    const auto& strips = mixer_.state().strips;
    const bool part = juce::isPositiveAndBelow(index, static_cast<int>(strips.size()))
                      && strips[static_cast<std::size_t>(index)].target.kind == app::LevelTarget::Kind::Part;
    const auto partId = part ? strips[static_cast<std::size_t>(index)].target.partId : juce::String();
    const juce::ScopedValueSetter<bool> following(fromMixer_, true);
    if (part) {
        timeline_.select({EditorSelection::Kind::Part, {}, partId});
        timeline_.scrollToPart(partId);
    } else if (timeline_.selection().kind == EditorSelection::Kind::Part) {
        timeline_.select({});
    }
}

void EditorMixerDock::followTracks(const EditorSelection& selection) {
    if (fromMixer_) return;                                       // the mixer's own choice, coming back: nothing to follow
    const int index = selection.kind == EditorSelection::Kind::Part ? stripOf(selection.id) : -1;
    if (index != mixer_.selectedStrip()) mixer_.selectStrip(index);
}

// The strips made again (or only redrawn): the panel kept the same part's strip selected, or none if it went; the track
// selection follows what the panel now shows, in case the track was chosen before its strip was there.
void EditorMixerDock::stripsMadeAgain() {
    const auto& selection = timeline_.selection();
    if (selection.kind == EditorSelection::Kind::Part) followTracks(selection);
}

}  // namespace sqlive::ui
