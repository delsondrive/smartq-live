#pragma once

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "app/Session.h"
#include "ui/ShowState.h"

namespace sqlive::ui {

// The mockup's .sections, .wave and .lanes in the main view's song map, for the song playing, or the next song when nothing
// plays.
//
// A song with sections (ShowState::tiles) has a row of section blocks across the top (the mockup's .sec, drawn by
// sectionTiles::paintBlock), one per step of its arrangement, each as wide as its step is long, in step order: the one
// playing reads "<name> · <n> bars left", the one to play next has the "NEXT" tag. A click on one of the first ten, while
// the song plays, chooses it to play next (Action::Section1 … Section10); nothing else here takes a click. The waveform
// under them is drawn step by step in the same order (a section played twice shows its audio twice), and its playhead is
// in the playing step's block, at its song time (ShowState::alongSteps), whatever was chosen or looped.
// A song with a single step has no row, and its waveform is placed where the audio is across the map's span
// (mapSpanSeconds: the count-in, then the song), the playhead at its song time.
//
// The waveform: from LoadedSong::overviewMin/Max (every part summed), scaled to the song's loudest point. The part already
// played is mint at 90%, the rest the mockup's grey; a white playhead with a soft glow marks the place. A song not in
// memory yet shows the empty box. 112 px high.
//
// Under it, the song's lanes (ShowState::lanes): 6 px down, one 22 px lane each, 3 px apart (a lane that doesn't fit whole
// in the map's height is left out): the Cues lane first when the song has cues (an automatic cue a dashed chip with no
// colour, a manual one in mid), then one per device with a change in the song, its name at the left in dim and each
// change a chip in the device's colour at its place across the waveform. A chip ends before the next one starts, down to
// a tick, and none covers the lane's name.
//
// Its bounds reach `sideBleed` px beyond what it draws on either side, so the blocks' outlines aren't cut off.
//
// refresh() reads the session (now(), the song's document and the cache's loaded song; never a file) and
// repaints only what moved: the strip of waveform the playhead crossed, or everything when the song, its span, its steps
// or its audio changed. setSections and setLanes repaint their rows only when they changed. It never takes the keyboard.
class SongMapView : public juce::Component {
public:
    static constexpr int waveHeight = 112, lanesTop = 6, laneHeight = 22, laneGap = 3;
    static constexpr int sectionsTop = 3, sectionsHeight = 34, sectionsGap = 6, sideBleed = 3;

    explicit SongMapView(app::Session&);

    void refresh();

    // The section blocks to draw, the playing one's words ("Chorus · 6 bars left"; "" for its name alone), the steps laid
    // end to end and the playhead's place along them (ShowState::spans, alongSteps). Copied only when they changed. True
    // when the row appeared or went, so the map's idealHeight() did. Call it before refresh().
    bool setSections(const std::vector<ShowState::Tile>&, const juce::String& playingWords, const StepSpans&, double alongSteps);
    // The lanes to draw; true when their number changed, so the map's idealHeight() did.
    bool setLanes(std::vector<ShowState::Lane>);
    int idealHeight() const;                                      // the blocks, the waveform, and the lanes under it
    // The tallest it can be within room with whole lanes only (at least the blocks and the waveform): a lane that doesn't
    // fit whole is left out, never cut in half.
    int heightWithin(int room) const;
    int lanesShown() const;                                       // for tests: the lanes that fit whole in its height
    juce::String songId() const { return songId_; }               // the song shown; "" for none
    bool hasSections() const { return !tiles_.empty(); }          // the row of blocks shows
    // For tests: the blocks as laid out, in its own pixels, and the step whose block is at a point (-1 for none).
    const std::vector<juce::Rectangle<float>>& blocks() const { return blocks_; }
    int blockAt(juce::Point<float>) const;
    float playheadAt() const { return playheadX(); }              // for tests: the playhead's x, in its own pixels

    void paint(juce::Graphics&) override;
    void resized() override;
    bool hitTest(int x, int y) override;                          // only the first ten blocks, while the song plays
    void mouseUp(const juce::MouseEvent&) override;

private:
    int sectionsShown() const;                                    // the height the block row takes: 0 for none
    juce::Rectangle<float> waveArea() const;
    float xAt(double position01) const;                           // across the waveform's inside, as the chips are
    float playheadX() const;
    void makeBars();                                              // the waveform's bars at this width
    void layoutBlocks();
    void layoutChips();
    juce::Rectangle<float> laneArea(std::size_t lane) const;
    void paintLanes(juce::Graphics&);

    app::Session& session_;
    juce::String songId_;
    std::weak_ptr<const app::LoadedSong> loaded_;                 // the audio the overview came from; never kept alive here
    std::vector<float> min_, max_;                                // a copy of its overview
    double start_ = 0.0, end_ = 1.0;                              // a single step: the map's span, in song time
    double audioFrom_ = 0.0, audioTo_ = 1.0;                      // a single step: where the audio lies across it, 0..1
    double audioStart_ = 0.0, audioLength_ = 0.0;                 // the audio in song time: from -audioStart_, that long
    StepSpans spans_;                                             // sections: the steps laid end to end; else empty
    double along_ = 0.0;                                          // sections: the playhead's place along them, 0..1
    double progress_ = 0.0;                                       // the playhead's place across it, 0..1
    bool playing_ = false;                                        // a song plays: the blocks take a click
    struct Bar { float x, top, bottom; };
    std::vector<Bar> bars_;
    std::vector<ShowState::Tile> tiles_;
    juce::String playingWords_;
    std::vector<juce::Rectangle<float>> blocks_;                  // one a tile, as laid out
    std::vector<ShowState::Lane> lanes_;
    std::vector<std::vector<juce::Rectangle<float>>> chips_;      // each lane's chips, as laid out
};

}  // namespace sqlive::ui
