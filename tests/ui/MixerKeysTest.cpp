#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/ShowController.h"
#include "model/Words.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/ChannelStrip.h"
#include "ui/Helpers.h"
#include "ui/MainView.h"
#include "ui/MixerPage.h"
#include "ui/MixerPanel.h"
#include "ui/Pages.h"
#include "ui/SongEditorPage.h"
#include "ui/StripValueEditor.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::Action;
using app::LevelTarget;
using K = juce::KeyPress;

// The mixer's keyboard (the spec's "The keyboard (Edit mode only)"): a strip selected by a click takes the arrows, Tab, M,
// S, Enter and Esc; a run of arrows is one undo step; a typed value sets the level or the pan; in Show mode nothing in the
// mixer takes a key, and the show's keys reach the window's ShowController.

namespace {

const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");   // "−"

K key(int code, juce::ModifierKeys mods = {}) { return K(code, mods, 0); }
K shifted(int code) { return key(code, juce::ModifierKeys::shiftModifier); }

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> at, int clicks = 1) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0, &c, &c, now, at, now,
                            clicks, false);
}

void click(juce::Component& c, juce::Point<int> at) {
    c.mouseDown(mouseAt(c, at.toFloat()));
    c.mouseUp(mouseAt(c, at.toFloat()));
}

// A click on the strip's name label: it selects the strip and does nothing else.
void clickStrip(MixerPanel& panel, int index) {
    auto* strip = panel.stripAt(index);
    REQUIRE(strip != nullptr);
    click(*strip, strip->nameArea().getCentre());
}

void forEachComponent(juce::Component& root, const std::function<void(juce::Component&)>& visit) {
    visit(root);
    for (auto* child : root.getChildren()) forEachComponent(*child, visit);
}

bool anyTakesKeyboardFocus(juce::Component& root) {
    bool takes = false;
    forEachComponent(root, [&](juce::Component& c) { takes = takes || c.getWantsKeyboardFocus() || c.getMouseClickGrabsKeyboardFocus(); });
    return takes;
}

// A key as JUCE's window delivers it (ComponentPeer::handleKeyPress): to the component with the keyboard, then up through
// its parents until one uses it, the window's key listener before the window itself.
bool deliverKey(juce::Component& focused, const K& k, juce::Component* window = nullptr, juce::KeyListener* windowKeys = nullptr) {
    for (auto* c = &focused; c != nullptr; c = c->getParentComponent()) {
        if (c == window && windowKeys != nullptr && windowKeys->keyPressed(k, c)) return true;
        if (c->keyPressed(k)) return true;
    }
    return false;
}

struct Mixer {
    DemoSession d;
    app::Session& s = *d.session;
    ScopedLook look;
    MixerPanel panel{s};
    double nowMs = 1000.0;
    std::vector<int> selections;
    juce::String songId = d.setlist().entries[0].songId, outputId = s.model().rig(s.rigId())->outputs[0].id;

    explicit Mixer(bool output = false) {
        panel.clockMs = [this] { return nowMs; };
        panel.onStripSelected = [this](int index) { selections.push_back(index); };
        panel.show(songId, output ? outputId : juce::String());
        snapshot(panel, 900, 420);
    }
    bool press(const K& k) {
        nowMs += 100.0;
        return panel.keyPressed(k);
    }
    const MixerState::Strip& strip(int index) const { return panel.state().strips[static_cast<std::size_t>(index)]; }
    LevelTarget target(int index) const { return strip(index).target; }
    std::optional<float> level(int index) const { return s.level(target(index)); }
    void type(const juce::String& text, const K& ending = key(K::returnKey)) {
        REQUIRE(panel.typing() != nullptr);
        panel.typing()->setText(text);
        panel.typing()->press(ending);
    }
};

}  // namespace

