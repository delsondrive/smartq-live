#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Effects.h"
#include "tests/support/Pixels.h"
#include "ui/ChannelStrip.h"
#include "ui/StripEffects.h"
#include "ui/Theme.h"

// The Effects box on a mixer strip (docs/mockups/effects.html, .rack): its slots, what a click, a dot and a drag on them
// do, and how it gives way on a short strip. The strip's other rows are ChannelStripTest's.

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

namespace {

const juce::Colour kKeys{0xffff9a68};

MixerState::Strip partStrip(juce::Colour colour = kKeys) {
    MixerState::Strip s;
    s.target = {app::LevelTarget::Kind::Part, "song", "part", {}, {}};
    s.name = "Keys";
    s.colour = colour;
    s.db = -3.0f;
    s.value = "-3.0";
    s.canMute = true;
    s.pan = -40;
    s.canSolo = true;
    return s;
}

// A mouse event on the strip at `at`, the gesture having started at `down`.
juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool dragged) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0, 0, 0, 0, &c, &c, now, down, now, 1,
                            dragged);
}

void press(juce::Component& c, juce::Point<float> at) {
    c.mouseDown(mouseAt(c, at, at, false));
    c.mouseUp(mouseAt(c, at, at, false));
}

void drag(juce::Component& c, juce::Point<float> from, juce::Point<float> to) {
    c.mouseDown(mouseAt(c, from, from, false));
    c.mouseDrag(mouseAt(c, (from + to) * 0.5f, from, true));
    c.mouseDrag(mouseAt(c, to, from, true));
    c.mouseUp(mouseAt(c, to, from, true));
}

juce::Colour pixel(const juce::Image& image, int x, int y) {
    const juce::Image::BitmapData data(image, juce::Image::BitmapData::readOnly);
    return data.getPixelColour(x, y);
}

using Slot = MixerState::EffectSlot;
using Row = stripeffects::Row;

MixerState::Strip withEffects(std::vector<Slot> slots) {
    auto s = partStrip();
    s.effects = std::move(slots);
    s.effectsTarget = {"song", "part", {}, {}};
    return s;
}

Slot slot(const juce::String& kind, bool on = true, float reductionDb = 0.0f) { return {kind, model::effectName(kind), on, reductionDb}; }

const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");

}  // namespace

TEST_CASE("a part's Effects box sits above the pan: its slots, each kind's bar, a grey dot for one switched off", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    strip.set(withEffects({slot("eq"), slot("dynamic", false)}), ChannelStrip::Size::Full);
    const auto image = snapshot(strip, 112, 620);
    const auto box = strip.effectsArea();
    REQUIRE_FALSE(box.isEmpty());
    CHECK(box.getY() == 10);                                      // under the 4 px colour bar, in the strip's padding
    CHECK(box.getBottom() + 7 == strip.panArea().getY());          // then the pan, 7 px under it
    CHECK(box.getWidth() == 96);                                  // the strip inside its border and 7 px padding
    CHECK(strip.slotsShown() == 4);
    CHECK(box.getHeight() == stripeffects::boxHeight(4));
    CHECK(strip.faderZoneArea().getHeight() >= 120);
    REQUIRE(strip.slotRows().size() == 4);
    CHECK(strip.slotRows()[0] == Row{Row::Kind::Effect, 0, 0});
    CHECK(strip.slotRows()[1] == Row{Row::Kind::Effect, 1, 0});
    CHECK(strip.slotRows()[2] == Row{Row::Kind::Add, 2, 0});     // the first empty slot: "+"
    CHECK(strip.slotRows()[3] == Row{Row::Kind::Empty, 0, 0});
    for (int row = 0; row < 4; ++row) {
        CHECK(strip.slotArea(row).getHeight() == 20);
        if (row > 0) CHECK(strip.slotArea(row).getY() == strip.slotArea(row - 1).getBottom() + 3);
        CHECK(box.contains(strip.slotArea(row)));
    }
    CHECK(strip.slotText(0) == "EQ");  CHECK(strip.slotText(1) == "Dynamic");  CHECK(strip.slotText(2) == "+");
    CHECK(strip.slotText(3) == "");
    // Each filled slot's 3 px bar in its kind's colour; the first's dot mint, the second's (off) grey.
    const auto bar = [&](int row) { return pixel(image, strip.slotArea(row).getX() + 1, strip.slotArea(row).getCentreY()); };
    CHECK(near(bar(0), theme::effectColour("eq"), 30));
    CHECK(near(bar(1), theme::effectColour("dynamic"), 30));
    const auto dot = [&](int row) { const auto c = strip.powerArea(row).getCentre(); return pixel(image, c.x, c.y); };
    CHECK(near(dot(0), theme::mint, 40));
    CHECK(near(dot(1), juce::Colour(0xff4a4350), 20));
    CHECK(hasColour(image.getClippedImage(box.withHeight(stripeffects::kLabelHeight + 6)), theme::mint, 30));   // the count, "2"

    // The selected strip's first slot is outlined in mint.
    strip.setSelected(true);
    const auto selected = snapshot(strip, 112, 620);
    const auto first = strip.slotArea(0);
    CHECK(near(pixel(selected, first.getCentreX(), first.getY()), theme::mint, 60));
}

