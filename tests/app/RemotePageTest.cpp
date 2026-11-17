#include <catch2/catch_test_macros.hpp>

#include "SmartQLiveAssets.h"
#include "app/RemoteActions.h"

using namespace sqlive;

// The phone's page (assets/remote.html) is one file the laptop serves as it is: nothing it loads comes from anywhere else,
// and nothing a server sends it is ever taken as markup. Its behaviour runs in a phone's browser; these checks pin what
// can be read from the file itself.

namespace {

juce::String remotePage() {
    return juce::String::fromUTF8(sqlive_assets::remote_html, sqlive_assets::remote_htmlSize);
}

juce::String utf8(const char* text) { return juce::String::fromUTF8(text); }

}  // namespace

TEST_CASE("the phone page is self-contained and safe", "[remotepage]") {
    const auto page = remotePage();
    CHECK(page.contains("<meta name=\"viewport\""));
    CHECK(page.contains("EventSource"));  CHECK(page.contains("/api/join"));  CHECK(page.contains("/api/action"));
    CHECK(page.contains("localStorage"));
    for (auto phrase : {"Waiting to be let in", "Ask at the laptop: Setup, then Remote.", "Hold to stop", "Loop this section",
                        "This phone can watch but not change anything.", "The laptop isn't in Show mode yet.", "Reconnecting",
                        "That PIN isn't right.", "Smart Q Live remote", "Your name", "Connect", "Full control", "View only"})
        CHECK(page.contains(phrase));
    for (auto phrase : {"Hold \xe2\x96\xb6 for a second to stop",             // the mockup's hint under the buttons
                        "Loading\xe2\x80\xa6", "\xe2\x80\xb9 earlier", "later \xe2\x80\xba",
                        "\xe2\x97\x80", "earlier song", "\xe2\x96\xb6\xe2\x96\xb6", "later song", "\xe2\x96\xb6 Start"})
        CHECK(page.contains(utf8(phrase)));

    CHECK_FALSE(page.contains("innerHTML"));                        // server text only ever goes in as text
    CHECK_FALSE(page.contains("outerHTML"));  CHECK_FALSE(page.contains("insertAdjacentHTML"));
    CHECK_FALSE(page.contains("document.write"));  CHECK_FALSE(page.contains("eval("));
    CHECK(page.contains("textContent"));
    CHECK_FALSE(page.contains("http://"));  CHECK_FALSE(page.contains("https://"));   // nothing is loaded from anywhere else
    CHECK_FALSE(page.contains("<script src"));  CHECK_FALSE(page.contains("<link"));  CHECK_FALSE(page.contains("@import"));
    CHECK_FALSE(page.contains("url("));
    CHECK(page.length() < 60000);
}

TEST_CASE("the phone page joins with a numeric PIN and keeps its token under a known key", "[remotepage]") {
    const auto page = remotePage();
    CHECK(page.contains("inputmode=\"numeric\""));
    CHECK(page.contains("\"sqlive.remote.token\""));                // --remote-demo's file names this key
}

TEST_CASE("the phone page notices a forgotten phone and reconnects by itself", "[remotepage]") {
    const auto page = remotePage();
    CHECK(page.contains("/api/access"));                            // asked when the stream closes for good, and on unlocking
    CHECK(page.contains("visibilitychange"));
    CHECK(page.contains("/api/events?token="));
}

TEST_CASE("the phone page offers every action a phone may send", "[remotepage]") {
    // The Setup page's PHONE column marks these; each is a quoted name in the page's script.
    const auto page = remotePage();
    for (const auto& name : app::remotePageActions()) {
        INFO(name);
        CHECK(page.contains("\"" + name + "\""));
    }
}