TEST_CASE("a click on a strip selects it and the mixer takes the keys; Esc gives them back", "[mixerkeys]") {
    Mixer m;
    CHECK(m.panel.selectedStrip() == -1);
    CHECK_FALSE(m.panel.takesKeys());
    CHECK_FALSE(m.panel.keyPressed(key(K::upKey)));               // nothing selected: the keys go on to where they went
    clickStrip(m.panel, 2);
    CHECK(m.panel.selectedStrip() == 2);
    CHECK(m.panel.stripAt(2)->isSelected());
    CHECK(m.panel.takesKeys());
    CHECK(m.panel.getWantsKeyboardFocus());
    CHECK(m.selections == std::vector<int>{2});
    m.panel.selectStrip(3);                                       // as the tracks would: no word back
    CHECK(m.panel.selectedStrip() == 3);
    CHECK_FALSE(m.panel.stripAt(2)->isSelected());
    CHECK(m.selections == std::vector<int>{2});
    CHECK(m.press(key(K::escapeKey)));
    CHECK(m.panel.selectedStrip() == -1);
    CHECK_FALSE(m.panel.takesKeys());
    CHECK_FALSE(m.panel.getWantsKeyboardFocus());
    CHECK(m.selections == std::vector<int>{2, -1});
}

TEST_CASE("up and down move the level half a dB, a tenth with Shift, on a song strip and an output strip", "[mixerkeys]") {
    for (const bool output : {false, true}) {
        INFO((output ? "output" : "this song"));
        Mixer m(output);
        clickStrip(m.panel, 2);                                   // Keys
        const float start = *m.level(2);
        CHECK(m.press(key(K::upKey)));
        CHECK(*m.level(2) == toTenth(start + 0.5f));
        CHECK(m.press(shifted(K::downKey)));
        CHECK(*m.level(2) == toTenth(start + 0.4f));
        CHECK(m.press(key(K::downKey)));
        CHECK(*m.level(2) == toTenth(start - 0.1f));
        for (int i = 0; i < 40; ++i) m.press(key(K::upKey));     // never past +6
        CHECK(*m.level(2) == 6.0f);
        CHECK(m.panel.stripAt(2)->levelText() == "+6.0");
    }
}

TEST_CASE("up from off starts sending at -60 dB; down from off does nothing", "[mixerkeys]") {
    Mixer m(true);
    m.panel.toggleMute(2);                                        // Keys no longer sent to the headphones
    REQUIRE_FALSE(m.level(2).has_value());
    clickStrip(m.panel, 2);
    m.press(key(K::downKey));
    CHECK_FALSE(m.level(2).has_value());
    m.press(shifted(K::upKey));
    CHECK(m.level(2) == std::optional<float>(-60.0f));
    m.press(key(K::upKey));
    CHECK(m.level(2) == std::optional<float>(-59.5f));
}

TEST_CASE("a run of arrows on one strip is one undo step; a second's pause, another key or strip starts another", "[mixerkeys]") {
    Mixer m;
    clickStrip(m.panel, 2);
    const float start = *m.level(2);
    for (int i = 0; i < 6; ++i) m.press(key(K::upKey));           // 100 ms apart
    CHECK(*m.level(2) == toTenth(start + 3.0f));
    m.s.commands().undo();
    CHECK(*m.level(2) == start);

    m.press(key(K::upKey));
    m.nowMs += 1500.0;                                            // 1.5 s later: a run of its own
    m.press(key(K::upKey));
    CHECK(*m.level(2) == toTenth(start + 1.0f));
    m.s.commands().undo();
    CHECK(*m.level(2) == toTenth(start + 0.5f));
    m.s.commands().undo();
    CHECK(*m.level(2) == start);

    m.press(key(K::upKey));
    m.press(key('M'));  m.press(key('M'));                        // another key ends the run (and M twice changes nothing)
    m.press(key(K::upKey));
    m.s.commands().undo();
    CHECK(*m.level(2) == toTenth(start + 0.5f));

    m.press(key(K::upKey));
    m.press(key(K::tabKey));  m.press(shifted(K::tabKey));        // another strip and back
    m.press(key(K::upKey));
    m.s.commands().undo();
    CHECK(*m.level(2) == toTenth(start + 1.0f));

    m.press(key(K::upKey));
    clickStrip(m.panel, 2);                                       // the mouse ends it too
    m.press(key(K::upKey));
    m.s.commands().undo();
    CHECK(*m.level(2) == toTenth(start + 1.5f));
}

