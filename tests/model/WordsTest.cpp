#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "model/Words.h"

using namespace sqlive::model;

namespace {
const juce::String kMinus = juce::String::fromUTF8("\xe2\x88\x92");     // U+2212, the minus the app writes
const juce::String kInfinity = juce::String::fromUTF8("\xe2\x88\x9e");  // U+221E
}  // namespace

TEST_CASE("a pan is written C, L 40 or R 100", "[words]") {
    CHECK(formatPan(0) == "C");
    CHECK(formatPan(-40) == "L 40");
    CHECK(formatPan(100) == "R 100");
    CHECK(formatPan(5) == "R 5");
    CHECK(formatPan(-100) == "L 100");
    CHECK(formatPan(-250) == "L 100");                                       // clamped
    CHECK(formatPan(250) == "R 100");
}

TEST_CASE("a typed pan is read, or refused", "[words]") {
    const std::pair<const char*, int> accepted[] = {
        {"C", 0},       {"c", 0},     {"L30", -30},  {"L 30", -30}, {"R100", 100}, {"r 5", 5},   {"-40", -40},
        {"40", 40},     {"+40", 40},  {" l 30 ", -30}, {"0", 0},    {"L 150", -100}, {"R 101", 100}, {"200", 100},
        {"-500", -100}, {"L0", 0},    {"R  7", 7}};
    for (const auto& [text, pan] : accepted) {
        INFO(text);
        CHECK(parsePan(text) == std::optional<int>(pan));
    }
    CHECK(parsePan(kMinus + "40") == std::optional<int>(-40));            // a Unicode minus

    const char* rejected[] = {"", "  ", "x", "L", "R", "LR", "L-30", "L+30", "C 10", "C0", "40.5", "4 0", "--3", "++4", "+-4",
                              "1e9", "L 3 0", "30L", "left", "40%", "0x10"};
    for (const auto* text : rejected) {
        INFO(text);
        CHECK(parsePan(text) == std::nullopt);
    }
}

TEST_CASE("a typed level is read, or refused", "[words]") {
    const std::pair<juce::String, float> accepted[] = {
        {"-4.5", -4.5f}, {kMinus + "4.5", -4.5f}, {"+2", 2.0f}, {"2", 2.0f}, {"0", 0.0f}, {" -3 ", -3.0f},
        {"-4.56", -4.6f}, {"-4.54", -4.5f}, {"-0.04", 0.0f}, {"-100", -60.0f}, {"-60", -60.0f}, {"12", 6.0f},
        {"6", 6.0f}, {"0.5", 0.5f}, {".5", 0.5f}, {"-12.", -12.0f}};
    for (const auto& [text, db] : accepted) {
        INFO(text);
        const auto got = parseDb(text);
        REQUIRE(got.has_value());
        CHECK(*got == db);
        CHECK_FALSE((std::signbit(*got) && *got == 0.0f));                     // never -0
    }

    for (const auto& off : {juce::String("-inf"), juce::String("off"), juce::String("OFF"), juce::String("-INF"), kMinus + "inf",
                            kMinus + kInfinity, "-" + kInfinity, juce::String(" off ")}) {
        INFO(off);
        const auto got = parseDb(off);
        REQUIRE(got.has_value());
        CHECK(*got == -std::numeric_limits<float>::infinity());                // "not sent"
    }

    for (const auto& text : {juce::String("abc"), juce::String("--3"), juce::String(""), juce::String("   "), juce::String("1e9"),
                             juce::String("-"), juce::String("."), juce::String("+"), juce::String("3-"), juce::String("1.2.3"),
                             juce::String("inf"), juce::String("+inf"), kInfinity, juce::String("nan"), juce::String("0x10"),
                             juce::String("- 3"), juce::String("2 3"), kMinus + kMinus + "3", juce::String("-3dB")}) {
        INFO(text);
        CHECK(parseDb(text) == std::nullopt);
    }
}
