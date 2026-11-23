#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "engine/EffectChain.h"
#include "model/Effects.h"
#include "tests/support/AllocGuard.h"
#include "tests/support/DemoSession.h"
#include "ui/fx/ChainHost.h"
#include "ui/fx/ChainParams.h"
#include "ui/fx/DynamicPage.h"
#include "ui/fx/LimitPage.h"
#include "ui/fx/ModulePages.h"
#include "ui/fx/SmartQLookAndFeel.h"

// Smart Q's module pages in Live: each opens on a chain's saved settings, a drag on it is one effects command, loading
// settings sends nothing back, and the page analyzer hears the shown effect through its chain's tap.

using namespace sqlive;
using sqlive::ui::fx::ChainHost;
using sqlive::ui::fx::ChainParams;

namespace {

model::EffectDoc with(model::EffectDoc e, const char* field, juce::var value) {
    e.values.set(juce::Identifier(field), value);
    return e;
}

// Every slider in the component's tree.
void slidersIn(juce::Component& c, std::vector<juce::Slider*>& out) {
    if (auto* slider = dynamic_cast<juce::Slider*>(&c)) out.push_back(slider);
    for (auto* child : c.getChildren()) slidersIn(*child, out);
}

bool showsValue(juce::Component& page, double value) {
    std::vector<juce::Slider*> sliders;
    slidersIn(page, sliders);
    for (auto* s : sliders)
        if (std::abs(s->getValue() - value) < 1.0e-3 * std::max(1.0, std::abs(value))) return true;
    return false;
}

// One changed value for each kind, far from its default, on a control each page shows.
struct Changed { const char* kind; const char* field; double value; };
constexpr Changed kChanged[] = {
    {"eq", "output_gain", -7.5}, {"dynamic", "threshold", -23.0}, {"deess", "frequency", 7300.0},
    {"width", "width", 143.0},   {"transient", "attack", 5.5},    {"limit", "ceiling", -1.7},
};

}  // namespace

TEST_CASE("each module page opens and paints from saved settings", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    smartq::SmartQLookAndFeel look;
    for (const auto& c : kChanged) {
        INFO(c.kind);
        ChainParams params({with(model::defaultEffect(c.kind), c.field, c.value)});
        REQUIRE(params.slotOf(0) >= 0);
        ChainHost host(params, true);
        auto page = sqlive::ui::fx::makeModulePage(c.kind, host, params.slotOf(0));
        REQUIRE(page != nullptr);
        page->setLookAndFeel(&look);
        page->setBounds(0, 0, 1300, 840);
        page->update(1.0 / 30.0);
        const auto image = page->createComponentSnapshot(page->getLocalBounds());
        CHECK(image.getWidth() == 1300);
        CHECK(showsValue(*page, c.value));
        CHECK(sqlive::ui::fx::makeModulePage(juce::String(c.kind) == "eq" ? "limit" : "eq", host, params.slotOf(0)) == nullptr);
        auto miniature = sqlive::ui::fx::makeMiniature(c.kind, host, params.slotOf(0));
        REQUIRE(miniature != nullptr);
        miniature->setBounds(0, 0, 168, 44);
        miniature->update(1.0 / 30.0);
        miniature->createComponentSnapshot(miniature->getLocalBounds());
        page->setLookAndFeel(nullptr);
    }
}

TEST_CASE("a chain of four of one kind has a page for each", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    std::vector<model::EffectDoc> chain;
    for (double t : {-10.0, -20.0, -30.0, -40.0}) chain.push_back(with(model::defaultEffect("dynamic"), "threshold", t));
    ChainParams params(chain);
    ChainHost host(params, false);
    for (int i = 0; i < 4; ++i) {
        INFO(i);
        REQUIRE(params.slotOf(i) >= 0);
        CHECK(params.indexOfSlot(params.slotOf(i)) == i);
        auto page = sqlive::ui::fx::makeModulePage("dynamic", host, params.slotOf(i));
        REQUIRE(page != nullptr);
        page->setBounds(0, 0, 1300, 840);
        CHECK(showsValue(*page, -10.0 * (i + 1)));
    }
}

