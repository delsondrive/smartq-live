#include "ui/AppShell.h"

#include "app/AudioHost.h"
#include "app/ShowController.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::Mode;

AppShell::AppShell(app::Session& session, app::AudioHost* host) : session_(session), host_(host), topBar_(session) {
    setWantsKeyboardFocus(true);                                  // keys reach the window's ShowController from here
    setOpaque(true);

    topBar_.onPage = [this](Page p) { showPage(p); };
    topBar_.onMode = [this](Mode m) { requestMode(m); };
    topBar_.onAction = [this](app::Action a) { session_.perform(a); };
    topBar_.onUndo = [this] {
        if (session_.mode() == Mode::Edit && session_.commands().canUndo()) session_.commands().undo();
    };
    topBar_.onOpenAnotherBand = [this] { if (onOpenAnotherBand) onOpenAnotherBand(); };
    addAndMakeVisible(topBar_);

    banner_.onClick = [this] { takeKeyboard(); };
    addChildComponent(banner_);

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
        session_.enterShow();
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
    banner_.toFront(false);
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
    banner_.toFront(false);
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

void AppShell::paint(juce::Graphics& g) { theme::paintAppBackground(g, getLocalBounds()); }

void AppShell::resized() {
    auto area = getLocalBounds();
    topBar_.setBounds(area.removeFromTop(TopBar::height));
    for (auto& page : pages_)
        if (page != nullptr) page->setBounds(area);
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) view->setBounds(area);
    banner_.setBounds(area.withHeight(KeyboardBanner::height));
}

void AppShell::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void AppShell::timerCallback() { refreshTopBar(); }

void AppShell::refresh() {
    auto* current = currentContent();
    for (auto& page : pages_)
        if (page != nullptr) page->setVisible(page.get() == current);
    for (auto* view : {mainView_.get(), stageView_.get()})
        if (view != nullptr) view->setVisible(view == current);
    refreshTopBar();
}

void AppShell::refreshTopBar() { topBar_.setState(makeTopBarState(session_, host_, controller_, page_, keyboardElsewhere_)); }

void AppShell::takeKeyboard() {
    if (auto* window = getTopLevelComponent()) window->toFront(true);
    grabKeyboardFocus();
}

}  // namespace sqlive::ui
