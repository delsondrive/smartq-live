#include "model/MidiFileImport.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>

#include <juce_audio_basics/juce_audio_basics.h>

#include "model/DocumentsJson.h"

namespace sqlive::model {

namespace {

constexpr int kMaxValuesPerController = 200;     // more than this in one track is a pedal sweep
constexpr double kBeatResolution = 960.0;        // positions are rounded to 1/960 of a beat
constexpr int kLastBar = 99999;                  // the documents' last bar
constexpr double kDefaultBpm = 120.0;
constexpr double kFarBar = 1.0e6;                // positions are saturated here, so no cast of a huge double happens
constexpr size_t kMaxMarkers = 2000;             // places with changes, over all tracks

const char* const kUnreadable = "This file couldn't be read as a MIDI file.";

struct Position { int bar = 1; double beat = 1.0; };

// Where bars start: one stretch per time signature. A signature that isn't on a bar line starts at the next bar.
class BarGrid {
public:
    BarGrid(const std::vector<std::pair<double, std::pair<int, int>>>& signatures, double ppq) : ppq_(ppq) {
        stretches_.push_back(make(0.0, 1, 4, 4));
        for (auto& [tick, sig] : signatures) {
            auto& last = stretches_.back();
            if (tick <= last.tick) { last = make(last.tick, last.bar, sig.first, sig.second); continue; }
            if (sig.first == last.numerator && sig.second == last.denominator) continue;
            const double bars = std::max(1.0, std::ceil((tick - last.tick) / last.ticksPerBar - 1e-9));
            if (last.bar + bars > kFarBar) break;                  // far past any song: the length check refuses the file
            stretches_.push_back(make(last.tick + bars * last.ticksPerBar, last.bar + (int) bars, sig.first, sig.second));
        }
    }

