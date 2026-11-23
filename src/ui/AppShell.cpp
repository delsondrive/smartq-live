#include "ui/AppShell.h"

#include <algorithm>
#include <cmath>

#include "app/AudioHost.h"
#include "app/ShowController.h"
#include "ui/EffectsWindow.h"
#include "ui/PreShowDialog.h"
#include "ui/SnapshotsDialog.h"
#include "ui/Theme.h"
#include "ui/Veil.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

using app::Mode;

namespace {

// The mockup's .dlg, .dh and .df, as the pre-show dialog draws them.
constexpr float kDialogRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 52, kBodyPadY = 14, kFooterPadY = 12, kButtonHeight = 30, kFooterGap = 10;
constexpr int kQuestionWidth = 460;

}  // namespace

// "Keep tonight's changes?": the sentence "<n> changes were made during the show." (levels and pans) and the buttons
// "Drop them" and "Keep them" (primary), drawn as the mockup's dialogs are.
class AppShell::TonightQuestion : public juce::Component {
public:
    std::function<void()> onKeep, onDrop;

    TonightQuestion() {
        keep_.onClick = [this] { if (onKeep) onKeep(); };
        drop_.onClick = [this] { if (onDrop) onDrop(); };
        addAndMakeVisible(drop_);
        addAndMakeVisible(keep_);
    }

    void setCount(int n) {
        const auto sentence = n == 1 ? juce::String("1 change was made during the show.")
                                     : juce::String(n) + " changes were made during the show.";
        if (sentence == sentence_) return;
        sentence_ = sentence;
        repaint();
    }
    const juce::String& sentence() const { return sentence_; }

    int idealHeight() const { return kHeaderHeight + 2 * kBodyPadY + bodyLineHeight() + 2 * kFooterPadY + kButtonHeight; }

    void resized() override {
        auto foot = getLocalBounds().removeFromBottom(2 * kFooterPadY + kButtonHeight).reduced(kPadX, kFooterPadY);
        keep_.setBounds(foot.removeFromRight(keep_.idealWidth()));
        foot.removeFromRight(kFooterGap);
        drop_.setBounds(foot.removeFromRight(drop_.idealWidth()));
    }

    void paint(juce::Graphics& g) override {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(theme::popup);
        g.fillRoundedRectangle(bounds, kDialogRadius);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(bounds.reduced(0.5f), kDialogRadius, 1.0f);
        const int footerY = getHeight() - (2 * kFooterPadY + kButtonHeight);
        g.fillRect(1, kHeaderHeight - 1, getWidth() - 2, 1);
        g.fillRect(1, footerY, getWidth() - 2, 1);

        g.setColour(theme::text);
        g.setFont(theme::font(18.0f, true));
        g.drawText("Keep tonight's changes?", juce::Rectangle<int>(0, 0, getWidth(), kHeaderHeight).reduced(kPadX, 0),
                   juce::Justification::centredLeft, true);
        g.setColour(theme::mid);
        g.setFont(theme::font(theme::sizeBody));
        g.drawFittedText(sentence_, juce::Rectangle<int>(0, kHeaderHeight, getWidth(), footerY - kHeaderHeight).reduced(kPadX, kBodyPadY),
                         juce::Justification::centredLeft, 2, 1.0f);
    }

private:
    static int bodyLineHeight() { return static_cast<int>(std::ceil(theme::font(theme::sizeBody).getHeight())) + 2; }

    juce::String sentence_;
    PillButton drop_{"Drop them"}, keep_{"Keep them", PillButton::Style::Primary};
};

AppShell::AppShell(app::Session& session, app::AudioHost* host)
    : session_(session), host_(host), topBar_(session), interrupted_(session, host), comesBack_(session), outside_(session) {
    setWantsKeyboardFocus(true);                                  // keys reach the window's ShowController from here
    setOpaque(true);

    topBar_.onPage = [this](Page p) { showPage(p); };
    topBar_.onMode = [this](Mode m) { requestMode(m); };
    topBar_.onAction = [this](app::Action a) { session_.perform(a); };
    topBar_.onUndo = [this] {
        if (session_.mode() == Mode::Edit && session_.commands().canUndo()) session_.commands().undo();
    };
    topBar_.onOpenAnotherBand = [this] { if (onOpenAnotherBand) onOpenAnotherBand(); };
    topBar_.onCheckFiles = [this] { openFilesDialog(); };
    topBar_.onSnapshots = [this] { openSnapshotsDialog(); };
    addAndMakeVisible(topBar_);

    addChildComponent(interrupted_);
    addChildComponent(comesBack_);
    banner_.onClick = [this] { takeKeyboard(); };
    addChildComponent(banner_);
    addChildComponent(outside_);
    effects_ = std::make_unique<EffectsWindow>(session_);
    addChildComponent(*effects_);
    raiseOverlays();

    session_.changes().addChangeListener(this);
    refresh();
    startTimerHz(30);
}