TEST_CASE("Click, Cues and the compact strips have no box; full strips keep its row, so the rows stay aligned", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip part, click, plain;
    part.set(withEffects({slot("eq")}), ChannelStrip::Size::Full);
    auto c = partStrip(theme::ember);
    c.name = "Click";  c.pan.reset();  c.canMute = false;  c.canSolo = false;
    click.set(c, ChannelStrip::Size::Full);
    click.setEffectsRow(true);                                    // a row of strips with boxes: it keeps the box's room
    for (auto* s : {&part, &click}) s->setBounds(0, 0, 112, 620);
    CHECK(click.effectsArea().isEmpty());
    CHECK(click.slotRows().empty());
    CHECK(click.levelBoxArea().getY() == part.levelBoxArea().getY());
    CHECK(click.faderZoneArea() == part.faderZoneArea());
    CHECK(click.nameArea() == part.nameArea());

    // A strip in a row without boxes (Show mode) keeps none: its pan at the top, as before.
    plain.set(partStrip(), ChannelStrip::Size::Full);
    plain.setBounds(0, 0, 112, 620);
    CHECK(plain.effectsArea().isEmpty());
    CHECK(plain.panArea().getY() == 10);
    CHECK(plain.faderZoneArea().getHeight() > part.faderZoneArea().getHeight());

    // Compact (the main view): no box, even with effects, and the pan where a compact strip has it.
    ChannelStrip compact, compactPlain;
    compact.set(withEffects({slot("eq")}), ChannelStrip::Size::Compact);
    compact.setEffectsRow(true);
    compactPlain.set(partStrip(), ChannelStrip::Size::Compact);
    for (auto* s : {&compact, &compactPlain}) s->setBounds(0, 0, 118, 400);
    CHECK(compact.effectsArea().isEmpty());
    CHECK(compact.panArea() == compactPlain.panArea());
    CHECK(compact.faderZoneArea() == compactPlain.faderZoneArea());
}

