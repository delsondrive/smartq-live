#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "tests/support/DemoSession.h"
#include "tests/support/Params.h"
#include "tests/support/Pixels.h"
#include "ui/AppShell.h"
#include "ui/Pages.h"
#include "ui/RoutingGrid.h"
#include "ui/SetupPage.h"

using namespace sqlive;
using namespace sqlive::ui;
using namespace sqlive::test;

namespace {

void forEachComponent(juce::Component& root, const std::function<void(juce::Component&)>& visit) {
    visit(root);
    for (auto* child : root.getChildren()) forEachComponent(*child, visit);
}

bool anyTakesKeyboardFocus(juce::Component& root) {
    bool takes = false;
    forEachComponent(root, [&](juce::Component& c) {
        takes = takes || c.getWantsKeyboardFocus() || c.getMouseClickGrabsKeyboardFocus();
    });
    return takes;
}

// Fourteen more outputs after Headphones and PA: sixteen in all, on channels 5/6 to 31/32.
void addFourteenOutputs(app::Session& s) {
    for (int i = 0; i < 14; ++i)
        s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "Out " + juce::String(i)}, {"firstChannel", 5 + i * 2},
                                               {"stereo", true}, {"label", "monitor"}}));
}

}  // namespace

TEST_CASE("the routing grid is the rig, with forbidden cells marked", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    auto state = makeRoutingState(s, nullptr);
    CHECK(state.rigName == "Headphones");  CHECK(state.connection == "Not connected");
    REQUIRE(state.columns.size() == 2);
    CHECK(state.columns[0].name == "Headphones");  CHECK(state.columns[0].channels == "1/2");  CHECK(state.columns[0].label == "In-ears");  CHECK(state.columns[0].trim == "0 dB");
    CHECK(state.columns[1].label == "PA");
    REQUIRE(state.rows.size() == 10);                             // Click, then the nine part roles (Cues and Pad come later)
    CHECK(state.rows[0].name == "Click");
    CHECK(state.rows[0].cells[0].sent);  CHECK(state.rows[0].cells[0].text == "0");
    CHECK_FALSE(state.rows[0].cells[1].allowed);  CHECK(state.rows[0].cells[1].whyNot == "Click can only go to in-ear outputs.");
    CHECK(state.rows[1].cells[1].allowed);  CHECK_FALSE(state.rows[1].cells[1].sent);
}

TEST_CASE("clicking and dragging cells edits the mixes through commands", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    sqlive::test::ScopedLook look;
    ui::RoutingGrid grid(s);  grid.setBounds(0, 0, 1000, 500);
    grid.clickCell(1, 1);                                         // Keys → PA
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "0");
    grid.dragCell(1, 1, -3.0f, "g1");  grid.dragCell(1, 1, -3.5f, "g1");
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "-3.5");
    grid.dragCell(1, 1, 5.0f, "g2");
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].boosted);
    s.commands().undo();  s.commands().undo();                    // each drag is one step
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[1].text == "0");
    grid.clickCell(1, 1);
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[1].cells[1].sent);
    grid.clickCell(0, 1);                                         // Click → PA: refused
    CHECK(grid.lastMessage() == "Click can only go to in-ear outputs.");
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[0].cells[1].sent);
    for (int i = 0; i < 14; ++i) s.commands().run("output.add", params({{"rigId", rigId}, {"name", "Out " + juce::String(i)}, {"firstChannel", 5 + i * 2}, {"stereo", true}, {"label", "monitor"}}));
    REQUIRE(makeRoutingState(s, nullptr).columns.size() == 16);
    d.settle();                                                   // the session says it changed through the message loop
    snapshot(grid, 1000, 500);
    CHECK(childrenInside(grid));
    auto& area = grid.viewport();
    REQUIRE(area.getViewedComponent() != nullptr);
    CHECK(area.getViewedComponent()->getWidth() >= 16 * 72);     // 16 columns: each keeps 72 px
    CHECK(area.getViewedComponent()->getWidth() > area.getViewWidth());   // and the grid scrolls sideways
}

