#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/EffectChain.h"
#include "model/Effects.h"
#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/EffectsChainColumn.h"
#include "ui/EffectsWindow.h"
#include "ui/fx/ChainParams.h"
#include "ui/fx/DynamicPage.h"
#include "ui/fx/EqPage.h"
#include "ui/fx/params/Parameters.h"

// The effects window: one part's (or output's) chain, Live's own chain column on the left, the shown effect's page from
// Smart Q on the right, a header that names the place and moves to its neighbours, and a footer that says whether the
// song is prepared for the show. Every change made in it is one command.

using namespace sqlive;
using sqlive::test::DemoSession;
using sqlive::ui::EffectsWindow;

namespace {

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

juce::MouseEvent eventOn(juce::Component& c, juce::Point<float> at, juce::Point<float> down, bool buttonDown) {
    const auto now = juce::Time::getCurrentTime();
    const auto mods = buttonDown ? juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                            0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down, now, 1, false);
}

// A press and a release at the component's middle, as a click.
void click(juce::Component& c) {
    const auto at = c.getLocalBounds().getCentre().toFloat();
    c.mouseDown(eventOn(c, at, at, true));
    c.mouseUp(eventOn(c, at, at, false));
}

// A button's click, as the button posts it.
void press(juce::Button& b) {
    b.triggerClick();
    pump();
}

// A card carried from its middle to y in its own coordinates, and let go there.
void drag(juce::Component& card, float toY) {
    const auto from = card.getLocalBounds().getCentre().toFloat();
    card.mouseDown(eventOn(card, from, from, true));
    for (int step = 1; step <= 4; ++step) {
        const juce::Point<float> at{from.x, from.y + (toY - from.y) * (float) step / 4.0f};
        card.mouseDrag(eventOn(card, at, from, true));
    }
    card.mouseUp(eventOn(card, {from.x, toY}, from, false));
}

juce::StringArray kindsOf(const std::vector<model::EffectDoc>& effects) {
    juce::StringArray kinds;
    for (const auto& e : effects) kinds.add(e.kind);
    return kinds;
}

// A demo session, the first song's Keys and Bass (mono parts), the rig's first output (stereo), and a window over 1280 x 700.
struct Fixture {
    DemoSession d;
    sqlive::test::ScopedLook look;
    app::Session& s = *d.session;
    model::SongDoc song = *s.model().song(d.setlist().entries[0].songId);
    app::EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
    app::EffectsTarget bass{song.id, song.parts[1].id, {}, {}};
    app::EffectsTarget out{{}, {}, s.rigId(), s.model().rig(s.rigId())->outputs[0].id};
    EffectsWindow window{s};

    Fixture() {
        REQUIRE(song.parts[0].name == "Keys");
        REQUIRE(song.parts[1].name == "Bass");
        window.setBounds(0, 0, 1280, 700);
    }
};

}  // namespace

