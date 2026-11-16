#include "ui/WelcomePage.h"

#include <algorithm>

#include "model/BandFolder.h"
#include "ui/Logo.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr int kPadX = 48, kPadY = 44, kLogoHeight = 40, kGap = 16, kRowHeight = 60, kRowGap = 12, kButtonHeight = 30;

const juce::String kArrow = juce::String::fromUTF8("\xe2\x80\xba");        // ›
const juce::String kDot = juce::String::fromUTF8("\xc2\xb7");              // ·
const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");     // …

juce::String counted(int n, const juce::String& one, const juce::String& many) { return juce::String(n) + " " + (n == 1 ? one : many); }

juce::String bandName(const juce::File& folder) {
    const auto parsed = juce::JSON::parse(folder.getChildFile("band.json"));
    const auto name = parsed.getProperty("name", {}).toString();
    return name.isNotEmpty() ? name : folder.getFileName();
}

}  // namespace

WelcomeState makeWelcomeState(const app::Settings& settings) {
    WelcomeState state;
    for (const auto& folder : settings.recentBandFolders()) {
        WelcomeState::Recent recent;
        recent.folder = folder;
        recent.name = bandName(folder);
        const int songs = folder.getChildFile("songs").findChildFiles(juce::File::findDirectories, false).size();
        const int setlists = folder.getChildFile("setlists").findChildFiles(juce::File::findFiles, false, "*.json").size();
        recent.detail = folder.getParentDirectory().getFileName() + " " + kArrow + " " + folder.getFileName() + " " + kDot + " "
                        + counted(songs, "song", "songs") + ", " + counted(setlists, "setlist", "setlists");
        state.recent.push_back(std::move(recent));
    }
    return state;
}

WelcomePage::WelcomePage(app::Settings& settings)
    : settings_(settings),
      another_("Open another folder" + kEllipsis),
      new_("Start a new band folder" + kEllipsis),
      demo_("Try the demo band") {
    for (auto* button : {&another_, &new_, &demo_}) addAndMakeVisible(*button);
    another_.onClick = [this] { openAnother(); };
    new_.onClick = [this] { startNew(); };
    demo_.onClick = [this] { if (onDemo) onDemo(); };
    refresh();
}

void WelcomePage::refresh() {
    state_ = makeWelcomeState(settings_);
    openButtons_.clear();
    for (std::size_t i = 0; i < state_.recent.size(); ++i) {
        auto button = std::make_unique<PillButton>("Open", i == 0 ? PillButton::Style::Primary : PillButton::Style::Plain);
        const auto folder = state_.recent[i].folder;
        button->onClick = [this, folder] { if (onOpen) onOpen(folder); };
        addAndMakeVisible(*button);
        openButtons_.push_back(std::move(button));
    }
    resized();
    repaint();
}

int WelcomePage::visibleRows() const {
    // What is left under the logo and the sentence, less the buttons, the problem line and the hint.
    const int top = kPadY + kLogoHeight + kGap + 22 + kGap;
    const int below = kButtonHeight + 8 + 18 + kGap + 34 + kPadY;
    const int room = getHeight() - top - below;
    const int rows = std::max(1, (room + kRowGap) / (kRowHeight + kRowGap));
    return std::min<int>(rows, static_cast<int>(state_.recent.size()));
}

juce::Rectangle<int> WelcomePage::rowArea(int index) const {
    const int top = kPadY + kLogoHeight + kGap + 22 + kGap;
    return {kPadX, top + index * (kRowHeight + kRowGap), std::max(0, getWidth() - 2 * kPadX), kRowHeight};
}

void WelcomePage::resized() {
    const int rows = visibleRows();
    for (std::size_t i = 0; i < openButtons_.size(); ++i) {
        auto& button = *openButtons_[i];
        const bool shown = static_cast<int>(i) < rows;
        button.setVisible(shown);
        if (!shown) continue;
        const auto row = rowArea(static_cast<int>(i));
        const int w = button.idealWidth();
        button.setBounds(row.getRight() - 14 - w, row.getCentreY() - kButtonHeight / 2, w, kButtonHeight);
    }
    const int y = rows > 0 ? rowArea(rows - 1).getBottom() + kGap : kPadY + kLogoHeight + kGap + 22 + kGap;
    int x = kPadX;
    for (auto* button : {&another_, &new_, &demo_}) {
        const int w = button->idealWidth();
        button->setBounds(x, y, w, kButtonHeight);
        x += w + 10;
    }
}

