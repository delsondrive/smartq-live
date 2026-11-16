#pragma once

#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/ShowState.h"

namespace sqlive::ui {

// The mockup's .setstrip: the setlist as a row of cards across the top of the main view, 168 px wide and 8 px apart.
// Each card: its number, its title (shortened with "…" to fit), its length and its note. Played cards at 45%; the
// playing card with the mint outline, the "NOW" tag and a 3 px mint bar along its bottom for how far it has played; the
// next card with a dashed ember outline and the "NEXT" tag. With no songs it says "No songs yet".
//
// It scrolls sideways by itself, to keep the playing and the next card in view whenever either changes (or its size
// does), and with the mouse wheel. A click on a card makes it the next song (Session::chooseNext). It never takes the
// keyboard. set() repaints only the cards that changed; titles are fitted again only when a title or the count changes.
class SetlistStrip : public juce::Component {
public:
    static constexpr int idealHeight = 99;                        // 12 px, a 74 px card, 12 px, the 1 px line below

    explicit SetlistStrip(app::Session&);

    void set(const std::vector<ShowState::Card>&);
    juce::String drawnTitle(int card) const;                      // for tests: the card's title as laid out
    juce::Rectangle<int> cardBounds(int card) const;              // where it is drawn now, scrolled
    bool isEmptyShown() const { return cards_.empty(); }          // for tests: "No songs yet" is drawn
    int titlesFitted() const { return titles_fitted_; }           // for tests: how many times the titles were fitted

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    int contentWidth() const;
    void setScroll(int);
    void keepInView();                                            // the playing and the next card
    void fitTitles();
    void paintCard(juce::Graphics&, const ShowState::Card&, int index) const;

    app::Session& session_;
    std::vector<ShowState::Card> cards_;
    std::vector<juce::String> drawnTitles_;
    int scroll_ = 0, playing_ = -1, next_ = -1, titles_fitted_ = 0;
};

}  // namespace sqlive::ui
