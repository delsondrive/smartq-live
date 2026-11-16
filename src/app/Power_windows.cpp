#include "app/Power.h"

#include <windows.h>

namespace sqlive::app {

PowerState powerState() {
    SYSTEM_POWER_STATUS status{};
    if (! GetSystemPowerStatus(&status) || status.ACLineStatus == 255) return {};
    PowerState state;
    state.known = true;
    state.onBattery = status.ACLineStatus == 0;
    state.percent = status.BatteryLifePercent == 255 ? 100 : static_cast<int>(status.BatteryLifePercent);
    return state;
}

}  // namespace sqlive::app
