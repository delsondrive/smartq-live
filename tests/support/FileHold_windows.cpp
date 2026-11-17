#include "tests/support/FileHold.h"

#include <windows.h>

namespace sqlive::test {

ExclusiveFileHold::ExclusiveFileHold(const juce::File& file) {
    HANDLE h = CreateFileW(file.getFullPathName().toWideCharPointer(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) handle_ = h;
}
ExclusiveFileHold::~ExclusiveFileHold() {
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
}

bool markAsInTheCloud(const juce::File& file) {
    const auto name = file.getFullPathName();
    const auto path = name.toWideCharPointer();
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW(path, attributes | FILE_ATTRIBUTE_OFFLINE) != 0;
}

}  // namespace sqlive::test
