#include "ui/fx/params/ModuleControls.h"

// Copied from Smart Q (smarteq_vst src/plugin/ModuleControls.cpp): see ModuleControls.h.

#include <string_view>

namespace smartq {

// A type Smart Q Live has no table of has no controls.
std::span<const ControlSpec> controlsOf(ModuleType type) {
    switch (type) {
        case ModuleType::Eq: return kEqControls;
        case ModuleType::Dynamic: return kDynamicControls;
        case ModuleType::DeEss: return kDeEssControls;
        case ModuleType::Limit: return kLimitControls;
        case ModuleType::Width: return kWidthControls;
        case ModuleType::Transient: return kTransientControls;
        case ModuleType::Gate:
        case ModuleType::Drive:
        case ModuleType::Multiband:
        case ModuleType::Resonance: return {};
    }
    jassertfalse;
    return kEqControls;
}

int controlIndex(ModuleType type, const char* field) {
    return indexOfControl(controlsOf(type), field);
}

const char* typeName(ModuleType type) {
    switch (type) {
        case ModuleType::Eq: return "eq";
        case ModuleType::Dynamic: return "dynamic";
        case ModuleType::Gate: return "gate";
        case ModuleType::DeEss: return "deess";
        case ModuleType::Drive: return "drive";
        case ModuleType::Limit: return "limit";
        case ModuleType::Width: return "width";
        case ModuleType::Multiband: return "multiband";
        case ModuleType::Transient: return "transient";
        case ModuleType::Resonance: return "resonance";
    }
    jassertfalse;
    return "eq";
}

bool reportsReduction(ModuleType type) noexcept {
    switch (type) {
        case ModuleType::Eq:
        case ModuleType::Drive:
        case ModuleType::Width: return false;
        case ModuleType::Dynamic:
        case ModuleType::Gate:
        case ModuleType::DeEss:
        case ModuleType::Limit:
        case ModuleType::Multiband:
        case ModuleType::Transient:
        case ModuleType::Resonance: return true;
    }
    jassertfalse;
    return false;
}

bool reportsBoost(ModuleType type) noexcept { return type == ModuleType::Transient; }

}  // namespace smartq
