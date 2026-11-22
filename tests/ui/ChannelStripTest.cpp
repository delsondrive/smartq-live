#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Units.h"
#include "model/Words.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Pixels.h"
#include "ui/ChannelStrip.h"
#include "ui/MixerPanel.h"
#include "ui/Theme.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;
using app::LevelTarget;

namespace {

const juce::Colour kKeys{0xffff9a68}, kBass{0xff3fd39a};
const juce::Colour kCapLine{0xfff1edf4};

MixerState::Strip partStrip(juce::Colour colour = kKeys) {
    MixerState::Strip s;
    s.target = {LevelTarget::Kind::Part, "song", "part", {}, {}};
    s.name = "Keys";
    s.colour = colour;
    s.db = -3.0f;
    s.value = "-3.0";
    s.canMute = true;
    s.pan = -40;
    s.canSolo = true;
    return s;
}

MixerState::Strip outputStrip(bool linked, bool linkable = true) {
    MixerState::Strip s = partStrip();
    s.target = {LevelTarget::Kind::Mix, {}, {}, "out", "keys"};
    s.canSolo = false;
    s.linkable = linkable;
    s.linked = linked;
    s.pan = linked ? 0 : -60;
    return s;
}

// A mouse event on the strip at `at`, the gesture having started at `down`.
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged,
                         juce::ModifierKeys mods = {}, int clicks = 1) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, 1.0f, 0, 0, 0, 0, &c, &c, now, down,
                            now, clicks, dragged);
}

void press(juce::Component& c, juce::Point<float> at) {
    c.mouseDown(mouseAt(c, at, at, false));
    c.mouseUp(mouseAt(c, at, at, false));
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {}) {
    c.mouseDown(mouseAt(c, from, from, false, mods));
    c.mouseDrag(mouseAt(c, (from + to) * 0.5f, from, true, mods));
    c.mouseDrag(mouseAt(c, to, from, true, mods));
    c.mouseUp(mouseAt(c, to, from, true, mods));
}

juce::Colour pixel(const juce::Image& image, int x, int y) {
    const juce::Image::BitmapData data(image, juce::Image::BitmapData::readOnly);
    return data.getPixelColour(x, y);
}

// Some pixel of the area is text-bright: brighter than the strip's dark surfaces by far.
bool hasText(const juce::Image& image, juce::Rectangle<int> area) {
    const juce::Image::BitmapData data(image, juce::Image::BitmapData::readOnly);
    for (int y = std::max(0, area.getY()); y < std::min(data.height, area.getBottom()); ++y)
        for (int x = std::max(0, area.getX()); x < std::min(data.width, area.getRight()); ++x)
            if (data.getPixelColour(x, y).getBrightness() > 0.35f) return true;
    return false;
}

struct Gesture {
    float value;
    bool start, end;
};

}  // namespace

TEST_CASE("the fader's travel puts 0 dB three quarters of the way up", "[strip]") {
    CHECK(ChannelStrip::dbAtFraction(0.75f) == Catch::Approx(0.0f).margin(0.01f));
    CHECK(ChannelStrip::dbAtFraction(1.0f) == 6.0f);
    CHECK(ChannelStrip::dbAtFraction(0.0f) == -60.0f);
    CHECK(ChannelStrip::fractionForDb(-30.0f) == Catch::Approx(0.15f));
    for (float db : {-60.0f, -24.0f, -6.0f, 0.0f, 3.0f, 6.0f})
        CHECK(ChannelStrip::dbAtFraction(ChannelStrip::fractionForDb(db)) == Catch::Approx(db).margin(0.05f));
}

