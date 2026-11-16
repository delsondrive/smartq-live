#include "tests/support/FileHold.h"

#include <windows.h>

namespace sqlive::test {

ExclusiveFileHold::ExclusiveFileHold(const juce::File& file) {
    HANDLE h = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) handle = h;
}
ExclusiveFileHold::~ExclusiveFileHold() {
    if (handle != nullptr) CloseHandle(static_cast<HANDLE>(handle));
}

}  // namespace sqlive::test