TEST_CASE("a knob drag on a page is one effects command", "[fx-ui]") {
    sqlive::test::DemoSession d;
    auto& s = *d.session;
    const auto song = *s.model().song(d.setlist().entries[0].songId);
    const app::EffectsTarget keys{song.id, song.parts[0].id, {}, {}};
    REQUIRE(s.addEffect(keys, "dynamic"));
    s.commands().clearHistory();

    ChainParams params(s.effects(keys));
    struct Edit { int index; model::EffectDoc effect; juce::String gesture; };
    std::vector<Edit> edits;
    params.onEdited = [&](int index, const model::EffectDoc& effect, const juce::String& gesture) {
        edits.push_back({index, effect, gesture});
        s.setEffect(keys, index, effect, gesture);
    };
    ChainHost host(params, true, ChainHost::sessionSource(s, keys));
    auto page = sqlive::ui::fx::makeModulePage("dynamic", host, params.slotOf(0));
    REQUIRE(page != nullptr);

    auto* threshold = params.apvts().getParameter(smartq::moduleParamId(params.slotOf(0), "threshold"));
    REQUIRE(threshold != nullptr);
    threshold->beginChangeGesture();
    for (double db : {-5.0, -10.0, -15.0, -20.0, -25.0}) threshold->setValueNotifyingHost(threshold->convertTo0to1((float) db));
    threshold->endChangeGesture();

    REQUIRE(edits.size() == 5);
    for (const auto& e : edits) {
        CHECK(e.index == 0);
        CHECK(e.gesture == edits[0].gesture);
        CHECK(e.effect.kind == "dynamic");
    }
    CHECK(edits[0].gesture.isNotEmpty());
    CHECK((double) edits.back().effect.values["threshold"] == Catch::Approx(-25.0).margin(1.0e-4));
    CHECK((double) edits.back().effect.values["ratio"] == 3.0);           // what wasn't touched is as it was saved
    CHECK((double) s.effects(keys)[0].values["threshold"] == Catch::Approx(-25.0).margin(1.0e-4));
    CHECK(dynamic_cast<smartq::DynamicPage&>(*page).thresholdKnob().getValue() == Catch::Approx(-25.0).margin(1.0e-4));

    threshold->beginChangeGesture();                                        // the next drag is a gesture of its own
    threshold->setValueNotifyingHost(threshold->convertTo0to1(-30.0f));
    threshold->endChangeGesture();
    REQUIRE(edits.size() == 6);
    CHECK(edits.back().gesture != edits[0].gesture);

    s.commands().undo();
    CHECK((double) s.effects(keys)[0].values["threshold"] == Catch::Approx(-25.0).margin(1.0e-4));
    s.commands().undo();                                                    // the first drag's five, all at once
    CHECK((double) s.effects(keys)[0].values["threshold"] == 0.0);
    CHECK_FALSE(s.commands().canUndo());
}

TEST_CASE("an EQ band added on the graph is one effects command", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ChainParams params({model::defaultEffect("eq")});
    std::vector<juce::String> gestures;
    model::EffectDoc last;
    params.onEdited = [&](int, const model::EffectDoc& effect, const juce::String& gesture) {
        gestures.push_back(gesture);
        last = effect;
    };
    smartq::dsp::BandSettings band;
    band.enabled = true;
    band.frequencyHz = 440.0;
    band.gainDb = 6.0;
    band.q = 2.0;
    smartq::writeBand(params.apvts(), 0, 3, band);
    REQUIRE_FALSE(gestures.empty());
    for (const auto& g : gestures) CHECK(g == gestures[0]);
    const auto* bands = last.values["bands"].getArray();
    REQUIRE(bands != nullptr);
    REQUIRE(bands->size() == 1);
    CHECK((double) (*bands)[0]["freq"] == Catch::Approx(440.0).margin(1.0e-3));
    CHECK((double) (*bands)[0]["gain"] == Catch::Approx(6.0).margin(1.0e-4));

    params.load({last});                                                    // what was sent comes back: nothing moves
    CHECK(smartq::readBand(params.apvts(), 0, 3).enabled);
    CHECK_FALSE(smartq::readBand(params.apvts(), 0, 0).enabled);
}