TEST_CASE("a strip is 112 px wide (118 compact), its rows in the mockup's order", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    CHECK(ChannelStrip::stripWidth(ChannelStrip::Size::Full) == 112);
    CHECK(ChannelStrip::stripWidth(ChannelStrip::Size::Compact) == 118);

    ChannelStrip strip;
    strip.set(partStrip(), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, ChannelStrip::minimumHeight(ChannelStrip::Size::Full));
    const auto bounds = strip.getLocalBounds();
    CHECK(strip.panArea().getWidth() == 96);  CHECK(strip.panArea().getHeight() == 18);
    CHECK(strip.panArea().getCentreX() == 56);
    CHECK(strip.muteArea().getWidth() == 30);  CHECK(strip.muteArea().getHeight() == 20);
    CHECK(strip.soloArea().getX() == strip.muteArea().getRight() + 5);
    CHECK(strip.levelBoxArea().getWidth() == 46);  CHECK(strip.peakBoxArea().getX() == strip.levelBoxArea().getRight() + 4);
    // Top to bottom: pan, M and S, the boxes, the fader zone, the name.
    CHECK(strip.panArea().getY() >= 9);
    CHECK(strip.panArea().getBottom() < strip.muteArea().getY());
    CHECK(strip.muteArea().getBottom() < strip.levelBoxArea().getY());
    CHECK(strip.levelBoxArea().getBottom() < strip.faderZoneArea().getY());
    CHECK(strip.faderZoneArea().getBottom() < strip.nameArea().getY());
    CHECK(strip.faderZoneArea().getHeight() >= 120);
    for (const auto& area : {strip.panArea(), strip.muteArea(), strip.soloArea(), strip.levelBoxArea(), strip.peakBoxArea(),
                             strip.faderZoneArea(), strip.nameArea()})
        CHECK(bounds.contains(area));
    // Inside the zone: the scale right of the cap, the meter right of the scale, the clip light above the meter.
    CHECK(strip.scaleArea().getX() >= juce::roundToInt(strip.capBounds().getRight()));
    CHECK(strip.meterArea().getX() > strip.scaleArea().getRight());
    CHECK(strip.clipLightArea().getBottom() <= strip.meterArea().getY());
    CHECK(strip.meterArea().getWidth() == 16);

    // Compact: no number boxes and no S; the pan and M stay.
    strip.set(partStrip(), ChannelStrip::Size::Compact);
    strip.setBounds(0, 0, 118, ChannelStrip::minimumHeight(ChannelStrip::Size::Compact));
    CHECK(ChannelStrip::minimumHeight(ChannelStrip::Size::Compact) < ChannelStrip::minimumHeight(ChannelStrip::Size::Full));
    CHECK(strip.levelBoxArea().isEmpty());  CHECK(strip.peakBoxArea().isEmpty());
    CHECK(strip.soloArea().isEmpty());
    CHECK_FALSE(strip.panArea().isEmpty());  CHECK_FALSE(strip.muteArea().isEmpty());
    CHECK(strip.muteArea().getCentreX() == 59);                   // M alone, centred
    CHECK(strip.faderZoneArea().getHeight() >= 120);
}

TEST_CASE("the cap's centre line, the scale and the meter share the fader's travel", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    for (const float db : {0.0f, -6.0f, 6.0f}) {
        INFO(db << " dB");
        auto s = partStrip();
        s.db = db;
        strip.set(s, ChannelStrip::Size::Full);
        auto image = snapshot(strip, 112, 560);
        const auto zone = strip.faderZoneArea();
        const float y = static_cast<float>(zone.getBottom()) - 10.0f - ChannelStrip::fractionForDb(db) * static_cast<float>(zone.getHeight() - 20);
        CHECK(strip.yForDb(db) == Catch::Approx(y));
        CHECK(strip.capBounds().getCentreY() == Catch::Approx(y));
        CHECK(strip.capBounds().getWidth() == 30.0f);  CHECK(strip.capBounds().getHeight() == 46.0f);
        const int cx = juce::roundToInt(strip.capBounds().getCentreX()), cy = static_cast<int>(y);
        CHECK(near(pixel(image, cx, cy), kCapLine, 40));          // the bright centre line marks the level
        s.changedTonight = true;
        strip.set(s, ChannelStrip::Size::Full);
        image = snapshot(strip, 112, 560);
        CHECK(near(pixel(image, cx, cy), theme::caution, 50));    // amber when changed tonight
    }

    // Each label of the scale at its mark, and none between +6 and +3.
    auto image = snapshot(strip, 112, 560);
    const auto scale = strip.scaleArea();
    for (const float mark : {6.0f, 3.0f, 0.0f, -3.0f, -6.0f, -10.0f, -15.0f, -20.0f, -30.0f, -40.0f, -60.0f}) {
        INFO("mark " << mark);
        const int y = juce::roundToInt(strip.yForDb(mark));
        CHECK(hasText(image, {scale.getX(), y - 3, scale.getWidth(), 7}));
    }
    const int between = juce::roundToInt(strip.yForDb(4.5f));
    CHECK_FALSE(hasText(image, {scale.getX(), between - 2, scale.getWidth(), 5}));
}

