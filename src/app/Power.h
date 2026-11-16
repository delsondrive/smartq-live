#pragma once

namespace sqlive::app {

struct PowerState { bool known = false; bool onBattery = false; int percent = 100; };

// What the operating system says about the laptop's power. known = false when it cannot tell. Power_*.cpp.
PowerState powerState();

}  // namespace sqlive::app
