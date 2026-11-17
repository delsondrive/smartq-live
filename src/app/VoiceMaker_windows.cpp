#include "app/VoiceMaker.h"

#include <cwchar>

namespace sqlive::app {

namespace {

// A PowerShell single-quoted string: nothing inside is expanded. PowerShell also takes the typographic single quotes as
// quotes, so each of them is doubled too.
juce::String quoted(const juce::File& file) {
    juce::String out("'");
    for (auto p = file.getFullPathName().getCharPointer(); !p.isEmpty();) {
        const auto c = p.getAndAdvance();
        if (c == '\'' || c == 0x2018 || c == 0x2019 || c == 0x201A || c == 0x201B) out += c;
        out += c;
    }
    return out + "'";
}

}  // namespace

// PowerShell with the System.Speech that ships with Windows. The script holds only the two app-made paths: it reads the
// words from textFile, so they never appear in the script or in the arguments. It goes in as -EncodedCommand (base64 of
// UTF-16LE), so no quoting of the command line is needed.
VoiceCommand voiceCommand(const juce::String& text, const juce::File& tempFolder) {
    VoiceCommand command;
    command.textFile = tempFolder.getChildFile("words.txt");
    command.wavFile = tempFolder.getChildFile("voice.wav");
    command.textFile.replaceWithText(text, false, false, nullptr);   // UTF-8, no byte-order mark

    const juce::String script =
        "$ErrorActionPreference = 'Stop'\n"
        "Add-Type -AssemblyName System.Speech\n"
        "$words = [System.IO.File]::ReadAllText(" + quoted(command.textFile) + ", [System.Text.Encoding]::UTF8)\n"
        "$format = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(48000, "
        "[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)\n"
        "$voice = [System.Speech.Synthesis.SpeechSynthesizer]::new()\n"
        "try {\n"
        "    $voice.SetOutputToWaveFile(" + quoted(command.wavFile) + ", $format)\n"
        "    $voice.Speak($words)\n"
        "} finally {\n"
        "    $voice.Dispose()\n"
        "}\n";
    const auto* utf16 = script.toWideCharPointer();                   // wchar_t is UTF-16, little-endian, on Windows
    const auto encoded = juce::Base64::toBase64(utf16, sizeof(wchar_t) * std::wcslen(utf16));

    const auto powershell = juce::File::getSpecialLocation(juce::File::windowsSystemDirectory)
                                .getChildFile("WindowsPowerShell/v1.0/powershell.exe");
    command.program = powershell.existsAsFile() ? powershell.getFullPathName() : juce::String("powershell.exe");
    command.arguments = {"-NoProfile", "-NonInteractive", "-EncodedCommand", encoded};
    return command;
}

}  // namespace sqlive::app