TEST_CASE("the routing state says how the rig is set up and what is wrong with it, blockers first", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    auto state = makeRoutingState(s, nullptr);
    CHECK(state.sampleRate == "48 kHz");  CHECK(state.buffer == juce::String::fromUTF8("512 · safest"));
    CHECK(state.problems.empty());  CHECK(state.blockingProblems == 0);
    s.commands().run("rig.set", params({{"rigId", rigId}, {"sampleRate", 44100}, {"bufferSize", 256}}));
    const auto& rig = *s.model().rig(rigId);
    for (const auto& role : s.model().band().roles)               // nothing sent anywhere
        s.commands().run("mix.setLevel", params({{"rigId", rigId}, {"outputId", rig.outputs[0].id}, {"roleId", role.id}, {"db", juce::var()}}));
    s.commands().run("output.set", params({{"rigId", rigId}, {"outputId", rig.outputs[0].id}, {"trimDb", -2.5}}));
    state = makeRoutingState(s, nullptr);
    CHECK(state.sampleRate == "44.1 kHz");  CHECK(state.buffer == "256");
    CHECK(state.columns[0].trim == "-2.5 dB");
    REQUIRE(state.problems.size() == 2);  CHECK(state.blockingProblems == 1);
    CHECK(state.problems[0] == "Nothing is sent to any output, so nothing would be heard.");
    CHECK(state.problems[1] == "The click isn't sent to any output.");
    CHECK(state.rows[0].cells[0].text.isEmpty());  CHECK_FALSE(state.rows[0].cells[0].boosted);

    // A timecode output takes nothing, and says why.
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "Timecode"}, {"firstChannel", 3}, {"stereo", false}, {"label", "timecode"}}));
    state = makeRoutingState(s, nullptr);
    REQUIRE(state.columns.size() == 2);
    CHECK(state.columns[1].channels == "3");  CHECK(state.columns[1].label == "Timecode");
    CHECK_FALSE(state.rows[1].cells[1].allowed);  CHECK(state.rows[1].cells[1].whyNot == "A timecode output carries timecode only.");
}

TEST_CASE("the routing grid's header menu and cells refuse what the commands refuse, and say why", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    sqlive::test::ScopedLook look;
    ui::RoutingGrid grid(s);  grid.setBounds(0, 0, 1000, 500);
    const auto paId = makeRoutingState(s, nullptr).columns[1].outputId;
    grid.setOutputChannels(paId, 2, true);                        // would share output 2 with Headphones
    CHECK(grid.lastMessage() == "Headphones would share output 2 with PA, which goes to the PA.");
    CHECK(makeRoutingState(s, nullptr).columns[1].channels == "3/4");
    grid.clickCell(1, 1);                                         // Keys → PA
    grid.setOutputLabel(paId, model::OutputLabel::Timecode);      // Keys taken out, as the command says
    CHECK(grid.lastMessage() == "Keys was taken out of PA.");
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[1].cells[1].sent);
    grid.dragCell(1, 1, 2.0f, "g1");                             // a timecode output takes nothing
    CHECK(grid.lastMessage() == "A timecode output carries timecode only.");
    CHECK_FALSE(makeRoutingState(s, nullptr).rows[1].cells[1].sent);

    // Overall level: one drag is one undo step.
    CHECK(makeRoutingState(s, nullptr).columns[0].trim == "0 dB");
    grid.dragTrim(0, -1.0f, "t1");  grid.dragTrim(0, -2.5f, "t1");
    CHECK(makeRoutingState(s, nullptr).columns[0].trim == "-2.5 dB");
    s.commands().undo();
    CHECK(makeRoutingState(s, nullptr).columns[0].trim == "0 dB");

    // Show mode: the grid changes nothing.
    s.enterShow();
    grid.clickCell(1, 0);
    CHECK(makeRoutingState(s, nullptr).rows[1].cells[0].sent);
}

TEST_CASE("one output per role replaces the rig's outputs, as one undo step", "[routing-ui]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::RoutingGrid grid(s);  grid.setBounds(0, 0, 1000, 500);
    grid.oneOutputPerRole();
    const auto& rig = *s.model().rig(s.rigId());
    REQUIRE(rig.outputs.size() == 10);                            // the nine parts in stereo, then the click in mono
    CHECK(rig.outputs[0].name == "Keys");  CHECK(rig.outputs[0].firstChannel == 1);  CHECK(rig.outputs[0].stereo);
    CHECK(rig.outputs[8].name == "FX");  CHECK(rig.outputs[8].firstChannel == 17);
    CHECK(rig.outputs[9].name == "Click");  CHECK(rig.outputs[9].firstChannel == 19);  CHECK_FALSE(rig.outputs[9].stereo);
    for (const auto& out : rig.outputs) {
        CHECK(out.label == model::OutputLabel::Monitor);
        REQUIRE(out.mix.size() == 1);
        CHECK(s.model().role(out.mix.begin()->first)->name == out.name);
    }
    auto state = makeRoutingState(s, nullptr);
    CHECK(state.rows[1].cells[0].sent);  CHECK_FALSE(state.rows[1].cells[1].sent);
    s.commands().undo();
    REQUIRE(s.model().rig(s.rigId())->outputs.size() == 1);
    CHECK(s.model().rig(s.rigId())->outputs[0].name == "Headphones");
}

