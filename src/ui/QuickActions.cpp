#include "ui/QuickActions.h"

#include <algorithm>
#include <utility>

#include "model/JsonObject.h"
#include "model/Sections.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/MidiLane.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

// The right-hand column's cards (.card), and the mockup's QUICK ACTIONS rows: 12 px words in mid, the type a .pills pill
// (11 px bold on its colour, 4 px corners, 6 px either side). A row is a little wider than the words (kRowPad either side),
// so its hover fill doesn't touch them.
constexpr int kCardPadX = 12, kCardPadY = 10, kCapsHeight = 14, kCapsGap = 6, kRow = 26, kRowPad = 6, kWordsGap = 6;
constexpr int kChoiceHeight = 24, kMessageGap = 4, kMessageLine = 16;
constexpr float kPillHeight = 17.0f, kPillPad = 6.0f, kPillRadius = 4.0f;

juce::Font rowFont() { return theme::font(theme::sizeSmall); }
juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }

}  // namespace

QuickActionsState makeQuickActionsState(const app::Session& session, const juce::String& songId, const EditorSelection& picked) {
    QuickActionsState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto& band = session.model().band();
    const auto tiles = model::tileSections(*song, model::barsInSong(*song));
    auto section = tiles.end();
    if (picked.kind == EditorSelection::Kind::Marker) {
        const auto* marker = findMarker(session, songId, picked.deviceId, picked.id);
        if (marker == nullptr) return state;
        const int bar = marker->bar;
        section = std::find_if(tiles.begin(), tiles.end(), [bar](const auto& t) { return t.startBar <= bar && bar < t.endBar; });
        const auto typeId = section != tiles.end() ? section->typeId : juce::String();
        const auto type = std::find_if(band.sectionTypes.begin(), band.sectionTypes.end(),
                                       [&typeId](const auto& t) { return typeId.isNotEmpty() && t.id == typeId; });
        if (type != band.sectionTypes.end()) {
            state.deviceId = picked.deviceId;
            state.markerId = picked.id;
            state.copyType = type->name;
            state.copyColour = theme::sectionColour(band, type->id);
        }
    } else if (picked.kind == EditorSelection::Kind::Section && picked.id.isNotEmpty()) {
        section = std::find_if(tiles.begin(), tiles.end(), [&](const auto& t) { return t.id == picked.id; });
    }
    if (section != tiles.end() && section->id.isNotEmpty()) {     // a stored section, which the commands can name
        state.sectionId = section->id;
        for (const auto& part : song->parts) {
            const auto* role = session.model().role(part.roleId);
            state.parts.add(part.name);
            state.partIds.add(part.id);
            state.partColours.push_back(role != nullptr ? role->colour : theme::dim);
        }
    }
    state.visible = state.copyType.isNotEmpty() || state.sectionId.isNotEmpty();
    return state;
}

// ---- A row -------------------------------------------------------------------------------------------------------------

// One action: its words on the left, a pill or more words on the right, a field fill under the mouse.
class QuickActions::Row : public juce::Button {
public:
    Row(const juce::String& left, const juce::String& right) : juce::Button(left), left_(left), right_(right) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(false);
    }

    void setPill(const juce::String& words, juce::Colour colour) {
        pill_ = words;
        pillColour_ = colour;
        setTooltip(left_ + " " + words);
        repaint();
    }
    int leftWidth() const { return widthInPixels(rowFont(), left_); }
    int rightWidth() const { return widthInPixels(rowFont(), right_); }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        auto area = getLocalBounds().toFloat();
        if (isEnabled() && (highlighted || down)) {
            g.setColour(down ? theme::fieldHover : theme::field);
            g.fillRoundedRectangle(area, theme::radiusField);
        }
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        auto words = area.reduced(static_cast<float>(kRowPad), 0.0f);
        if (pill_.isNotEmpty()) {
            const float w = std::min(words.getWidth() / 2.0f, widthOf(pillFont(), pill_) + 2.0f * kPillPad);
            const auto pill = words.removeFromRight(w).withSizeKeepingCentre(w, kPillHeight);
            g.setColour(pillColour_.withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(pill, kPillRadius);
            g.setColour(theme::onAccent);
            g.setFont(pillFont());
            g.drawText(shorten(pill_, pillFont(), pill.getWidth() - 2.0f * kPillPad), pill.reduced(kPillPad, 0.0f),
                       juce::Justification::centredLeft, false);
            words.removeFromRight(static_cast<float>(kWordsGap));
        }
        g.setColour(theme::mid.withMultipliedAlpha(alpha));
        g.setFont(rowFont());
        if (right_.isNotEmpty()) {
            const float w = std::min(words.getWidth(), widthOf(rowFont(), right_) + 1.0f);
            g.drawText(right_, words.removeFromRight(w), juce::Justification::centredRight, false);
        }
        g.drawText(shorten(left_, rowFont(), words.getWidth()), words, juce::Justification::centredLeft, false);
    }

private:
    juce::String left_, right_, pill_;
    juce::Colour pillColour_;
};

// ---- The card ----------------------------------------------------------------------------------------------------------