AppShell::~AppShell() {
    stopTimer();
    session_.changes().removeChangeListener(this);
}

void AppShell::setShowController(app::ShowController* controller) {
    controller_ = controller;
    refreshTopBar();
}

void AppShell::showPage(Page page) {
    if (page == page_) return;
    page_ = page;
    effects_->close();                                            // it was over the page that went
    refresh();
}

void AppShell::requestMode(Mode mode) {
    if (mode == session_.mode()) return;
    if (mode == Mode::Show) {
        if (preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr || snapshots_ != nullptr) return;   // one dialog at a time
        auto dialog = std::make_unique<PreShowDialog>(session_, host_);
        auto* d = dialog.get();
        d->onGo = [this] {
            close(preShow_);
            session_.enterShow();
        };
        d->onBack = [this] { close(preShow_); };
        preShow_ = std::make_unique<Veil>(std::move(dialog), PreShowDialog::width, [d] { return d->idealHeight(); });
        addAndMakeVisible(*preShow_);
        preShow_->setBounds(getLocalBounds());
        if (d->isShowing()) d->grabKeyboardFocus();
        return;
    }
    auto options = juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::NoIcon).withAssociatedComponent(this);
    if (session_.now().playing) {
        dialog_ = juce::AlertWindow::showScopedAsync(options.withTitle("Stop the song before leaving Show mode.").withButton("OK"),
                                                     nullptr);
        return;
    }
    // "Stay in Show" is the Escape button, as Escape is a stop key during a show.
    dialog_ = juce::AlertWindow::showScopedAsync(options.withTitle("Leave Show mode?").withButton("Leave").withButton("Stay in Show"),
                                                 [this](int result) {
                                                     if (result == 1) session_.leaveShow();     // refused if a song started meanwhile
                                                 });
}

void AppShell::setPageComponent(Page page, std::unique_ptr<juce::Component> component) {
    auto& slot = pages_[static_cast<std::size_t>(page)];
    if (slot != nullptr) removeChildComponent(slot.get());
    slot = std::move(component);
    if (slot != nullptr) addChildComponent(*slot);
    raiseOverlays();
    resized();
    refresh();
}

void AppShell::setShowComponents(std::unique_ptr<juce::Component> mainView, std::unique_ptr<juce::Component> stageView) {
    for (auto* old : {mainView_.get(), stageView_.get()})
        if (old != nullptr) removeChildComponent(old);
    mainView_ = std::move(mainView);
    stageView_ = std::move(stageView);
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) addChildComponent(*view);
    raiseOverlays();
    resized();
    refresh();
}

juce::Component* AppShell::currentContent() const {
    if (session_.mode() == Mode::Show) return session_.stageView() ? stageView_.get() : mainView_.get();
    return pages_[static_cast<std::size_t>(page_)].get();
}

void AppShell::setKeyboardElsewhere(bool elsewhere) {
    if (elsewhere == keyboardElsewhere_) return;
    keyboardElsewhere_ = elsewhere;
    banner_.setVisible(elsewhere);
    refreshTopBar();
}

bool AppShell::showingPreShowDialog() const { return preShow_ != nullptr; }
bool AppShell::showingInterruptedBanner() const { return interrupted_.isVisible(); }
bool AppShell::showingComesBackBox() const { return comesBack_.isVisible(); }
bool AppShell::showingTonightQuestion() const { return tonight_ != nullptr; }
bool AppShell::showingFilesDialog() const { return files_ != nullptr; }
bool AppShell::showingSnapshotsDialog() const { return snapshots_ != nullptr; }
bool AppShell::showingOutsideChangesBanner() const { return outside_.isVisible(); }

void AppShell::openEffects(const app::EffectsTarget& target, int index) {
    if (session_.mode() != Mode::Edit || preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr || snapshots_ != nullptr) return;
    effects_->show(target, index);
    raiseOverlays();
}