TEST_CASE("a click on a slot opens it, on its dot switches it; a drag reorders, one move on release", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    std::vector<int> opened, selects;
    std::vector<std::pair<int, bool>> switched;
    std::vector<std::pair<int, int>> moved;
    strip.onEffectOpen = [&](int i) { opened.push_back(i); };
    strip.onEffectSwitch = [&](int i, bool on) { switched.emplace_back(i, on); };
    strip.onEffectMove = [&](int from, int to) { moved.emplace_back(from, to); };
    strip.onSelect = [&] { selects.push_back(1); };
    strip.set(withEffects({slot("eq"), slot("dynamic", false), slot("deess")}), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 620);

    press(strip, strip.slotArea(1).getCentre().toFloat().translated(10.0f, 0.0f));   // the name: open it
    CHECK(opened == std::vector<int>{1});
    CHECK(switched.empty());
    press(strip, strip.powerArea(1).getCentre().toFloat());      // the dot: switch it (on, as it is off)
    CHECK(switched == std::vector<std::pair<int, bool>>{{1, true}});
    press(strip, strip.powerArea(0).getCentre().toFloat());
    CHECK(switched.back() == std::pair<int, bool>{0, false});
    CHECK(opened.size() == 1);
    press(strip, strip.slotArea(3).getCentre().toFloat());       // "+": the first empty slot
    CHECK(opened.back() == 3);
    CHECK(selects.size() == 4);                                   // every press selects the strip first

    // A drag from the first slot to below the second: one move, on release, and nothing opened.
    const auto from = strip.slotArea(0).getCentre().toFloat();
    drag(strip, from, strip.slotArea(1).getCentre().toFloat().translated(0.0f, 4.0f));
    CHECK(moved == std::vector<std::pair<int, int>>{{0, 1}});
    CHECK(opened.size() == 2);
    // Up to the top from the third: to 0. Let go where it was: no move, and no open either.
    drag(strip, strip.slotArea(2).getCentre().toFloat(), strip.slotArea(0).getPosition().toFloat().translated(30.0f, 1.0f));
    CHECK(moved.back() == std::pair<int, int>{2, 0});
    drag(strip, strip.slotArea(1).getCentre().toFloat(), strip.slotArea(1).getCentre().toFloat().translated(30.0f, 6.0f));
    CHECK(moved.size() == 2);
    CHECK(opened.size() == 2);
}

TEST_CASE("the reduction shown on a slot follows the effect's", "[strip][strip-fx]") {
    CHECK(stripeffects::reductionText(3.2f) == kMinus + "3");
    CHECK(stripeffects::reductionText(0.6f) == kMinus + "1");
    CHECK(stripeffects::reductionText(0.2f) == "");
    CHECK(stripeffects::reductionText(0.0f) == "");
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    strip.set(withEffects({slot("eq"), slot("dynamic", true, 3.0f), slot("deess", true, 0.0f)}), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 620);
    CHECK(strip.reductionText(0) == "");
    CHECK(strip.reductionText(1) == kMinus + "3");
    CHECK(strip.reductionText(2) == "");
    const auto before = snapshot(strip, 112, 620);
    strip.set(withEffects({slot("eq"), slot("dynamic", true, 5.0f), slot("deess", true, 0.0f)}), ChannelStrip::Size::Full);
    CHECK(strip.reductionText(1) == kMinus + "5");
    const auto after = snapshot(strip, 112, 620);
    const auto right = strip.slotArea(1).withTrimmedLeft(70);     // where the reading is drawn: other words
    bool differs = false;
    for (int y = right.getY(); y < right.getBottom(); ++y)
        for (int x = right.getX(); x < right.getRight(); ++x) differs = differs || pixel(before, x, y) != pixel(after, x, y);
    CHECK(differs);
}

