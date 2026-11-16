#include <juce_gui_basics/juce_gui_basics.h>

#include "app/BuildInfo.h"

namespace {

class MainWindow : public juce::DocumentWindow {
public:
    explicit MainWindow(std::function<void()> onClose)
        : juce::DocumentWindow("Smart Q Live", juce::Colours::black, juce::DocumentWindow::allButtons),
          closed_(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        centreWithSize(1000, 640);
        setVisible(true);
    }

    void closeButtonPressed() override { closed_(); }

private:
    std::function<void()> closed_;
};

class SmartQLiveApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Smart Q Live"; }
    const juce::String getApplicationVersion() override { return sqlive::buildInfo().version; }

    void initialise(const juce::String&) override {
        window_ = std::make_unique<MainWindow>([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    void shutdown() override { window_.reset(); }

private:
    std::unique_ptr<MainWindow> window_;
};

}  // namespace

START_JUCE_APPLICATION(SmartQLiveApp)
