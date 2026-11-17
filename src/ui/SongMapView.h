#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/ShowState.h"

namespace sqlive::ui {

// The mockup's .wave and .lanes in the main view's song map, for the song playing, or the next song when nothing plays.
//
// The waveform: from LoadedSong::overviewMin/Max (every part summed), scaled to the song's loudest point, placed where the
// audio is across the map's span (mapSpanSeconds: the count-in, then the song). The part already played is mint at 90%,
// the rest the mockup's grey; a white playhead with a soft glow marks the place. A song not in memory yet shows the empty
// box. 112 px high.
//
// Under it, the song's MIDI lanes (ShowState::lanes): 6 px down, one 22 px lane per device with a change in the song, 3 px
// apart (a lane that doesn't fit whole in the map's height is left out), the device's name at the left in dim and each
// change a chip in the device's colour at its place across the same span. A chip ends before the next one starts, down
// to a tick. They are read-only: nothing here takes a click. Sections and cues are later stages'.
//
// refresh() reads the session (now(), the song's document and the cache's loaded song; never a file) and repaints only
// what moved: the strip of waveform the playhead crossed, or everything when the song, its span or its audio changed.
// setLanes repaints the lanes only when they changed. It never takes the keyboard.
class SongMapView : public juce::Component {
public:
    static constexpr int waveHeight = 112, lanesTop = 6, laneHeight = 22, laneGap = 3;

    explicit SongMapView(app::Session&);

    void refresh();
    // The lanes to draw; true when their number changed, so the map's idealHeight() did.
    bool setLanes(std::vector<ShowState::Lane>);
    int idealHeight() const;                                      // the waveform, and the lanes under it
    // The tallest it can be within room with whole lanes only (at least the waveform): a lane that doesn't fit whole is
    // left out, never cut in half.
    int heightWithin(int room) const;
    int lanesShown() const;                                       // for tests: the lanes that fit whole in its height
    juce::String songId() const { return songId_; }               // the song shown; "" for none

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    float xAt(double position01) const;                           // across the waveform's inside, as the chips are
    float playheadX() const;
    void makeBars();                                              // the waveform's bars at this width
    void layoutChips();
    juce::Rectangle<float> laneArea(std::size_t lane) const;
    void paintLanes(juce::Graphics&);

    app::Session& session_;
    juce::String songId_;
    std::weak_ptr<const app::LoadedSong> loaded_;                 // the audio the overview came from; never kept alive here
    std::vector<float> min_, max_;                                // a copy of its overview
    double start_ = 0.0, end_ = 1.0;                              // the map's span, in song time
    double audioFrom_ = 0.0, audioTo_ = 1.0;                      // where the audio lies across it, 0..1
    double progress_ = 0.0;                                       // the playhead's place across it, 0..1
    struct Bar { float x, top, bottom; };
    std::vector<Bar> bars_;
    std::vector<ShowState::Lane> lanes_;
    std::vector<std::vector<juce::Rectangle<float>>> chips_;      // each lane's chips, as laid out
};

}  // namespace sqlive::ui
