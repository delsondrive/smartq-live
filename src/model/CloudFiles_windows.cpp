#include "model/CloudFiles.h"

#include <windows.h>

namespace sqlive::model {

bool isOnThisComputer(const juce::File& file) {
    const DWORD attributes = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    return ! attributesMeanPlaceholder(static_cast<std::uint32_t>(attributes));
}

namespace cloud_detail {

void fetchFile(const juce::String& path) {
    HANDLE h = CreateFileW(path.toWideCharPointer(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char byte = 0;  DWORD got = 0;
    ReadFile(h, &byte, 1, &got, nullptr);   // reading makes the sync tool fetch the whole file
    CloseHandle(h);
}

void interruptFetch(std::thread& thread) noexcept {
    // Cancels the CreateFileW or ReadFile that thread is blocked in; does nothing when it is blocked in neither.
    if (thread.joinable()) CancelSynchronousIo(static_cast<HANDLE>(thread.native_handle()));
}

}  // namespace cloud_detail

}  // namespace sqlive::model