TEST_CASE("a short strip shows fewer slots and says how many more, then one slim line, and at its smallest none", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    std::vector<int> opened;
    ChannelStrip strip;
    strip.onEffectOpen = [&](int i) { opened.push_back(i); };
    strip.set(withEffects({slot("eq"), slot("dynamic"), slot("transient")}), ChannelStrip::Size::Full);

    // The fader keeps its 120 px whatever the height: the box gives way.
    const int smallest = ChannelStrip::minimumHeight(ChannelStrip::Size::Full);
    for (int h = smallest; h <= 620; h += 7) {
        strip.setBounds(0, 0, 112, h);
        INFO("height " << h);
        CHECK(strip.faderZoneArea().getHeight() >= 120);
        CHECK(strip.getLocalBounds().contains(strip.effectsArea()));
    }
    strip.setBounds(0, 0, 112, 620);
    CHECK(strip.slotsShown() == 4);

    // Two rows: the first effect, then "+2 more", which opens the first effect not shown.
    const int twoRows = smallest + stripeffects::boxHeight(2) + 7;
    strip.setBounds(0, 0, 112, twoRows);
    CHECK(strip.slotsShown() == 2);
    CHECK(strip.faderZoneArea().getHeight() >= 120);
    REQUIRE(strip.slotRows().size() == 2);
    CHECK(strip.slotRows()[0] == Row{Row::Kind::Effect, 0, 0});
    CHECK(strip.slotRows()[1] == Row{Row::Kind::More, 1, 2});
    CHECK(strip.slotText(1) == "+2 more");
    press(strip, strip.slotArea(1).getCentre().toFloat());
    CHECK(opened == std::vector<int>{1});

    // Shorter: one slim line, the fader still 120 px. A click opens the first effect.
    const int slim = smallest + stripeffects::kSlimHeight + 7;
    strip.setBounds(0, 0, 112, slim);
    CHECK(strip.slotsShown() == 0);
    CHECK(strip.effectsArea().getHeight() == stripeffects::kSlimHeight);
    CHECK(strip.faderZoneArea().getHeight() >= 120);
    const auto image = snapshot(strip, 112, slim);
    CHECK(hasColour(image.getClippedImage(strip.effectsArea()), theme::effectColour("transient"), 30));   // a bar per effect
    press(strip, strip.effectsArea().getCentre().toFloat());
    CHECK(opened.back() == 0);
    strip.set(withEffects({}), ChannelStrip::Size::Full);         // none: a click there adds one
    press(strip, strip.effectsArea().getCentre().toFloat());
    CHECK(opened.back() == 0);
    CHECK(opened.size() == 3);

    // The smallest (the Song editor's dock at the smallest window): no box, nor its room; the pan at the top.
    strip.setBounds(0, 0, 112, smallest);
    CHECK(strip.effectsArea().isEmpty());
    CHECK(strip.panArea().getY() == 10);
    CHECK(strip.faderZoneArea().getHeight() >= 120);

    // The rows for every chain in every box.
    using K = Row::Kind;
    CHECK(stripeffects::rowsFor(4, 4) == std::vector<Row>{{K::Effect, 0, 0}, {K::Effect, 1, 0}, {K::Effect, 2, 0}, {K::Effect, 3, 0}});
    CHECK(stripeffects::rowsFor(0, 4) == std::vector<Row>{{K::Add, 0, 0}, {}, {}, {}});
    CHECK(stripeffects::rowsFor(3, 3) == std::vector<Row>{{K::Effect, 0, 0}, {K::Effect, 1, 0}, {K::Effect, 2, 0}});
    CHECK(stripeffects::rowsFor(4, 3) == std::vector<Row>{{K::Effect, 0, 0}, {K::Effect, 1, 0}, {K::More, 2, 2}});
    CHECK(stripeffects::rowsFor(2, 1) == std::vector<Row>{{K::More, 0, 2}});
    CHECK(stripeffects::rowsFor(1, 1) == std::vector<Row>{{K::Effect, 0, 0}});
}