TEST_CASE("the meter blends to caution above -6 dB and danger above 0; holds, clips and its peak box", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    int resets = 0;
    strip.onPeakReset = [&] { ++resets; };
    strip.set(partStrip(kBass), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 560);
    CHECK(strip.peakText() == juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e"));   // "−∞": nothing yet
    const float loud = model::dbToGain(4.0f);
    strip.setMeter({loud, loud}, 0.0);
    CHECK(strip.meterDb(0) == Catch::Approx(4.0f).margin(0.01f));
    CHECK(strip.clipLit());                                       // over full scale
    CHECK(strip.peakText() == "+4.0");

    const auto image = snapshot(strip, 112, 560);
    const int x = strip.meterArea().getX() + 3;
    CHECK(near(pixel(image, x, juce::roundToInt(strip.yForDb(-20.0f))), kBass, 30));
    CHECK(near(pixel(image, x, juce::roundToInt(strip.yForDb(-1.0f))), theme::caution, 40));
    CHECK(near(pixel(image, x, juce::roundToInt(strip.yForDb(2.0f))), theme::danger, 30));
    CHECK_FALSE(near(pixel(image, x, juce::roundToInt(strip.yForDb(5.5f))), theme::danger, 60));   // above the level: empty
    CHECK(hasColour(image.getClippedImage(strip.clipLightArea()), theme::danger, 30));
    CHECK(hasColour(image.getClippedImage(strip.peakBoxArea().reduced(2)), theme::danger, 20));    // red above 0

    // Falling 1.5 dB a frame; the hold line stays about 1.5 s; the clip light 2 s.
    strip.setMeter({0.0f, 0.0f}, 33.0);
    CHECK(strip.meterDb(0) == Catch::Approx(2.5f).margin(0.01f));
    CHECK(strip.holdDb(0) == Catch::Approx(4.0f).margin(0.01f));
    strip.setMeter({0.0f, 0.0f}, 1400.0);
    CHECK(strip.holdDb(0) == Catch::Approx(4.0f).margin(0.01f));
    CHECK(strip.clipLit());
    strip.setMeter({0.0f, 0.0f}, 1600.0);
    CHECK(strip.holdDb(0) < 0.0f);
    strip.setMeter({0.0f, 0.0f}, 2100.0);
    CHECK_FALSE(strip.clipLit());
    CHECK(strip.peakText() == "+4.0");                            // the peak box keeps the loudest moment

    press(strip, strip.peakBoxArea().getCentre().toFloat());      // a click resets it
    CHECK_FALSE(strip.peakDb().has_value());
    CHECK(strip.peakText() == juce::String::fromUTF8("\xe2\x88\x92\xe2\x88\x9e"));
    CHECK(resets == 1);

    // Muted: the meter shows nothing.
    auto muted = partStrip(kBass);
    muted.muted = true;
    strip.set(muted, ChannelStrip::Size::Full);
    strip.setMeter({0.5f, 0.5f}, 3000.0);
    CHECK(strip.meterDb(0) == ChannelStrip::kMinDb);
    const auto silent = snapshot(strip, 112, 560);
    CHECK_FALSE(near(pixel(silent, x, juce::roundToInt(strip.yForDb(-40.0f))), kBass, 40));
}

TEST_CASE("M, S and the link button act on a press; S only where a solo is offered", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    std::vector<juce::String> said;
    strip.onMute = [&](bool on) { said.push_back(on ? "mute" : "unmute"); };
    strip.onSolo = [&](bool on) { said.push_back(on ? "solo" : "unsolo"); };
    strip.onLink = [&](bool on) { said.push_back(on ? "link" : "unlink"); };
    strip.onSelect = [&] { said.push_back("select"); };
    strip.set(partStrip(), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 520);
    press(strip, strip.muteArea().getCentre().toFloat());
    press(strip, strip.soloArea().getCentre().toFloat());
    CHECK(said == std::vector<juce::String>{"select", "mute", "select", "solo"});

    auto soloed = partStrip();
    soloed.soloed = true;
    strip.set(soloed, ChannelStrip::Size::Full);
    CHECK(hasColour(snapshot(strip, 112, 520).getClippedImage(strip.soloArea().reduced(3)), theme::caution, 20));

    auto silenced = partStrip();                                  // another part's solo: a hollow red M
    silenced.silencedBySolo = true;
    strip.set(silenced, ChannelStrip::Size::Full);
    auto image = snapshot(strip, 112, 520);
    CHECK(hasColour(image.getClippedImage(strip.muteArea()), theme::danger, 30));
    CHECK_FALSE(near(pixel(image, strip.muteArea().getX() + 3, strip.muteArea().getY() + 3), theme::danger, 40));

    auto noSolo = partStrip();
    noSolo.canSolo = false;
    strip.set(noSolo, ChannelStrip::Size::Full);
    CHECK(strip.soloArea().isEmpty());
    CHECK(strip.muteArea().getCentreX() == 56);

    auto noMute = noSolo;                                         // Click on "This song": no M, its place kept
    noMute.canMute = false;
    noMute.pan.reset();
    strip.set(noMute, ChannelStrip::Size::Full);
    said.clear();
    press(strip, {56.0f, static_cast<float>(strip.levelBoxArea().getY() - 15)});
    CHECK(said == std::vector<juce::String>{"select"});
    CHECK(strip.panArea().isEmpty());  CHECK(strip.panText().isEmpty());

    strip.set(outputStrip(true), ChannelStrip::Size::Full);       // an output: linked, "Follow"
    CHECK(strip.panText() == "Follow");
    CHECK(strip.panArea().getWidth() == 74);
    CHECK(strip.linkArea().getWidth() == 18);
    CHECK(strip.linkArea().getX() == strip.panArea().getRight() + 4);
    said.clear();
    press(strip, strip.linkArea().getCentre().toFloat());
    CHECK(said == std::vector<juce::String>{"select", "unlink"});
    strip.set(outputStrip(false), ChannelStrip::Size::Full);
    CHECK(strip.panText() == "L 60");
    said.clear();
    press(strip, strip.linkArea().getCentre().toFloat());
    CHECK(said == std::vector<juce::String>{"select", "link"});
    strip.set(outputStrip(true, false), ChannelStrip::Size::Full);   // Show mode: no link button
    CHECK(strip.linkArea().isEmpty());
    CHECK(strip.panArea().getWidth() == 96);
    CHECK(strip.panText() == "Follow");
}

TEST_CASE("the fader and the pan are dragged, double-clicked and wheeled as gestures", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    std::vector<Gesture> levels, pans;
    strip.onLevelDrag = [&](float db, bool start, bool end) { levels.push_back({db, start, end}); };
    strip.onPanDrag = [&](int pan, bool start, bool end) { pans.push_back({static_cast<float>(pan), start, end}); };
    auto s = partStrip();
    s.pan = 0;
    strip.set(s, ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 560);

    // A press and release without a move: a start and an end at the level as it is, nothing between.
    const auto cap = strip.capBounds().getCentre();
    press(strip, cap);
    REQUIRE(levels.size() == 2);
    CHECK((levels[0].start && !levels[0].end && levels[0].value == -3.0f));
    CHECK((!levels[1].start && levels[1].end && levels[1].value == -3.0f));
    levels.clear();
    drag(strip, cap, cap.translated(0.0f, -40.0f));               // up raises it
    REQUIRE(levels.size() >= 3);
    CHECK(levels.back().end);
    CHECK(levels.back().value > -3.0f);
    const float coarse = levels.back().value - -3.0f;
    levels.clear();
    strip.set(s, ChannelStrip::Size::Full);
    drag(strip, cap, cap.translated(0.0f, -40.0f), juce::ModifierKeys::shiftModifier);
    CHECK(levels.back().value - -3.0f < 0.5f * coarse);           // Shift: fine
    levels.clear();
    strip.mouseDoubleClick(mouseAt(strip, cap, cap, false, {}, 2));
    REQUIRE(levels.size() == 1);
    CHECK((levels[0].value == 0.0f && levels[0].start && levels[0].end));
    levels.clear();
    strip.set(s, ChannelStrip::Size::Full);
    juce::MouseWheelDetails notch{0.0f, 0.25f, false, false, false};
    strip.mouseWheelMove(mouseAt(strip, cap, cap, false), notch);
    REQUIRE(levels.size() == 1);
    CHECK((levels[0].value == -2.5f && levels[0].start && levels[0].end));

    // The pan: the bar's 96 px is the whole range of 200.
    const auto bar = strip.panArea().getCentre().toFloat();
    press(strip, bar);
    REQUIRE(pans.size() == 2);
    CHECK((pans[0].start && pans[1].end && pans[0].value == 0.0f && pans[1].value == 0.0f));
    pans.clear();
    drag(strip, bar, bar.translated(24.0f, 0.0f));
    CHECK(pans.back().end);  CHECK(pans.back().value == 50.0f);
    pans.clear();
    strip.set(s, ChannelStrip::Size::Full);
    drag(strip, bar, bar.translated(24.0f, 0.0f), juce::ModifierKeys::shiftModifier);
    CHECK(pans.back().value == Catch::Approx(12.5f).margin(1.0f));
    pans.clear();
    s.pan = -40;
    strip.set(s, ChannelStrip::Size::Full);
    strip.mouseDoubleClick(mouseAt(strip, bar, bar, false, {}, 2));
    REQUIRE(pans.size() == 1);
    CHECK((pans[0].value == 0.0f && pans[0].start && pans[0].end));
    pans.clear();
    strip.set(s, ChannelStrip::Size::Full);
    strip.mouseWheelMove(mouseAt(strip, bar, bar, false), notch);
    REQUIRE(pans.size() == 1);
    CHECK((pans[0].value == -39.0f && pans[0].start && pans[0].end));

    // A linked pan starts at the centre; a double-click on it does nothing.
    pans.clear();
    strip.set(outputStrip(true), ChannelStrip::Size::Full);
    const auto linkedBar = strip.panArea().getCentre().toFloat();
    strip.mouseDoubleClick(mouseAt(strip, linkedBar, linkedBar, false, {}, 2));
    CHECK(pans.empty());
    drag(strip, linkedBar, linkedBar.translated(-15.0f, 0.0f));
    REQUIRE(pans.size() >= 2);
    CHECK((pans.front().start && pans.front().value == 0.0f));
    CHECK(pans.back().value < 0.0f);
}

TEST_CASE("which strips have a pan bar, a link button and an S", "[strip]") {
    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    const auto& rig = *s.model().rig(s.rigId());
    auto song = makeMixerState(s, songId, {});
    REQUIRE(song.strips.size() == 5);
    CHECK_FALSE(song.strips[0].pan.has_value());  CHECK_FALSE(song.strips[1].pan.has_value());   // Click and Cues
    CHECK(song.strips[2].pan == std::optional<int>(0));
    CHECK_FALSE(song.strips[0].canSolo);  CHECK_FALSE(song.strips[1].canSolo);  CHECK(song.strips[2].canSolo);
    CHECK_FALSE(song.strips[2].linkable);

    s.setSolo(songId, song.strips[2].target.partId, true);
    song = makeMixerState(s, songId, {});
    CHECK(song.strips[2].soloed);  CHECK_FALSE(song.strips[2].silencedBySolo);
    CHECK(song.strips[3].silencedBySolo);  CHECK_FALSE(song.strips[0].silencedBySolo);

    auto output = makeMixerState(s, songId, rig.outputs[0].id);
    for (const auto& strip : output.strips) {
        CHECK_FALSE(strip.canSolo);
        CHECK((strip.linkable && strip.linked && strip.pan == std::optional<int>(0)));
    }
    s.setPan(output.strips[2].target, 30, "pan");
    output = makeMixerState(s, songId, rig.outputs[0].id);
    CHECK_FALSE(output.strips[2].linked);  CHECK(output.strips[2].pan == std::optional<int>(30));

    s.enterShow();
    song = makeMixerState(s, songId, {});
    for (const auto& strip : song.strips) CHECK_FALSE(strip.canSolo);
    output = makeMixerState(s, songId, rig.outputs[0].id);
    for (const auto& strip : output.strips) CHECK_FALSE(strip.linkable);
    s.setPan(output.strips[3].target, -20, "tonight");            // a linked role given a pan tonight: amber, and said
    output = makeMixerState(s, songId, rig.outputs[0].id);
    CHECK(output.strips[3].panChangedTonight);
    CHECK(output.strips[3].footer == "pan L 20 tonight");  CHECK(output.strips[3].footerIsWarning);
}

TEST_CASE("dragging a linked pan unlinks it in one step; a press and release changes nothing", "[strip]") {
    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    const auto& rig = *s.model().rig(s.rigId());
    ScopedLook look;
    MixerPanel panel(s);
    panel.show(songId, rig.outputs[0].id);
    snapshot(panel, 1400, 600);
    auto* keys = panel.stripAt(2);
    REQUIRE(keys != nullptr);
    const auto target = keys->strip().target;
    REQUIRE_FALSE(s.pan(target).has_value());
    const auto undoBefore = s.commands().undoName();
    const auto bar = keys->panArea().getCentre().toFloat();
    press(*keys, bar);
    CHECK_FALSE(s.pan(target).has_value());
    CHECK(s.commands().undoName() == undoBefore);
    drag(*keys, bar, bar.translated(20.0f, 0.0f));
    REQUIRE(s.pan(target).has_value());
    CHECK(*s.pan(target) > 0);
    CHECK(panel.stripAt(2)->panText() == model::formatPan(*s.pan(target)));
    s.commands().undo();                                          // the unlink and the move go back together
    CHECK_FALSE(s.pan(target).has_value());
    panel.show(songId, rig.outputs[0].id);                         // as the session's change message would

    press(*panel.stripAt(2), panel.stripAt(2)->linkArea().getCentre().toFloat());   // the link button: unlinked, at the centre
    CHECK(s.pan(target) == std::optional<int>(0));
    press(*panel.stripAt(2), panel.stripAt(2)->linkArea().getCentre().toFloat());   // and linked again
    CHECK_FALSE(s.pan(target).has_value());
}

TEST_CASE("the panel's strips show the live meters, and every peak box resets when a song starts", "[strip]") {
    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    const auto& rig = *s.model().rig(s.rigId());
    ScopedLook look;
    MixerPanel panel(s);
    panel.setVisible(true);
    panel.show(songId, {});
    snapshot(panel, 1400, 600);
    REQUIRE(panel.stripAt(2) != nullptr);
    panel.stripAt(2)->setMeter({2.0f, 2.0f}, 0.0);                // +6 dB, from before the song
    REQUIRE(panel.stripAt(2)->peakDb().has_value());

    s.playSong(songId, 0.0);
    s.advanceWithoutDevice(48000);
    d.settle();
    panel.tickMeters(100.0);                                       // the song's meters are new to it: what it held goes
    s.advanceWithoutDevice(4800);
    panel.tickMeters(133.0);
    auto* keys = panel.stripAt(2);
    CHECK(keys->meterDb(0) > -60.0f);                              // the demo's keys play at about -16 dB
    REQUIRE(keys->peakDb().has_value());
    CHECK(*keys->peakDb() < 0.0f);                                 // the +6 from before the song is gone

    panel.show(songId, rig.outputs[0].id);                         // an output: each role on its bus
    snapshot(panel, 1400, 600);
    panel.tickMeters(200.0);                                       // the first ask starts the bus's role meters
    s.advanceWithoutDevice(4800);
    panel.tickMeters(300.0);
    CHECK(panel.stripAt(2)->meterDb(0) > -60.0f);
    s.perform(app::Action::Stop);
}

TEST_CASE("the pan words sit above the marker and clear of it", "[strip]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    for (const int pan : {0, -40, 15}) {
        INFO("pan " << pan);
        auto s = partStrip(kBass);
        s.pan = pan;
        strip.set(s, ChannelStrip::Size::Full);
        const auto image = snapshot(strip, 112, 520);
        const auto inner = strip.panArea().reduced(1);
        // The marker and its glow, the bottom 5 px inside the bar: no part of the words in it.
        CHECK_FALSE(hasColour(image.getClippedImage(inner.withTop(inner.getBottom() - 5)), theme::text, 60));
        CHECK(hasColour(image.getClippedImage(inner.withTrimmedBottom(5)), theme::text, 60));   // the words, above it
    }
}

