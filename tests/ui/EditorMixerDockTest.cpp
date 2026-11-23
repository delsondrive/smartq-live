#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/DocumentsJson.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/BarRuler.h"
#include "ui/ChannelStrip.h"
#include "ui/EditorMixerDock.h"
#include "ui/Helpers.h"
#include "ui/MixerPanel.h"
#include "ui/PartLanes.h"
#include "ui/SongEditorPage.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using K = juce::KeyPress;

// The Song editor's mixer panel (the spec's "Where the mixer appears", Song editor, and the mockup's #editor): under the
// tracks, a splitter labelled MIXER · THIS SONG that drags within limits and folds, kept per computer; its selection linked
// with the tracks both ways; the keys going to the mixer once a strip is clicked and back to the tracks once a track is.

namespace {

juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> at, juce::Point<float> downAt, juce::ModifierKeys mods = {},
                         bool dragged = false) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0, &c, &c, now, downAt, now, 1,
                            dragged);
}

void press(juce::Component& c, juce::Point<int> at, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
    c.mouseDown(mouseAt(c, at.toFloat(), at.toFloat(), mods));
    c.mouseUp(mouseAt(c, at.toFloat(), at.toFloat()));
}

// A press on the splitter's grip, a drag by dy, and the release.
void dragSplitter(EditorMixerDock& dock, int dy) {
    auto& splitter = dock.splitter();
    const juce::Point<float> from(static_cast<float>(splitter.getWidth() / 2), static_cast<float>(splitter.getHeight() / 2));
    const auto to = from.translated(0.0f, static_cast<float>(dy));
    splitter.mouseDown(mouseAt(splitter, from, from, juce::ModifierKeys::leftButtonModifier));
    splitter.mouseDrag(mouseAt(splitter, to, from, juce::ModifierKeys::leftButtonModifier, true));
    splitter.mouseUp(mouseAt(splitter, to, from, {}, true));
}

void clickStrip(MixerPanel& panel, int index) {
    auto* strip = panel.stripAt(index);
    REQUIRE(strip != nullptr);
    press(*strip, strip->nameArea().getCentre());
}

PartLane* laneOf(SongEditorPage& page, const juce::String& partId) {
    for (const auto& lane : page.timeline().lanes())
        if (auto* part = dynamic_cast<PartLane*>(lane.get()); part != nullptr && part->partId() == partId) return part;
    return nullptr;
}

// A press on the part's lane header, in the timeline's scrolling body.
void pressHeader(SongEditorPage& page, const juce::String& partId, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier) {
    auto* lane = laneOf(page, partId);
    REQUIRE(lane != nullptr);
    auto* body = page.lanesViewport().getViewedComponent();
    REQUIRE(body != nullptr);
    press(*body, {40, lane->getY() + lane->getHeight() / 2}, mods);
}

int stripOf(MixerPanel& panel, const juce::String& partId) {
    const auto& strips = panel.state().strips;
    for (std::size_t i = 0; i < strips.size(); ++i)
        if (strips[i].target.kind == app::LevelTarget::Kind::Part && strips[i].target.partId == partId) return static_cast<int>(i);
    return -1;
}

// The strip is wholly in the mixer's view, and the lane wholly in the tracks' view.
bool stripInView(MixerPanel& panel, int index) {
    auto* strip = panel.stripAt(index);
    const auto& view = panel.viewport();
    return strip != nullptr && view.getViewArea().contains(strip->getBounds());
}
bool laneInView(SongEditorPage& page, const juce::String& partId) {
    auto* lane = laneOf(page, partId);
    return lane != nullptr && page.lanesViewport().getViewArea().getVerticalRange().contains(lane->getBounds().getVerticalRange());
}