TEST_CASE("the effects window opens on a part's effect and shows its page", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    REQUIRE(f.s.addEffect(f.keys, "dynamic"));
    pump();
    CHECK_FALSE(f.window.isOpen());

    f.window.show(f.keys, 1);
    REQUIRE(f.window.isOpen());
    CHECK(f.window.isVisible());
    CHECK(f.window.shownIndex() == 1);
    CHECK(dynamic_cast<smartq::DynamicPage*>(f.window.page()) != nullptr);
    CHECK(f.window.title() == "Keys");
    CHECK(f.window.subtitle()
          == juce::String::fromUTF8("Effects in \xe2\x80\x9c" "First Light\xe2\x80\x9d \xc2\xb7 heard on every output Keys goes to"));
    auto& column = f.window.column();
    REQUIRE(column.cardCount() == 2);
    CHECK(column.card(0).kind() == "eq");
    CHECK(column.card(1).kind() == "dynamic");
    CHECK_FALSE(column.card(0).isSelected());
    CHECK(column.card(1).isSelected());
    CHECK(column.card(0).miniature() != nullptr);
    CHECK(column.slotsText() == "2 of 4 slots used.");
    CHECK_FALSE(column.addMenuOpen());
    CHECK(f.window.footer().note == "Every change is one undo step");

    // The panel as the mockup's #chain: centred, the chain column 280 wide on its left.
    const auto panel = f.window.panel();
    CHECK(std::abs(panel.getCentreX() - 640) <= 1);
    CHECK(column.getWidth() == ui::EffectsChainColumn::width);
    CHECK(panel.contains(column.getBounds()));
    CHECK(sqlive::test::childrenInside(f.window));
    f.window.step(1.0 / 30.0);
    const auto image = f.window.createComponentSnapshot(f.window.getLocalBounds());
    CHECK(sqlive::test::hasColour(image, theme::mint));                // the shown card's outline

    // A click on a card shows its page; -1 opens the add menu over the first effect's page.
    click(column.card(0));
    CHECK(f.window.shownIndex() == 0);
    CHECK(dynamic_cast<smartq::EqPage*>(f.window.page()) != nullptr);
    CHECK(column.card(0).isSelected());
    f.window.show(f.keys, -1);
    CHECK(column.addMenuOpen());
    CHECK(f.window.shownIndex() == 0);

    // A part with no effects: the add menu, and no page.
    f.window.show(f.bass, -1);
    CHECK(f.window.title() == "Bass");
    CHECK(f.window.column().cardCount() == 0);
    CHECK(f.window.page() == nullptr);
    CHECK(f.window.shownIndex() == -1);
    CHECK(f.window.column().addMenuOpen());
    CHECK(f.window.column().slotsText() == "0 of 4 slots used.");

    f.window.close();
    CHECK_FALSE(f.window.isOpen());
    CHECK_FALSE(f.window.isVisible());
    CHECK(f.window.page() == nullptr);
}

TEST_CASE("the add menu offers only what the place can take", "[fx-window]") {
    Fixture f;
    const auto offered = [&] {
        juce::StringArray kinds;
        auto* menu = f.window.column().addMenu();
        REQUIRE(menu != nullptr);
        for (int i = 0; i < menu->itemCount(); ++i) kinds.add(menu->item(i).choice().kind);
        return kinds;
    };

    // A mono part: no Width; every one can be added, with the mockup's hints.
    f.window.show(f.keys, -1);
    CHECK(offered() == juce::StringArray{"eq", "dynamic", "deess", "transient"});
    auto* menu = f.window.column().addMenu();
    CHECK(menu->item(0).choice().hint == "tone");
    CHECK(menu->item(1).choice().hint == "evens out");
    CHECK(menu->item(2).choice().hint == juce::String::fromUTF8("harsh \xe2\x80\x9cs\xe2\x80\x9d"));
    CHECK(menu->item(3).choice().hint == "attack, tail");
    for (int i = 0; i < menu->itemCount(); ++i) {
        CHECK_FALSE(menu->item(i).isRefused());
        CHECK(menu->item(i).getTooltip().isEmpty());
    }

    // An output: Limit, and Width (it is stereo); Limit greyed once there is one, with the command's own sentence.
    f.window.show(f.out, -1);
    CHECK(f.window.subtitle() == juce::String::fromUTF8("Effects on ") + f.s.model().rig(f.s.rigId())->outputs[0].name
                                     + juce::String::fromUTF8(" \xc2\xb7 every song"));
    CHECK(offered() == juce::StringArray{"eq", "dynamic", "limit", "width"});
    CHECK(f.window.column().addMenu()->item(2).choice().hint == "ceiling");
    click(f.window.column().addMenu()->item(2));
    pump();
    CHECK(kindsOf(f.s.effects(f.out)) == juce::StringArray{"limit"});
    CHECK_FALSE(f.window.column().addMenuOpen());                      // a choice closes it
    f.window.column().openAddMenu();
    menu = f.window.column().addMenu();
    REQUIRE(menu != nullptr);
    CHECK(menu->item(2).isRefused());
    CHECK(menu->item(2).getTooltip() == "An output holds one Limit.");
    CHECK_FALSE(menu->item(0).isRefused());
    click(menu->item(2));                                              // greyed: nothing happens
    pump();
    CHECK(kindsOf(f.s.effects(f.out)) == juce::StringArray{"limit"});

    // A full chain: everything greyed, with the chain's own sentence.
    for (const char* kind : {"eq", "dynamic", "width"}) REQUIRE(f.s.addEffect(f.out, kind));
    pump();
    f.window.column().openAddMenu();
    menu = f.window.column().addMenu();
    REQUIRE(menu != nullptr);
    for (int i = 0; i < menu->itemCount(); ++i) {
        CHECK(menu->item(i).isRefused());
        CHECK(menu->item(i).getTooltip() == "A chain holds up to 4 effects.");
    }
    CHECK(f.window.column().slotsText() == "4 of 4 slots used.");
}

