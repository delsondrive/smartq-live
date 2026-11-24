#include "ui/EffectsWindow.h"

#include <algorithm>
#include <cmath>

#include "model/Effects.h"
#include "ui/Helpers.h"
#include "ui/LiveLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/ChainParams.h"
#include "ui/fx/DeEssPage.h"
#include "ui/fx/HistoryPage.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/ModulePages.h"
#include "ui/fx/SmartQLookAndFeel.h"
#include "ui/fx/params/ModuleControls.h"

namespace sqlive::ui {

namespace {

// The mockup's #chain: .win (1580 x 900 at 1920 x 1080, 12 px corners on #121016, a border of white at .16, a deep shadow),
// .whead (56 px, 18 px either side, 14 between; a 12 px dot, a 17 px name, a 13 px dim line), .wfoot (54 px, 13 px in mid; an
// 8 px glowing dot), and .shade (the page behind at rgba(5,4,7,.72)).
constexpr int kMaxWidth = 1580, kMaxHeight = 900, kMarginX = 24, kMarginY = 20;
constexpr int kHeaderHeight = 56, kFooterHeight = 54, kPadX = 18, kHeaderGap = 14, kButtonHeight = 32;
constexpr float kRadius = 12.0f;
constexpr double kLongestStep = 0.1, kFooterSeconds = 0.5;

const juce::Colour kShade = juce::Colour(5, 4, 7).withAlpha(0.72f), kPanelFill{0xff121016}, kPanelBorder{0x29ffffff};
const juce::Colour kHeaderFill{0x05ffffff};

juce::Font nameFont() { return theme::font(17.0f, true); }
juce::Font lineFont() { return theme::font(theme::sizeBody); }

juce::String quoted(const juce::String& s) { return utf8("\xe2\x80\x9c") + s + utf8("\xe2\x80\x9d"); }

const model::PartDoc* partOf(const model::SongDoc* song, const juce::String& partId, int& at) {
    if (song == nullptr) return nullptr;
    for (size_t i = 0; i < song->parts.size(); ++i)
        if (song->parts[i].id == partId) {
            at = static_cast<int>(i);
            return &song->parts[i];
        }
    return nullptr;
}

const model::OutputDoc* outputOf(const model::RigDoc* rig, const juce::String& outputId, int& at) {
    if (rig == nullptr) return nullptr;
    for (size_t i = 0; i < rig->outputs.size(); ++i)
        if (rig->outputs[i].id == outputId) {
            at = static_cast<int>(i);
            return &rig->outputs[i];
        }
    return nullptr;
}

}  // namespace

// "Hold to hear without effects", drawn as the mockup's .btn with its words bright; lit mint while held.
class EffectsWindow::HoldKey : public smartq::HoldButton {
public:
    HoldKey() : smartq::HoldButton("Hold to hear without effects") {}

    int idealWidth() const { return widthInPixels(theme::font(theme::sizeBody, true), text()) + 26; }