TEST_CASE("loading settings into the page sends nothing back", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    std::vector<model::EffectDoc> chain{model::defaultEffect("eq"), model::defaultEffect("limit")};
    ChainParams params(chain);
    int sent = 0;
    params.onEdited = [&](int, const model::EffectDoc&, const juce::String&) { ++sent; };
    ChainHost host(params, true);
    auto page = sqlive::ui::fx::makeModulePage("limit", host, params.slotOf(1));
    REQUIRE(page != nullptr);
    page->setBounds(0, 0, 1300, 840);

    auto changed = chain;
    changed[1] = with(changed[1], "ceiling", -3.0);
    changed[1].on = false;
    auto* b = new juce::DynamicObject();
    b->setProperty("shape", 0);  b->setProperty("freq", 1000.0);  b->setProperty("gain", -4.0);  b->setProperty("q", 1.0);
    changed[0] = with(changed[0], "bands", juce::Array<juce::var>{juce::var(b)});
    REQUIRE(params.fits(changed));
    params.load(changed);
    page->update(1.0 / 30.0);
    CHECK(sent == 0);
    CHECK(showsValue(*page, -3.0));
    CHECK(*params.apvts().getRawParameterValue(smartq::moduleParamId(params.slotOf(1), "bypass")) >= 0.5f);
    CHECK(smartq::readBand(params.apvts(), 0, 0).gainDb == Catch::Approx(-4.0));
    CHECK(params.effects() == changed);

    CHECK_FALSE(params.fits({model::defaultEffect("limit"), model::defaultEffect("eq")}));   // another order is another chain
}

TEST_CASE("pages offer no lookahead on a part's Dynamic", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ChainParams params({model::defaultEffect("dynamic"), model::defaultEffect("limit")});
    ChainHost host(params, true);
    auto dynamic = sqlive::ui::fx::makeModulePage("dynamic", host, params.slotOf(0));
    REQUIRE(dynamic != nullptr);
    dynamic->setBounds(0, 0, 1300, 840);
    auto& page = dynamic_cast<smartq::DynamicPage&>(*dynamic);
    CHECK_FALSE(page.lookaheadKnob().isVisible());
    CHECK_FALSE(page.delayReading().isVisible());
    CHECK_FALSE(page.listenStrip().isVisible());                           // no sidechain in Live
    CHECK_FALSE(page.sidechainHighpassKnob().isVisible());

    auto limit = sqlive::ui::fx::makeModulePage("limit", host, params.slotOf(1));
    REQUIRE(limit != nullptr);
    limit->setBounds(0, 0, 1300, 840);
    auto* lookahead = params.apvts().getParameter(smartq::moduleParamId(params.slotOf(1), "lookahead"));
    REQUIRE(lookahead != nullptr);
    CHECK(lookahead->convertFrom0to1(1.0f) == Catch::Approx(5.0f));       // Limit's lookahead stops at 5 ms
}

