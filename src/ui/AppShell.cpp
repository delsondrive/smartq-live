#include "ui/AppShell.h"

#include <algorithm>
#include <cmath>

#include "app/AudioHost.h"
#include "app/ShowController.h"
#include "ui/PreShowDialog.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

using app::Mode;

namespace {

constexpr int kVeilMargin = 80;

// The mockup's .dlg, .dh and .df, as the pre-show dialog draws them.
constexpr float kDialogRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 52, kBodyPadY = 14, kFooterPadY = 12, kButtonHeight = 30, kFooterGap = 10;
constexpr int kQuestionWidth = 460;

}  // namespace

// The mockup's .veil: the app dimmed behind a dialog that sits in the middle, its own width or the shell's less 80, and
// as tall as it says it needs (or the shell's height less 80). The shell keeps it over all of itself. It takes every click
// outside the dialog, and keeps the keys pressed in it from reaching the app.
class AppShell::Veil : public juce::Component {
public:
    Veil(std::unique_ptr<juce::Component> content, int width, std::function<int()> idealHeight)
        : content_(std::move(content)), width_(width), idealHeight_(std::move(idealHeight)) {
        setWantsKeyboardFocus(true);
        addAndMakeVisible(*content_);
    }

    juce::Component& content() { return *content_; }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(5, 4, 7).withAlpha(0.7f));
        juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 80, {0, 30}).drawForRectangle(g, content_->getBounds());
    }

    void resized() override {
        const int w = std::max(0, std::min(width_, getWidth() - kVeilMargin));
        content_->setSize(w, std::max(1, content_->getHeight()));   // so the height it asks for is for this width
        const int h = std::max(0, std::min(idealHeight_(), getHeight() - kVeilMargin));
        content_->setBounds(getLocalBounds().withSizeKeepingCentre(w, h));
    }

    bool keyPressed(const juce::KeyPress&) override { return true; }

private:
    std::unique_ptr<juce::Component> content_;
    int width_;
    std::function<int()> idealHeight_;
};

// "Keep tonight's level changes?": the sentence "<n> levels were changed during the show." and the buttons "Drop them" and
// "Keep them" (primary), drawn as the mockup's dialogs are.
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
        const auto sentence = n == 1 ? juce::String("1 level was changed during the show.")
                                     : juce::String(n) + " levels were changed during the show.";
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
        g.drawText("Keep tonight's level changes?", juce::Rectangle<int>(0, 0, getWidth(), kHeaderHeight).reduced(kPadX, 0),
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
    : session_(session), host_(host), topBar_(session), interrupted_(session, host), outside_(session) {
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
    addAndMakeVisible(topBar_);

    addChildComponent(interrupted_);
    addChildComponent(comesBack_);
    banner_.onClick = [this] { takeKeyboard(); };
    addChildComponent(banner_);
    addChildComponent(outside_);

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
    refresh();
}

void AppShell::requestMode(Mode mode) {
    if (mode == session_.mode()) return;
    if (mode == Mode::Show) {
        if (preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr) return;   // one dialog at a time
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
bool AppShell::showingOutsideChangesBanner() const { return outside_.isVisible(); }

void AppShell::answerTonightQuestion(bool keep) {
    if (tonight_ == nullptr || session_.mode() != Mode::Edit) return;
    close(tonight_);
    if (keep) session_.keepTonightChanges();
    else session_.dropTonightChanges();
}

void AppShell::paint(juce::Graphics& g) { theme::paintAppBackground(g, getLocalBounds()); }

void AppShell::resized() {
    auto area = getLocalBounds();
    topBar_.setBounds(area.removeFromTop(TopBar::height));
    for (auto& page : pages_)
        if (page != nullptr) page->setBounds(area);
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) view->setBounds(area);
    // The red banner first, the amber one under it when both are up.
    comesBack_.setBounds(area.withTrimmedBottom(24).reduced(28, 0).removeFromBottom(ComesBackBox::height));
    interrupted_.setBounds(area.withHeight(InterruptedBanner::height));
    if (interrupted_.isVisible()) area.removeFromTop(InterruptedBanner::height);
    banner_.setBounds(area.withHeight(KeyboardBanner::height));
    outside_.setBounds(area.withHeight(OutsideChangesBanner::height));   // Edit mode only: the two above are Show mode's
    for (auto* veil : {preShow_.get(), tonight_.get(), files_.get()})
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

void AppShell::refreshTopBar() { topBar_.setState(makeTopBarState(session_, host_, controller_, page_, keyboardElsewhere_)); }

void AppShell::refreshBanners() {
    interrupted_.refresh();
    // The box goes with the banner while the song it was playing waits to start again from the top.
    const bool box = interrupted_.wanted() && session_.now().interrupted;
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
    if (session_.mode() != Mode::Edit || preShow_ != nullptr || tonight_ != nullptr || files_ != nullptr) return;
    if (makeFilesState(session_).isEmpty()) return;               // nothing to check
    auto dialog = std::make_unique<FilesDialog>(session_);
    auto* d = dialog.get();
    d->onClose = [this] { close(files_); };
    files_ = std::make_unique<Veil>(std::move(dialog), FilesDialog::width, [d] { return d->idealHeight(); });
    addAndMakeVisible(*files_);
    files_->setBounds(getLocalBounds());
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
    // Often asked from inside the dialog's own button: it is deleted once that call has returned.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AppShell>(this)] {
        if (safe != nullptr) safe->closed_.clear();
    });
    if (isShowing()) grabKeyboardFocus();
}

void AppShell::raiseOverlays() {
    interrupted_.toFront(false);
    comesBack_.toFront(false);
    banner_.toFront(false);
    outside_.toFront(false);
    for (auto* veil : {preShow_.get(), tonight_.get(), files_.get()})
        if (veil != nullptr) veil->toFront(false);
}

void AppShell::takeKeyboard() {
    if (auto* window = getTopLevelComponent()) window->toFront(true);
    grabKeyboardFocus();
}

}  // namespace sqlive::ui
