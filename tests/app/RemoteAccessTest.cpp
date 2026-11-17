#include <catch2/catch_test_macros.hpp>

#include "app/RemoteAccess.h"
#include "tests/support/DemoSession.h"

using namespace sqlive;
using sqlive::app::PhoneAccess;
using sqlive::test::DemoSession;

TEST_CASE("a phone joins with the PIN, waits, and is let in from the laptop", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    CHECK(access.pin().length() == 4);  CHECK(access.pin().containsOnly("0123456789"));
    CHECK(access.join("Sam's iPhone", access.pin() == "0000" ? "1111" : "0000", "192.168.1.9", 0.0).status == 403);
    auto joined = access.join("  Sam's iPhone  ", access.pin(), "192.168.1.9", 1.0);
    REQUIRE(joined.status == 200);  CHECK(joined.token.length() >= 32);
    CHECK(access.accessFor(joined.token) == PhoneAccess::Waiting);
    CHECK(access.accessFor("made-up") == PhoneAccess::Refused);
    REQUIRE(access.phones().size() == 1);  CHECK(access.phones()[0].name == "Sam's iPhone");
    auto id = access.phones()[0].id;
    CHECK_FALSE(access.decide(id, PhoneAccess::FullControl, false));                 // not during a show
    CHECK(access.accessFor(joined.token) == PhoneAccess::Waiting);
    CHECK(access.decide(id, PhoneAccess::FullControl, true));
    CHECK(access.accessFor(joined.token) == PhoneAccess::FullControl);
    CHECK(access.decide(id, PhoneAccess::ViewOnly, false));                          // changing a phone already let in is allowed at any time
    app::RemoteAccess again(*d.settings, d.session->folder().root());                // after a restart
    CHECK(again.accessFor(joined.token) == PhoneAccess::ViewOnly);
    CHECK(again.pin() == access.pin());
    again.forget(id);
    CHECK(again.accessFor(joined.token) == PhoneAccess::Refused);
    CHECK_FALSE(d.settings->remoteMemory(d.session->folder().root()).tokenHashes.contains(joined.token));   // only a hash is kept
}

TEST_CASE("wrong PINs lock an address out, and names are kept sane", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    auto wrong = access.pin() == "1234" ? "4321" : "1234";
    for (int i = 0; i < 5; ++i) CHECK(access.join("X", wrong, "10.0.0.5", double(i)).status == 403);
    CHECK(access.join("X", access.pin(), "10.0.0.5", 6.0).status == 429);            // locked, even with the right PIN
    CHECK(access.join("Y", access.pin(), "10.0.0.6", 6.0).status == 200);            // another address is fine
    CHECK(access.join("X", access.pin(), "10.0.0.5", 70.0).status == 200);           // a minute later it may try again
    CHECK(access.join("", access.pin(), "10.0.0.7", 0.0).status == 400);
    auto longName = access.join(juce::String::repeatedString("n", 500), access.pin(), "10.0.0.8", 0.0);
    REQUIRE(longName.status == 200);
    for (auto& p : access.phones()) CHECK(p.name.length() <= 40);
    auto old = access.pin();  access.newPin();
    CHECK(access.join("Z", old, "10.0.0.9", 0.0).status == (old == access.pin() ? 200 : 403));
    for (int i = 0; i < 40; ++i) access.join("Flood " + juce::String(i), access.pin(), "10.0.1." + juce::String(i), 100.0);
    CHECK(access.phones().size() <= 16);                                              // waiting phones beyond 16 are refused
}