// A song of n parts (their files aren't there), opened in a page of this size.
juce::String addBigSong(app::Session& s, int n) {
    model::SongDoc song;
    song.id = "big";
    song.title = "Big";
    song.lengthSeconds = 200.0;
    for (int i = 0; i < n; ++i) {
        model::PartDoc part;
        part.id = "p" + juce::String(i);
        part.name = "Part " + juce::String(i + 1);
        part.file = "audio/p" + juce::String(i) + ".wav";
        part.roleId = s.model().band().roles[3].id;
        song.parts.push_back(part);
    }
    REQUIRE(s.commands().run("song.add", params({{"song", model::toVar(song)}})).ok);
    return song.id;
}

// Counts the notifications each way: the timeline's selection changes and the mixer's own selections.
struct Counted {
    int tracks = 0, strips = 0;
    explicit Counted(SongEditorPage& page) {
        auto tracksBefore = page.timeline().onSelectionChanged;
        page.timeline().onSelectionChanged = [this, tracksBefore] { ++tracks; if (tracksBefore) tracksBefore(); };
        auto& mixer = page.mixerDock().mixer();
        auto stripsBefore = mixer.onStripSelected;
        mixer.onStripSelected = [this, stripsBefore](int index) { ++strips; if (stripsBefore) stripsBefore(index); };
    }
};

}  // namespace

TEST_CASE("the song editor shows the mixer under the tracks, on This song, under a splitter", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    SongEditorPage page(s);  page.openSong(id);
    snapshot(page, 1920, 1018);
    auto& dock = page.mixerDock();
    CHECK(dock.isVisible());
    CHECK(dock.splitterWords() == juce::String::fromUTF8("MIXER \xc2\xb7 THIS SONG"));
    CHECK(dock.mixer().songId() == id);  CHECK(dock.mixer().outputId().isEmpty());
    CHECK(dock.mixer().state().strips.size() == 5);              // Click, Cues, Keys, Bass, Perc
    CHECK(page.timeline().getBottom() == dock.getY());            // the tracks above, the dock under them,
    CHECK(dock.getBottom() == page.getHeight() - 32);             // the footer under it
    CHECK(dock.splitter().getHeight() == EditorMixerDock::splitterHeight);
    CHECK(dock.panelHeight() == 440);                             // the mockup's, on a 1080 screen
    CHECK(dock.mixer().getHeight() >= ChannelStrip::minimumHeight(ChannelStrip::Size::Full));
    CHECK(childrenInside(page));
    SongEditorPage empty(s);                                      // no song open: no dock
    snapshot(empty, 1100, 618);
    CHECK_FALSE(empty.mixerDock().isVisible());
}

