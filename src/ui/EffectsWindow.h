#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/EffectsChainColumn.h"
#include "ui/Widgets.h"
#include "ui/fx/HoldButton.h"

namespace smartq {
class ChainHost;
class ModuleHistory;
class ModulePage;
class SmartQLookAndFeel;
}  // namespace smartq
namespace sqlive::ui::fx { class ChainParams; }

namespace sqlive::ui {

// The effects window (docs/mockups/effects.html, #chain): one part's chain (in one song) or one output's (in the rig), laid over
// the page, which it shades. A panel in the middle, 1580 x 900 at 1920 x 1080 and smaller to fit: a header with the place's dot
// and name, "Effects in “<song>” · heard on every output <part> goes to" (an output: "Effects on <output> · every song"),
// "‹ <previous>" and "<next> ›" (the song's parts in its order, the rig's outputs in its order; none at either end), "Hold to
// hear without effects" (Session::holdWithoutEffects while held) and close (Escape too); the chain column on the left
// (EffectsChainColumn); the shown effect's page from Smart Q on the right, in Smart Q's look; and a footer with whether the
// song is prepared for the show (Session::effectsState: "Changed · <song> will be prepared again for the show" in caution,
// "Preparing effects 40%" in caution, "Prepared" in mint), on an output with a delay "Delay 2.4 ms, all outputs kept
// together · a Limit switched off still delays them" (a Limit's delay stays while it is in a chain, so that nothing jumps in
// time as it is switched), and on the right "Every change is one undo step".
//
// Each change is the Session's command (addEffect, removeEffect, moveEffect, switchEffect, and setEffect from a page, one
// per gesture), named by the Session for the undo tip. The window shows what the documents say: after any change (its own,
// an undo, another computer's) it reads the chain again; a change of settings goes into the pages as they are, and a change
// of kinds or order makes the chain's parameters (ui::fx::ChainParams), its host (ui::fx::ChainHost, the one for the chain:
// the old one goes first), the histories and the cards again before anything else can reach them.
//
// A page's edit is sent only while the effect at its index is still the one the page was made for: the chain can change under
// it (an undo, a move on a strip, another computer) before the change message arrives, and the edit would land on another
// effect. An output's window shows the chosen rig's output only: choosing another rig closes it.
//
// It never opens in Show mode (show does nothing there), and entering Show mode closes it, which lets go of everything held
// (its host: Width's Mono, a De-Ess's Listen; and the hold button). Message thread.
class EffectsWindow : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    explicit EffectsWindow(app::Session&);
    ~EffectsWindow() override;

    // Opens on that place's chain, showing the page of the effect at that index (the first when there's none such), or with
    // index -1 the add menu open over the first effect's page. Nothing in Show mode, or for a place that doesn't exist.
    void show(const app::EffectsTarget&, int index);
    void close();
    bool isOpen() const noexcept { return open_; }

    // The place shown now, after ‹ or › moved to another (not after show).
    std::function<void(const app::EffectsTarget&)> onTargetChanged;

    const app::EffectsTarget& target() const noexcept { return target_; }
    int shownIndex() const noexcept { return shown_; }          // the effect whose page shows; -1 for none
    smartq::ModulePage* page() const noexcept { return page_.get(); }
    EffectsChainColumn& column() noexcept { return column_; }
    fx::ChainParams* params() const noexcept { return params_.get(); }
    juce::Button& previousButton() noexcept { return previous_; }
    juce::Button& nextButton() noexcept { return next_; }
    juce::Button& closeButton() noexcept { return close_; }
    smartq::HoldButton& holdButton() noexcept;
    const juce::String& title() const noexcept { return title_; }
    const juce::String& subtitle() const noexcept { return subtitle_; }

    struct Footer {
        juce::String state;                  // the song's preparation; "" for an output
        juce::Colour stateColour;
        juce::String delay;                  // an output's delay; "" for none, and for a part
        juce::String note{"Every change is one undo step"};
    };
    const Footer& footer() const noexcept { return footer_; }

    juce::Rectangle<int> panel() const;      // the window itself, in this component

    // One step of the display (the timer's, 30 times a second while open): the host, the histories, the cards and the page.
    void step(double dtSeconds);

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    class HoldKey;
    // What the window shows of the place, read from the documents.
    struct Place {
        juce::String name, songTitle;
        juce::Colour colour;
        bool stereo = false;
        std::vector<model::EffectDoc> effects;
        juce::String previousName, nextName;   // "" at either end
        bool operator==(const Place&) const = default;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    // close(), and from the destructor without moving the keyboard focus (the shell holding this may be going too).
    void shut(bool moveFocus);

    // Reads the documents again: closes in Show mode or when the place has gone; else the header, the chain (made again when
    // its kinds or order changed, else loaded), the add menu and the footer.
    void refresh();
    bool readPlace(Place&) const;
    void rebuildChain(const std::vector<model::EffectDoc>&, bool stereo);
    void dropChain();
    void showEffect(int index);
    void dropPage();
    void refreshHeader(const Place&);
    void refreshFooter();
    void moveTo(int step);                   // -1 the previous place, +1 the next
    app::EffectsTarget neighbour(int step) const;
    juce::Rectangle<int> pageArea() const;

    void addEffect(const juce::String& kind);
    void removeEffect(int index);
    void moveEffect(int from, int to);

    app::Session& session_;
    bool open_ = false;
    app::EffectsTarget target_;
    model::EffectPlace place_ = model::EffectPlace::Part;
    bool stereo_ = false;
    int shown_ = -1;
    int wanted_ = -1;                        // the effect to show once the chain has it (after an add, a move, a remove)
    Place seen_;                             // as last read
    // The chain as last read, with each edit sent from here since: a page's edit goes only onto the effect it was made for.
    std::vector<model::EffectDoc> known_;
    juce::String title_, subtitle_;
    juce::Colour dot_;
    Footer footer_;
    double lastStepMs_ = 0.0, footerSeconds_ = 0.0;

    // The chain, in the order it goes: the page first, then the column's cards (their miniatures), the histories, the host,
    // and its parameters last.
    std::unique_ptr<fx::ChainParams> params_;
    std::unique_ptr<smartq::ChainHost> host_;
    std::vector<std::unique_ptr<smartq::ModuleHistory>> histories_;   // by effect index; null for a kind with none
    std::unique_ptr<smartq::SmartQLookAndFeel> smartqLook_;
    juce::Component pageHolder_;             // in Smart Q's look
    EffectsChainColumn column_;
    std::unique_ptr<smartq::ModulePage> page_;

    PillButton previous_{{}}, next_{{}}, close_{juce::String::fromUTF8("\xe2\x9c\x95")};
    std::unique_ptr<HoldKey> hold_;          // last, so letting go as it goes finds the session and the place
};

}  // namespace sqlive::ui