TEST_CASE("adding, removing, reordering and switching in the window are each one command", "[fx-window]") {
    Fixture f;
    f.s.commands().clearHistory();
    auto& column = f.window.column();
    f.window.show(f.keys, -1);

    // Add: the menu's choice, shown at once.
    click(column.addMenu()->item(0));                                  // EQ
    pump();
    CHECK(kindsOf(f.s.effects(f.keys)) == juce::StringArray{"eq"});
    CHECK(f.s.commands().undoWords() == "add EQ on Keys");
    CHECK(f.window.shownIndex() == 0);
    column.openAddMenu();
    click(column.addMenu()->item(1));                                  // Dynamic
    pump();
    CHECK(kindsOf(f.s.effects(f.keys)) == juce::StringArray{"eq", "dynamic"});
    CHECK(f.s.commands().undoWords() == "add Dynamic on Keys");
    REQUIRE(column.cardCount() == 2);
    CHECK(f.window.shownIndex() == 1);                                 // the one just added
    CHECK(dynamic_cast<smartq::DynamicPage*>(f.window.page()) != nullptr);

    // Reorder: the Dynamic carried above the EQ, and its page still shown.
    drag(column.card(1), -(float) column.card(1).getY() + 4.0f);
    pump();
    CHECK(kindsOf(f.s.effects(f.keys)) == juce::StringArray{"dynamic", "eq"});
    CHECK(f.s.commands().undoWords() == "move Dynamic on Keys");
    REQUIRE(column.cardCount() == 2);
    CHECK(column.card(0).kind() == "dynamic");
    CHECK(f.window.shownIndex() == 0);
    CHECK(dynamic_cast<smartq::DynamicPage*>(f.window.page()) != nullptr);
    // A card let go where it was moves nothing.
    drag(column.card(0), (float) column.card(0).getHeight() / 2.0f + 2.0f);
    pump();
    CHECK(f.s.commands().undoWords() == "move Dynamic on Keys");

    // Power: switched off and on, the card following.
    press(column.card(1).powerButton());
    pump();
    CHECK_FALSE(f.s.effects(f.keys)[1].on);
    CHECK(f.s.commands().undoWords() == "switch off EQ on Keys");
    CHECK_FALSE(column.card(1).isOn());
    press(column.card(1).powerButton());
    pump();
    CHECK(f.s.effects(f.keys)[1].on);
    CHECK(f.s.commands().undoWords() == "switch on EQ on Keys");

    // Remove: the card goes, and the page shown is the one left.
    press(column.card(0).removeButton());
    pump();
    CHECK(kindsOf(f.s.effects(f.keys)) == juce::StringArray{"eq"});
    CHECK(f.s.commands().undoWords() == "remove Dynamic on Keys");
    REQUIRE(column.cardCount() == 1);
    CHECK(f.window.shownIndex() == 0);
    CHECK(dynamic_cast<smartq::EqPage*>(f.window.page()) != nullptr);

    // Undo brings the Dynamic back, and the window follows; every step above was one.
    f.s.commands().undo();
    pump();
    CHECK(column.cardCount() == 2);
    for (int i = 0; i < 5; ++i) {
        INFO(i);
        REQUIRE(f.s.commands().canUndo());
        f.s.commands().undo();
        pump();
    }
    CHECK_FALSE(f.s.commands().canUndo());
    CHECK(f.s.effects(f.keys).empty());
    CHECK(column.cardCount() == 0);
    CHECK(f.window.page() == nullptr);
}