TEST_CASE("left and right move the pan by 5, by 1 with Shift", "[mixerkeys]") {
    Mixer m;
    clickStrip(m.panel, 2);
    REQUIRE(m.s.pan(m.target(2)) == std::optional<int>(0));
    m.press(key(K::leftKey));  m.press(key(K::leftKey));
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(-10));
    m.press(shifted(K::rightKey));
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(-9));
    CHECK(m.panel.stripAt(2)->panText() == "L 9");
    for (int i = 0; i < 30; ++i) m.press(key(K::rightKey));
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(100));       // never past R 100
    m.press(key(K::escapeKey));
    clickStrip(m.panel, 0);                                       // the click has no song pan: nothing to move
    m.press(key(K::leftKey));
    CHECK(m.panel.stripAt(0)->panText().isEmpty());
}

TEST_CASE("on a linked output strip the first arrow unlinks it, in one undo step with the run", "[mixerkeys]") {
    Mixer m(true);
    REQUIRE(m.strip(2).linked);
    clickStrip(m.panel, 2);
    m.press(key(K::rightKey));
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(5));
    CHECK_FALSE(m.strip(2).linked);
    m.press(key(K::rightKey));  m.press(shifted(K::rightKey));
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(11));
    m.s.commands().undo();                                        // the run and the unlink go back together
    CHECK_FALSE(m.s.pan(m.target(2)).has_value());
}

TEST_CASE("Tab and Shift+Tab choose the next and the previous strip, wrapping round", "[mixerkeys]") {
    Mixer m;
    const int count = static_cast<int>(m.panel.state().strips.size());
    REQUIRE(count == 5);
    clickStrip(m.panel, count - 1);
    CHECK(m.press(key(K::tabKey)));
    CHECK(m.panel.selectedStrip() == 0);
    CHECK(m.press(shifted(K::tabKey)));
    CHECK(m.panel.selectedStrip() == count - 1);
    m.press(shifted(K::tabKey));
    CHECK(m.panel.selectedStrip() == count - 2);
    CHECK(m.selections == std::vector<int>{count - 1, 0, count - 1, count - 2});
}

TEST_CASE("M mutes a part or stops sending a role; S solos a part on This song only", "[mixerkeys]") {
    {
        Mixer m;
        clickStrip(m.panel, 2);
        CHECK(m.press(key('M')));
        CHECK(m.s.muted(m.songId, m.target(2).partId));
        m.press(key('m'));
        CHECK_FALSE(m.s.muted(m.songId, m.target(2).partId));
        CHECK(m.press(key('S')));
        CHECK(m.s.soloed(m.songId, m.target(2).partId));
        CHECK(m.strip(3).silencedBySolo);
        m.press(key('s'));
        CHECK_FALSE(m.s.soloed(m.songId, m.target(2).partId));
        CHECK_FALSE(m.press(key(K::tabKey, juce::ModifierKeys::commandModifier)));   // not the mixer's: Ctrl+Tab goes on
        clickStrip(m.panel, 0);                                   // the click: no M, no S
        m.press(key('M'));  m.press(key('S'));
        CHECK(m.strip(0).value == "0.0");
    }
    {
        Mixer m(true);
        clickStrip(m.panel, 2);
        m.press(key('M'));
        CHECK_FALSE(m.level(2).has_value());                      // not sent
        m.press(key('M'));
        CHECK(m.level(2) == std::optional<float>(0.0f));
        m.press(key('S'));                                        // no solo on an output
        CHECK(m.s.soloedParts(m.songId).isEmpty());
    }
}

