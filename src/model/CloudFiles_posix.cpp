#include "model/CloudFiles.h"

#include <cstdio>

namespace sqlive::model {

bool isOnThisComputer(const juce::File& file) {
    if (! file.existsAsFile()) return false;
    // iCloud leaves ".<name>.icloud" beside a file it has not downloaded.
    return ! file.getSiblingFile("." + file.getFileName() + ".icloud").exists();
}

namespace cloud_detail {

void fetchFile(const juce::String& path) {
    // iCloud's stub is ".<name>.icloud"; reading the real name is what asks for the download.
    if (std::FILE* f = std::fopen(path.toRawUTF8(), "rb")) {
        char byte = 0;
        (void) std::fread(&byte, 1, 1, f);
        std::fclose(f);
    }
}

void interruptFetch(std::thread&) noexcept {}   // a blocked fopen or fread can't be cut short here; exit waits for it

}  // namespace cloud_detail

}  // namespace sqlive::model