void WelcomePage::paint(juce::Graphics& g) {
    theme::paintAppBackground(g, getLocalBounds());
    drawLogo(g, juce::Rectangle<float>(static_cast<float>(kPadX), static_cast<float>(kPadY), 600.0f, static_cast<float>(kLogoHeight)), true);

    g.setFont(theme::font(15.0f));
    g.setColour(theme::mid);
    g.drawText("Open your band folder to get started.", kPadX, kPadY + kLogoHeight + kGap, getWidth() - 2 * kPadX, 22,
               juce::Justification::centredLeft, true);

    const int rows = visibleRows();
    for (int i = 0; i < rows; ++i) {
        const auto row = rowArea(i).toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(row, theme::radiusPanel);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(row.reduced(0.5f), theme::radiusPanel, 1.0f);

        // A folder: a tab and a body.
        const auto icon = juce::Rectangle<float>(row.getX() + 14.0f, row.getCentreY() - 11.0f, 28.0f, 22.0f);
        g.setColour(theme::dim);
        g.fillRoundedRectangle(icon.getX(), icon.getY(), 12.0f, 7.0f, 2.0f);
        g.fillRoundedRectangle(icon.getX(), icon.getY() + 3.0f, icon.getWidth(), icon.getHeight() - 3.0f, 3.0f);

        const int textX = static_cast<int>(row.getX()) + 14 + 28 + 12;
        const int textW = std::max(0, openButtons_[static_cast<std::size_t>(i)]->getX() - 12 - textX);
        const auto& recent = state_.recent[static_cast<std::size_t>(i)];
        g.setColour(theme::text);
        g.setFont(theme::font(16.0f, true));
        g.drawText(recent.name, textX, static_cast<int>(row.getY()) + 10, textW, 22, juce::Justification::centredLeft, true);
        g.setColour(theme::dim);
        g.setFont(theme::font(theme::sizeSmall));
        g.drawText(recent.detail, textX, static_cast<int>(row.getY()) + 32, textW, 18, juce::Justification::centredLeft, true);
    }

    if (problem_.isNotEmpty()) {
        g.setColour(theme::danger);
        g.setFont(theme::font(theme::sizeSmall));
        g.drawText(problem_, kPadX, another_.getBottom() + 8, getWidth() - 2 * kPadX, 18, juce::Justification::centredLeft, true);
    }

    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawFittedText("A band folder is an ordinary folder. Keep it in OneDrive or iCloud Drive to share it with the band and to have a backup.",
                     kPadX, getHeight() - kPadY - 34, getWidth() - 2 * kPadX, 34, juce::Justification::bottomLeft, 2);
}

void WelcomePage::say(const juce::String& problem) {
    problem_ = problem;
    repaint();
}

void WelcomePage::openAnother() {
    say({});
    const auto start = state_.recent.empty() ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                             : state_.recent.front().folder.getParentDirectory();
    chooser_ = std::make_unique<juce::FileChooser>("Open your band folder", start, juce::String(), true);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<WelcomePage>(this)](const juce::FileChooser& chooser) {
                              if (safe == nullptr) return;
                              const auto folder = chooser.getResult();
                              if (folder == juce::File()) return;
                              if (model::BandFolder::looksLikeBandFolder(folder)) {
                                  if (safe->onOpen) safe->onOpen(folder);
                              } else {
                                  safe->say("That folder isn't a band folder. Choose the folder that has band.json in it.");
                              }
                          });
}

void WelcomePage::startNew() {
    say({});
    auto* window = new juce::AlertWindow("Start a new band folder", "What is the band called?", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", {});
    window->addButton("Next", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<WelcomePage>(this), window](int result) {
                                const auto name = window->getTextEditorContents("name").trim();
                                if (result == 1 && name.isNotEmpty() && safe != nullptr) safe->askWhere(name);
                            }),
                            true);
}

void WelcomePage::askWhere(const juce::String& bandName) {
    chooser_ = std::make_unique<juce::FileChooser>("Where should the band folder go?",
                                                   juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), juce::String(), true);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [safe = juce::Component::SafePointer<WelcomePage>(this), bandName](const juce::FileChooser& chooser) {
                              if (safe == nullptr) return;
                              const auto parent = chooser.getResult();
                              if (parent == juce::File()) return;
                              const auto root = parent.getChildFile(juce::File::createLegalFileName(bandName));
                              const auto made = model::BandFolder::create(root, bandName);
                              if (made.failed()) safe->say(made.getErrorMessage());
                              else if (safe->onCreate) safe->onCreate(root, bandName);
                          });
}

}  // namespace sqlive::ui
