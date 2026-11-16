#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"

namespace sqlive::ui {

// The mockup's .wave in the main view's song map: the waveform of the song playing, or of the next song when nothing
// plays, from LoadedSong::overviewMin/Max (every part summed), scaled to the song's loudest point. The part already
// played is mint at 90%, the rest the mockup's grey; a white playhead with a soft glow marks the place. A song not in
// memory yet shows the empty box. 112 px high in this stage; later stages add the sections above it and the MIDI and cue
// lanes below.
//
// refresh() reads the session (now() and the cache's loaded song; never a file) and repaints only what moved: the strip
// of waveform the playhead crossed, or everything when the song or its audio changed. It never takes the keyboard.
class SongMapView : public juce::Component {
public:
    static constexpr int idealHeight = 112;

    explicit SongMapView(app::Session&);

    void refresh();
    juce::String songId() const { return songId_; }               // the song shown; "" for none

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    float playheadX() const;
    void makeBars();                                              // the waveform's bars at this width

    app::Session& session_;
    juce::String songId_;
    std::weak_ptr<const app::LoadedSong> loaded_;                 // the audio the overview came from; never kept alive here
    std::vector<float> min_, max_;                                // a copy of its overview
    double progress_ = 0.0;
    struct Bar { float x, top, bottom; };
    std::vector<Bar> bars_;
};

}  // namespace sqlive::ui