TEST_CASE("a view shown again starts its meters from nothing", "[strip]") {
    DemoSession d; auto& s = *d.session;
    const auto songId = d.setlist().entries[0].songId;
    ScopedLook look;
    MixerPanel panel(s);
    juce::Component page;                                          // a page the panel is on, as in the app
    page.addAndMakeVisible(panel);
    panel.show(songId, {});
    page.setSize(1400, 600);
    panel.setBounds(page.getLocalBounds());
    snapshot(panel, 1400, 600);
    REQUIRE(panel.stripAt(0) != nullptr);
    panel.stripAt(0)->setMeter({2.0f, 2.0f}, 0.0);                // Click over full scale, before the view was left
    REQUIRE(panel.stripAt(0)->clipLit());

    panel.setVisible(false);                                      // another page; a song plays meanwhile
    s.playSong(songId, 0.0);
    s.advanceWithoutDevice(48000);
    d.settle();
    panel.tickMeters(100.0);                                      // hidden: the timer gives it no frames
    s.advanceWithoutDevice(48000);
    d.settle();

    panel.setVisible(true);                                       // back: the first frame shows nothing from before
    panel.tickMeters(200.0);
    for (int i = 0; i < 5; ++i) {
        INFO("strip " << i);
        auto* strip = panel.stripAt(i);
        REQUIRE(strip != nullptr);
        CHECK(strip->meterDb(0) == ChannelStrip::kMinDb);
        CHECK(strip->holdDb(0) == ChannelStrip::kMinDb);
        CHECK_FALSE(strip->clipLit());
        CHECK_FALSE(strip->peakDb().has_value());
    }
    s.advanceWithoutDevice(4800);                                 // and the next frames show the music
    panel.tickMeters(233.0);
    CHECK(panel.stripAt(2)->meterDb(0) > -60.0f);
    s.perform(app::Action::Stop);
}