TEST_CASE("a page that goes mid-gesture leaves no gesture open", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "dynamic"));
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    f.s.commands().clearHistory();
    f.window.show(f.keys, 0);
    auto* params = f.window.params();
    REQUIRE(params != nullptr);
    auto* threshold = params->apvts().getParameter(smartq::moduleParamId(params->slotOf(0), "threshold"));
    auto* gain = params->apvts().getParameter(smartq::moduleParamId(params->slotOf(1), "output_gain"));
    REQUIRE(threshold != nullptr);
    REQUIRE(gain != nullptr);

    threshold->beginChangeGesture();                                   // a drag on the Dynamic's page, never let go:
    threshold->setValueNotifyingHost(threshold->convertTo0to1(-20.0f));
    click(f.window.column().card(1));                                  // that page goes
    REQUIRE(f.window.shownIndex() == 1);
    gain->setValueNotifyingHost(gain->convertTo0to1(3.0f));
    gain->setValueNotifyingHost(gain->convertTo0to1(6.0f));
    pump();
    CHECK((double) f.s.effects(f.keys)[1].values["output_gain"] == Catch::Approx(6.0).margin(1.0e-3));

    f.s.commands().undo();                                             // the second change alone
    pump();
    CHECK((double) f.s.effects(f.keys)[1].values["output_gain"] == Catch::Approx(3.0).margin(1.0e-3));
    CHECK((double) f.s.effects(f.keys)[0].values["threshold"] == Catch::Approx(-20.0).margin(1.0e-3));
}

TEST_CASE("the effects window moves to the neighbouring part", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.bass, "eq"));
    pump();
    std::vector<app::EffectsTarget> told;
    f.window.onTargetChanged = [&](const app::EffectsTarget& t) { told.push_back(t); };

    f.window.show(f.keys, -1);
    CHECK_FALSE(f.window.previousButton().isVisible());                // Keys is the song's first part
    REQUIRE(f.window.nextButton().isVisible());
    CHECK(f.window.nextButton().getButtonText() == juce::String::fromUTF8("Bass \xe2\x80\xba"));

    press(f.window.nextButton());
    CHECK(f.window.target().partId == f.bass.partId);
    CHECK(f.window.title() == "Bass");
    CHECK(f.window.shownIndex() == 0);                                 // its first effect
    CHECK_FALSE(f.window.column().addMenuOpen());
    REQUIRE(told.size() == 1);
    CHECK(told[0].partId == f.bass.partId);
    CHECK(f.window.previousButton().getButtonText() == juce::String::fromUTF8("\xe2\x80\xb9 Keys"));
    CHECK(f.window.nextButton().getButtonText() == juce::String::fromUTF8("Perc \xe2\x80\xba"));

    press(f.window.previousButton());
    CHECK(f.window.target().partId == f.keys.partId);
    CHECK(told.size() == 2);
}

TEST_CASE("holding the button hears the chain without its effects", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    f.s.playSong(f.song.id, 0.0);
    f.s.advanceWithoutDevice(4800);
    const auto* chain = f.s.liveChain(f.keys);
    REQUIRE(chain != nullptr);
    f.window.show(f.keys, 0);

    auto& hold = f.window.holdButton();
    const auto at = hold.getLocalBounds().getCentre().toFloat();
    hold.mouseDown(eventOn(hold, at, at, true));
    CHECK(chain->heldWithoutEffects());
    hold.mouseUp(eventOn(hold, at, at, false));
    CHECK_FALSE(chain->heldWithoutEffects());

    hold.mouseDown(eventOn(hold, at, at, true));                       // closing the window lets go too
    CHECK(chain->heldWithoutEffects());
    f.window.close();
    CHECK_FALSE(chain->heldWithoutEffects());
}

