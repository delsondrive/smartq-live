#include "app/VoiceMaker.h"

namespace sqlive::app {

// The Mac's `say`, reading the words from textFile, so they never appear in the arguments. Elsewhere there is no `say`: the
// runner fails to start it and make says the voice couldn't be used.
VoiceCommand voiceCommand(const juce::String& text, const juce::File& tempFolder) {
    VoiceCommand command;
    command.textFile = tempFolder.getChildFile("words.txt");
    command.wavFile = tempFolder.getChildFile("voice.wav");
    command.textFile.replaceWithText(text, false, false, nullptr);   // UTF-8, no byte-order mark
    command.program = "say";
    command.arguments = {"-o", command.wavFile.getFullPathName(), "--data-format=LEI16@48000", "-f", command.textFile.getFullPathName()};
    return command;
}

}  // namespace sqlive::app
