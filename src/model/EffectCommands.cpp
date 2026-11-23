#include "model/EffectCommands.h"

#include "model/CommandParams.h"
#include "model/DocumentsJson.h"
#include "model/Effects.h"

namespace sqlive::model {

namespace {

using namespace commands;

struct Chain { std::vector<EffectDoc>& effects; EffectPlace place; bool stereo; };

Chain chainOf(BandModel& m, const Params& p) {
    if (p.present("outputId")) {
        auto id = p.id("outputId");
        auto& out = found(findIn(rigOf(m, p).outputs, [&](const OutputDoc& o) { return o.id == id; }), "output");
        return {out.effects, EffectPlace::Output, out.stereo};
    }
    auto& song = songOf(m, p);
    auto id = p.id("partId");
    auto& part = found(findIn(song.parts, [&](const PartDoc& d) { return d.id == id; }), "part");
    return {part.effects, EffectPlace::Part, part.channels == 2};
}

EffectDoc& effectAt(Chain& chain, const Params& p, const char* key) {
    const int index = p.whole(key, 0, 1000000);
    if (index >= (int) chain.effects.size()) refuse("That effect no longer exists.");
    return chain.effects[(size_t) index];
}

void checkKind(const Chain& chain, const juce::String& kind) {
    if (! effectKindsFor(chain.place, true).contains(kind)) refuse("That effect can't go there.");
    if (kind == "width" && ! chain.stereo)
        refuse(chain.place == EffectPlace::Part ? "Width needs a stereo part." : "Width needs a stereo output.");
}

}  // namespace

void registerEffectCommands(CommandLayer& layer) {
    auto add = [&layer](const char* name, auto fn) { addCommand(layer, name, fn); };

    add("effects.add", [](BandModel& m, const Params& p) {
        auto chain = chainOf(m, p);
        const auto kind = p.text("kind");
        checkKind(chain, kind);
        if ((int) chain.effects.size() >= kMaxEffects) refuse("A chain holds up to " + juce::String(kMaxEffects) + " effects.");
        if (kind == "limit")
            for (auto& e : chain.effects)
                if (e.kind == "limit") refuse("An output holds one Limit.");
        const int at = p.given("at") ? p.whole("at", 0, kMaxEffects) : (int) chain.effects.size();
        chain.effects.insert(chain.effects.begin() + std::min(at, (int) chain.effects.size()), defaultEffect(kind));
        return CommandResult::success();
    });
    add("effects.remove", [](BandModel& m, const Params& p) {
        auto chain = chainOf(m, p);
        auto& e = effectAt(chain, p, "index");
        chain.effects.erase(chain.effects.begin() + (&e - chain.effects.data()));
        return CommandResult::success();
    });
    add("effects.move", [](BandModel& m, const Params& p) {
        auto chain = chainOf(m, p);
        const auto from = &effectAt(chain, p, "from") - chain.effects.data();
        const auto to = &effectAt(chain, p, "to") - chain.effects.data();
        auto moved = std::move(chain.effects[(size_t) from]);
        chain.effects.erase(chain.effects.begin() + from);
        chain.effects.insert(chain.effects.begin() + to, std::move(moved));
        return CommandResult::success();
    });
    add("effects.set", [](BandModel& m, const Params& p) {
        auto chain = chainOf(m, p);
        auto& e = effectAt(chain, p, "index");
        EffectDoc next; ParseNotes notes;
        if (! fromVar(p.raw("effect"), next, notes)) refuse("An effect needs its settings.");
        if (next.kind != e.kind) refuse("An effect keeps its kind.");
        e = std::move(next);
        return CommandResult::success();
    });
    add("effects.switch", [](BandModel& m, const Params& p) {
        auto chain = chainOf(m, p);
        effectAt(chain, p, "index").on = p.flag("on");
        return CommandResult::success();
    });
}

}  // namespace sqlive::model