TEST_CASE("Enter types a level: a Unicode minus works, bad words leave it and flash, Esc leaves it", "[mixerkeys]") {
    Mixer m;
    clickStrip(m.panel, 2);
    const float start = *m.level(2);
    CHECK(m.press(key(K::returnKey)));
    REQUIRE(m.panel.typing() != nullptr);
    CHECK(m.panel.typing()->text() == m.panel.stripAt(2)->levelText());   // starts from what the box says
    CHECK(m.panel.typing()->getParentComponent() == m.panel.stripAt(2));
    CHECK(m.panel.typing()->getBounds().contains(m.panel.stripAt(2)->levelBoxArea()));
    m.type(kMinus + "4.5");
    CHECK(m.panel.typing() == nullptr);
    CHECK(m.level(2) == std::optional<float>(-4.5f));
    m.s.commands().undo();                                        // one typed value, one step
    CHECK(*m.level(2) == start);

    m.press(key(K::returnKey));
    m.type("abc");
    CHECK(*m.level(2) == start);
    CHECK(m.panel.stripAt(2)->flashing() == ChannelStrip::Field::Level);

    m.press(key(K::returnKey));
    m.type("-12", key(K::escapeKey));
    CHECK(*m.level(2) == start);
    CHECK(m.panel.typing() == nullptr);

    m.press(key(K::returnKey));
    m.type("off");                                                // "This song" has no "not sent"
    CHECK(*m.level(2) == start);

    m.press(key(K::returnKey));
    m.type("+9");                                                 // limited to +6
    CHECK(*m.level(2) == 6.0f);

    m.press(key(K::returnKey));                                   // the keyboard going elsewhere sets it too
    m.panel.typing()->setText("-20");
    m.panel.typing()->keyboardWentElsewhere();
    CHECK(*m.level(2) == -20.0f);
    CHECK(m.panel.typing() == nullptr);
}

TEST_CASE("on an output, off typed stops sending, and a number sends again", "[mixerkeys]") {
    Mixer m(true);
    clickStrip(m.panel, 2);
    m.press(key(K::returnKey));
    m.type(kMinus + "inf");
    CHECK_FALSE(m.level(2).has_value());
    m.press(key(K::returnKey));
    CHECK(m.panel.typing()->text() == "off");
    m.type("-3");
    CHECK(m.level(2) == std::optional<float>(-3.0f));
}

TEST_CASE("a click on the level box types a level; a click on the pan's words types a pan", "[mixerkeys]") {
    Mixer m;
    auto& strip = *m.panel.stripAt(3);
    click(strip, strip.levelBoxArea().getCentre());
    CHECK(m.panel.selectedStrip() == 3);
    REQUIRE(m.panel.typing() != nullptr);
    CHECK(m.panel.typing()->field() == ChannelStrip::Field::Level);
    m.type("-7");
    CHECK(m.level(3) == std::optional<float>(-7.0f));

    click(strip, strip.panBoxArea().getCentre());                 // after the double-click time, as a double-click centres it
    CHECK(m.panel.typing() == nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(juce::MouseEvent::getDoubleClickTimeout() + 150);
    REQUIRE(m.panel.typing() != nullptr);
    CHECK(m.panel.typing()->field() == ChannelStrip::Field::Pan);
    m.type("R 20");
    CHECK(m.s.pan(m.target(3)) == std::optional<int>(20));
}

TEST_CASE("Shift+Enter types a pan: L30, C, or a number", "[mixerkeys]") {
    Mixer m;
    clickStrip(m.panel, 2);
    CHECK(m.press(shifted(K::returnKey)));
    REQUIRE(m.panel.typing() != nullptr);
    CHECK(m.panel.typing()->text() == "C");
    CHECK(m.panel.typing()->getBounds().contains(m.panel.stripAt(2)->panBoxArea()));
    m.type("L30");
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(-30));
    m.press(shifted(K::returnKey));
    m.type("40");
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(40));
    m.press(shifted(K::returnKey));
    m.type("L 1.5");                                              // can't be read: stays, and the bar flashes
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(40));
    CHECK(m.panel.stripAt(2)->flashing() == ChannelStrip::Field::Pan);
    m.press(shifted(K::returnKey));
    m.type("c");
    CHECK(m.s.pan(m.target(2)) == std::optional<int>(0));

    m.press(key(K::escapeKey));
    clickStrip(m.panel, 0);                                       // the click has no pan on "This song"
    m.press(shifted(K::returnKey));
    CHECK(m.panel.typing() == nullptr);
}