    void paint(juce::Graphics& g) override {
        const bool held = isHeld();
        LiveLookAndFeel::fillButton(g, getLocalBounds().toFloat(), held ? theme::mint.withAlpha(0.16f) : theme::field,
                                    held ? theme::mint : theme::hairline, isMouseOver() && !held, false);
        g.setColour(held ? theme::mint : theme::text);
        g.setFont(theme::font(theme::sizeBody, true));
        g.drawText(text(), getLocalBounds().reduced(12, 0), juce::Justification::centred, true);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
};

EffectsWindow::EffectsWindow(app::Session& session)
    : session_(session), smartqLook_(std::make_unique<smartq::SmartQLookAndFeel>()), hold_(std::make_unique<HoldKey>()) {
    setWantsKeyboardFocus(true);
    setVisible(false);
    pageHolder_.setLookAndFeel(smartqLook_.get());
    addAndMakeVisible(pageHolder_);
    addAndMakeVisible(column_);
    for (auto* button : {&previous_, &next_, &close_}) addChildComponent(*button);
    close_.setVisible(true);
    close_.setTooltip("Close (Esc)");
    addAndMakeVisible(*hold_);

    previous_.onClick = [this] { moveTo(-1); };
    next_.onClick = [this] { moveTo(1); };
    close_.onClick = [this] { close(); };
    hold_->onHeld = [this](bool held) { session_.holdWithoutEffects(target_, held); };

    column_.onSelect = [this](int index) { showEffect(index); };
    column_.onSwitch = [this](int index, bool on) { session_.switchEffect(target_, index, on); };
    column_.onRemove = [this](int index) { removeEffect(index); };
    column_.onMove = [this](int from, int to) { moveEffect(from, to); };
    column_.onAdd = [this](const juce::String& kind) { addEffect(kind); };

    addMouseListener(this, true);                                 // a press anywhere closes the add menu
    session_.changes().addChangeListener(this);
}

EffectsWindow::~EffectsWindow() {
    session_.changes().removeChangeListener(this);
    removeMouseListener(this);
    shut(false);                                                  // the shell may be going too: no focus moves now
    pageHolder_.setLookAndFeel(nullptr);
}

smartq::HoldButton& EffectsWindow::holdButton() noexcept { return *hold_; }

// ---- Opening and closing -------------------------------------------------------------------------------------------------

namespace {
bool samePlace(const app::EffectsTarget& a, const app::EffectsTarget& b, const juce::String& rigId) {
    const auto rigOf = [&](const app::EffectsTarget& t) { return t.rigId.isEmpty() ? rigId : t.rigId; };
    return a.songId == b.songId && a.partId == b.partId && a.outputId == b.outputId && (a.isPart() || rigOf(a) == rigOf(b));
}
}  // namespace

void EffectsWindow::show(const app::EffectsTarget& target, int index) {
    if (session_.mode() == app::Mode::Show) return;
    const auto before = target_;
    target_ = target;
    Place place;
    if (!readPlace(place)) {
        target_ = before;
        return;
    }
    if (!open_ || !samePlace(before, target, session_.rigId())) dropChain();   // another chain: another host
    open_ = true;
    hold_->setVisible(true);
    setVisible(true);
    wanted_ = index >= 0 ? index : 0;
    refresh();
    showEffect(params_ != nullptr && !params_->effects().empty()
                   ? std::clamp(index >= 0 ? index : 0, 0, static_cast<int>(params_->effects().size()) - 1)
                   : -1);
    if (index < 0) column_.openAddMenu();
    else column_.closeAddMenu();
    lastStepMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
    if (isShowing()) grabKeyboardFocus();
}

void EffectsWindow::close() { shut(true); }

void EffectsWindow::shut(bool moveFocus) {
    stopTimer();
    const bool hadKeyboard = moveFocus && hasKeyboardFocus(true);
    hold_->setVisible(false);                                     // lets go while the place is still the one held
    setVisible(false);
    if (auto* parent = getParentComponent(); hadKeyboard && parent != nullptr && parent->isShowing() && parent->getWantsKeyboardFocus())
        parent->grabKeyboardFocus();                              // the shell's keys (Undo, the transport) go on working
    column_.closeAddMenu();
    dropChain();
    open_ = false;
}

// ---- Following the documents ---------------------------------------------------------------------------------------------

void EffectsWindow::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

bool EffectsWindow::readPlace(Place& place) const {
    const auto& model = session_.model();
    if (target_.isPart()) {
        const auto* song = model.song(target_.songId);
        int at = -1;
        const auto* part = partOf(song, target_.partId, at);
        if (part == nullptr) return false;
        const auto* role = model.role(part->roleId);
        place.name = part->name;
        place.songTitle = song->title;
        place.colour = role != nullptr ? role->colour : theme::mid;
        place.stereo = part->channels == 2;
        if (at > 0) place.previousName = song->parts[static_cast<size_t>(at - 1)].name;
        if (at + 1 < static_cast<int>(song->parts.size())) place.nextName = song->parts[static_cast<size_t>(at + 1)].name;
    } else {
        if (target_.rigId.isNotEmpty() && target_.rigId != session_.rigId()) return false;   // another rig chosen: nobody hears it
        const auto* rig = model.rig(target_.rigId.isEmpty() ? session_.rigId() : target_.rigId);
        int at = -1;
        const auto* output = outputOf(rig, target_.outputId, at);
        if (output == nullptr) return false;
        place.name = output->name;
        place.colour = theme::outputColour;
        place.stereo = output->stereo;
        if (at > 0) place.previousName = rig->outputs[static_cast<size_t>(at - 1)].name;
        if (at + 1 < static_cast<int>(rig->outputs.size())) place.nextName = rig->outputs[static_cast<size_t>(at + 1)].name;
    }
    place.effects = session_.effects(target_);
    return true;
}

void EffectsWindow::refresh() {
    if (!open_) return;
    Place place;
    if (session_.mode() == app::Mode::Show || !readPlace(place)) {
        close();
        return;
    }
    // While a song plays the session says it has changed thirty times a second: nothing to do here unless the place has.
    if (params_ != nullptr && wanted_ < 0 && place == seen_) return;
    seen_ = place;
    known_ = place.effects;
    place_ = target_.isPart() ? model::EffectPlace::Part : model::EffectPlace::Output;
    refreshHeader(place);

    const int count = static_cast<int>(place.effects.size());
    if (params_ == nullptr || place.stereo != stereo_ || !params_->fits(place.effects)) {
        rebuildChain(place.effects, place.stereo);
    } else {
        params_->load(place.effects);
        for (int i = 0; i < count; ++i) column_.setOn(i, place.effects[static_cast<size_t>(i)].on);
    }
    column_.setAddChoices(addChoicesFor(place.effects, place_, place.stereo));

    int want = wanted_ >= 0 ? wanted_ : shown_;
    wanted_ = -1;
    want = count == 0 ? -1 : std::clamp(want < 0 ? 0 : want, 0, count - 1);
    if (want != shown_ || (want >= 0 && page_ == nullptr)) showEffect(want);
    else column_.setSelected(shown_);

    refreshFooter();
}

void EffectsWindow::rebuildChain(const std::vector<model::EffectDoc>& effects, bool stereo) {
    dropChain();
    stereo_ = stereo;
    params_ = std::make_unique<fx::ChainParams>(effects);
    params_->onEdited = [this](int index, const model::EffectDoc& effect, const juce::String& gestureId) {
        // Only onto the effect the page was made for: if the chain changed since it was last read (its change message still
        // on its way), the edit is dropped, and the window reads the chain again as the message arrives.
        const auto now = session_.effects(target_);
        const auto at = static_cast<std::size_t>(index);
        if (index < 0 || at >= now.size() || now.size() != known_.size() || params_ == nullptr || !params_->fits(now)
            || model::effectsKey({now[at]}) != model::effectsKey({known_[at]}))
            return;
        session_.setEffect(target_, index, effect, gestureId);
        if (const auto after = session_.effects(target_); at < after.size() && after.size() == known_.size()) known_[at] = after[at];
        // The chain now differs from the one last read, so the next refresh must read it again, even if an undo or a reload
        // takes it straight back before the change message arrives (else refresh would skip, and known_ keep this edit).
        seen_ = {};
    };
    host_ = std::make_unique<smartq::ChainHost>(*params_, stereo, smartq::ChainHost::sessionSource(session_, target_));

    std::vector<EffectsChainColumn::Effect> cards;
    histories_.resize(effects.size());
    for (size_t i = 0; i < effects.size(); ++i) {
        const int slot = params_->slotOf(static_cast<int>(i));
        EffectsChainColumn::Effect card;
        card.kind = effects[i].kind;
        card.on = effects[i].on;
        if (slot >= 0) {
            const auto type = smartq::slotInfo(slot).type;
            if (smartq::reportsReduction(type) || smartq::reportsBoost(type))
                histories_[i] = std::make_unique<smartq::ModuleHistory>(*host_, slot);
            card.miniature = fx::makeMiniature(effects[i].kind, *host_, slot);
            card.history = histories_[i].get();
        }
        cards.push_back(std::move(card));
    }
    column_.setEffects(std::move(cards));
}

// The page, the cards, the histories, the host and the parameters, in that order: each reads the next.
void EffectsWindow::dropChain() {
    dropPage();
    shown_ = -1;
    column_.setEffects({});
    histories_.clear();
    host_.reset();
    params_.reset();
}

void EffectsWindow::dropPage() {
    if (page_ != nullptr) {
        page_->endGestures();
        pageHolder_.removeChildComponent(page_.get());
        page_.reset();
    }
    if (params_ != nullptr) params_->forgetGestures();          // a drag on it may never be let go of now
    if (host_ != nullptr) host_->setShownSlot(-1);
}

void EffectsWindow::showEffect(int index) {
    const int count = params_ != nullptr ? static_cast<int>(params_->effects().size()) : 0;
    if (index < 0 || index >= count) index = -1;
    if (index == shown_ && (page_ != nullptr || index < 0)) {
        column_.setSelected(shown_);
        return;
    }
    dropPage();
    shown_ = index;
    column_.setSelected(shown_);
    if (shown_ < 0 || host_ == nullptr) {
        repaint(pageArea());                                      // the hint in its place
        return;
    }
    const int slot = params_->slotOf(shown_);
    page_ = fx::makeModulePage(params_->effects()[static_cast<size_t>(shown_)].kind, *host_, slot);
    if (page_ == nullptr) return;
    if (const auto* history = histories_[static_cast<size_t>(shown_)].get()) {
        if (auto* historyPage = dynamic_cast<smartq::HistoryPage*>(page_.get())) historyPage->setHistory(history);
        else if (auto* deEss = dynamic_cast<smartq::DeEssPage*>(page_.get())) deEss->setHistory(history);
    }
    pageHolder_.addAndMakeVisible(*page_);
    page_->sendLookAndFeelChange();                               // made after the holder took Smart Q's look
    page_->setBounds(pageHolder_.getLocalBounds());
    host_->setShownSlot(slot);
    page_->pageShown();
    repaint(pageArea());
}

void EffectsWindow::refreshHeader(const Place& place) {
    title_ = place.name;
    subtitle_ = target_.isPart() ? "Effects in " + quoted(place.songTitle) + kDot + "heard on every output " + place.name + " goes to"
                                 : "Effects on " + place.name + kDot + "every song";
    dot_ = place.colour;
    previous_.setButtonText(utf8("\xe2\x80\xb9 ") + place.previousName);
    next_.setButtonText(place.nextName + utf8(" \xe2\x80\xba"));
    previous_.setVisible(place.previousName.isNotEmpty());
    next_.setVisible(place.nextName.isNotEmpty());
    hold_->setTooltip("While held, " + place.name + " plays without its effects, at the same loudness");
    resized();
    const auto p = panel();
    repaint(p.withHeight(kHeaderHeight));
}

void EffectsWindow::refreshFooter() {
    Footer footer;
    if (target_.isPart()) {
        const auto* song = session_.model().song(target_.songId);
        const auto title = song != nullptr ? song->title : juce::String();
        switch (session_.effectsState(target_.songId)) {
            case app::EffectsPreparer::State::Prepared:
                footer.state = "Prepared";
                footer.stateColour = theme::mint;
                break;
            case app::EffectsPreparer::State::Changed:
                footer.state = "Changed" + kDot + title + " will be prepared again for the show";
                footer.stateColour = theme::caution;
                break;
            case app::EffectsPreparer::State::Preparing:
                footer.state = "Preparing effects "
                               + juce::String(juce::roundToInt(100.0 * session_.effectsProgress(target_.songId))) + "%";
                footer.stateColour = theme::caution;
                break;
        }
    } else if (const int samples = session_.outputEffectsDelaySamples(); samples > 0 && session_.sampleRate() > 0.0) {
        footer.delay = "Delay " + juce::String(1000.0 * samples / session_.sampleRate(), 1) + " ms, all outputs kept together"
                       + kDot + "a Limit switched off still delays them";
    }
    if (footer.state == footer_.state && footer.delay == footer_.delay && footer.stateColour == footer_.stateColour) return;
    footer_ = footer;
    const auto p = panel();
    repaint(p.withTop(p.getBottom() - kFooterHeight));
}

// ---- Acting ------------------------------------------------------------------------------------------------------------

app::EffectsTarget EffectsWindow::neighbour(int step) const {
    const auto& model = session_.model();
    if (target_.isPart()) {
        const auto* song = model.song(target_.songId);
        int at = -1;
        if (partOf(song, target_.partId, at) == nullptr) return {};
        const int to = at + step;
        if (to < 0 || to >= static_cast<int>(song->parts.size())) return {};
        return {target_.songId, song->parts[static_cast<size_t>(to)].id, {}, {}};
    }
    const auto rigId = target_.rigId.isEmpty() ? session_.rigId() : target_.rigId;
    const auto* rig = model.rig(rigId);
    int at = -1;
    if (outputOf(rig, target_.outputId, at) == nullptr) return {};
    const int to = at + step;
    if (to < 0 || to >= static_cast<int>(rig->outputs.size())) return {};
    return {{}, {}, target_.rigId, rig->outputs[static_cast<size_t>(to)].id};
}

void EffectsWindow::moveTo(int step) {
    const auto to = neighbour(step);
    if (to.partId.isEmpty() && to.outputId.isEmpty()) return;
    show(to, 0);
    if (open_ && onTargetChanged) onTargetChanged(to);
}

void EffectsWindow::addEffect(const juce::String& kind) {
    const int count = params_ != nullptr ? static_cast<int>(params_->effects().size()) : 0;
    wanted_ = count;                                              // the one added, once the chain has it
    if (!session_.addEffect(target_, kind)) wanted_ = -1;
}

void EffectsWindow::removeEffect(int index) {
    const int count = params_ != nullptr ? static_cast<int>(params_->effects().size()) : 0;
    wanted_ = shown_ > index ? shown_ - 1 : shown_ == index ? std::min(index, count - 2) : shown_;
    if (!session_.removeEffect(target_, index)) wanted_ = -1;
}

void EffectsWindow::moveEffect(int from, int to) {
    int follow = shown_;                                          // the page shown goes with its effect
    if (shown_ == from) follow = to;
    else if (from < shown_ && shown_ <= to) follow = shown_ - 1;
    else if (to <= shown_ && shown_ < from) follow = shown_ + 1;
    wanted_ = follow;
    if (!session_.moveEffect(target_, from, to)) wanted_ = -1;
}

// ---- The display ---------------------------------------------------------------------------------------------------------

void EffectsWindow::timerCallback() {
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double dt = std::min(kLongestStep, (now - lastStepMs_) * 0.001);
    lastStepMs_ = now;
    if (isShowing()) step(dt);
}

void EffectsWindow::step(double dtSeconds) {
    if (!open_) return;
    if (page_ != nullptr) page_->advanceGestures(dtSeconds);
    if (host_ != nullptr) host_->step();
    for (auto& history : histories_)                              // first: the cards and the page draw what they hold
        if (history != nullptr) history->step(dtSeconds);
    column_.step(dtSeconds);
    if (page_ != nullptr) page_->update(dtSeconds);
    footerSeconds_ += dtSeconds;
    if (footerSeconds_ >= kFooterSeconds) {
        footerSeconds_ = 0.0;
        refreshFooter();
    }
}

juce::Rectangle<int> EffectsWindow::panel() const {
    const int w = std::max(0, std::min(kMaxWidth, getWidth() - 2 * kMarginX));
    const int h = std::max(0, std::min(kMaxHeight, getHeight() - 2 * kMarginY));
    return {(getWidth() - w) / 2, (getHeight() - h) * 2 / 5, w, h};   // a little above the middle, as the mockup has it
}

juce::Rectangle<int> EffectsWindow::pageArea() const {
    auto body = panel().reduced(1, 0).withTrimmedTop(kHeaderHeight).withTrimmedBottom(kFooterHeight);
    body.removeFromLeft(EffectsChainColumn::width);
    return body;
}

void EffectsWindow::resized() {
    const auto p = panel();
    auto body = p.reduced(1, 0).withTrimmedTop(kHeaderHeight).withTrimmedBottom(kFooterHeight);
    column_.setBounds(body.removeFromLeft(EffectsChainColumn::width));
    pageHolder_.setBounds(body);
    if (page_ != nullptr) page_->setBounds(pageHolder_.getLocalBounds());

    // The header's buttons, from the right: close, hold, next, previous.
    auto row = p.withHeight(kHeaderHeight).reduced(kPadX, (kHeaderHeight - kButtonHeight) / 2);
    close_.setBounds(row.removeFromRight(close_.idealWidth()));
    row.removeFromRight(kHeaderGap - 4);
    hold_->setBounds(row.removeFromRight(hold_->idealWidth()));
    row.removeFromRight(kHeaderGap - 4);
    for (auto* button : {&next_, &previous_})
        if (button->isVisible()) {
            button->setBounds(row.removeFromRight(button->idealWidth()));
            row.removeFromRight(kHeaderGap - 4);
        }
}

void EffectsWindow::paint(juce::Graphics& g) {
    g.fillAll(kShade);
    const auto p = panel();
    if (p.isEmpty()) return;
    const auto area = p.toFloat();
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 80, {0, 30}).drawForRectangle(g, p);
    g.setColour(kPanelFill);
    g.fillRoundedRectangle(area, kRadius);