TEST_CASE("the splitter drags the panel within its limits at the smallest window and at 1920 x 1080", "[editor][dock]") {
    // The page under the 62 px top bar: 1100 x 680 and 1920 x 1080 windows.
    for (const auto [w, h] : {std::pair{1100, 618}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        DemoSession d; auto& s = *d.session;
        ScopedLook look;
        const auto id = addBigSong(s, 12);
        SongEditorPage page(s);  page.openSong(id);
        snapshot(page, w, h);
        auto& dock = page.mixerDock();
        const int start = dock.panelHeight();
        dragSplitter(dock, -30);                                  // up: a taller panel
        CHECK(dock.panelHeight() == std::min(start + 30, dock.maximumPanelHeight()));
        dragSplitter(dock, 20);                                   // down: a shorter one
        CHECK(dock.panelHeight() == std::max(std::min(start + 30, dock.maximumPanelHeight()) - 20, dock.minimumPanelHeight()));

        dragSplitter(dock, -5000);                                // as tall as it goes: about two tracks stay
        CHECK(dock.panelHeight() == dock.maximumPanelHeight());
        CHECK(page.timeline().getHeight() >= BarRuler::height + 2 * PartLane::height);
        CHECK(page.lanesViewport().getViewHeight() >= 2 * PartLane::height);
        CHECK(page.timeline().getHeight() < BarRuler::height + 3 * PartLane::height);   // and no more

        dragSplitter(dock, 5000);                                 // as short as it goes: a strip can still be drawn
        CHECK(dock.panelHeight() == dock.minimumPanelHeight());
        auto* strip = dock.mixer().stripAt(2);
        REQUIRE(strip != nullptr);
        CHECK(strip->getHeight() >= ChannelStrip::minimumHeight(ChannelStrip::Size::Full));
        CHECK(dock.maximumPanelHeight() >= dock.minimumPanelHeight());   // both hold, even at the smallest window
        CHECK(childrenInside(page));
    }
}

TEST_CASE("folding the panel leaves the splitter; unfolding brings back its height", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    SongEditorPage page(s);  page.openSong(id);
    snapshot(page, 1920, 1018);
    auto& dock = page.mixerDock();
    dragSplitter(dock, 60);
    const int height = dock.panelHeight();
    const int tracks = page.timeline().getHeight();
    REQUIRE(height == 380);
    CHECK(dock.foldWords() == juce::String::fromUTF8("\xe2\x96\xbe Hide"));

    dock.foldButton().onClick();                                  // "▾ Hide"
    CHECK(dock.folded());
    CHECK(dock.panelHeight() == 0);
    CHECK(dock.getHeight() == EditorMixerDock::splitterHeight);
    CHECK_FALSE(dock.mixer().isVisible());
    CHECK(page.timeline().getHeight() == tracks + height);        // the tracks take the room
    CHECK(dock.foldWords() == juce::String::fromUTF8("\xe2\x96\xb4 Mixer"));
    dragSplitter(dock, -100);                                     // folded, the splitter doesn't drag
    CHECK(dock.folded());

    dock.foldButton().onClick();                                  // "▴ Mixer"
    CHECK_FALSE(dock.folded());
    CHECK(dock.panelHeight() == height);
    CHECK(page.timeline().getHeight() == tracks);
}

TEST_CASE("the panel's height and folded state are kept per computer", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    {
        SongEditorPage page(s);  page.openSong(id);
        snapshot(page, 1920, 1018);
        dragSplitter(page.mixerDock(), 100);
        CHECK(s.settings().editorMixerHeight() == 340);
        page.mixerDock().setFolded(true);
        CHECK(s.settings().editorMixerFolded());
    }
    s.flushNow();                                                 // written with the band's next flush
    app::Settings again(d.dir.dir().getChildFile("settings.json"));
    CHECK(again.editorMixerHeight() == 340);
    CHECK(again.editorMixerFolded());

    SongEditorPage page(s);  page.openSong(id);                   // the next page opens as it was left
    snapshot(page, 1920, 1018);
    CHECK(page.mixerDock().folded());
    page.mixerDock().setFolded(false);
    CHECK(page.mixerDock().panelHeight() == 340);
    CHECK_FALSE(s.settings().editorMixerFolded());
}

TEST_CASE("a click on a track's header selects its strip and scrolls the mixer to it, telling nothing back", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = addBigSong(s, 12);
    SongEditorPage page(s);  page.openSong(id);
    snapshot(page, 1100, 618);
    auto& mixer = page.mixerDock().mixer();
    Counted counted(page);

    pressHeader(page, "p11");                                     // the last part: its strip is far to the right
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Part, {}, "p11"});
    CHECK(mixer.selectedStrip() == stripOf(mixer, "p11"));
    CHECK(stripInView(mixer, stripOf(mixer, "p11")));
    CHECK(counted.tracks == 1);
    CHECK(counted.strips == 0);                                   // selectStrip tells nothing back: no loop

    // A press on the lanes' empty space clears the selection, on both sides.
    auto* lane = laneOf(page, "p11");
    REQUIRE(lane != nullptr);
    auto* body = page.lanesViewport().getViewedComponent();
    press(*body, {Timeline::headerWidth + 100, lane->getY() + 10});
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);
    CHECK(mixer.selectedStrip() == -1);
    CHECK(counted.strips == 0);
}