TEST_CASE("Space and Ctrl+Z / Ctrl+Y still work while a strip is selected", "[mixerkeys]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    AppShell shell(s, nullptr);  shell.setBounds(0, 0, 1280, 760);
    plugInPages(shell, s, nullptr, nullptr);
    shell.showPage(Page::SongEditor);
    auto* page = dynamic_cast<SongEditorPage*>(shell.currentContent());
    REQUIRE(page != nullptr);
    const auto songId = d.setlist().entries[0].songId;
    page->openSong(songId);
    MixerPanel panel(s);                                          // under the tracks, as the Song editor will have it
    page->addAndMakeVisible(panel);
    panel.setBounds(0, 300, 900, 420);
    panel.show(songId, {});
    clickStrip(panel, 2);
    REQUIRE(panel.takesKeys());

    const auto start = *s.level(panel.state().strips[2].target);
    CHECK(deliverKey(panel, key(K::upKey)));
    CHECK(*s.level(panel.state().strips[2].target) == toTenth(start + 0.5f));
    CHECK(deliverKey(panel, key('z', juce::ModifierKeys::commandModifier)));   // the app's undo
    CHECK(*s.level(panel.state().strips[2].target) == start);
    CHECK(deliverKey(panel, key('y', juce::ModifierKeys::commandModifier)));   // and redo
    CHECK(*s.level(panel.state().strips[2].target) == toTenth(start + 0.5f));
    CHECK(deliverKey(panel, key(K::spaceKey)));                   // play, as the Song editor's Space does
    s.advanceWithoutDevice(4800);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(s.now().playing);
    CHECK(panel.selectedStrip() == 2);                            // and the strip stays selected
    s.perform(Action::Stop);
}

TEST_CASE("in Show mode a click selects nothing, no key changes anything, and the arrows reach the show", "[mixerkeys]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    s.enterShow();  d.settle();
    app::ShowController show(s);
    std::vector<Action> seen;
    show.onAction = [&](Action a) { seen.push_back(a); };
    juce::Component window;                                       // the window: the ShowController listens to its keys
    window.addKeyListener(&show);
    MainView view(s);
    window.addAndMakeVisible(view);
    window.setBounds(0, 0, 1280, 760);
    snapshot(view, 1280, 760);
    d.settle();
    auto& panel = view.mixer();
    REQUIRE(panel.state().strips.size() >= 3);
    panel.clockMs = [] { return 0.0; };

    const auto before = makeMixerState(s, panel.songId(), panel.outputId());
    const int tonight = s.tonightChangeCount();
    clickStrip(panel, 2);
    CHECK(panel.selectedStrip() == -1);
    CHECK_FALSE(panel.takesKeys());
    panel.selectStrip(2);                                         // not even when asked
    CHECK(panel.selectedStrip() == -1);
    CHECK_FALSE(anyTakesKeyboardFocus(panel));

    for (const auto& k : {key(K::upKey), key(K::downKey), key(K::leftKey), key(K::rightKey), shifted(K::upKey), key(K::tabKey),
                          key('M'), key('S'), key(K::returnKey), shifted(K::returnKey), key(K::escapeKey)})
        CHECK_FALSE(panel.keyPressed(k));
    CHECK(panel.typing() == nullptr);
    CHECK(makeMixerState(s, panel.songId(), panel.outputId()) == before);
    CHECK(s.tonightChangeCount() == tonight);

    // Even sent to the mixer itself, the keys go on up to the window, where the show takes them.
    CHECK(deliverKey(panel, key(K::downKey), &window, &show));
    CHECK(deliverKey(*panel.stripAt(2), key(K::leftKey), &window, &show));
    CHECK(deliverKey(panel, key(K::rightKey), &window, &show));
    show.keyStateChanged(false, nullptr);                         // → let go at once: a tap
    CHECK(std::find(seen.begin(), seen.end(), Action::NextSectionLater) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), Action::NextSongEarlier) != seen.end());
    CHECK(std::find(seen.begin(), seen.end(), Action::NextSongLater) != seen.end());
    CHECK(makeMixerState(s, panel.songId(), panel.outputId()) == before);
    window.removeKeyListener(&show);
}

