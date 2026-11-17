#include "model/Routing.h"

#include <algorithm>
#include <set>

#include "model/Units.h"

namespace sqlive::model {

namespace {

bool usesBus(OutputLabel l) noexcept { return l == OutputLabel::Monitor || l == OutputLabel::Foh; }

int lastChannel(const OutputDoc& o) noexcept { return o.firstChannel + (o.stereo ? 1 : 0); }   // 1-based, inclusive

bool onDevice(const OutputDoc& o, int deviceChannels) noexcept {
    return o.firstChannel >= 1 && (deviceChannels <= 0 || o.firstChannel <= deviceChannels);
}

// Whether buildMixSetup gives the output a bus (while there is room for one).
bool hasBus(const OutputDoc& o, int deviceChannels) noexcept { return usesBus(o.label) && onDevice(o, deviceChannels); }

juce::String channelsText(const OutputDoc& o) {
    return o.stereo ? juce::String(o.firstChannel) + "/" + juce::String(o.firstChannel + 1) : juce::String(o.firstChannel);
}

juce::String kindName(RoleKind k) {
    switch (k) {
        case RoleKind::Click: return "Click";
        case RoleKind::Cues: return "Cues";
        case RoleKind::Pad: return "Pad";
        default: return "Part";
    }
}

}  // namespace

bool mayRoute(RoleKind kind, OutputLabel label) noexcept {
    switch (label) {
        case OutputLabel::Monitor: return true;
        case OutputLabel::Foh: return kind != RoleKind::Click && kind != RoleKind::Cues;
        default: return false;
    }
}

juce::String whyNot(RoleKind kind, OutputLabel label) {
    if (mayRoute(kind, label)) return {};
    switch (label) {
        case OutputLabel::Timecode: return "A timecode output carries timecode only.";
        case OutputLabel::Unused: return "This output is marked as not used.";
        default: return kindName(kind) + " can only go to in-ear outputs.";
    }
}

int roleIndex(const BandDoc& band, const juce::String& roleId) noexcept {
    for (size_t i = 0; i < band.roles.size(); ++i)
        if (band.roles[i].id == roleId) return static_cast<int>(i);
    return -1;
}

int firstSharedChannel(const OutputDoc& a, const OutputDoc& b) noexcept {
    const int lo = std::max(a.firstChannel, b.firstChannel);
    return lo <= std::min(lastChannel(a), lastChannel(b)) ? lo : 0;
}

std::vector<RoutingProblem> checkRig(const BandDoc& band, const RigDoc& rig, int deviceChannels) {
    using Kind = RoutingProblem::Kind;
    std::vector<RoutingProblem> out;
    bool anyHeard = false, clickHeard = false, hasClickRole = false;
    for (auto& r : band.roles) hasClickRole = hasClickRole || r.kind == RoleKind::Click;

    for (auto& o : rig.outputs) {
        if (o.label == OutputLabel::Unused) continue;
        for (auto& [roleId, db] : o.mix) {
            const int ri = roleIndex(band, roleId);
            if (ri < 0) continue;
            const auto& role = band.roles[static_cast<size_t>(ri)];
            if (!mayRoute(role.kind, o.label)) {
                out.push_back({Kind::ForbiddenSend, o.id, roleId,
                               role.name + " can't go to " + o.name + ": " + whyNot(role.kind, o.label), false});
            } else if (onDevice(o, deviceChannels) && dbToGain(db) > 0.0f) {
                anyHeard = true;
                if (role.kind == RoleKind::Click) clickHeard = true;
            }
        }
    }

    for (size_t a = 0; a < rig.outputs.size(); ++a) {
        const auto& oa = rig.outputs[a];
        if (oa.label == OutputLabel::Unused) continue;
        for (size_t b = a + 1; b < rig.outputs.size(); ++b) {
            const auto& ob = rig.outputs[b];
            if (ob.label == OutputLabel::Unused) continue;
            if (const int lo = firstSharedChannel(oa, ob); lo != 0)
                out.push_back({Kind::ChannelsOverlap, oa.id, {},
                               oa.name + " and " + ob.name + " both use output " + juce::String(lo) + ".", true});
        }
    }

    if (deviceChannels > 0)
        for (auto& o : rig.outputs)
            if (o.label != OutputLabel::Unused && lastChannel(o) > deviceChannels)
                out.push_back({Kind::BeyondDevice, o.id, {},
                               o.name + " uses output" + (o.stereo ? "s " : " ") + channelsText(o) + ", but this interface has "
                                   + juce::String(deviceChannels) + ".",
                               false});

    if (!anyHeard)
        out.push_back({Kind::NothingHeard, {}, {}, "Nothing is sent to any output, so nothing would be heard.", true});
    if (hasClickRole && !clickHeard)
        out.push_back({Kind::ClickNotHeard, {}, {}, "The click isn't sent to any output.", false});
    return out;
}

std::shared_ptr<const engine::MixSetup> buildMixSetup(const BandDoc& band, const RigDoc& rig, int deviceChannels) {
    auto mix = std::make_shared<engine::MixSetup>();
    mix->roleCount = static_cast<int>(std::min<size_t>(band.roles.size(), engine::kMaxRoles));
    for (int i = 0; i < mix->roleCount; ++i) {
        const auto kind = band.roles[static_cast<size_t>(i)].kind;
        if (kind == RoleKind::Click && mix->clickRole < 0) mix->clickRole = i;
        if (kind == RoleKind::Cues && mix->cuesRole < 0) mix->cuesRole = i;
        if (kind == RoleKind::Pad && mix->padRole < 0) mix->padRole = i;
    }

    std::vector<const OutputDoc*> used;
    for (auto& o : rig.outputs) {
        if (used.size() >= static_cast<size_t>(engine::kMaxBuses)) break;
        if (hasBus(o, deviceChannels)) used.push_back(&o);
    }
    // Every device channel the PA is on. An in-ear output that shares one of them (only a hand-edited file can have
    // that) would carry the click into the PA, so the click and cues are kept off the whole of it.
    std::set<int> pa;
    for (auto& o : rig.outputs)
        if (o.label == OutputLabel::Foh)
            for (int c = o.firstChannel; c <= lastChannel(o); ++c) pa.insert(c);
    const auto touchesPa = [&](const OutputDoc& o) {
        for (int c = o.firstChannel; c <= lastChannel(o); ++c)
            if (pa.count(c) != 0) return true;
        return false;
    };

    const size_t n = used.size();
    mix->gain.assign(static_cast<size_t>(mix->roleCount) * n, 0.0f);
    for (size_t b = 0; b < n; ++b) {
        const auto& o = *used[b];
        const bool nearPa = touchesPa(o);
        mix->buses.push_back({o.firstChannel - 1, o.stereo, dbToGain(o.trimDb)});
        for (auto& [roleId, db] : o.mix) {
            const int ri = roleIndex(band, roleId);
            if (ri < 0 || ri >= mix->roleCount) continue;
            const auto kind = band.roles[static_cast<size_t>(ri)].kind;
            if (!mayRoute(kind, o.label) || (nearPa && !mayRoute(kind, OutputLabel::Foh))) continue;
            mix->gain[static_cast<size_t>(ri) * n + b] = dbToGain(db);
        }
    }
    return mix;
}

int busIndexOf(const RigDoc& rig, const juce::String& outputId, int deviceChannels) noexcept {
    int bus = 0;
    for (auto& o : rig.outputs) {
        const bool has = bus < engine::kMaxBuses && hasBus(o, deviceChannels);
        if (o.id == outputId) return has ? bus : -1;
        if (has) ++bus;
    }
    return -1;
}

}  // namespace sqlive::model
