#pragma once

#include <juce_core/juce_core.h>

// Copied from Smart Q (smarteq_vst src/plugin/Slots.h) and adapted for Smart Q Live: only the six kinds Live plays, four slots of
// each (a Live chain holds at most four effects, a kind possibly four times), so that any chain fits. The EQs stay the first slots,
// so an EQ's slot is its number among the EQs, as the pages take it. Ids keep Smart Q's scheme ("eq1", "dynamic3").

namespace smartq {

// The chain's slots. A slot's number and ID are permanent: parameter IDs name them.
inline constexpr int kEqSlots = 4;
inline constexpr int kDynamicSlots = 4;
inline constexpr int kDeEssSlots = 4, kLimitSlots = 4;
inline constexpr int kWidthSlots = 4, kTransientSlots = 4;
inline constexpr int kSlotCount = kEqSlots + kDynamicSlots + kDeEssSlots + kLimitSlots + kWidthSlots + kTransientSlots;

// Smart Q's types, all of them, so that what the copied pages say of each stays as it is; Live has no slot of a Gate, a Drive, a
// Multiband or a Resonance.
enum class ModuleType { Eq, Dynamic, Gate, DeEss, Drive, Limit, Width, Multiband, Transient, Resonance };

struct SlotInfo {
    const char* id;           // the prefix of the slot's parameter IDs
    ModuleType type;
    const char* displayName;
    int typeIndex;            // which of its type's slots this is, 0-based
};

// An out-of-range slot is a programming error. It answers with the first slot's info.
const SlotInfo& slotInfo(int slot);

// -1 if no slot has this ID.
int slotFromId(const juce::String& id);

// Where a slot stands among the slots of its type, and back. Every per-type array is indexed by typeIndex, and only these turn a
// slot into one.
int typeIndexOf(int slot, ModuleType) noexcept;  // slotInfo(slot).typeIndex if the slot is of that type, otherwise -1
int slotOf(ModuleType, int typeIndex) noexcept;  // -1 if there's none

}  // namespace smartq