TEST_CASE("entering Show mode drops the selection and any typing", "[mixerkeys]") {
    Mixer m;
    clickStrip(m.panel, 2);
    m.press(key(K::returnKey));
    REQUIRE(m.panel.typing() != nullptr);
    const float start = *m.level(2);
    m.panel.typing()->setText("-30");
    m.s.enterShow();
    m.s.changes().sendSynchronousChangeMessage();
    CHECK(m.panel.typing() == nullptr);
    CHECK(m.panel.selectedStrip() == -1);
    CHECK_FALSE(m.panel.takesKeys());
    CHECK(*m.level(2) == start);                                  // nothing typed was set
    CHECK_FALSE(anyTakesKeyboardFocus(m.panel));
}

TEST_CASE("a run of level and pan arrows is one undo step named after both", "[mixerkeys]") {
    {
        Mixer m;
        clickStrip(m.panel, 2);
        m.press(key(K::rightKey));                                // pan first: its words would hide the level
        m.press(key(K::upKey));
        CHECK(m.s.commands().undoWords() == "level and pan Keys");
        m.press(key(K::escapeKey));
        clickStrip(m.panel, 2);
        m.press(key(K::upKey));  m.press(key(K::upKey));          // level only: as the session names it
        CHECK(m.s.commands().undoWords() != "level and pan Keys");
    }
    {
        Mixer m(true);
        clickStrip(m.panel, 2);
        m.press(key(K::upKey));
        m.press(key(K::leftKey));
        CHECK(m.s.commands().undoWords() == "level and pan Keys on Headphones");
        m.s.commands().undo();                                    // both back at once
        CHECK(m.level(2) == std::optional<float>(0.0f));
        CHECK_FALSE(m.s.pan(m.target(2)).has_value());
    }
}

TEST_CASE("choosing another output or song sets what is being typed first", "[mixerkeys]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    MixerPage page(s);
    const auto songId = d.setlist().entries[0].songId;
    page.openSong(songId);
    snapshot(page, 1280, 698);
    auto& panel = page.panel();
    clickStrip(panel, 2);
    const auto keys = panel.state().strips[2].target;
    panel.keyPressed(key(K::returnKey));
    REQUIRE(panel.typing() != nullptr);
    panel.typing()->setText("-5");
    page.chooseOutput(s.model().rig(s.rigId())->outputs[0].id);   // the strips are made again
    CHECK(panel.typing() == nullptr);
    CHECK(s.level(keys) == std::optional<float>(-5.0f));
    CHECK_FALSE(panel.watchingPresses());
}

namespace {

// A press and release of the real mouse at a point of a component on the desktop, as the window's peer passes it on:
// to the component under it, then to every global mouse listener.
void realClick(juce::Component& onDesktop, juce::Component& target, juce::Point<int> at) {
    auto* peer = onDesktop.getPeer();
    REQUIRE(peer != nullptr);
    const auto where = onDesktop.getLocalPoint(&target, at).toFloat();
    static juce::int64 time = juce::Time::currentTimeMillis();
    time += 1000;                                                 // never a double-click
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, where, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, where, juce::ModifierKeys(),
                           juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, time + 50);
}

Segmented* levelsForIn(juce::Component& root) {
    Segmented* found = nullptr;
    forEachComponent(root, [&](juce::Component& c) {
        if (auto* s = dynamic_cast<Segmented*>(&c); s != nullptr && found == nullptr && s->isVisible()) found = s;
    });
    return found;
}

}  // namespace