TEST_CASE("the lockout counts only wrong PINs within a minute, and a right PIN clears it", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    const juce::String wrong = access.pin() == "1234" ? "4321" : "1234";
    for (int i = 0; i < 4; ++i) CHECK(access.join("A", wrong, "10.0.0.5", double(i) * 20.0).status == 403);   // 0, 20, 40, 60
    CHECK(access.join("A", wrong, "10.0.0.5", 61.0).status == 403);                  // the one at 0 s has dropped out: four in the minute
    CHECK(access.join("A", access.pin(), "10.0.0.5", 62.0).status == 200);           // not locked; and the count starts again
    for (int i = 0; i < 4; ++i) CHECK(access.join("A", wrong, "10.0.0.5", 63.0 + i).status == 403);
    CHECK(access.join("A", access.pin(), "10.0.0.5", 67.0).status == 200);
    for (int i = 0; i < 5; ++i) CHECK(access.join("A", wrong, "10.0.0.5", 70.0 + i).status == 403);
    auto locked = access.join("A", access.pin(), "10.0.0.5", 133.0);                  // 59 s after the fifth
    CHECK(locked.status == 429);  CHECK(locked.message.isNotEmpty());  CHECK(locked.token.isEmpty());
    CHECK(access.join("A", access.pin(), "10.0.0.5", 134.5).status == 200);          // 60 s after the fifth
}

TEST_CASE("what a phone sends is checked for size and shape", "[remoteaccess]") {
    DemoSession d; app::RemoteAccess access(*d.settings, d.session->folder().root());
    CHECK(access.join(" \t\r\n ", access.pin(), "10.0.0.1", 0.0).status == 400);    // only white space
    CHECK(access.join(juce::String::fromUTF8("\x01\x1b\x7f"), access.pin(), "10.0.0.1", 0.0).status == 400);   // only control characters
    auto odd = access.join(juce::String::fromUTF8("Bass\n\x07 phone \xc3\xa9"), access.pin(), "10.0.0.2", 0.0);
    REQUIRE(odd.status == 200);
    CHECK(access.phones().back().name == juce::String::fromUTF8("Bass phone \xc3\xa9"));
    auto cut = access.join("x" + juce::String::repeatedString("ab ", 13) + "cdefgh", access.pin(), "10.0.0.3", 0.0);   // the 40th character is a space
    REQUIRE(cut.status == 200);
    CHECK(access.phones().back().name.length() <= 40);  CHECK(access.phones().back().name == access.phones().back().name.trim());
    CHECK(access.join("P", access.pin() + "0", "10.0.0.4", 0.0).status == 403);      // the PIN is the four digits exactly
    CHECK(access.join("P", juce::String::repeatedString("9", 4000), "10.0.0.4", 0.0).status == 403);

    REQUIRE(odd.token.length() == 64);  CHECK(odd.token.containsOnly("0123456789abcdef"));
    CHECK(odd.token != cut.token);
    CHECK(access.accessFor({}) == PhoneAccess::Refused);
    CHECK(access.accessFor(odd.token.toUpperCase()) == PhoneAccess::Refused);
    CHECK(access.accessFor(odd.token + "0") == PhoneAccess::Refused);
    CHECK(access.accessFor(juce::String::repeatedString("z", 100000)) == PhoneAccess::Refused);
    CHECK(access.phoneIdFor(odd.token) == access.phones()[0].id);
    CHECK(access.phoneIdFor("made-up").isEmpty());
    CHECK(access.phones()[0].id != access.phones()[1].id);
}

