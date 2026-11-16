#include "model/CloudFiles.h"

#include <string>
#include <thread>

#include <windows.h>

namespace sqlive::model {

bool isOnThisComputer(const juce::File& file) {
    const DWORD attributes = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    return ! attributesMeanPlaceholder(static_cast<std::uint32_t>(attributes));
}

void startDownload(const juce::File& file) {
    // The thread owns a copy of the path and uses only Win32 calls, so it touches nothing that is destroyed at exit.
    std::wstring path = file.getFullPathName().toWideCharPointer();
    std::thread([path = std::move(path)] {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        char byte = 0;  DWORD got = 0;
        ReadFile(h, &byte, 1, &got, nullptr);   // reading makes the sync tool fetch the whole file
        CloseHandle(h);
    }).detach();
}

}  // namespace sqlive::model
