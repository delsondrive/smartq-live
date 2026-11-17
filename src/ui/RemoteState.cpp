#include "ui/RemoteState.h"

#include <cmath>

#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/ShowState.h"

namespace sqlive::ui {

namespace {

using Card = ShowState::Card;
using Tile = ShowState::Tile;
using model::jsonObject;

juce::String cardKind(Card::Kind kind) {
    switch (kind) {
    case Card::Kind::Done: return "done";
    case Card::Kind::Playing: return "playing";
    case Card::Kind::Next: return "next";
    case Card::Kind::Later: break;
    }
    return "later";
}

juce::String tileKind(Tile::Kind kind) {
    switch (kind) {
    case Tile::Kind::Past: return "past";
    case Tile::Kind::Now: return "now";
    case Tile::Kind::Next: return "next";
    case Tile::Kind::Later: break;
    }
    return "later";
}

juce::String readinessWord(Readiness readiness) {
    switch (readiness) {
    case Readiness::Ready: return "ready";
    case Readiness::Loading: return "loading";
    case Readiness::Failed: return "failed";
    case Readiness::None: break;
    }
    return "none";
}

const Card* cardOf(const ShowState& st, Card::Kind kind) {
    for (const auto& card : st.cards)
        if (card.kind == kind) return &card;
    return nullptr;
}

}  // namespace

juce::String makeRemoteStateJson(const app::Session& session) {
    JUCE_ASSERT_MESSAGE_THREAD
    const auto now = session.now();
    const auto st = makeShowState(session);
    const bool songPlays = now.playing && !now.inGap;   // a song of the setlist plays: its steps can be chosen

    juce::var song;                                     // null with nothing playing
    if (st.playing) {
        const auto* playing = cardOf(st, Card::Kind::Playing);
        song = jsonObject({{"title", st.title},
                           {"number", playing != nullptr ? juce::var(playing->number) : juce::var()},
                           {"of", static_cast<int>(st.cards.size())}});
    }

    juce::Array<juce::var> tiles;
    for (std::size_t i = 0; i < st.tiles.size(); ++i) {
        const auto& tile = st.tiles[i];
        tiles.add(jsonObject({{"name", tile.name},
                              {"colour", tile.colour.toDisplayString(false).toLowerCase()},
                              {"kind", tileKind(tile.kind)},
                              {"choosable", songPlays && i < 10}}));     // only section1 … section10 exist
    }

    juce::var next;                                     // null after the last song
    if (const auto* card = cardOf(st, Card::Kind::Next))
        next = jsonObject({{"title", card->title}, {"number", card->number}, {"ready", readinessWord(st.readiness)}});

    juce::Array<juce::var> setlist;
    for (const auto& card : st.cards) {
        const auto note = card.kind == Card::Kind::Playing ? juce::String("playing")
                          : card.kind == Card::Kind::Next  ? juce::String("next")
                                                           : card.length;
        setlist.add(jsonObject({{"title", card.title}, {"note", note}, {"kind", cardKind(card.kind)}}));
    }

    const auto state = jsonObject({
        {"show", session.mode() == app::Mode::Show},
        {"playing", st.playing},
        {"countIn", st.countIn},
        {"interrupted", st.interrupted},
        {"canGoOn", now.canGoOn},
        {"song", song},
        {"bar", now.bar},
        {"beat", static_cast<int>(std::floor(now.beat))},
        {"beatsInBar", now.numerator},
        {"bpm", formatBpm(now.bpm)},
        {"left", st.left.isNotEmpty() ? st.left : st.upLength},
        {"progress", std::round(st.songProgress * 1000.0) / 1000.0},
        {"section", st.sectionName},
        {"barsLeft", st.barsLeft.getIntValue()},
        {"then", st.then},
        {"looping", st.looping},
        {"tiles", tiles},
        {"next", next},
        {"setlist", setlist},
    });
    return juce::JSON::toString(state, true);
}

}  // namespace sqlive::ui