TEST_CASE("refusing, forgetting and a new PIN", "[remoteaccess]") {
    DemoSession d; const auto root = d.session->folder().root();
    app::RemoteAccess access(*d.settings, root);
    const auto version = access.version();
    auto a = access.join("A", access.pin(), "10.0.0.1", 0.0), b = access.join("B", access.pin(), "10.0.0.2", 0.0);
    CHECK(access.version() > version);
    const auto idA = access.phoneIdFor(a.token), idB = access.phoneIdFor(b.token);
    CHECK(access.decide(idA, PhoneAccess::Refused, false));                           // refusing a waiting phone forgets it
    CHECK(access.accessFor(a.token) == PhoneAccess::Refused);
    REQUIRE(access.phones().size() == 1);  CHECK(access.phones()[0].id == idB);
    CHECK_FALSE(access.decide(idA, PhoneAccess::FullControl, true));                  // gone
    CHECK_FALSE(access.decide("no such phone", PhoneAccess::ViewOnly, true));
    CHECK_FALSE(access.decide(idB, PhoneAccess::Waiting, true));                      // the laptop lets in or refuses; it never puts back to waiting
    CHECK(access.decide(idB, PhoneAccess::FullControl, true));
    CHECK(access.decide(idB, PhoneAccess::Refused, false));                           // a phone let in can be refused at any time
    CHECK(access.accessFor(b.token) == PhoneAccess::Refused);
    CHECK(access.phones().empty());

    const auto old = access.pin();
    access.newPin();
    CHECK(access.pin() != old);  CHECK(access.pin().length() == 4);  CHECK(access.pin().containsOnly("0123456789"));
    CHECK(d.settings->remoteMemory(root).pin == access.pin());
}

TEST_CASE("the remote's switch and port are kept per band, and being connected is not", "[remoteaccess]") {
    DemoSession d; const auto root = d.session->folder().root();
    const auto otherBand = d.dir.dir().getChildFile("Other band");
    {
        app::RemoteAccess access(*d.settings, root);
        CHECK_FALSE(access.enabled());  CHECK(access.port() == 8080);
        access.setEnabled(true);
        CHECK_FALSE(access.setPort(80000));  CHECK_FALSE(access.setPort(0));
        CHECK(access.setPort(8123));
        auto joined = access.join("Phone", access.pin(), "10.0.0.1", 0.0);
        const auto id = access.phoneIdFor(joined.token);
        REQUIRE(access.decide(id, PhoneAccess::FullControl, true));
        const auto version = access.version();
        access.setConnected(id, true);
        CHECK(access.phones()[0].connected);  CHECK(access.version() > version);
        access.setConnected("no such phone", true);
    }
    REQUIRE(d.settings->save().wasOk());
    app::Settings reread(d.dir.dir().getChildFile("settings.json"));                  // from the file, as the next run reads it
    const auto memory = reread.remoteMemory(root);
    CHECK(memory.enabled);  CHECK(memory.port == 8123);  CHECK(memory.pin.length() == 4);
    REQUIRE(memory.phones.size() == 1);  REQUIRE(memory.tokenHashes.size() == 1);
    CHECK(memory.phones[0].access == PhoneAccess::FullControl);  CHECK(memory.phones[0].name == "Phone");
    CHECK_FALSE(memory.phones[0].connected);
    CHECK(memory.tokenHashes[0].length() == 64);

    app::RemoteAccess other(reread, otherBand);                                       // another band remembers its own phones
    CHECK(other.phones().empty());  CHECK_FALSE(other.enabled());
}

TEST_CASE("remote memory that was damaged by hand is made sane on load", "[remoteaccess]") {
    DemoSession d; const auto root = d.session->folder().root();
    app::RemoteMemory memory;
    memory.pin = "12a4";  memory.port = -3;
    for (int i = 0; i < 20; ++i) {
        memory.phones.push_back({"id" + juce::String(i), "Phone " + juce::String(i), i == 0 ? PhoneAccess::Refused : PhoneAccess::ViewOnly, true});
        memory.tokenHashes.add(juce::String::repeatedString("a", 63) + juce::String::toHexString(i % 16));
    }
    d.settings->setRemoteMemory(root, memory);
    app::RemoteAccess access(*d.settings, root);
    CHECK(access.pin().length() == 4);  CHECK(access.pin().containsOnly("0123456789"));
    CHECK(access.port() == 8080);
    CHECK(access.phones().size() <= 16);
    for (const auto& p : access.phones()) { CHECK(p.access != PhoneAccess::Refused);  CHECK_FALSE(p.connected); }
}
