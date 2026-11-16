#include <juce_gui_basics/juce_gui_basics.h>

#include "app/BuildInfo.h"

namespace {

class MainWindow : public juce::DocumentWindow {
public:
    explicit MainWindow(std::function<void()> onClose)
        : juce::DocumentWindow("Smart Q Live", juce::Colours::black, juce::DocumentWindow::allButtons),
          closed(std::move(onClose)) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        centreWithSize(1000, 640);
        setVisible(true);
    }

    void closeButtonPressed() override { closed(); }

private:
    std::function<void()> closed;
};

class SmartQLiveApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Smart Q Live"; }
    const juce::String getApplicationVersion() override { return sqlive::buildInfo().version; }

    void initialise(const juce::String&) override {
        window = std::make_unique<MainWindow>([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    }

    void shutdown() override { window.reset(); }

private:
    std::unique_ptr<MainWindow> window;
};

}  // namespace

START_JUCE_APPLICATION(SmartQLiveApp)
