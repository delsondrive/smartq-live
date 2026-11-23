#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/fx/BandPanel.h"
#include "ui/fx/EqGraphComponent.h"
#include "ui/fx/ModulePage.h"

namespace smartq {

class ChainHost;

inline constexpr int kBandPanelHeight = 112;

// One EQ's page: its graph over its band panel.
class EqPage final : public ModulePage {
public:
    EqPage(ChainHost&, int eq);
    ~EqPage() override;

    int eq() const noexcept { return graph_.eq(); }

    // Reads the parameters and the analyzer, and brings the panel into step with the graph.
    void update(double dtSeconds) override;
    bool anyLayerLost() const override { return graph_.anyLayerLost(); }
    void pageShown() override { graph_.clearSpectrum(); }
    void advanceGestures(double dtSeconds) override { graph_.advanceWheelGesture(dtSeconds); }
    void endGestures() override { graph_.endWheelGesture(); }

    EqGraphComponent& graph() noexcept { return graph_; }
    BandPanel& panel() noexcept { return panel_; }

    // Hears every press in the panel: a control taking over ends any scroll still held open on the graph.
    void mouseDown(const juce::MouseEvent&) override;
    void resized() override;

private:
    EqGraphComponent graph_;
    BandPanel panel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqPage)
};

}  // namespace smartq