void AppShell::answerTonightQuestion(bool keep) {
    if (tonight_ == nullptr || session_.mode() != Mode::Edit) return;
    close(tonight_);
    if (!keep) {
        session_.dropTonightChanges();
        return;
    }
    // What couldn't be kept (its song or output went meanwhile) is said, never dropped silently.
    if (const auto lost = session_.keepTonightChanges(); lost.isNotEmpty())
        dialog_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                         .withIconType(juce::MessageBoxIconType::NoIcon)
                                                         .withAssociatedComponent(this)
                                                         .withTitle(lost)
                                                         .withButton("OK"),
                                                     nullptr);
}

void AppShell::paint(juce::Graphics& g) { theme::paintAppBackground(g, getLocalBounds()); }

bool AppShell::keyPressed(const juce::KeyPress& key) {
    if (session_.mode() != Mode::Edit) return false;
    const auto mods = key.getModifiers();
    if (!mods.isCommandDown() || mods.isAltDown()) return false;
    const auto letter = juce::CharacterFunctions::toLowerCase(static_cast<juce::juce_wchar>(key.getKeyCode()));
    auto& commands = session_.commands();
    if (letter == 'z' && !mods.isShiftDown()) {
        if (commands.canUndo()) commands.undo();
        return true;
    }
    if (letter == 'y' || letter == 'z') {
        if (commands.canRedo()) commands.redo();
        return true;
    }
    return false;
}

void AppShell::resized() {
    auto area = getLocalBounds();
    topBar_.setBounds(area.removeFromTop(TopBar::height));
    for (auto& page : pages_)
        if (page != nullptr) page->setBounds(area);
    // Under the red banner, as the mockup's screen 5 has it: the show view moves down out of its way and dims to half.
    const bool cutOff = interrupted_.isVisible();
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) {
            view->setBounds(cutOff ? area.withTrimmedTop(InterruptedBanner::height) : area);
            view->setAlpha(cutOff ? 0.5f : 1.0f);
        }
    // The red banner first, the amber one under it when both are up.
    comesBack_.setBounds(area.withTrimmedBottom(24).reduced(28, 0).removeFromBottom(ComesBackBox::height));
    interrupted_.setBounds(area.withHeight(InterruptedBanner::height));
    if (interrupted_.isVisible()) area.removeFromTop(InterruptedBanner::height);
    banner_.setBounds(area.withHeight(KeyboardBanner::height));
    outside_.setBounds(area.withHeight(OutsideChangesBanner::height));   // Edit mode only: the two above are Show mode's
    effects_->setBounds(getLocalBounds().withTrimmedTop(TopBar::height));
    for (auto* veil : {preShow_.get(), tonight_.get(), files_.get(), snapshots_.get()})
        if (veil != nullptr) veil->setBounds(getLocalBounds());
}

void AppShell::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void AppShell::timerCallback() {
    refreshBanners();                                             // the device can be lost or back without the session changing
    refreshTopBar();
}

void AppShell::refresh() {
    followMode();
    auto* current = currentContent();
    for (auto& page : pages_)
        if (page != nullptr) page->setVisible(page.get() == current);
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) view->setVisible(view == current);
    refreshBanners();
    refreshTopBar();
}

void AppShell::setWindowControls(bool shown, bool maximised) {
    if (shown == windowButtons_ && maximised == maximised_) return;
    windowButtons_ = shown;
    maximised_ = maximised;
    refreshTopBar();
}

WindowArea AppShell::windowAreaAt(juce::Point<int> local) {
    const auto area = topBar_.windowAreaAt(topBar_.getLocalPoint(this, local));
    if (area != WindowArea::Caption) return area;
    // The caption only where the bar itself is on top: a veil (and its dialog, whose top reaches into the bar in a short
    // window) takes the press for itself.
    auto* top = getComponentAt(local);
    return top == &topBar_ || topBar_.isParentOf(top) ? WindowArea::Caption : WindowArea::Client;
}

void AppShell::refreshTopBar() {
    auto state = makeTopBarState(session_, host_, controller_, page_, keyboardElsewhere_,
                                 remoteStatus ? remoteStatus() : app::RemoteStatus{});
    state.windowButtons = windowButtons_;
    state.maximised = maximised_;
    topBar_.setState(state);
}