TEST_CASE("a real press anywhere outside the typing field sets what was typed; a press inside it doesn't", "[mixerkeys][.window]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    MixerPage page(s);
    const auto songId = d.setlist().entries[0].songId;
    page.openSong(songId);
    page.setBounds(0, 0, 1280, 698);
    // This needs a window on the desktop, which a test machine may not give.
    page.setAlwaysOnTop(true);                                    // over every other window, foreground or not
    page.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    page.setVisible(true);
    page.toFront(false);
    auto& panel = page.panel();
    d.settle();
    // The press goes to whatever is on top on the screen there (Windows is asked): another window over it means no test.
    const auto onTop = [&](juce::Component& c, juce::Point<int> at) {
        auto* found = juce::Desktop::getInstance().findComponentAt(c.localPointToGlobal(at));
        return found != nullptr && (found == &page || page.isParentOf(found));
    };
    if (!page.isShowing() || panel.stripAt(2) == nullptr || !onTop(*panel.stripAt(2), panel.stripAt(2)->levelBoxArea().getCentre())) {
        page.removeFromDesktop();
        SKIP("this desktop doesn't show a test window on top");
    }
    REQUIRE(panel.state().strips.size() == 5);
    const auto level = [&](int i) { return s.level(panel.state().strips[static_cast<std::size_t>(i)].target); };
    const auto typeInto = [&](int i, const juce::String& text) {
        auto& strip = *panel.stripAt(i);
        realClick(page, strip, strip.levelBoxArea().getCentre());   // a press on the level box opens the field
        REQUIRE(panel.typing() != nullptr);
        CHECK(panel.watchingPresses());
        panel.typing()->setText(text);
    };

    typeInto(2, "-4.5");
    realClick(page, *panel.typing(), panel.typing()->getLocalBounds().getCentre());   // inside the field: still typing
    REQUIRE(panel.typing() != nullptr);
    CHECK(level(2) != std::optional<float>(-4.5f));
    realClick(page, panel, {3, 3});                               // the panel's own background
    CHECK(panel.typing() == nullptr);
    CHECK_FALSE(panel.watchingPresses());
    CHECK(level(2) == std::optional<float>(-4.5f));

    typeInto(2, "-6");
    const auto row = panel.stripAt(3)->getBounds();               // the gap between two strips
    realClick(page, *panel.stripAt(3)->getParentComponent(), {row.getX() - 3, row.getCentreY()});
    CHECK(panel.typing() == nullptr);
    CHECK(level(2) == std::optional<float>(-6.0f));

    typeInto(2, "-7");
    realClick(page, *panel.stripAt(3), panel.stripAt(3)->nameArea().getCentre());   // another strip
    CHECK(panel.typing() == nullptr);
    CHECK(level(2) == std::optional<float>(-7.0f));
    CHECK(panel.selectedStrip() == 3);

    typeInto(2, "abc");                                           // can't be read: closes as Esc would, and flashes
    realClick(page, panel, {3, 3});
    CHECK(panel.typing() == nullptr);
    CHECK(level(2) == std::optional<float>(-7.0f));
    CHECK(panel.stripAt(2)->flashing() == ChannelStrip::Field::Level);

    typeInto(2, "-8");                                            // "Levels for" Headphones: set before the switch
    const auto keys = panel.state().strips[2].target;
    auto* levelsFor = levelsForIn(page);
    REQUIRE(levelsFor != nullptr);
    realClick(page, *levelsFor, {levelsFor->getWidth() * 7 / 8, levelsFor->getHeight() / 2});
    CHECK(page.panel().outputId().isNotEmpty());
    CHECK(panel.typing() == nullptr);
    CHECK(s.level(keys) == std::optional<float>(-8.0f));
    CHECK_FALSE(panel.watchingPresses());

    s.enterShow();  d.settle();                                   // never watching in a show
    CHECK_FALSE(panel.watchingPresses());
    page.removeFromDesktop();
}