    // The header: a faint lift, a hairline under it; the place's dot, its name, and the line about it.
    juce::Path head;
    head.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), (float) kHeaderHeight, kRadius, kRadius, true, true, false, false);
    g.setColour(kHeaderFill);
    g.fillPath(head);
    g.setColour(theme::hairline);
    g.fillRect(p.getX() + 1, p.getY() + kHeaderHeight - 1, p.getWidth() - 2, 1);
    g.fillRect(p.getX() + 1, p.getBottom() - kFooterHeight, p.getWidth() - 2, 1);

    auto row = p.withHeight(kHeaderHeight).withTrimmedLeft(kPadX);
    int right = p.getRight() - kPadX;
    for (const juce::Component* button : {static_cast<juce::Component*>(&previous_), static_cast<juce::Component*>(&next_),
                                          static_cast<juce::Component*>(hold_.get()), static_cast<juce::Component*>(&close_)})
        if (button->isVisible()) right = std::min(right, button->getX() - kHeaderGap);
    row.setRight(std::max(row.getX(), right));
    g.setColour(dot_);
    g.fillEllipse(row.removeFromLeft(12).toFloat().withSizeKeepingCentre(12.0f, 12.0f));
    row.removeFromLeft(kHeaderGap);
    const int nameWidth = std::min(row.getWidth(), widthInPixels(nameFont(), title_) + 2);
    g.setColour(theme::text);
    g.setFont(nameFont());
    g.drawText(title_, row.removeFromLeft(nameWidth), juce::Justification::centredLeft, true);
    row.removeFromLeft(kHeaderGap);
    g.setColour(theme::dim);
    g.setFont(lineFont());
    g.drawText(subtitle_, row, juce::Justification::centredLeft, true);

    // The footer: the song's preparation, an output's delay, and the undo note on the right.
    auto foot = p.withTop(p.getBottom() - kFooterHeight).reduced(kPadX, 0);
    g.setFont(lineFont());
    g.setColour(theme::mid);
    const int noteWidth = widthInPixels(lineFont(), footer_.note) + 2;
    g.drawText(footer_.note, foot.removeFromRight(noteWidth), juce::Justification::centredRight, false);
    foot.removeFromRight(kHeaderGap);
    if (footer_.state.isNotEmpty()) {
        const auto dot = foot.removeFromLeft(8).toFloat().withSizeKeepingCentre(8.0f, 8.0f);
        juce::DropShadow(footer_.stateColour.withAlpha(0.8f), 6, {}).drawForRectangle(g, dot.toNearestInt());
        g.setColour(footer_.stateColour);
        g.fillEllipse(dot);
        foot.removeFromLeft(7);
        const int w = std::min(foot.getWidth(), widthInPixels(lineFont(), footer_.state) + 2);
        g.drawText(footer_.state, foot.removeFromLeft(w), juce::Justification::centredLeft, true);
        foot.removeFromLeft(12);
    }
    if (footer_.delay.isNotEmpty()) {
        g.setColour(theme::mid);
        g.drawText(footer_.delay, foot, juce::Justification::centredLeft, true);
    }

    // With no effect to show, the page's place says how to start.
    if (page_ == nullptr) {
        const auto empty = pageArea();
        g.setGradientFill(juce::ColourGradient::vertical(juce::Colour(0xff151119), (float) empty.getY(), juce::Colour(0xff0f0d12),
                                                         (float) empty.getBottom()));
        g.fillRect(empty);
        g.setColour(theme::mid);
        g.setFont(theme::font(15.0f));
        g.drawText(params_ != nullptr && !params_->effects().empty() ? juce::String("Choose an effect on the left")
                                                                     : juce::String("Add an effect to start"),
                   empty, juce::Justification::centred, false);
    }
}

void EffectsWindow::paintOverChildren(juce::Graphics& g) {
    const auto p = panel();
    if (p.isEmpty()) return;
    g.setColour(kPanelBorder);
    g.drawRoundedRectangle(p.toFloat().reduced(0.5f), kRadius, 1.0f);
}

bool EffectsWindow::keyPressed(const juce::KeyPress& key) {
    if (!open_ || key != juce::KeyPress::escapeKey) return false;
    if (column_.addMenuOpen()) column_.closeAddMenu();
    else close();
    return true;
}

// Heard for every press in the window: one beside the add menu (and its button) closes it, and one outside the page ends any
// gesture its graph holds open (a scroll), as a press beside it does in Smart Q.
void EffectsWindow::mouseDown(const juce::MouseEvent& e) {
    auto* on = e.eventComponent;
    if (column_.addMenuOpen()) {
        auto* menu = column_.addMenu();
        if (on != &column_.addButton() && on != menu && !menu->isParentOf(on)) column_.closeAddMenu();
    }
    if (page_ != nullptr && on != page_.get() && !page_->isParentOf(on)) page_->endGestures();
}

}  // namespace sqlive::ui