QuickActions::QuickActions(app::Session& session)
    : session_(session),
      copy_(std::make_unique<Row>("Do this at every", juce::String())),
      fade_(std::make_unique<Row>("Fade", "in over this section")),
      mute_(std::make_unique<Row>("Mute the click in this section", juce::String())) {
    copy_->onClick = [this] { copyToType(); };
    fade_->onClick = [this] { fadeIn(part_.selected()); };
    mute_->onClick = [this] { muteClick(); };
    part_.setBold(true);
    addAndMakeVisible(label_);
    for (auto* row : {copy_.get(), fade_.get(), mute_.get()}) addChildComponent(*row);
    addChildComponent(part_);                                     // over the fade row, between its words
}

QuickActions::~QuickActions() { stopTimer(); }

bool QuickActions::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

void QuickActions::show(const juce::String& songId, const EditorSelection& picked) {
    auto next = makeQuickActionsState(session_, songId, picked);
    // The part chosen for the fade stays chosen while the song has it.
    const int was = part_.selected();
    const auto chosen = was >= 0 && was < state_.partIds.size() ? state_.partIds[was] : juce::String();
    const bool other = songId != songId_ || next.sectionId != state_.sectionId || next.markerId != state_.markerId;
    songId_ = songId;
    state_ = std::move(next);
    part_.setItems(state_.parts, state_.parts.isEmpty() ? -1 : std::max(0, state_.partIds.indexOf(chosen)));
    part_.setDots(state_.partColours);
    copy_->setPill(state_.copyType, state_.copyColour);
    copy_->setVisible(state_.copyType.isNotEmpty());
    fade_->setVisible(state_.sectionId.isNotEmpty() && !state_.parts.isEmpty());
    part_.setVisible(fade_->isVisible());
    mute_->setVisible(state_.sectionId.isNotEmpty());
    const bool edit = editable();
    for (juce::Component* c : {static_cast<juce::Component*>(copy_.get()), static_cast<juce::Component*>(fade_.get()),
                               static_cast<juce::Component*>(mute_.get()), static_cast<juce::Component*>(&part_)})
        c->setEnabled(edit);
    if (other && message_.isNotEmpty()) setMessage({});           // it was about something no longer selected
    resized();
    repaint();
}

int QuickActions::idealHeight() const {
    int rows = 0;
    for (const auto* row : {copy_.get(), fade_.get(), mute_.get()}) rows += row->isVisible() ? 1 : 0;
    int height = 2 * kCardPadY + kCapsHeight + kCapsGap + rows * kRow;
    if (message_.isNotEmpty()) {
        const int room = std::max(1, getWidth() - 2 * kCardPadX);
        height += kMessageGap + (widthOf(rowFont(), message_) > static_cast<float>(room) ? 2 : 1) * kMessageLine;
    }
    return height;
}

void QuickActions::resized() {
    auto inner = getLocalBounds().reduced(kCardPadX, kCardPadY);
    label_.setBounds(inner.removeFromTop(kCapsHeight));
    inner.removeFromTop(kCapsGap);
    for (auto* row : {copy_.get(), fade_.get(), mute_.get()})
        if (row->isVisible()) row->setBounds(inner.removeFromTop(std::min(kRow, inner.getHeight())).expanded(kRowPad, 0));
    if (fade_->isVisible()) {                                     // the part's Choice, between "Fade" and the words after it
        const auto row = fade_->getBounds().reduced(kRowPad, 0);
        const int left = row.getX() + fade_->leftWidth() + kWordsGap;
        const int room = std::max(0, row.getRight() - fade_->rightWidth() - kWordsGap - left);
        const int w = std::min(part_.idealWidth(), room);
        part_.setBounds(left, row.getCentreY() - kChoiceHeight / 2, w, kChoiceHeight);
    }
    messageArea_ = message_.isNotEmpty() ? inner.withTrimmedTop(kMessageGap) : juce::Rectangle<int>();
}

void QuickActions::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    if (messageArea_.isEmpty()) return;
    g.setColour(theme::text);
    g.setFont(rowFont());
    g.drawFittedText(message_, messageArea_, juce::Justification::topLeft, 2, 1.0f);
}

// ---- The actions -------------------------------------------------------------------------------------------------------

bool QuickActions::run(const juce::String& command, const juce::var& params, juce::String* said) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params);
    if (!result.ok) tellUser(box_, *this, result.message);
    else if (said != nullptr) *said = result.message;
    return result.ok;
}

void QuickActions::copyToType() {
    if (state_.copyType.isEmpty()) return;
    juce::String said;
    const auto params = jsonObject({{"songId", songId_}, {"deviceId", state_.deviceId}, {"markerId", state_.markerId}});
    if (run("marker.copyToType", params, &said)) setMessage(said);
}

void QuickActions::fadeIn(int part) {
    if (state_.sectionId.isEmpty() || part < 0 || part >= state_.partIds.size()) return;
    run("automation.fadeIn", jsonObject({{"songId", songId_}, {"target", state_.partIds[part]}, {"sectionId", state_.sectionId}}));
}

void QuickActions::muteClick() {
    if (state_.sectionId.isEmpty()) return;
    run("automation.muteIn", jsonObject({{"songId", songId_}, {"target", "click"}, {"sectionId", state_.sectionId}}));
}

void QuickActions::setMessage(const juce::String& words) {
    const int before = idealHeight();
    message_ = words;
    if (message_.isNotEmpty()) startTimer(messageMs);
    else stopTimer();
    resized();
    repaint();
    if (idealHeight() != before && onHeightChanged) onHeightChanged();
}

void QuickActions::timerCallback() { setMessage({}); }

}  // namespace sqlive::ui
