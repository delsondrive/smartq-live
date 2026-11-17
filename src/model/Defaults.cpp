#include "model/Defaults.h"

#include "model/DocumentsJson.h"

namespace sqlive::model {

juce::Colour roleColour(int index) {
    static const juce::uint32 colours[] = {0xffff9a68, 0xffe070ff, 0xffa8dc68, 0xff5ed8a2, 0xffe9d568,
                                           0xffb8c0cc, 0xff4fcabd, 0xffff7d93, 0xff9fb4ff};
    constexpr int n = (int) (sizeof(colours) / sizeof(colours[0]));
    return juce::Colour(colours[((index % n) + n) % n]);
}

std::vector<Role> defaultRoles() {
    std::vector<Role> roles;
    roles.push_back({newId(), "Click", RoleKind::Click, juce::Colour(0xfff29a78), {}});
    roles.push_back({newId(), "Cues", RoleKind::Cues, juce::Colour(0xffc9c2cf), {}});
    roles.push_back({newId(), "Pad", RoleKind::Pad, juce::Colour(0xff86b7a8), {}});
    const char* parts[] = {"Keys", "Synth", "Guitars", "Bass", "Drums", "Perc", "BVs", "Strings", "FX"};
    int i = 0;
    for (auto* name : parts) roles.push_back({newId(), name, RoleKind::Part, roleColour(i++), {}});
    return roles;
}

std::vector<SectionTypeDoc> defaultSectionTypes() {
    struct Type { const char* name; juce::uint32 colour; };
    static const Type types[] = {{"Intro", 0xff8491ff},        {"Verse", 0xff6ea8ff}, {"Pre-chorus", 0xff58d3e8},
                                 {"Chorus", 0xffffc163},       {"Bridge", 0xffb69dff}, {"Solo", 0xffff8fd2},
                                 {"Instrumental", 0xff9fd8a0}, {"Breakdown", 0xffe9d568}, {"Outro", 0xffa79fb0}};
    std::vector<SectionTypeDoc> out;
    for (const auto& t : types) {
        const juce::String name(t.name);
        out.push_back({name.toLowerCase(), name, juce::Colour(t.colour), "cues/built-in/" + name + ".wav", {}});
    }
    return out;
}

RigDoc defaultRig(const BandDoc& band) {
    RigDoc rig; rig.id = newId(); rig.name = "Headphones"; rig.sampleRate = band.sampleRate;
    OutputDoc out; out.id = newId(); out.name = "Headphones"; out.firstChannel = 1; out.stereo = true; out.label = OutputLabel::Monitor;
    for (auto& r : band.roles) out.mix[r.id] = 0.0f;
    rig.outputs.push_back(out);
    return rig;
}

}  // namespace sqlive::model