TEST_CASE("the shown effect's input and output reach its page", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr double kRate = 48000.0;
    auto eq = model::defaultEffect("eq");
    eq.values.set("output_gain", 12.0);
    const std::vector<model::EffectDoc> docs{eq};
    auto chain = engine::EffectChain::make(model::chainOf(docs, model::EffectPlace::Part, true), kRate, 512, true);

    // The tap: nothing until a slot is tapped, then the slot's own input and output, oldest first, and no allocation.
    std::vector<double> left(512), right(512);
    const auto fill = [&](int block) {
        for (int i = 0; i < 512; ++i)
            left[(size_t) i] = right[(size_t) i] = 0.1 * std::sin(2.0 * std::numbers::pi * 1000.0 * (block * 512 + i) / kRate);
    };
    fill(0);
    chain->process(left.data(), right.data(), 512);
    std::uint64_t at = 0;
    std::vector<float> bl(4096), br(4096), al(4096), ar(4096);
    CHECK(chain->readTap(at, bl.data(), br.data(), al.data(), ar.data(), 4096) == 0);
    chain->setTappedSlot(0);
    {
        sqlive::test::AllocGuard guard;
        for (int b = 1; b <= 4; ++b) {
            fill(b);
            chain->process(left.data(), right.data(), 512);
        }
    }
    CHECK(sqlive::test::allocationCount() == 0);
    const int n = chain->readTap(at, bl.data(), br.data(), al.data(), ar.data(), 4096);
    REQUIRE(n == 4 * 512);
    float before = 0.0f, after = 0.0f;
    for (int i = 1024; i < n; ++i) {
        before = std::max(before, std::abs(bl[(size_t) i]));
        after = std::max(after, std::abs(al[(size_t) i]));
    }
    CHECK(20.0 * std::log10(after / before) == Catch::Approx(12.0).margin(0.3));
    CHECK(chain->readTap(at, bl.data(), br.data(), al.data(), ar.data(), 4096) == 0);   // all read

    // The host taps the shown slot and feeds the page analyzer from it, on the message thread.
    ChainParams params(docs);
    ChainHost host(params, true, [chain] { return ChainHost::Playing{chain, {0}, kRate}; });
    host.setShownSlot(params.slotOf(0));
    smartq::DisplayFrame frame;
    for (int b = 5; b < 40; ++b) {
        fill(b);
        chain->process(left.data(), right.data(), 512);
        host.step();
    }
    CHECK(host.audioBlocksProcessed() > 0);
    REQUIRE(host.pageAnalyzer().readDisplay(frame));
    CHECK(frame.sampleRate == kRate);
    REQUIRE(frame.beforeDb.size() == frame.afterDb.size());
    const auto bin = (size_t) std::lround(1000.0 / (kRate / 8192.0));
    CHECK(frame.afterDb[bin] - frame.beforeDb[bin] == Catch::Approx(12.0).margin(0.5));
    host.setShownSlot(-1);
}

TEST_CASE("a page shown in another's place starts from nothing", "[fx-ui]") {
    juce::ScopedJuceInitialiser_GUI gui;
    constexpr double kRate = 48000.0;
    auto loud = model::defaultEffect("eq");
    loud.values.set("output_gain", 12.0);
    const std::vector<model::EffectDoc> docs{loud, model::defaultEffect("eq")};
    auto chain = engine::EffectChain::make(model::chainOf(docs, model::EffectPlace::Part, true), kRate, 512, true);
    std::vector<double> left(512), right(512);
    int block = 0;
    const auto play = [&](int blocks) {
        for (int b = 0; b < blocks; ++b, ++block) {
            for (int i = 0; i < 512; ++i)
                left[(size_t) i] = right[(size_t) i] = 0.1 * std::sin(2.0 * std::numbers::pi * 1000.0 * (block * 512 + i) / kRate);
            chain->process(left.data(), right.data(), 512);
        }
    };

    ChainParams params(docs);
    ChainHost host(params, true, [chain] { return ChainHost::Playing{chain, {0, 1}, kRate}; });
    host.setShownSlot(params.slotOf(0));
    smartq::DisplayFrame frame;
    for (int b = 0; b < 40; ++b) {
        play(1);
        host.step();
    }
    REQUIRE(host.pageAnalyzer().readDisplay(frame));                    // the first EQ's, +12 dB
    play(8);                                                            // heard of the first EQ, and not yet read

    host.setShownSlot(params.slotOf(1));
    CHECK_FALSE(host.pageAnalyzer().readDisplay(frame));                // nothing of the first EQ's is left
    play(3);
    host.step();
    CHECK_FALSE(host.pageAnalyzer().readDisplay(frame));                // nor taken from the tap: 1536 samples aren't a frame
    for (int b = 0; b < 40; ++b) {
        play(1);
        host.step();
    }
    REQUIRE(host.pageAnalyzer().readDisplay(frame));
    const auto bin = (size_t) std::lround(1000.0 / (kRate / 8192.0));
    CHECK(frame.afterDb[bin] - frame.beforeDb[bin] == Catch::Approx(0.0).margin(0.5));   // the second EQ's own
    host.setShownSlot(-1);
}
