cmake_minimum_required(VERSION 3.25)
project(SmartQLive VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.16.1
    GIT_SHALLOW TRUE)
# The phone remote's web server: one header, used by src/app/RemoteServer.cpp alone. Only fetched: SOURCE_SUBDIR names a
# folder it doesn't have, so its own CMakeLists never runs, and so never looks for OpenSSL, zlib, Brotli or zstd on this
# machine. Nothing links httplib::httplib; the header's folder is an include directory of that one source file (below).
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.64.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-add)
# The QR code on Setup's Remote page: one C++ file and its header (cpp/qrcodegen.cpp), compiled into both targets below.
# The repository has no CMakeLists of its own; it is only fetched.
FetchContent_Declare(qrcodegen
    GIT_REPOSITORY https://github.com/nayuki/QR-Code-generator.git
    GIT_TAG v1.8.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(JUCE Catch2 httplib qrcodegen)

# The build stamp: the short commit, the commit's time and whether the tree has uncommitted changes (tracked files only,
# so a scratch file doesn't mark the build), read when this configures. scripts/build.ps1 configures before every build,
# so the stamp follows the commit. Without git, or outside a repository, it is "unknown", false and 0, and this still succeeds.
set(SQLIVE_GIT_COMMIT "unknown")
set(SQLIVE_GIT_MODIFIED "false")
set(SQLIVE_GIT_SECONDS "0")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE SQLIVE_GIT_RESULT
        OUTPUT_VARIABLE SQLIVE_GIT_OUT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(SQLIVE_GIT_RESULT EQUAL 0 AND SQLIVE_GIT_OUT MATCHES "^[0-9a-f]+$")
        execute_process(COMMAND "${GIT_EXECUTABLE}" log -1 --format=%ct
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE SQLIVE_GIT_TIME_RESULT
            OUTPUT_VARIABLE SQLIVE_GIT_TIME OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE SQLIVE_GIT_STATUS_RESULT
            OUTPUT_VARIABLE SQLIVE_GIT_STATUS OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(SQLIVE_GIT_TIME_RESULT EQUAL 0 AND SQLIVE_GIT_STATUS_RESULT EQUAL 0 AND SQLIVE_GIT_TIME MATCHES "^[0-9]+$")
            set(SQLIVE_GIT_COMMIT "${SQLIVE_GIT_OUT}")
            set(SQLIVE_GIT_SECONDS "${SQLIVE_GIT_TIME}")
            if(NOT SQLIVE_GIT_STATUS STREQUAL "")
                set(SQLIVE_GIT_MODIFIED "true")
            endif()
        endif()
    endif()
endif()
# configure_file rewrites the file only when its content changes, so an unchanged stamp rebuilds nothing.
set(SQLIVE_BUILD_INFO_CPP "${CMAKE_BINARY_DIR}/generated/BuildInfo.cpp")
configure_file("${CMAKE_SOURCE_DIR}/cmake/BuildInfo.cpp.in" "${SQLIVE_BUILD_INFO_CPP}" @ONLY)

# Compiled into both the app and the tests. There is no JUCE-linked library target: it compiles the JUCE modules twice.
# A file ending _windows.cpp is built on Windows only and _posix.cpp everywhere else.
if(WIN32)
    set(SQLIVE_PLATFORM_SUFFIX "windows")
else()
    set(SQLIVE_PLATFORM_SUFFIX "posix")
endif()
file(GLOB_RECURSE SQLIVE_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/model/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/app/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/*.cpp")
set(SQLIVE_PLATFORM_SOURCES ${SQLIVE_CORE_SOURCES})
list(FILTER SQLIVE_CORE_SOURCES EXCLUDE REGEX "_(windows|posix)[.]cpp$")
list(FILTER SQLIVE_PLATFORM_SOURCES INCLUDE REGEX "_${SQLIVE_PLATFORM_SUFFIX}[.]cpp$")
list(APPEND SQLIVE_CORE_SOURCES ${SQLIVE_PLATFORM_SOURCES})
list(APPEND SQLIVE_CORE_SOURCES "${SQLIVE_BUILD_INFO_CPP}")
# httplib.h is visible to this one file only, in both targets.
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/app/RemoteServer.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${httplib_SOURCE_DIR}")
# qrcodegen.hpp is visible to src/ui/QrCode.cpp only; the library's one source is built with the app's, in both targets.
set(SQLIVE_QRCODEGEN_CPP "${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp")
list(APPEND SQLIVE_CORE_SOURCES "${SQLIVE_QRCODEGEN_CPP}")
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/ui/QrCode.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${qrcodegen_SOURCE_DIR}/cpp")

# The owner's logo, as plain vectors, and the phone remote's page.
juce_add_binary_data(SmartQLiveAssets
    HEADER_NAME SmartQLiveAssets.h
    NAMESPACE sqlive_assets
    SOURCES "${CMAKE_SOURCE_DIR}/assets/smart-q-logo.svg" "${CMAKE_SOURCE_DIR}/assets/remote.html")

set(SQLIVE_DEFINITIONS
    JUCE_ASIO=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

juce_add_gui_app(SmartQLive
    PRODUCT_NAME "Smart Q Live"
    COMPANY_NAME "Smart Q"
    BUNDLE_ID com.smartq.SmartQLive)
target_sources(SmartQLive PRIVATE "${CMAKE_SOURCE_DIR}/src/Main.cpp" ${SQLIVE_CORE_SOURCES})
target_include_directories(SmartQLive PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(SmartQLive PRIVATE ${SQLIVE_DEFINITIONS})
target_link_libraries(SmartQLive
    PRIVATE
        SmartQLiveAssets
        sqlive_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Smart Q's DSP, copied unchanged from the Smart Q VST3: plain C++, no JUCE, namespace smartq::dsp, included as "dsp/X.h".
file(GLOB SQLIVE_DSP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/dsp/*.cpp")
add_library(smartq_dsp STATIC ${SQLIVE_DSP_SOURCES})
target_compile_features(smartq_dsp PUBLIC cxx_std_20)
target_include_directories(smartq_dsp PUBLIC "${CMAKE_SOURCE_DIR}/src")

# The engine: timing and mixing with no JUCE in it.
file(GLOB SQLIVE_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/engine/*.cpp")
add_library(sqlive_engine STATIC ${SQLIVE_ENGINE_SOURCES})
target_compile_features(sqlive_engine PUBLIC cxx_std_20)
target_include_directories(sqlive_engine PUBLIC "${CMAKE_SOURCE_DIR}/src")
# An interface target: it adds JUCE's recommended warning flags and nothing else, so the engine stays JUCE-free.
target_link_libraries(sqlive_engine PUBLIC smartq_dsp PRIVATE juce::juce_recommended_warning_flags)

# Tests
# As with the app's sources, a test file ending _windows.cpp is built on Windows only and _posix.cpp everywhere else. A
# test of something only one platform has (WindowFrameTest_windows.cpp) needs no twin.
file(GLOB_RECURSE SQLIVE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/*.cpp")
set(SQLIVE_TEST_PLATFORM_SOURCES ${SQLIVE_TEST_SOURCES})
list(FILTER SQLIVE_TEST_SOURCES EXCLUDE REGEX "_(windows|posix)[.]cpp$")
list(FILTER SQLIVE_TEST_PLATFORM_SOURCES INCLUDE REGEX "_${SQLIVE_PLATFORM_SUFFIX}[.]cpp$")
list(APPEND SQLIVE_TEST_SOURCES ${SQLIVE_TEST_PLATFORM_SOURCES})
juce_add_console_app(sqlive_tests PRODUCT_NAME "sqlive_tests")
target_sources(sqlive_tests PRIVATE ${SQLIVE_TEST_SOURCES} ${SQLIVE_CORE_SOURCES})
target_include_directories(sqlive_tests PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
target_compile_definitions(sqlive_tests PRIVATE ${SQLIVE_DEFINITIONS})
target_link_libraries(sqlive_tests
    PRIVATE
        SmartQLiveAssets
        sqlive_engine
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        Catch2::Catch2WithMain)
set_target_properties(sqlive_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
