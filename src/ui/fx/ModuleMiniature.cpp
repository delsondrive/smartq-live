#include "ui/fx/ModuleMiniature.h"

// From Smart Q's ModuleCard.cpp (smarteq_vst src/ui/ModuleCard.cpp): see ModuleMiniature.h.

#include "ui/fx/ChainHost.h"
#include "ui/fx/DeEssMiniature.h"
#include "ui/fx/DynamicMiniature.h"
#include "ui/fx/EqMiniature.h"
#include "ui/fx/LimitMiniature.h"
#include "ui/fx/TransientMiniature.h"
#include "ui/fx/WidthMiniature.h"

namespace smartq {

void ModuleMiniature::setDimmed(bool dimmed) {
    if (dimmed == dimmed_) return;
    dimmed_ = dimmed;
    repaint();
}

// Smart Q Live has no slot of the other types.
std::unique_ptr<ModuleMiniature> makeMiniature(ChainHost& processor, int slot, MiniatureData data) {
    switch (slotInfo(slot).type) {
        case ModuleType::Eq: return std::make_unique<EqMiniature>(processor, slot, data);
        case ModuleType::Dynamic: return std::make_unique<DynamicMiniature>(processor, slot, data);
        case ModuleType::DeEss: return std::make_unique<DeEssMiniature>(processor, slot, data);
        case ModuleType::Limit: return std::make_unique<LimitMiniature>(processor, slot, data);
        case ModuleType::Width: return std::make_unique<WidthMiniature>(processor, slot, data);
        case ModuleType::Transient: return std::make_unique<TransientMiniature>(processor, slot, data);
        case ModuleType::Gate:
        case ModuleType::Drive:
        case ModuleType::Multiband:
        case ModuleType::Resonance: break;
    }
    return nullptr;
}

}  // namespace smartq
