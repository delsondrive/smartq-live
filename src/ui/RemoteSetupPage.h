#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/RemoteAccess.h"
#include "app/RemoteServer.h"
#include "app/Session.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// What Setup's Remote page shows (the top of the mockup's screen 14), in its words.
struct RemoteSetupState {
    bool on = false;                                              // the server runs
    juce::String address, pin;                                    // "http://192.168.1.20:8080", "4719"; both "" when off
    juce::StringArray addresses;                                  // every address a phone may open (address is one); none off
    struct Row {
        juce::String id, name, status;                            // status "waiting", "connected", "not connected"
        app::PhoneAccess access = app::PhoneAccess::Waiting;
        bool waiting = false, connected = false;
        bool operator==(const Row&) const = default;
    };
    std::vector<Row> rows;                                        // RemoteAccess::phones(), in the order they joined
    juce::String note;                                            // why a waiting phone can't be let in right now, or ""
    juce::String problem;                                         // off: why it couldn't start (RemoteServer::lastError)
    bool operator==(const RemoteSetupState&) const = default;
};

// Message thread. note: "New phones can be let in when the show is over." while a phone waits outside Edit mode. address:
// chosenRemoteAddress of the server's addresses and the one chosen on the page.
RemoteSetupState makeRemoteSetupState(const app::Session&, const app::RemoteAccess&, const app::RemoteServer&,
                                      const juce::String& chosenAddress = {});
// The address chosen, while it is one of them; else the one to offer first (app::preferredRemoteAddress); "" for none.
juce::String chosenRemoteAddress(const juce::StringArray& addresses, const juce::String& chosen);

// Setup's "Remote" page (the top of the mockup's screen 14; its backup laptop and timecode panels are stage 5's). One
// panel: "Phone remote" and its switch (on starts the server on the remembered port, then remembers that it is on and the
// port it got; off stops it and remembers that). While it is on: the QR code of the address (150 px, on white), "Address"
// (a choice of every address a phone may open when there are several, a virtual adapter's among them; the QR code follows
// the one chosen) and "PIN", with a small "New PIN" link. Then the phones, whenever it is on or knows any (so a lost
// phone can be forgotten with the remote off): PHONE / CAN / (action). A waiting phone's row is tinted caution (it needs a look)
// and offers "Full control" (primary) and "View only", which let it in (only in Edit mode: the note above the table says
// so otherwise, in caution), and "Refuse", which forgets it. A phone already let in has a choice of the two and "Forget".
// A connected phone has a mint dot before its name. With no phones: "No phones yet. Scan the code with a phone on the same
// Wi-Fi." Under the panel: "No internet is needed." Why the server couldn't start (from the switch, or when the band
// opened with the remote left on) is said in caution under the title.
//
// bindAddress is where the switch starts the server: "0.0.0.0" (every local address) in the app; tests give "127.0.0.1".
// Nothing here writes the band folder (RemoteAccess keeps this computer's settings), so it works in either mode. It
// refreshes on the access's changes, on a change of mode, and every 2 s while shown (the address follows the Wi-Fi).
class RemoteSetupPage : public juce::Component, private juce::ChangeListener, private juce::Timer {
public:
    RemoteSetupPage(app::Session&, app::RemoteAccess&, app::RemoteServer&, juce::String bindAddress = "0.0.0.0");
    ~RemoteSetupPage() override;

    const RemoteSetupState& state() const { return state_; }
    juce::Rectangle<int> rowBounds(int row) const;                // for tests: where a row of the table is; empty if not shown
    void refreshNow() { refresh(true); }                          // the Setup page, when it shows this page
    // For screenshots: draws this state, with no server running, until the page next refreshes.
    void showState(const RemoteSetupState&);

    // The gestures, by row (an index into state().rows).
    void setOn(bool);
    void newPin();
    void chooseAddress(int index);                                // one of state().addresses: shown, and the QR code of it
    void letIn(int row, app::PhoneAccess);                        // a waiting phone let in (Edit mode only), or changed
    void refuse(int row);
    void forget(int row);

    void paint(juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    struct RowControls;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh(bool force = false);
    void apply();                                                 // the children from state_, then the layout
    const RemoteSetupState::Row* rowAt(int row) const;

    app::Session& session_;
    app::RemoteAccess& access_;
    app::RemoteServer& server_;
    const juce::String bindAddress_;
    RemoteSetupState state_;
    app::Mode mode_;
    juce::String chosen_;                                         // the address chosen, "" for the one offered first
    juce::String qrFor_;                                          // the address qr_image_ was made for
    juce::Image qrImage_;

    Switch switch_;
    Choice addressChoice_;                                        // shown for several addresses
    std::vector<std::unique_ptr<RowControls>> rowControls_;      // one per row shown; never deleted while the page lives

    // Laid out by resized(), drawn by paint().
    juce::Rectangle<int> box_, qr_, title_, onLabel_, address_, addressValue_, pin_, pinValue_, newPin_, problemLine_,
        noteLine_, head_, empty_, internet_;
    std::vector<juce::Rectangle<int>> rows_, links_;              // each row, and its Refuse or Forget
    int canX_ = 0, actionX_ = 0;
};

}  // namespace sqlive::ui