TEST_CASE("a slot let go outside its box, or whose rows went from under it, moves nothing", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    std::vector<int> opened;
    std::vector<std::pair<int, int>> moved;
    strip.onEffectOpen = [&](int i) { opened.push_back(i); };
    strip.onEffectMove = [&](int from, int to) { moved.emplace_back(from, to); };
    strip.set(withEffects({slot("eq"), slot("dynamic"), slot("deess")}), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 620);
    const auto from = strip.slotArea(0).getCentre().toFloat();
    // Below the strip, above it, and over the next strip's box (6 px to its right): no move, and no open.
    for (const auto to : {juce::Point<float>(from.x, 660.0f), juce::Point<float>(from.x, -30.0f),
                          juce::Point<float>(112.0f + 6.0f + 40.0f, static_cast<float>(strip.slotArea(2).getCentreY()))}) {
        INFO("let go at " << to.x << ", " << to.y);
        strip.mouseDown(mouseAt(strip, from, from, false));
        strip.mouseDrag(mouseAt(strip, strip.slotArea(2).getCentre().toFloat(), from, true));   // over the third: a line
        const auto gap = strip.slotArea(1).withY(strip.slotArea(1).getBottom()).withHeight(3);
        CHECK(hasColour(snapshot(strip, 112, 620).getClippedImage(gap), theme::mint, 20));
        strip.mouseDrag(mouseAt(strip, to, from, true));                                      // then out: none
        const auto image = snapshot(strip, 112, 620);
        CHECK_FALSE(hasColour(image.getClippedImage(gap), theme::mint, 20));   // no insertion line
        strip.mouseUp(mouseAt(strip, to, from, true));
    }
    CHECK(moved.empty());
    CHECK(opened.empty());

    // The strip made shorter mid-drag (fewer rows than the one carried): the drag is let go, nothing sent.
    const auto third = strip.slotArea(2).getCentre().toFloat();
    strip.mouseDown(mouseAt(strip, third, third, false));
    strip.mouseDrag(mouseAt(strip, from, third, true));
    strip.setBounds(0, 0, 112, ChannelStrip::minimumHeight(ChannelStrip::Size::Full) + stripeffects::boxHeight(1) + 7);
    REQUIRE(strip.slotsShown() == 1);
    strip.mouseDrag(mouseAt(strip, from.translated(0.0f, 2.0f), third, true));
    strip.mouseUp(mouseAt(strip, from, third, true));
    CHECK(moved.empty());
    CHECK(opened.empty());

    // The chain changed under it (an undo took an effect away): let go, nothing sent.
    strip.setBounds(0, 0, 112, 620);
    strip.mouseDown(mouseAt(strip, third, third, false));
    strip.mouseDrag(mouseAt(strip, from, third, true));
    strip.set(withEffects({slot("eq"), slot("dynamic")}), ChannelStrip::Size::Full);
    strip.mouseDrag(mouseAt(strip, from.translated(0.0f, 2.0f), third, true));
    strip.mouseUp(mouseAt(strip, from, third, true));
    CHECK(moved.empty());
    CHECK(opened.empty());
}

TEST_CASE("a lifted slot whose strip was laid out again, or whose chain was reordered, is let go with nothing sent", "[strip][strip-fx]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedLook look;
    ChannelStrip strip;
    std::vector<int> opened;
    std::vector<std::pair<int, int>> moved;
    strip.onEffectOpen = [&](int i) { opened.push_back(i); };
    strip.onEffectMove = [&](int from, int to) { moved.emplace_back(from, to); };
    strip.set(withEffects({slot("eq"), slot("dynamic"), slot("deess")}), ChannelStrip::Size::Full);
    strip.setBounds(0, 0, 112, 620);
    const auto top = strip.slotArea(0).getPosition().toFloat().translated(30.0f, 1.0f);

    // The second slot lifted over the first, then the strip made shorter (its row now "+2 more"), then let go at once.
    const auto second = strip.slotArea(1).getCentre().toFloat();
    strip.mouseDown(mouseAt(strip, second, second, false));
    strip.mouseDrag(mouseAt(strip, top, second, true));
    strip.setBounds(0, 0, 112, ChannelStrip::minimumHeight(ChannelStrip::Size::Full) + stripeffects::boxHeight(2) + 7);
    REQUIRE(strip.slotRows().size() == 2);
    REQUIRE(strip.slotRows()[1].kind == Row::Kind::More);
    strip.mouseUp(mouseAt(strip, top, second, true));
    CHECK(moved.empty());
    CHECK(opened.empty());

    // The third lifted over the first, then the chain reordered under it (an undo): the slot carried is another effect now.
    strip.setBounds(0, 0, 112, 620);
    const auto third = strip.slotArea(2).getCentre().toFloat();
    strip.mouseDown(mouseAt(strip, third, third, false));
    strip.mouseDrag(mouseAt(strip, top, third, true));
    strip.set(withEffects({slot("deess"), slot("eq"), slot("dynamic")}), ChannelStrip::Size::Full);
    strip.mouseUp(mouseAt(strip, top, third, true));
    CHECK(moved.empty());
    CHECK(opened.empty());

    // A switch or a reading changing meanwhile is not a new chain: the move still goes.
    strip.mouseDown(mouseAt(strip, third, third, false));
    strip.mouseDrag(mouseAt(strip, top, third, true));
    strip.set(withEffects({slot("deess", false), slot("eq", true, 2.0f), slot("dynamic")}), ChannelStrip::Size::Full);
    strip.mouseUp(mouseAt(strip, top, third, true));
    CHECK(moved == std::vector<std::pair<int, int>>{{2, 0}});
}