TEST_CASE("a click on a strip selects its track and scrolls the tracks to it, telling nothing back", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = addBigSong(s, 12);
    SongEditorPage page(s);  page.openSong(id);
    snapshot(page, 1100, 618);
    auto& mixer = page.mixerDock().mixer();
    Counted counted(page);
    REQUIRE_FALSE(laneInView(page, "p11"));

    clickStrip(mixer, stripOf(mixer, "p11"));
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Part, {}, "p11"});
    CHECK(laneInView(page, "p11"));
    CHECK(counted.strips == 1);
    CHECK(counted.tracks == 1);                                   // one each way, and no more

    // The selected header is marked as the mockup's .lh.sel: a mint bar along its left edge; the one above it isn't.
    auto image = snapshot(page, 1100, 618);
    auto* body = page.lanesViewport().getViewedComponent();
    const auto top = [&](const juce::String& partId) { return page.getLocalPoint(body, laneOf(page, partId)->getBounds().getTopLeft()).y; };
    CHECK(hasColour(image.getClippedImage({200, top("p11") + 4, 3, PartLane::height - 8}), theme::mint));
    CHECK_FALSE(hasColour(image.getClippedImage({200, top("p11") - PartLane::height + 4, 3, PartLane::height - 8}), theme::mint));

    mixer.keyPressed(K(K::tabKey, {}, 0));                        // Tab: the next strip (the first, wrapping round: Click)
    CHECK(mixer.selectedStrip() == 0);
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);   // Click has no track to select
    CHECK(counted.strips == 2);
    CHECK(counted.tracks == 2);
    clickStrip(mixer, stripOf(mixer, "p0"));
    CHECK(page.timeline().selection().id == "p0");
    CHECK(laneInView(page, "p0"));

    mixer.keyPressed(K(K::escapeKey, {}, 0));                     // Esc: no strip, and no track either
    CHECK(mixer.selectedStrip() == -1);
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);
}

TEST_CASE("the strips made again keep the selection on the same part, and the track with it", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    const auto song = *s.model().song(id);
    const auto keys = song.parts[0].id, bass = song.parts[1].id;
    SongEditorPage page(s);  page.openSong(id);
    snapshot(page, 1920, 1018);
    auto& mixer = page.mixerDock().mixer();
    clickStrip(mixer, 3);                                         // Bass
    REQUIRE(mixer.state().strips[3].target.partId == bass);

    REQUIRE(s.commands().run("part.remove", params({{"songId", id}, {"partId", keys}})).ok);
    d.settle();
    s.changes().sendSynchronousChangeMessage();
    REQUIRE(mixer.state().strips.size() == 4);
    CHECK(mixer.selectedStrip() == 2);                            // Bass moved left: still Bass
    CHECK(mixer.state().strips[2].target.partId == bass);
    CHECK(page.timeline().selection() == EditorSelection{EditorSelection::Kind::Part, {}, bass});

    REQUIRE(s.commands().undo().ok);                              // Keys back in front of it
    d.settle();
    s.changes().sendSynchronousChangeMessage();
    REQUIRE(mixer.state().strips.size() == 5);
    CHECK(mixer.selectedStrip() == 3);
    CHECK(mixer.state().strips[3].target.partId == bass);
    CHECK(page.timeline().selection().id == bass);

    REQUIRE(s.commands().run("part.remove", params({{"songId", id}, {"partId", bass}})).ok);   // the selected part goes
    d.settle();
    s.changes().sendSynchronousChangeMessage();
    CHECK(mixer.selectedStrip() == -1);
    CHECK(page.timeline().selection().kind == EditorSelection::Kind::None);
}

TEST_CASE("Esc in the editor's mixer gives the keys to the tracks", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    SongEditorPage page(s);  page.openSong(d.setlist().entries[0].songId);
    snapshot(page, 1920, 1018);
    CHECK(page.mixerDock().mixer().keysHome() == &page);          // not whatever had them before the strip was clicked
}

