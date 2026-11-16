#include <memory>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/App.h"
#include "app/BuildInfo.h"
#include "app/Settings.h"
#include "ui/LiveLookAndFeel.h"

namespace {

class SmartQLiveApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Smart Q Live"; }
    const juce::String getApplicationVersion() override { return sqlive::buildInfo().version; }
    bool moreThanOneInstanceAllowed() override { return false; }  // one app at a time drives the audio interface

    void initialise(const juce::String&) override {
        look_ = std::make_unique<sqlive::ui::LiveLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel(look_.get());
        app_ = std::make_unique<sqlive::app::App>(sqlive::app::Settings::defaultFile(), getCommandLineParameterArray());
        app_->onQuit = [] { juce::JUCEApplication::quit(); };
        if (!app_->wantsWindow()) quit();
    }

    // Windows shutting down, logging off or restarting for an update: never while a song plays in Show mode.
    void systemRequestedQuit() override {
        if (app_ == nullptr || app_->mayQuitNow()) quit();
    }

    void shutdown() override {
        app_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        look_.reset();
    }

private:
    std::unique_ptr<sqlive::ui::LiveLookAndFeel> look_;
    std::unique_ptr<sqlive::app::App> app_;
};

}  // namespace

START_JUCE_APPLICATION(SmartQLiveApp)
