#include "model/CloudFiles.h"

#include <cstdio>
#include <string>
#include <thread>

namespace sqlive::model {

bool isOnThisComputer(const juce::File& file) {
    if (! file.existsAsFile()) return false;
    // iCloud leaves ".<name>.icloud" beside a file it has not downloaded.
    return ! file.getSiblingFile("." + file.getFileName() + ".icloud").exists();
}

void startDownload(const juce::File& file) {
    // The thread owns a copy of the path and uses only the C library, so it touches nothing that is destroyed at exit.
    // iCloud's stub is ".<name>.icloud"; reading the real name is what asks for the download.
    std::string path = file.getFullPathName().toStdString();
    std::thread([path = std::move(path)] {
        if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
            char byte = 0;
            (void) std::fread(&byte, 1, 1, f);
            std::fclose(f);
        }
    }).detach();
}

}  // namespace sqlive::model
