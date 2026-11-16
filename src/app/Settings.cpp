#include "app/Settings.h"

#include "model/AtomicFile.h"

namespace sqlive::app {

namespace {
constexpr int maxRecent = 8;
}

Settings::Settings(juce::File file) : file_(std::move(file)), bands_(new juce::DynamicObject()) {
    if (!file_.existsAsFile()) return;
    juce::var parsed;
    if (!juce::JSON::parse(file_.loadFileAsString(), parsed).wasOk() || !parsed.isObject()) return;

    if (auto* list = parsed.getProperty("recent", {}).getArray())
        for (const auto& item : *list)
            if (item.isString() && item.toString().isNotEmpty()) recent_.addIfNotAlreadyThere(item.toString());
    while (recent_.size() > maxRecent) recent_.remove(recent_.size() - 1);

    if (auto* bands = parsed.getProperty("bands", {}).getDynamicObject())
        for (const auto& property : bands->getProperties())
            if (property.value.getDynamicObject() != nullptr)
                bands_.getDynamicObject()->setProperty(property.name, property.value);
}

juce::File Settings::defaultFile() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Smart Q Live").getChildFile("settings.json");
}

juce::String Settings::computerName() const {
    auto name = juce::SystemStats::getComputerName().trim();
    return name.isEmpty() ? juce::String("This computer") : name;
}

juce::Array<juce::File> Settings::recentBandFolders() const {
    juce::Array<juce::File> folders;
    for (const auto& path : recent_) {
        juce::File folder(path);
        if (folder.isDirectory()) folders.add(folder);
    }
    return folders;
}

void Settings::noteOpened(const juce::File& bandRoot) {
    recent_.removeString(bandRoot.getFullPathName());
    recent_.insert(0, bandRoot.getFullPathName());
    while (recent_.size() > maxRecent) recent_.remove(recent_.size() - 1);
}

juce::var Settings::bandSettings(const juce::File& bandRoot) const {
    return bands_.getProperty(juce::Identifier(bandRoot.getFullPathName()), {});
}

juce::DynamicObject& Settings::bandObject(const juce::File& bandRoot) {
    const juce::Identifier key(bandRoot.getFullPathName());
    if (auto* existing = bands_.getProperty(key, {}).getDynamicObject()) return *existing;
    auto* created = new juce::DynamicObject();
    bands_.getDynamicObject()->setProperty(key, juce::var(created));
    return *created;
}

juce::String Settings::rigFor(const juce::File& bandRoot) const {
    return bandSettings(bandRoot).getProperty("rigId", {}).toString();
}

void Settings::setRigFor(const juce::File& bandRoot, const juce::String& rigId) {
    bandObject(bandRoot).setProperty("rigId", rigId);
}

juce::String Settings::setlistFor(const juce::File& bandRoot) const {
    return bandSettings(bandRoot).getProperty("setlistId", {}).toString();
}

void Settings::setSetlistFor(const juce::File& bandRoot, const juce::String& setlistId) {
    bandObject(bandRoot).setProperty("setlistId", setlistId);
}

ShowMemory Settings::showMemory(const juce::File& bandRoot) const {
    ShowMemory memory;
    const auto show = bandSettings(bandRoot).getProperty("show", {});
    if (!show.isObject()) return memory;
    memory.inShow = static_cast<bool>(show.getProperty("inShow", false));
    memory.stageView = static_cast<bool>(show.getProperty("stageView", true));
    memory.setlistId = show.getProperty("setlistId", {}).toString();
    memory.nextEntryId = show.getProperty("nextEntryId", {}).toString();
    memory.tonightJson = show.getProperty("tonightJson", {}).toString();
    return memory;
}

void Settings::setShowMemory(const juce::File& bandRoot, const ShowMemory& memory) {
    auto* show = new juce::DynamicObject();
    show->setProperty("inShow", memory.inShow);
    show->setProperty("stageView", memory.stageView);
    show->setProperty("setlistId", memory.setlistId);
    show->setProperty("nextEntryId", memory.nextEntryId);
    show->setProperty("tonightJson", memory.tonightJson);
    bandObject(bandRoot).setProperty("show", juce::var(show));
}

juce::Result Settings::save() {
    juce::Array<juce::var> recent;
    for (const auto& path : recent_) recent.add(path);
    auto* root = new juce::DynamicObject();
    root->setProperty("recent", recent);
    root->setProperty("bands", bands_);
    return model::writeFileAtomically(file_, juce::JSON::toString(juce::var(root)));
}

}  // namespace sqlive::app
