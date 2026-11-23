#include "ui/fx/ModulePages.h"

#include <algorithm>

#include "ui/fx/ChainParams.h"
#include "ui/fx/DeEssPage.h"
#include "ui/fx/DynamicPage.h"
#include "ui/fx/EqPage.h"
#include "ui/fx/LimitPage.h"
#include "ui/fx/TransientPage.h"
#include "ui/fx/WidthPage.h"

namespace sqlive::ui::fx {

namespace {

using smartq::ModuleType;

// Whether the slot is the kind's, and in the host's chain.
bool fitsSlot(const juce::String& kind, ChainHost& host, int slot) {
    const auto type = moduleTypeOf(kind);
    if (!type || slot < 0 || slot >= smartq::kSlotCount || smartq::slotInfo(slot).type != *type) return false;
    const auto slots = host.chain();
    return std::find(slots.begin(), slots.end(), slot) != slots.end();
}

}  // namespace

std::unique_ptr<smartq::ModulePage> makeModulePage(const juce::String& kind, ChainHost& host, int slot) {
    if (!fitsSlot(kind, host, slot)) return nullptr;
    switch (smartq::slotInfo(slot).type) {
        case ModuleType::Eq: return std::make_unique<smartq::EqPage>(host, smartq::slotInfo(slot).typeIndex);
        case ModuleType::Dynamic: return std::make_unique<smartq::DynamicPage>(host, slot);
        case ModuleType::DeEss: return std::make_unique<smartq::DeEssPage>(host, slot);
        case ModuleType::Width: return std::make_unique<smartq::WidthPage>(host, slot);
        case ModuleType::Transient: return std::make_unique<smartq::TransientPage>(host, slot);
        case ModuleType::Limit: return std::make_unique<smartq::LimitPage>(host, slot);
        case ModuleType::Gate:
        case ModuleType::Drive:
        case ModuleType::Multiband:
        case ModuleType::Resonance: break;
    }
    return nullptr;
}

std::unique_ptr<smartq::ModuleMiniature> makeMiniature(const juce::String& kind, ChainHost& host, int slot) {
    return fitsSlot(kind, host, slot) ? smartq::makeMiniature(host, slot, smartq::MiniatureData::Stored) : nullptr;
}

}  // namespace sqlive::ui::fx
