#include "ui/fx/params/Slots.h"

#include <array>

namespace smartq {

namespace {

constexpr std::array<SlotInfo, kSlotCount> kSlots{{
    {"eq1", ModuleType::Eq, "EQ", 0},
    {"eq2", ModuleType::Eq, "EQ 2", 1},
    {"eq3", ModuleType::Eq, "EQ 3", 2},
    {"eq4", ModuleType::Eq, "EQ 4", 3},
    {"dynamic1", ModuleType::Dynamic, "Dynamic", 0},
    {"dynamic2", ModuleType::Dynamic, "Dynamic 2", 1},
    {"dynamic3", ModuleType::Dynamic, "Dynamic 3", 2},
    {"dynamic4", ModuleType::Dynamic, "Dynamic 4", 3},
    {"deess1", ModuleType::DeEss, "De-Ess", 0},
    {"deess2", ModuleType::DeEss, "De-Ess 2", 1},
    {"deess3", ModuleType::DeEss, "De-Ess 3", 2},
    {"deess4", ModuleType::DeEss, "De-Ess 4", 3},
    {"limit1", ModuleType::Limit, "Limit", 0},
    {"limit2", ModuleType::Limit, "Limit 2", 1},
    {"limit3", ModuleType::Limit, "Limit 3", 2},
    {"limit4", ModuleType::Limit, "Limit 4", 3},
    {"width1", ModuleType::Width, "Width", 0},
    {"width2", ModuleType::Width, "Width 2", 1},
    {"width3", ModuleType::Width, "Width 3", 2},
    {"width4", ModuleType::Width, "Width 4", 3},
    {"transient1", ModuleType::Transient, "Transient", 0},
    {"transient2", ModuleType::Transient, "Transient 2", 1},
    {"transient3", ModuleType::Transient, "Transient 3", 2},
    {"transient4", ModuleType::Transient, "Transient 4", 3},
}};

}  // namespace

const SlotInfo& slotInfo(int slot) {
    jassert(slot >= 0 && slot < kSlotCount);
    return kSlots[static_cast<size_t>(juce::jlimit(0, kSlotCount - 1, slot))];
}

int slotFromId(const juce::String& id) {
    for (int slot = 0; slot < kSlotCount; ++slot)
        if (id == kSlots[static_cast<size_t>(slot)].id) return slot;
    return -1;
}

int typeIndexOf(int slot, ModuleType type) noexcept {
    if (slot < 0 || slot >= kSlotCount) return -1;
    const SlotInfo& info = kSlots[static_cast<size_t>(slot)];
    return info.type == type ? info.typeIndex : -1;
}

int slotOf(ModuleType type, int typeIndex) noexcept {
    for (int slot = 0; slot < kSlotCount; ++slot)
        if (typeIndex >= 0 && typeIndexOf(slot, type) == typeIndex) return slot;
    return -1;
}

}  // namespace smartq