void AppShell::refreshBanners() {
    interrupted_.refresh();
    // The box goes with the banner while the song it was playing waits to start again from the top.
    const bool box = interrupted_.wanted() && session_.now().interrupted;
    if (box) comesBack_.refresh();                                // its words, only while it shows (cheap when they hold)
    if (box != comesBack_.isVisible()) comesBack_.setVisible(box);
    outside_.refresh();
    if (outside_.wanted() != outside_.isVisible()) outside_.setVisible(outside_.wanted());
    if (interrupted_.wanted() == interrupted_.isVisible()) return;
    interrupted_.setVisible(interrupted_.wanted());
    resized();
}

void AppShell::followMode() {
    const auto mode = session_.mode();
    if (mode == Mode::Show) {
        // In Show mode the pre-show check has nothing left to say, and nothing may write the band folder.
        close(preShow_);
        close(tonight_);
        close(files_);
        close(snapshots_);
        effects_->close();                                        // lets go of whatever it held
        return;
    }
    // In Edit mode tonight's changes only come from a show that has ended (here, or before a restart), so whenever there
    // are some the question is up. Changes coalesce, so the Show → Edit step itself may never be seen.
    if (session_.hasTonightChanges()) askAboutTonight();
    else close(tonight_);                                         // answered some other way
    if (tonight_ != nullptr) static_cast<TonightQuestion&>(tonight_->content()).setCount(session_.tonightChangeCount());
    if (files_ != nullptr) static_cast<FilesDialog&>(files_->content()).refresh();   // may close it: nothing left to show
}

void AppShell::openFilesDialog() {
    if (session_.mode() != Mode::Edit || preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr || snapshots_ != nullptr) return;
    if (makeFilesState(session_).isEmpty()) return;               // nothing to check
    auto dialog = std::make_unique<FilesDialog>(session_);
    auto* d = dialog.get();
    d->onClose = [this] { close(files_); };
    files_ = std::make_unique<Veil>(std::move(dialog), FilesDialog::width, [d] { return d->idealHeight(); });
    addAndMakeVisible(*files_);
    files_->setBounds(getLocalBounds());
    if (d->isShowing()) d->grabKeyboardFocus();
}

void AppShell::openSnapshotsDialog() {
    if (session_.mode() != Mode::Edit || preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr || snapshots_ != nullptr) return;
    auto dialog = std::make_unique<SnapshotsDialog>(session_);
    auto* d = dialog.get();
    d->onClose = [this] { close(snapshots_); };
    snapshots_ = std::make_unique<Veil>(std::move(dialog), SnapshotsDialog::width, [d] { return d->idealHeight(); });
    addAndMakeVisible(*snapshots_);
    snapshots_->setBounds(getLocalBounds());
    topBar_.setSnapshotsOpen(true);
    if (d->isShowing()) d->grabKeyboardFocus();
}

void AppShell::askAboutTonight() {
    if (tonight_ != nullptr) return;
    auto question = std::make_unique<TonightQuestion>();
    auto* q = question.get();
    q->setCount(session_.tonightChangeCount());
    q->onKeep = [this] { answerTonightQuestion(true); };
    q->onDrop = [this] { answerTonightQuestion(false); };
    tonight_ = std::make_unique<Veil>(std::move(question), kQuestionWidth, [q] { return q->idealHeight(); });
    addAndMakeVisible(*tonight_);
    tonight_->setBounds(getLocalBounds());
    if (tonight_->isShowing()) tonight_->grabKeyboardFocus();
}

void AppShell::close(std::unique_ptr<Veil>& veil) {
    if (veil == nullptr) return;
    veil->setVisible(false);
    removeChildComponent(veil.get());
    closed_.push_back(std::move(veil));
    topBar_.setSnapshotsOpen(snapshots_ != nullptr);
    // Often asked from inside the dialog's own button: it is deleted once that call has returned.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AppShell>(this)] {
        if (safe != nullptr) safe->closed_.clear();
    });
    if (isShowing()) grabKeyboardFocus();
}

void AppShell::raiseOverlays() {
    if (effects_ != nullptr) effects_->toFront(false);           // over the pages, under the banners and the dialogs
    interrupted_.toFront(false);
    comesBack_.toFront(false);
    banner_.toFront(false);
    outside_.toFront(false);
    for (auto* veil : {preShow_.get(), tonight_.get(), files_.get(), snapshots_.get()})
        if (veil != nullptr) veil->toFront(false);
}

void AppShell::takeKeyboard() {
    if (auto* window = getTopLevelComponent()) window->toFront(true);
    grabKeyboardFocus();
}

}  // namespace sqlive::ui