TEST_CASE("the effects window's footer says whether the song is prepared for the show", "[fx-window]") {
    Fixture f;
    f.s.preparer().setPaused(true);
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    pump();
    f.window.show(f.keys, 0);
    CHECK(f.window.footer().state == juce::String::fromUTF8("Changed \xc2\xb7 First Light will be prepared again for the show"));
    CHECK(f.window.footer().stateColour == theme::caution);
    CHECK(f.window.footer().delay.isEmpty());
    CHECK(f.window.footer().note == "Every change is one undo step");

    f.s.preparer().setPaused(false);
    f.s.prepareForShow();
    for (int i = 0; i < 500 && f.s.preparingForShow(); ++i) pump();
    REQUIRE(f.s.preparer().waitUntilIdle(20000));
    pump();
    REQUIRE(f.s.effectsState(f.song.id) == app::EffectsPreparer::State::Prepared);
    f.window.step(1.0);                                                // the footer reads again twice a second
    CHECK(f.window.footer().state == "Prepared");
    CHECK(f.window.footer().stateColour == theme::mint);

    // An output's chain: no song to prepare; with a Limit, how late every output leaves.
    REQUIRE(f.s.addEffect(f.out, "limit"));
    f.s.advanceWithoutDevice(4800);                                    // the engine takes the outputs' new chains
    pump();
    f.window.show(f.out, 0);
    REQUIRE(f.s.outputEffectsDelaySamples() > 0);
    const double ms = 1000.0 * f.s.outputEffectsDelaySamples() / f.s.sampleRate();
    CHECK(f.window.footer().state.isEmpty());
    CHECK(f.window.footer().delay
          == "Delay " + juce::String(ms, 1) + juce::String::fromUTF8(" ms, all outputs kept together \xc2\xb7 a Limit switched off still delays them"));
}

TEST_CASE("entering Show mode closes the effects window, and it can't open there", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    pump();
    f.window.show(f.keys, 0);
    REQUIRE(f.window.isOpen());
    f.s.enterShow();
    pump();
    CHECK_FALSE(f.window.isOpen());
    CHECK_FALSE(f.window.isVisible());
    CHECK(f.window.params() == nullptr);                               // its host and page are gone with it
    f.window.show(f.keys, 0);
    CHECK_FALSE(f.window.isOpen());
    CHECK_FALSE(f.window.isVisible());
}