    // The rounded bar and beat at a tick.
    Position at(double tick) const {
        const auto& s = stretchAt(tick);
        const double rel = std::max(0.0, tick - s.tick);
        const double wholeBars = std::min(std::floor(rel / s.ticksPerBar + 1e-9), kFarBar);   // never past int
        const double beats = std::round((rel - wholeBars * s.ticksPerBar) / s.ticksPerBeat * kBeatResolution) / kBeatResolution;
        Position p{s.bar + (int) wholeBars, 1.0 + beats};
        if (beats >= s.numerator) { ++p.bar; p.beat = 1.0; }
        return p;
    }
    // The bar line nearest a tick; halfway goes to the later one.
    int nearestBar(double tick) const {
        const auto& s = stretchAt(tick);
        return s.bar + (int) std::min(std::floor(std::max(0.0, tick - s.tick) / s.ticksPerBar + 0.5), kFarBar);
    }
    bool onBarLine(double tick) const {
        const auto& s = stretchAt(tick);
        const double rel = std::max(0.0, tick - s.tick) / s.ticksPerBar;
        return std::abs(rel - std::round(rel)) < 1e-6;
    }
    int denominatorAt(double tick) const { return stretchAt(tick).denominator; }
    int numeratorAt(double tick) const { return stretchAt(tick).numerator; }
    std::vector<double> startTicks() const {
        std::vector<double> out;
        for (auto& s : stretches_) out.push_back(s.tick);
        return out;
    }

private:
    struct Stretch { double tick; int bar, numerator, denominator; double ticksPerBeat, ticksPerBar; };
    Stretch make(double tick, int bar, int numerator, int denominator) const {
        const double perBeat = ppq_ * 4.0 / denominator;
        return {tick, bar, numerator, denominator, perBeat, perBeat * numerator};
    }
    const Stretch& stretchAt(double tick) const {
        auto it = std::upper_bound(stretches_.begin(), stretches_.end(), tick, [](double t, const Stretch& s) { return t < s.tick; });
        return it == stretches_.begin() ? *it : *(it - 1);
    }
    double ppq_;
    std::vector<Stretch> stretches_;
};

// One program change, control change or note-on of a track, in file order.
struct Raw {
    MidiMessageDoc::Type type = MidiMessageDoc::Type::Program;
    double tick = 0.0;
    int channel = 1;                 // 1 to 16
    int number = 0;                  // program, controller or note
    int value = 0;                   // controller value or velocity
    double lengthTicks = 0.0;        // a note
    int bankMsb = -1, bankLsb = -1;
    bool isBank = false;             // a CC 0 or CC 32 that became a program's bank
    bool dropped = false;
};

// One track's program, control and note messages in file order, banks attached, sweeps left out.
void summarise(const juce::MidiMessageSequence& seq, int number, double ppq, const BarGrid& grid, MidiFileContent& out) {
    std::vector<Raw> raws;
    juce::String name;
    std::map<std::pair<int, int>, std::deque<size_t>> open;      // (channel, note) -> note-ons waiting for their note-off
    for (int i = 0; i < seq.getNumEvents(); ++i) {
        const auto& m = seq.getEventPointer(i)->message;
        const double tick = m.getTimeStamp();
        if (m.isTrackNameEvent()) {
            if (name.isEmpty()) name = m.getTextFromTextMetaEvent().trim();
        } else if (m.isProgramChange()) {
            Raw r; r.type = MidiMessageDoc::Type::Program; r.tick = tick; r.channel = m.getChannel(); r.number = m.getProgramChangeNumber();
            raws.push_back(r);
        } else if (m.isController()) {
            Raw r; r.type = MidiMessageDoc::Type::Control; r.tick = tick; r.channel = m.getChannel();
            r.number = m.getControllerNumber(); r.value = m.getControllerValue();
            raws.push_back(r);
        } else if (m.isNoteOff(true)) {
            auto& waiting = open[{m.getChannel(), m.getNoteNumber()}];
            if (! waiting.empty()) { raws[waiting.front()].lengthTicks = tick - raws[waiting.front()].tick; waiting.pop_front(); }
        } else if (m.isNoteOn()) {
            Raw r; r.type = MidiMessageDoc::Type::Note; r.tick = tick; r.channel = m.getChannel();
            r.number = m.getNoteNumber(); r.value = m.getVelocity(); r.lengthTicks = ppq;   // a quarter note when it never ends
            open[{r.channel, r.number}].push_back(raws.size());
            raws.push_back(r);
        }
    }
    if (name.isEmpty()) name = "Track " + juce::String(number);

    // A bank: CC 0 and CC 32 on the program's channel, at its tick or up to a sixteenth note before.
    std::map<std::pair<int, int>, std::vector<size_t>> bankCcs;   // (channel, 0 or 32) -> indices, in tick order
    for (size_t i = 0; i < raws.size(); ++i)
        if (raws[i].type == MidiMessageDoc::Type::Control && (raws[i].number == 0 || raws[i].number == 32))
            bankCcs[{raws[i].channel, raws[i].number}].push_back(i);
    for (auto& program : raws) {
        if (program.type != MidiMessageDoc::Type::Program) continue;
        for (int controller : {0, 32}) {
            auto found = bankCcs.find({program.channel, controller});
            if (found == bankCcs.end()) continue;
            auto& list = found->second;
            auto after = std::upper_bound(list.begin(), list.end(), program.tick, [&](double t, size_t i) { return t < raws[i].tick; });
            if (after == list.begin()) continue;
            Raw& best = raws[*(after - 1)];
            if (program.tick - best.tick > ppq / 4.0) continue;
            best.isBank = true;
            (controller == 0 ? program.bankMsb : program.bankLsb) = best.value;
        }
    }
    std::map<int, int> values;
    for (auto& r : raws)
        if (r.type == MidiMessageDoc::Type::Control && ! r.isBank) ++values[r.number];
    for (auto& [controller, count] : values)
        if (count > kMaxValuesPerController)
            out.notes.add(name + ": CC " + juce::String(controller) + " has " + juce::String(count)
                          + " values and was left out. It looks like a pedal sweep.");
    for (auto& r : raws)
        if (r.isBank || (r.type == MidiMessageDoc::Type::Control && values[r.number] > kMaxValuesPerController)) r.dropped = true;

    MidiFileTrack track;
    track.name = name;
    std::set<int> channels;
    for (auto& r : raws) {
        if (r.dropped) continue;
        channels.insert(r.channel);
        if (r.type == MidiMessageDoc::Type::Program) ++track.programChanges;
        else if (r.type == MidiMessageDoc::Type::Control) ++track.controlChanges;
        else ++track.notes;
    }
    if (channels.empty()) return;
    track.channels.assign(channels.begin(), channels.end());

    for (auto& r : raws) {
        if (r.dropped) continue;
        MidiMessageDoc m;
        m.type = r.type;
        m.channel = channels.size() == 1 ? 0 : r.channel;
        switch (r.type) {
        case MidiMessageDoc::Type::Program: m.program = r.number; m.bankMsb = r.bankMsb; m.bankLsb = r.bankLsb; break;
        case MidiMessageDoc::Type::Control: m.controller = r.number; m.value = r.value; break;
        case MidiMessageDoc::Type::Note:
            m.note = r.number; m.velocity = r.value; m.beats = r.lengthTicks / ppq * grid.denominatorAt(r.tick) / 4.0;
            break;
        }
        const auto place = grid.at(r.tick);
        if (track.markers.empty() || track.markers.back().bar != place.bar || track.markers.back().beat != place.beat) {
            MarkerDoc marker; marker.bar = place.bar; marker.beat = place.beat;
            track.markers.push_back(marker);
        }
        track.markers.back().messages.push_back(m);
    }
    // Every limit on a marker is the documents' own: send each through them.
    for (auto& marker : track.markers) {
        ParseNotes ignored;
        MarkerDoc limited;
        if (fromVar(toVar(marker), limited, ignored)) marker = limited;
    }
    out.tracks.push_back(std::move(track));
}

}  // namespace

juce::Result readMidiFile(const juce::File& file, MidiFileContent& content) {
    content = {};
    juce::MidiFile midi;
    // Read leniently: keep the header and the declared number of chunks, and ignore anything after them.
    juce::MemoryBlock data;
    if (! file.loadFileAsData(data) || data.getSize() > 200u * 1024u * 1024u) return juce::Result::fail(kUnreadable);
    {
        const auto* bytes = static_cast<const juce::uint8*>(data.getData());
        const size_t size = data.getSize();
        auto big32 = [&](size_t at) { return (juce::uint64) juce::ByteOrder::bigEndianInt(bytes + at); };
        if (size < 14 || juce::ByteOrder::bigEndianInt(bytes) != juce::ByteOrder::bigEndianInt("MThd"))
            return juce::Result::fail(kUnreadable);
        size_t end = 8 + (size_t) std::min<juce::uint64>(big32(4), size);
        for (int chunks = (bytes[10] << 8) | bytes[11]; chunks > 0 && end + 8 <= size; --chunks) {
            const juce::uint64 next = end + 8 + big32(end + 4);
            if (next > size) break;
            end = (size_t) next;
        }
        data.setSize(std::min(end, size));
    }
    juce::MemoryInputStream in(data, false);
    if (! midi.readFrom(in, false)) return juce::Result::fail(kUnreadable);
    const int format = midi.getTimeFormat();
    if (format < 0) return juce::Result::fail("This MIDI file uses a time format Smart Q Live can't read. Export it with bars and beats.");
    if (format == 0) return juce::Result::fail(kUnreadable);
    const double ppq = format;

    std::vector<std::pair<double, std::pair<int, int>>> signatures;
    std::vector<std::pair<double, double>> tempos;     // tick, BPM
    std::vector<std::pair<double, juce::String>> markers;
    double lastTick = 0.0;
    for (int t = 0; t < midi.getNumTracks(); ++t) {
        const auto& seq = *midi.getTrack(t);
        for (int i = 0; i < seq.getNumEvents(); ++i) {
            const auto& m = seq.getEventPointer(i)->message;
            const double tick = m.getTimeStamp();
            lastTick = std::max(lastTick, tick);
            if (m.isTimeSignatureMetaEvent()) {
                int numerator = 4, denominator = 4;
                m.getTimeSignatureInfo(numerator, denominator);
                const bool known = denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 || denominator == 16;
                if (known && numerator >= 1 && numerator <= 32) signatures.push_back({tick, {numerator, denominator}});
                else content.notes.add("A time signature of " + juce::String(numerator) + "/" + juce::String(denominator)
                                       + " was ignored because Smart Q Live can't use it.");
            } else if (m.isTempoMetaEvent()) {
                const double seconds = m.getTempoSecondsPerQuarterNote();
                if (seconds > 0.0) tempos.push_back({tick, 60.0 / seconds});
            } else if (m.isMetaEvent() && m.getMetaEventType() == 6) {
                const auto text = m.getTextFromTextMetaEvent().trim();
                if (text.isNotEmpty()) markers.push_back({tick, text});
            }
        }
    }
    std::stable_sort(signatures.begin(), signatures.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::stable_sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::stable_sort(markers.begin(), markers.end(), [](auto& a, auto& b) { return a.first < b.first; });
    const BarGrid grid(signatures, ppq);

    // Length: the bars up to the last event; a last event exactly on a bar line ends the bar before.
    if (lastTick > 0.0) {
        const auto end = grid.at(lastTick);
        content.bars = grid.onBarLine(lastTick) ? end.bar - 1 : end.bar;
    }

    // Tempo: a point wherever the tempo or the time signature changes.
    std::vector<double> changes = grid.startTicks();
    for (auto& t : tempos) changes.push_back(t.first);
    std::sort(changes.begin(), changes.end());
    changes.erase(std::unique(changes.begin(), changes.end()), changes.end());
    std::vector<TempoPointDoc> points;
    double bpm = kDefaultBpm;
    size_t nextTempo = 0;
    for (double tick : changes) {
        while (nextTempo < tempos.size() && tempos[nextTempo].first <= tick) bpm = tempos[nextTempo++].second;
        TempoPointDoc p;
        const auto place = grid.at(tick);
        p.bar = place.bar; p.beat = place.beat; p.bpm = bpm; p.numerator = grid.numeratorAt(tick); p.denominator = grid.denominatorAt(tick);
        if (! points.empty()) {
            auto& prev = points.back();
            if (prev.bar == p.bar && prev.beat == p.beat) { prev = p; continue; }
            if (prev.bpm == p.bpm && prev.numerator == p.numerator && prev.denominator == p.denominator) continue;
        }
        points.push_back(p);
    }
    {
        // The documents' limits on a tempo point.
        SongDoc song; song.tempo = points;
        SongDoc limited; ParseNotes ignored;
        if (fromVar(toVar(song), limited, ignored) && limited.tempo.size() == points.size()) {
            for (size_t i = 0; i < points.size(); ++i)
                if (limited.tempo[i].bpm != points[i].bpm)
                    content.notes.add("A tempo of " + juce::String(points[i].bpm, 1) + " BPM at bar " + juce::String(points[i].bar)
                                      + " is outside what Smart Q Live can play, so " + juce::String(limited.tempo[i].bpm, 0) + " BPM is used.");
            points = limited.tempo;
        }
    }
    content.tempo = points;

    // Tracks.
    for (int t = 0; t < midi.getNumTracks(); ++t) summarise(*midi.getTrack(t), t + 1, ppq, grid, content);
    // At most kMaxMarkers places over all tracks; the first ones in file order are kept, and the counts follow what is kept.
    size_t kept = 0, leftOut = 0;
    for (auto& track : content.tracks) {
        const size_t room = kept < kMaxMarkers ? kMaxMarkers - kept : 0;
        if (track.markers.size() > room) {
            leftOut += track.markers.size() - room;
            track.markers.resize(room);
            track.programChanges = track.controlChanges = track.notes = 0;
            for (auto& marker : track.markers)
                for (auto& m : marker.messages) {
                    if (m.type == MidiMessageDoc::Type::Program) ++track.programChanges;
                    else if (m.type == MidiMessageDoc::Type::Control) ++track.controlChanges;
                    else ++track.notes;
                }
        }
        kept += track.markers.size();
    }
    std::erase_if(content.tracks, [](const MidiFileTrack& track) { return track.markers.empty(); });
    for (auto& track : content.tracks) content.bars = std::max(content.bars, track.markers.back().bar);
    if (leftOut > 0)
        content.notes.add(juce::String(leftOut) + " places with changes were left out. Smart Q Live reads at most "
                          + juce::String((int) kMaxMarkers) + " from one file.");

    // Sections: one per named marker, on the nearest bar line; two on the same bar keep the later.
    std::vector<std::pair<int, juce::String>> starts;
    for (auto& [tick, text] : markers) {
        const int bar = grid.nearestBar(tick);
        if (! starts.empty() && starts.back().first == bar) starts.back().second = text;
        else starts.push_back({bar, text});
    }
    if (! starts.empty()) content.bars = std::max(content.bars, starts.back().first);
    if (content.bars > kLastBar) {
        content = {};
        return juce::Result::fail("This MIDI file is longer than Smart Q Live can use (99,999 bars).");
    }
    for (size_t i = 0; i < starts.size(); ++i) {
        SectionDoc s;
        s.name = starts[i].second; s.startBar = starts[i].first;
        s.endBar = i + 1 < starts.size() ? starts[i + 1].first : content.bars + 1;
        ParseNotes ignored; SectionDoc limited;
        if (fromVar(toVar(s), limited, ignored)) content.sections.push_back(limited);
    }
    if (content.tracks.empty() && content.sections.empty())
        content.notes.add("This file has no program changes, controller changes or notes to import.");
    return juce::Result::ok();
}

juce::String suggestDeviceId(const MidiFileTrack& track, const BandDoc& band) {
    auto words = [](const juce::String& text) {
        std::set<juce::String> out;
        juce::String word;
        auto flush = [&] { if (word.isNotEmpty()) out.insert(word == "gtr" ? juce::String("guitar") : word); word = juce::String(); };
        for (auto c : text.toLowerCase()) {
            if (juce::CharacterFunctions::isLetterOrDigit(c)) word += juce::String::charToString(c);
            else flush();
        }
        flush();
        return out;
    };
    const auto trackWords = words(track.name);
    juce::String found;
    for (auto& device : band.midiDevices) {
        const auto deviceWords = words(device.name);
        if (deviceWords.empty()) continue;
        if (! std::all_of(deviceWords.begin(), deviceWords.end(), [&](auto& w) { return trackWords.count(w) != 0; })) continue;
        if (found.isNotEmpty()) return {};
        found = device.id;
    }
    return found;
}

MidiImportChoice defaultMidiChoice(const MidiFileContent& content, const BandDoc& band) {
    MidiImportChoice choice;
    for (size_t i = 0; i < content.tracks.size(); ++i) {
        const auto id = suggestDeviceId(content.tracks[i], band);
        if (id.isNotEmpty()) choice.trackToDevice[(int) i] = id;
    }
    return choice;
}

// ---- The var forms -----------------------------------------------------------------------------------------------------

namespace {

juce::var object() { return juce::var(new juce::DynamicObject()); }

// A whole number in lo..hi, or the fallback when it isn't a finite number.
int wholeIn(const juce::var& v, int lo, int hi, int fallback) {
    if (! (v.isInt() || v.isInt64() || v.isDouble())) return fallback;
    const double d = v;
    return std::isfinite(d) ? (int) juce::jlimit((double) lo, (double) hi, std::round(d)) : fallback;
}

template <typename Item, typename Fn> juce::var listOf(const std::vector<Item>& items, Fn&& each) {
    juce::Array<juce::var> out;
    for (auto& item : items) out.add(each(item));
    return out;
}

template <typename Fn> void forEachIn(const juce::var& list, Fn&& each) {
    if (auto* items = list.getArray())
        for (auto& item : *items) each(item);
}

}  // namespace

juce::var toVar(const MidiFileContent& content) {
    auto v = object();
    auto* o = v.getDynamicObject();
    SongDoc song; song.tempo = content.tempo;
    o->setProperty("tempo", toVar(song).getProperty("tempo", juce::var()));
    o->setProperty("sections", listOf(content.sections, [](const SectionDoc& s) { return toVar(s); }));
    o->setProperty("tracks", listOf(content.tracks, [](const MidiFileTrack& t) {
        auto tv = object();
        auto* to = tv.getDynamicObject();
        to->setProperty("name", t.name);
        to->setProperty("channels", listOf(t.channels, [](int c) { return juce::var(c); }));
        to->setProperty("programChanges", t.programChanges);
        to->setProperty("controlChanges", t.controlChanges);
        to->setProperty("notes", t.notes);
        to->setProperty("markers", listOf(t.markers, [](const MarkerDoc& m) { return toVar(m); }));
        return tv;
    }));
    o->setProperty("bars", content.bars);
    juce::Array<juce::var> notes;
    for (auto& n : content.notes) notes.add(n);
    o->setProperty("notes", notes);
    return v;
}

bool fromVar(const juce::var& v, MidiFileContent& content) {
    if (v.getDynamicObject() == nullptr) return false;
    content = {};
    ParseNotes ignored;
    {
        // The tempo as a song document reads it: limited, and 120 BPM in 4/4 when there is none.
        auto holder = object();
        holder.getDynamicObject()->setProperty("tempo", v.getProperty("tempo", juce::var()));
        SongDoc song;
        if (fromVar(holder, song, ignored)) content.tempo = song.tempo;
        if (content.tempo.empty()) content.tempo = {TempoPointDoc{}};
    }
    forEachIn(v.getProperty("sections", {}), [&](const juce::var& item) {
        SectionDoc s;
        if (fromVar(item, s, ignored)) content.sections.push_back(s);
    });
    forEachIn(v.getProperty("tracks", {}), [&](const juce::var& item) {
        if (item.getDynamicObject() == nullptr) return;
        MidiFileTrack t;
        t.name = item.getProperty("name", {}).toString();
        forEachIn(item.getProperty("channels", {}), [&](const juce::var& c) {
            const int channel = wholeIn(c, 1, 16, 0);
            if (channel > 0 && std::find(t.channels.begin(), t.channels.end(), channel) == t.channels.end()) t.channels.push_back(channel);
        });
        std::sort(t.channels.begin(), t.channels.end());
        t.programChanges = wholeIn(item.getProperty("programChanges", {}), 0, std::numeric_limits<int>::max(), 0);
        t.controlChanges = wholeIn(item.getProperty("controlChanges", {}), 0, std::numeric_limits<int>::max(), 0);
        t.notes = wholeIn(item.getProperty("notes", {}), 0, std::numeric_limits<int>::max(), 0);
        forEachIn(item.getProperty("markers", {}), [&](const juce::var& m) {
            MarkerDoc marker;
            if (fromVar(m, marker, ignored)) t.markers.push_back(std::move(marker));
        });
        content.tracks.push_back(std::move(t));
    });
    content.bars = wholeIn(v.getProperty("bars", {}), 0, kLastBar, 0);
    forEachIn(v.getProperty("notes", {}), [&](const juce::var& n) {
        if (n.isString()) content.notes.add(n.toString());
    });
    return true;
}

juce::var toVar(const MidiImportChoice& choice) {
    auto v = object();
    auto* o = v.getDynamicObject();
    o->setProperty("tempo", choice.tempo);
    o->setProperty("sections", choice.sections);
    juce::Array<juce::var> tracks;
    for (auto& [track, deviceId] : choice.trackToDevice) {
        auto entry = object();
        entry.getDynamicObject()->setProperty("track", track);
        entry.getDynamicObject()->setProperty("deviceId", deviceId);
        tracks.add(entry);
    }
    o->setProperty("tracks", tracks);
    return v;
}

bool fromVar(const juce::var& v, MidiImportChoice& choice) {
    if (v.getDynamicObject() == nullptr) return false;
    choice = {};
    const auto flag = [&](const char* key) {
        const auto value = v.getProperty(key, true);
        return value.isBool() ? (bool) value : true;
    };
    choice.tempo = flag("tempo");
    choice.sections = flag("sections");
    forEachIn(v.getProperty("tracks", {}), [&](const juce::var& entry) {
        const int track = wholeIn(entry.getProperty("track", {}), -1, std::numeric_limits<int>::max(), -1);
        const auto deviceId = entry.getProperty("deviceId", {}).toString();
        if (track >= 0 && deviceId.isNotEmpty()) choice.trackToDevice[track] = deviceId;
    });
    return true;
}

}  // namespace sqlive::model
