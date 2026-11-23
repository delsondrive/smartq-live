#include "ui/fx/EqPage.h"

namespace smartq {

EqPage::EqPage(ChainHost& processor, int eq) : graph_(processor, eq), panel_(processor, eq) {
    addAndMakeVisible(graph_);
    addAndMakeVisible(panel_);
    panel_.addMouseListener(this, true);

    panel_.onBandChosen = [this](int band) {
        graph_.selectBand(band);
        panel_.showBand(band);
    };
    panel_.refresh(graph_.bands());
    panel_.showBand(graph_.selectedBand());
    panel_.refresh(graph_.bands());
}

// What the graph and the panel hold open closes with them: a gesture on the graph in their own members, a knob's in the
// panel's destructor.
EqPage::~EqPage() { panel_.removeMouseListener(this); }

void EqPage::update(double dtSeconds) {
    graph_.tick(dtSeconds);

    if (panel_.band() != graph_.selectedBand() && !panel_.isControlBeingDragged()) panel_.showBand(graph_.selectedBand());
    panel_.refresh(graph_.bands(), dtSeconds);

    const int band = panel_.band();
    if (band >= 0) panel_.setAnchorX(graph_.mapping().freqToX(graph_.bands()[static_cast<size_t>(band)].frequencyHz));
    graph_.showValueTag(panel_.isControlBeingDragged() ? band : -1);
}

void EqPage::mouseDown(const juce::MouseEvent&) { graph_.endWheelGesture(); }

void EqPage::resized() {
    auto area = getLocalBounds();
    panel_.setBounds(area.removeFromBottom(kBandPanelHeight));
    graph_.setBounds(area);
}

}  // namespace smartq