TEST_CASE("Escape closes the effects window, or first its add menu", "[fx-window]") {
    Fixture f;
    f.window.show(f.keys, -1);
    REQUIRE(f.window.column().addMenuOpen());
    CHECK(f.window.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(f.window.column().addMenuOpen());
    CHECK(f.window.isOpen());
    CHECK(f.window.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(f.window.isOpen());
    press(f.window.closeButton());                                     // closed already: nothing more
    CHECK_FALSE(f.window.isOpen());
}

TEST_CASE("the shell opens the effects window over its content", "[fx-window]") {
    DemoSession d;
    sqlive::test::ScopedLook look;
    auto& s = *d.session;
    const auto& song = *s.model().song(d.setlist().entries[0].songId);
    const app::EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
    ui::AppShell shell(s, nullptr);
    shell.setBounds(0, 0, 1280, 760);
    shell.showPage(ui::Page::Mixer);

    shell.openEffects(keys, -1);
    auto& window = shell.effectsWindow();
    REQUIRE(window.isOpen());
    CHECK(window.getBounds() == juce::Rectangle<int>(0, ui::TopBar::height, 1280, 760 - ui::TopBar::height));   // the top bar stays
    CHECK(window.column().addMenuOpen());
    shell.showPage(ui::Page::Setup);                                  // another page: it goes with the one it was over
    CHECK_FALSE(window.isOpen());

    shell.openEffects(keys, 0);
    REQUIRE(window.isOpen());
    s.enterShow();
    pump();
    CHECK_FALSE(window.isOpen());
    shell.openEffects(keys, 0);                                       // never in Show mode
    CHECK_FALSE(window.isOpen());
}

TEST_CASE("an edit made as the chain changes under the page lands on no other effect", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    auto first = f.s.effects(f.keys)[0];
    first.values.set("output_gain", 3.0);
    f.s.setEffect(f.keys, 0, first, "first");
    auto second = f.s.effects(f.keys)[1];
    second.values.set("output_gain", -3.0);
    f.s.setEffect(f.keys, 1, second, "second");
    pump();
    f.window.show(f.keys, 0);
    REQUIRE(f.window.params() != nullptr);
    const auto gainOfFirst = [&] {
        auto* params = f.window.params();
        return params->apvts().getParameter(smartq::moduleParamId(params->slotOf(0), "output_gain"));
    };

    // Another view swaps the two EQs; the window hears of it only once the change message arrives. An edit on its page
    // meanwhile was made for the EQ at +3 dB, which is now second: it is dropped, not written onto the other.
    REQUIRE(f.s.moveEffect(f.keys, 0, 1));
    const auto moved = model::effectsKey(f.s.effects(f.keys));
    auto* gain = gainOfFirst();
    gain->setValueNotifyingHost(gain->convertTo0to1(6.0f));
    CHECK(model::effectsKey(f.s.effects(f.keys)) == moved);

    pump();                                                            // the window reads the chain again
    gain = gainOfFirst();
    gain->setValueNotifyingHost(gain->convertTo0to1(6.0f));            // now on the EQ shown first, the one at -3 dB
    const auto after = f.s.effects(f.keys);
    CHECK((double) after[0].values["output_gain"] == Catch::Approx(6.0));
    CHECK((double) after[1].values["output_gain"] == Catch::Approx(3.0));
    gain->setValueNotifyingHost(gain->convertTo0to1(5.0f));            // and the next edit lands there too
    CHECK((double) f.s.effects(f.keys)[0].values["output_gain"] == Catch::Approx(5.0));
}

TEST_CASE("an edit undone before the window hears of it leaves the page's next edits landing", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.keys, "eq"));
    pump();
    f.window.show(f.keys, 0);
    REQUIRE(f.window.params() != nullptr);
    const auto gainOf = [&] {
        auto* params = f.window.params();
        return params->apvts().getParameter(smartq::moduleParamId(params->slotOf(0), "output_gain"));
    };

    // A turn of the knob, then an undo straight back, both before the change message arrives: the chain is again just as
    // the window last read it.
    auto* gain = gainOf();
    gain->setValueNotifyingHost(gain->convertTo0to1(6.0f));
    REQUIRE((double) f.s.effects(f.keys)[0].values["output_gain"] == Catch::Approx(6.0));
    f.s.commands().undo();
    REQUIRE((double) f.s.effects(f.keys)[0].values.getWithDefault("output_gain", 0.0) == Catch::Approx(0.0));

    pump();                                                            // the window reads the chain again
    gain = gainOf();
    gain->setValueNotifyingHost(gain->convertTo0to1(4.0f));            // and the next turn of the knob lands
    CHECK((double) f.s.effects(f.keys)[0].values["output_gain"] == Catch::Approx(4.0));
}

TEST_CASE("an output's effects window closes when another rig is chosen", "[fx-window]") {
    Fixture f;
    REQUIRE(f.s.addEffect(f.out, "eq"));
    pump();
    f.window.show(f.out, 0);
    REQUIRE(f.window.isOpen());
    const auto other = f.s.commands().run("rig.duplicate", sqlive::test::params({{"rigId", f.s.rigId()}, {"name", "Rehearsal"}}))
                           .value.toString();
    REQUIRE(f.s.model().rig(other) != nullptr);
    pump();
    REQUIRE(f.window.isOpen());                                        // a rig added: the one heard is still this one
    f.s.chooseRig(other);
    pump();
    CHECK_FALSE(f.window.isOpen());                                    // its edits would go to a rig nobody hears
    f.window.show(f.out, 0);
    CHECK_FALSE(f.window.isOpen());
}