TEST_CASE("the editor's status line names the soloed parts in caution", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    const auto song = *s.model().song(id);
    SongEditorPage page(s);  page.openSong(id);
    const auto footer = [&] {
        auto image = snapshot(page, 1280, 698);
        return image.getClippedImage({200, 698 - 31, 1280 - 200 - 290, 31});
    };
    CHECK(page.state().solo.isEmpty());
    CHECK_FALSE(hasColour(footer(), theme::caution));
    s.setSolo(id, song.parts[2].id, true);                        // Perc, then Keys: named in the song's order
    s.setSolo(id, song.parts[0].id, true);
    s.changes().sendSynchronousChangeMessage();
    CHECK(page.state().solo == "Solo: Keys, Perc");
    CHECK(hasColour(footer(), theme::caution));
    s.setSolo(id, song.parts[0].id, false);
    s.setSolo(id, song.parts[2].id, false);
    s.changes().sendSynchronousChangeMessage();
    CHECK(page.state().solo.isEmpty());
    CHECK(makeSongEditorState(s, id, 0.0).solo.isEmpty());
}

// The keyboard really moving needs a window on the desktop (focus is only given to what is showing): hidden from routine
// runs, as every test that shows a window.
TEST_CASE("the keys go to the mixer once a strip is clicked, and back to the tracks once a track is", "[editor][dock][.window]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    SongEditorPage page(s);  page.openSong(song.id);
    page.setBounds(0, 0, 1920, 1018);
    page.setAlwaysOnTop(true);
    page.addToDesktop(juce::ComponentPeer::windowIsTemporary);
    page.setVisible(true);
    page.toFront(true);
    d.settle();
    if (!page.isShowing()) {
        page.removeFromDesktop();
        SKIP("this desktop doesn't show a test window");
    }
    auto& mixer = page.mixerDock().mixer();
    page.grabKeyboardFocus();
    clickStrip(mixer, 3);
    CHECK(mixer.hasKeyboardFocus(true));
    pressHeader(page, song.parts[0].id);                          // a track: the keys to the tracks, its strip selected
    CHECK(page.hasKeyboardFocus(false));
    CHECK(mixer.selectedStrip() == 2);
    clickStrip(mixer, 4);
    CHECK(mixer.hasKeyboardFocus(true));
    CHECK(mixer.keyPressed(K(K::escapeKey, {}, 0)));
    CHECK(page.hasKeyboardFocus(false));
    clickStrip(mixer, 4);
    auto* body = page.lanesViewport().getViewedComponent();
    press(*body, {Timeline::headerWidth + 100, 5});               // the timeline
    CHECK(page.hasKeyboardFocus(false));
    page.removeFromDesktop();
}

// In the app the page is made and filled before the window gives it a size: the dock's mixer is shown (and its meters
// live) from the start, not only once it is laid out.
TEST_CASE("the dock's meters move while its song plays, even before the page was laid out", "[editor][dock]") {
    DemoSession d; auto& s = *d.session;
    ScopedLook look;
    const auto id = d.setlist().entries[0].songId;
    SongEditorPage page(s);  page.openSong(id);                   // never sized
    auto& mixer = page.mixerDock().mixer();
    CHECK(mixer.isVisible());
    s.playSong(id, 0.0);
    for (int i = 0; i < 6000 && s.now().seconds < 10.0; ++i) s.advanceWithoutDevice(480);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
    for (int i = 0; i < 15; ++i) {
        s.advanceWithoutDevice(1600);
        mixer.tickMeters(juce::Time::getMillisecondCounterHiRes() + i * 33.0);
    }
    REQUIRE(mixer.stripAt(0) != nullptr);
    CHECK(mixer.stripAt(0)->peakText() != juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e"));   // the click was heard
    s.perform(app::Action::Stop);
}