TEST_CASE("the routing grid scrolls both ways with sixteen outputs and many roles, its role names following", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA with a very long name indeed"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    addFourteenOutputs(s);
    for (int i = 0; i < 20; ++i) s.commands().run("role.add", params({{"name", "Extra role number " + juce::String(i)}}));
    sqlive::test::ScopedLook look;
    ui::RoutingGrid grid(s);
    for (const auto& [w, h] : {std::pair{860, 420}, std::pair{1100, 560}, std::pair{1700, 900}}) {
        INFO(w << " x " << h);
        snapshot(grid, w, h);
        CHECK(childrenInside(grid));
        REQUIRE(grid.state().columns.size() == 16);  REQUIRE(grid.state().rows.size() == 30);
        auto& area = grid.viewport();
        auto& names = grid.roleNames();
        REQUIRE(area.getViewedComponent() != nullptr);  REQUIRE(names.getViewedComponent() != nullptr);
        auto* body = area.getViewedComponent();
        CHECK(body->getWidth() >= 16 * 72);
        if (w < 1700) CHECK(body->getWidth() > area.getViewWidth());
        CHECK(body->getHeight() > area.getViewHeight());          // 30 roles don't fit: it scrolls down too
        // The role names sit left of the outputs, as tall as the rows they name, and show as much as the outputs do.
        CHECK(names.getRight() <= area.getX());
        CHECK(names.getViewedComponent()->getHeight() == body->getHeight());
        CHECK(names.getHeight() == area.getViewHeight());
        // Every cell is inside its column, the columns side by side.
        for (int c = 0; c < 16; ++c) {
            const auto cell = grid.cellBounds(29, c);
            CHECK(cell.getWidth() >= 72);
            CHECK(body->getLocalBounds().contains(cell));
            if (c > 0) CHECK(cell.getX() >= grid.cellBounds(29, c - 1).getRight());
        }
        // The footer is under the grid.
        CHECK(grid.footerBounds().getY() >= area.getBottom());
        CHECK(grid.getLocalBounds().contains(grid.footerBounds()));

        // Scrolled to the bottom, the role names follow.
        const_cast<juce::Viewport&>(area).setViewPosition(0, body->getHeight());
        CHECK(names.getViewPositionY() == area.getViewPositionY());
        CHECK(area.getViewPositionY() > 0);
        const_cast<juce::Viewport&>(area).setViewPosition(0, 0);
    }
}

TEST_CASE("the setup page lays out at every window size, and takes no keys in Show mode", "[routing-ui]") {
    DemoSession d; auto& s = *d.session; auto rigId = s.rigId();
    s.commands().run("output.add", params({{"rigId", rigId}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    addFourteenOutputs(s);
    for (const auto& role : s.model().band().roles)               // nothing heard: problems to list
        for (const auto& out : s.model().rig(rigId)->outputs)
            s.commands().run("mix.setLevel", params({{"rigId", rigId}, {"outputId", out.id}, {"roleId", role.id}, {"db", juce::var()}}));
    d.settle();
    sqlive::test::ScopedLook look;
    ui::SetupPage page(s, nullptr);
    REQUIRE(page.state().problems.size() == 2);
    for (const auto& [w, h] : {std::pair{1100, 618}, std::pair{1280, 698}, std::pair{1920, 1018}}) {
        INFO(w << " x " << h);
        snapshot(page, w, h);
        CHECK(childrenInside(page));
        // The top panel's controls side by side, none on another; the problems under it, the grid under them.
        std::vector<juce::Component*> controls;
        for (auto* child : page.getChildren())
            if (child->isVisible() && child != &page.grid()) controls.push_back(child);
        for (size_t i = 0; i < controls.size(); ++i) {
            CHECK(page.topBounds().contains(controls[i]->getBounds()));
            for (size_t j = i + 1; j < controls.size(); ++j) CHECK_FALSE(controls[i]->getBounds().intersects(controls[j]->getBounds()));
        }
        CHECK(page.problemsBounds().getY() >= page.topBounds().getBottom());
        CHECK(page.grid().getY() >= page.problemsBounds().getBottom());
        CHECK(page.grid().getHeight() >= 200);
        CHECK(page.grid().getX() >= page.subTabsBounds().getRight());
    }
    s.enterShow();
    snapshot(page, 1280, 698);
    CHECK_FALSE(anyTakesKeyboardFocus(page));
}

TEST_CASE("the setup page is the Setup tab", "[routing-ui]") {
    DemoSession d; auto& s = *d.session;
    sqlive::test::ScopedLook look;
    ui::AppShell shell(s, nullptr);
    plugInPages(shell, s, nullptr, nullptr);
    shell.setBounds(0, 0, 1280, 760);
    shell.showPage(Page::Setup);
    auto* page = dynamic_cast<ui::SetupPage*>(shell.currentContent());
    REQUIRE(page != nullptr);
    CHECK(page->grid().state().columns.size() == 1);
    // Changed while another page shows, the grid catches up when Setup is shown again.
    shell.showPage(Page::Setlists);
    s.commands().run("output.add", params({{"rigId", s.rigId()}, {"name", "PA"}, {"firstChannel", 3}, {"stereo", true}, {"label", "foh"}}));
    d.settle();
    CHECK(page->grid().state().columns.size() == 1);
    shell.showPage(Page::Setup);
    CHECK(page->grid().state().columns.size() == 2);
    CHECK(page->state().columns.size() == 2);
}
