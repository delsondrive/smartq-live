#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "model/Documents.h"
#include "model/Effects.h"

namespace smartq {
class ModuleHistory;
class ModuleMiniature;
}  // namespace smartq

namespace sqlive::ui {

// One kind the add menu offers: its name, its hint, and why it can't be added now ("" when it can).
struct AddChoice {
    juce::String kind, name, hint, refusal;
    bool operator==(const AddChoice&) const = default;
};

// The add menu's hint for a kind, as the mockup's has them: "tone", "evens out", "harsh “s”", "stereo, mono bass",
// "attack, tail", "ceiling"; "" for a kind not known.
juce::String effectHint(const juce::String& kind);

// What the add menu of that chain offers: model::effectKindsFor the place, in its order, each refused with the sentence the
// "effects.add" command would refuse it with (model::whyEffectCantBeAdded), so the menu says it before the command runs.
std::vector<AddChoice> addChoicesFor(const std::vector<model::EffectDoc>&, model::EffectPlace, bool stereo);

// The effects window's left column (the mockup's #chain .chain): Smart Q Live's own, not Smart Q's ChainColumn. A card per
// effect, in playing order: a grip, its name, how far it is turning the audio down (Dynamic, De-Ess and Limit), its power
// and remove buttons, and its miniature; the card on show outlined in mint. Under them "+ Add effect", which opens a menu
// of the kinds the place takes, each greyed with the reason when it can't be added; at the foot "2 of 4 slots used.".
//
// It changes nothing itself: a click on a card, its buttons, a card carried to another place (moved when let go), and a
// choice in the menu each tell the owner, which runs the command and, once the chain has changed, gives the column its
// cards again. None of them makes the cards again inside the call. The owner steps it (the readings and the miniatures).
// Message thread.
class EffectsChainColumn : public juce::Component {
public:
    static constexpr int width = 280;   // the mockup's .chain

    // An effect as its card shows it.
    struct Effect {
        juce::String kind;
        bool on = true;
        std::unique_ptr<smartq::ModuleMiniature> miniature;   // null: none
        const smartq::ModuleHistory* history = nullptr;       // its readings: a Dynamic's, a De-Ess's and a Limit's
    };

    class Card;
    class AddMenu;

    EffectsChainColumn();
    ~EffectsChainColumn() override;

    // The cards made again, for a chain whose kinds or order changed (or another chain): the menu closes, a drag ends.
    void setEffects(std::vector<Effect>);
    // A card's power as the chain has it now, without making the cards again.
    void setOn(int index, bool on);
    // The card on show, -1 for none.
    void setSelected(int index);
    int selected() const noexcept { return selected_; }
    // What the add menu offers; an open menu shows it at once.
    void setAddChoices(std::vector<AddChoice>);

    void openAddMenu();
    void closeAddMenu();
    bool addMenuOpen() const noexcept { return menu_ != nullptr; }
    AddMenu* addMenu() noexcept { return menu_.get(); }

    // One step of the display: each card's reading (ten times a second) and its miniature.
    void step(double dtSeconds);

    std::function<void(int index)> onSelect, onRemove;
    std::function<void(int index, bool on)> onSwitch;
    std::function<void(int from, int to)> onMove;
    std::function<void(const juce::String& kind)> onAdd;

    int cardCount() const noexcept { return static_cast<int>(cards_.size()); }
    Card& card(int index) { return *cards_[static_cast<size_t>(index)]; }
    juce::Component& addButton();
    juce::String slotsText() const;     // "2 of 4 slots used."
    int dropIndex() const noexcept { return drag_.target; }   // where the card carried would land; -1 while none is

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    class AddButton;
    class IconButton;

    void layOutCards();
    juce::Rectangle<int> menuBounds() const;
    void beginDrag(int index);
    void moveDrag(int index, float columnY);
    void endDrag(int index);

    std::vector<std::unique_ptr<Card>> cards_;
    std::unique_ptr<AddButton> add_;
    std::unique_ptr<AddMenu> menu_;
    std::vector<AddChoice> choices_;
    int selected_ = -1;
    int count_ = 0;   // the effects in the chain, for the slots line
    struct Drag {
        int index = -1;    // the card carried
        int target = -1;   // where it would land
    } drag_;
};

// A card. A press on it and a release without carrying it shows its page; carried more than a few pixels, it moves.
class EffectsChainColumn::Card : public juce::Component {
public:
    Card(EffectsChainColumn&, int index, Effect);
    ~Card() override;

    const juce::String& kind() const noexcept { return kind_; }
    bool isOn() const noexcept { return on_; }
    bool isSelected() const noexcept { return selected_; }
    const juce::String& reading() const noexcept { return reading_; }   // "−3 dB"; "" for none, or under a tenth of a dB
    juce::Button& powerButton();
    juce::Button& removeButton();
    smartq::ModuleMiniature* miniature() noexcept { return miniature_.get(); }

    void setOn(bool);
    void setSelected(bool);
    void setLifted(bool);
    void step(double dtSeconds);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr int height = 101;   // the mockup's .mcard with its 58 px picture

private:
    EffectsChainColumn& column_;
    const int index_;
    const juce::String kind_;
    bool on_, selected_ = false, lifted_ = false, carried_ = false;
    std::unique_ptr<smartq::ModuleMiniature> miniature_;
    const smartq::ModuleHistory* history_;
    std::unique_ptr<IconButton> power_, remove_;
    juce::String reading_;
    double readingSeconds_ = 0.0, readingPeakDb_ = 0.0;
    juce::Rectangle<int> header_, picture_, readingArea_;
};

// The add menu (the mockup's .menu): one line per kind, its colour, its name and its hint; one that can't be added greyed,
// its reason as its tooltip, and a click on it does nothing.
class EffectsChainColumn::AddMenu : public juce::Component {
public:
    class Item : public juce::Component, public juce::SettableTooltipClient {
    public:
        Item(AddMenu&, AddChoice);
        const AddChoice& choice() const noexcept { return choice_; }
        bool isRefused() const noexcept { return choice_.refusal.isNotEmpty(); }
        void paint(juce::Graphics&) override;
        void mouseEnter(const juce::MouseEvent&) override { repaint(); }
        void mouseExit(const juce::MouseEvent&) override { repaint(); }
        void mouseUp(const juce::MouseEvent&) override;

    private:
        AddMenu& menu_;
        AddChoice choice_;
    };

    AddMenu(EffectsChainColumn&, const std::vector<AddChoice>&);

    int itemCount() const noexcept { return static_cast<int>(items_.size()); }
    Item& item(int index) { return *items_[static_cast<size_t>(index)]; }
    int idealHeight() const;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 250, itemHeight = 32, pad = 6;

private:
    EffectsChainColumn& column_;
    std::vector<std::unique_ptr<Item>> items_;
};

}  // namespace sqlive::ui
