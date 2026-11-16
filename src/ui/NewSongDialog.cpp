#include "ui/NewSongDialog.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "model/JsonObject.h"
#include "model/Routing.h"
#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

const juce::String kTick = juce::String::fromUTF8("\xe2\x9c\x93");         // ✓
const juce::String kEnDash = juce::String::fromUTF8("\xe2\x80\x93");       // –
const juce::String kEmDash = juce::String::fromUTF8("\xe2\x80\x94");       // —
const juce::String kOpenQuote = juce::String::fromUTF8("\xe2\x80\x9c");    // “
const juce::String kCloseQuote = juce::String::fromUTF8("\xe2\x80\x9d");   // ”
const juce::String kDownArrow = juce::String::fromUTF8("\xe2\x87\xa9");    // ⇩
const juce::String kChooseRole = "Choose a role" + kEllipsis;
const juce::String kLeaveOut = "Leave out";

// The mockup's CSS: .dlg (1040 px, 14 px corners), .dh, .dbody (the table and a 320 px column, 16 px apart), .drop, th
// and td, .mini, .df and .check.
constexpr int kDialogWidth = 1040;
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 58, kTitleWidth = 360, kTitleHeight = 30, kHeaderGap = 12;
constexpr int kStepPadX = 10, kStepHeight = 22, kStepGap = 6;
constexpr int kBodyPadY = 16, kDropHeight = 42, kDropGap = 10, kDropPadX = 14;
constexpr int kHeadHeight = 29, kRowHeight = 38, kChoiceHeight = 24, kCellPad = 8, kMinRows = 2;
constexpr int kPreviewWidth = 120, kPreviewHeight = 18, kPreviewPoints = 120;
constexpr int kPreviewColumn = kPreviewWidth + 2 * kCellPad, kRoleColumn = 160, kHeardColumn = 180, kMarkColumn = 30;
constexpr int kPanelWidth = 320, kPanelGap = 16;                // .dbody: the table, then the MIDI panel
constexpr int kFooterPadY = 12, kButtonHeight = 30, kLineHeight = 20, kFooterGap = 10, kTickSize = 16, kProgressHeight = 8;

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font boldFont() { return theme::font(theme::sizeBody, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font stepFont() { return theme::font(theme::sizeSmall, true); }
juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }

juce::String counted(int n, const juce::String& one, const juce::String& many) { return juce::String(n) + " " + (n == 1 ? one : many); }

const model::Role* partRole(const model::BandDoc& band, const juce::String& roleId) {
    if (roleId.isEmpty()) return nullptr;
    for (const auto& r : band.roles)
        if (r.id == roleId && r.kind == model::RoleKind::Part) return &r;
    return nullptr;
}

// Where a part role is heard in the rig: "PA and 5 in-ears", "PA", "2 in-ears", "1 in-ear" or "nowhere yet".
juce::String heardIn(const model::RigDoc* rig, const juce::String& roleId) {
    bool pa = false;
    int ears = 0;
    if (rig != nullptr)
        for (const auto& out : rig->outputs) {
            if (out.mix.count(roleId) == 0 || !model::mayRoute(model::RoleKind::Part, out.label)) continue;
            if (out.label == model::OutputLabel::Foh) pa = true;
            else ++ears;
        }
    const auto inEars = counted(ears, "in-ear", "in-ears");
    if (pa) return ears > 0 ? "PA and " + inEars : juce::String("PA");
    return ears > 0 ? inEars : juce::String("nowhere yet");
}

// "2.4", "8": gigabytes to a tenth, with no trailing zero.
juce::String gigabytes(std::int64_t bytes) {
    auto text = juce::String(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 1);
    if (text.endsWith(".0")) text = text.dropLastCharacters(2);
    return text;
}

// A quick look at a file: the peak (0..1) of a short stretch at each of `points` places across it. Empty if it can't be read.
std::vector<float> scanPreview(juce::AudioFormatManager& formats, const juce::File& file, int points) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0) return {};
    constexpr int kStretch = 512;
    juce::AudioBuffer<float> buffer(static_cast<int>(reader->numChannels), kStretch);
    std::vector<float> peaks(static_cast<std::size_t>(points), 0.0f);
    for (int i = 0; i < points; ++i) {
        const auto start = reader->lengthInSamples * i / points;
        const auto frames = static_cast<int>(std::min<juce::int64>(kStretch, reader->lengthInSamples - start));
        if (frames <= 0 || !reader->read(&buffer, 0, frames, start, true, true)) continue;
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) peak = std::max(peak, buffer.getMagnitude(ch, 0, frames));
        peaks[static_cast<std::size_t>(i)] = std::min(peak, 1.0f);
    }
    return peaks;
}

// The table's columns across a width: FILE, PREVIEW, ROLE, HEARD IN and the mark, each from its x to the next one's.
struct Columns {
    int file = 0, preview = 0, role = 0, heard = 0, mark = 0, end = 0;
};

Columns columnsFor(int width) {
    Columns c;
    c.end = width;
    c.mark = std::max(0, c.end - kMarkColumn);
    c.heard = std::max(0, c.mark - kHeardColumn);
    c.role = std::max(0, c.heard - kRoleColumn);
    c.preview = std::max(0, c.role - kPreviewColumn);
    return c;
}

// A body that scrolls in its viewport: as wide as the viewport leaves it, and at least as tall.
void fit(juce::Viewport& view, juce::Component& body, int idealHeight) {
    const bool scrolls = idealHeight > view.getHeight();
    const int width = std::max(0, view.getWidth() - (scrolls ? view.getScrollBarThickness() : 0));
    body.setSize(width, std::max(idealHeight, view.getHeight()));
}

}  // namespace

// ---- State -------------------------------------------------------------------------------------------------------------

NewSongState makeNewSongState(const model::ImportPlan& plan, const app::Session& session, const model::MidiFileContent* midi,
                              const model::MidiImportChoice* choice) {
    NewSongState state;
    state.midi = makeMidiSummary(session, plan.midiFile, midi, choice);
    const auto& model = session.model();
    const auto& band = model.band();
    const auto* rig = model.rig(session.rigId());
    state.title = plan.title;
    state.rig = rig != nullptr ? rig->name : juce::String();
    if (const auto* list = model.setlist(session.setlistId())) state.setlist = list->name;

    int needing = 0;
    model::SongDoc kept;                                          // what the kept files will take in memory
    for (const auto& f : plan.files) {
        NewSongState::Row row;
        row.file = f.file.getFileName();
        row.hasProblem = f.problem.isNotEmpty();
        row.leftOut = f.leaveOut || row.hasProblem;
        const auto* role = partRole(band, f.roleId);
        row.needsRole = !row.leftOut && role == nullptr;
        if (row.leftOut) {
            row.role = kLeaveOut;
            row.mark = kEnDash;
            if (row.hasProblem) row.heardIn = f.problem;
            else if (model::looksLikeAClick(f.file.getFileNameWithoutExtension())) row.heardIn = "The app makes its own click";
            else row.heardIn = kEmDash;
        } else if (row.needsRole) {
            row.role = kChooseRole;
            row.mark = "?";
            row.heardIn = kEmDash;
            ++needing;
        } else {
            row.role = role->name;
            row.mark = kTick;
            row.heardIn = heardIn(rig, role->id);
        }
        if (!row.leftOut) {
            model::PartDoc part;
            part.channels = f.channels;
            kept.parts.push_back(part);
            kept.lengthSeconds = std::max(kept.lengthSeconds, f.seconds);
        }
        state.rows.push_back(row);
    }

    if (needing > 0) state.waiting = counted(needing, "file needs a role", "files need a role");
    else if (kept.parts.empty()) state.waiting = "Nothing to import yet";
    state.canCreate = needing == 0 && !kept.parts.empty();

    const auto bytes = model::bytesInMemory(kept, session.sampleRate());
    const auto budget = session.cache().budgetBytes();
    if (bytes > budget / 2)
        state.tooBig = "This song needs " + gigabytes(bytes) + " GB of memory. This computer keeps " + gigabytes(budget)
                       + " GB for songs, so it may load slowly or not at all.";
    return state;
}

// ---- The conversion ----------------------------------------------------------------------------------------------------

// Converts copies of the plan into the reserved folder ("Smart Q Live import"). It touches only files: never the model, the
// BandFolder or the UI. It tells the dialog through its atomics and its AsyncUpdater.
class NewSongDialog::Converter : public juce::Thread {
public:
    Converter(NewSongDialog& dialog, model::ImportPlan plan, juce::File folder, double bandRate)
        : juce::Thread("Smart Q Live import"), dialog_(dialog), plan_(std::move(plan)), folder_(std::move(folder)), rate_(bandRate) {}

    model::ConvertedSong result;                                  // read only once the thread has ended

    void run() override {
        if (!threadShouldExit()) {
            dialog_.began_ = true;
            result = model::convertSongFiles(plan_, folder_, rate_, [this](double progress) {
                dialog_.progress_ = progress;
                dialog_.triggerAsyncUpdate();
                return !threadShouldExit();
            });
        }
        dialog_.done_ = true;
        dialog_.triggerAsyncUpdate();
    }

private:
    NewSongDialog& dialog_;
    const model::ImportPlan plan_;
    const juce::File folder_;
    const double rate_;
};

// ---- The table's rows --------------------------------------------------------------------------------------------------

class NewSongDialog::RowsBody : public juce::Component {
public:
    explicit RowsBody(NewSongDialog& dialog) : dialog_(dialog) {}

    int idealHeight() const { return static_cast<int>(dialog_.state_.rows.size()) * kRowHeight; }
    Columns columns() const { return columnsFor(getWidth()); }

    void update() {
        const auto& rows = dialog_.state_.rows;
        roleNames_.clear();
        for (const auto& r : dialog_.session_.model().band().roles)
            if (r.kind == model::RoleKind::Part) roleNames_.add(r.name);

        while (choices_.size() > rows.size()) choices_.pop_back();
        while (choices_.size() < rows.size()) {
            const int row = static_cast<int>(choices_.size());
            auto choice = std::make_unique<Choice>();
            choice->onChange = [this, row](int item) { dialog_.chooseRole(row, item); };
            addAndMakeVisible(*choice);
            choices_.push_back(std::move(choice));
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            auto& choice = *choices_[i];
            if (row.hasProblem) {
                choice.setItems({kLeaveOut}, 0);
            } else if (row.needsRole) {
                juce::StringArray items{kChooseRole};
                items.addArray(roleNames_);
                items.add(kLeaveOut);
                choice.setItems(items, 0);
            } else {
                juce::StringArray items(roleNames_);
                items.add(kLeaveOut);
                choice.setItems(items, row.leftOut ? items.size() - 1 : roleNames_.indexOf(row.role));
            }
            choice.setNeedsAttention(row.needsRole);
            choice.setEnabled(!row.hasProblem && !dialog_.importing());
        }
        resized();
        repaint();
    }

    // How a row's role choice reads: its items are "Choose a role…" (only while it needs one), the part roles, "Leave out".
    bool offersChoose(int row) const { return dialog_.state_.rows[static_cast<std::size_t>(row)].needsRole; }
    const juce::StringArray& roleNames() const { return roleNames_; }

    void resized() override {
        const auto c = columns();
        for (std::size_t i = 0; i < choices_.size(); ++i) {
            const int y = static_cast<int>(i) * kRowHeight;
            choices_[i]->setBounds(c.role + kCellPad, y + (kRowHeight - kChoiceHeight) / 2,
                                   std::max(0, c.heard - c.role - 2 * kCellPad), kChoiceHeight);
        }
    }

    void paint(juce::Graphics& g) override {
        const auto c = columns();
        const auto& rows = dialog_.state_.rows;
        const auto& files = dialog_.plan_.files;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const int y = static_cast<int>(i) * kRowHeight;
            const auto cell = [&](int from, int to) {
                return juce::Rectangle<int>(from + kCellPad, y, std::max(0, to - from - 2 * kCellPad), kRowHeight).toFloat();
            };

            g.setColour(theme::text);
            g.setFont(boldFont());
            const auto fileArea = cell(c.file, c.preview);
            g.drawText(shorten(row.file, boldFont(), fileArea.getWidth()), fileArea, juce::Justification::centredLeft, false);

            if (i < files.size()) {
                const auto preview = dialog_.previews_.find(files[i].file.getFullPathName());
                if (preview != dialog_.previews_.end())
                    drawPreview(g, preview->second, cell(c.preview, c.role).withSizeKeepingCentre(kPreviewWidth, kPreviewHeight));
            }

            const auto heardColour = row.hasProblem ? theme::danger : row.needsRole ? theme::caution : row.leftOut ? theme::dim : theme::mid;
            const auto heardFont = row.leftOut ? hintFont() : bodyFont();
            g.setColour(heardColour);
            g.setFont(heardFont);
            const auto heardArea = cell(c.heard, c.mark);
            g.drawText(shorten(row.heardIn, heardFont, heardArea.getWidth()), heardArea, juce::Justification::centredLeft, false);

            g.setColour(row.needsRole ? theme::caution : row.leftOut ? theme::dim : theme::mint);
            g.setFont(boldFont());
            g.drawText(row.mark, cell(c.mark, c.end), juce::Justification::centredLeft, false);

            g.setColour(theme::rowLine);
            g.fillRect(0, y + kRowHeight - 1, getWidth(), 1);
        }
    }

private:
    // The mockup's .mini: thin bars in mid at 70%, each as tall as the file is loud there, scaled to the file's loudest.
    static void drawPreview(juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> area) {
        if (peaks.empty()) return;
        const float loudest = *std::max_element(peaks.begin(), peaks.end());
        if (loudest <= 0.0f) return;
        // The mockup's canvas.mini: a 1.3 px bar every 2 px at 70 %, each the loudest of the peaks under it.
        g.setColour(theme::mid.withAlpha(0.7f));
        const float perPixel = static_cast<float>(peaks.size()) / area.getWidth();
        for (float x = 0.0f; x + 1.0f <= area.getWidth(); x += 2.0f) {
            const auto from = static_cast<std::size_t>(x * perPixel);
            const auto to = std::min(peaks.size(), std::max(from + 1, static_cast<std::size_t>((x + 2.0f) * perPixel)));
            const float peak = from < to ? *std::max_element(peaks.begin() + static_cast<std::ptrdiff_t>(from), peaks.begin() + static_cast<std::ptrdiff_t>(to)) : 0.0f;
            const float half = std::max(0.5f, peak / loudest * area.getHeight() * 0.5f);
            g.fillRect(area.getX() + x, area.getCentreY() - half, 1.3f, half * 2.0f);
        }
    }

    NewSongDialog& dialog_;
    juce::StringArray roleNames_;
    std::vector<std::unique_ptr<Choice>> choices_;
};

// ---- The dialog --------------------------------------------------------------------------------------------------------

NewSongDialog::NewSongDialog(app::Session& session, juce::Array<juce::File> dropped)
    : session_(session), rows_(std::make_unique<RowsBody>(*this)), midiPanel_(session) {
    setWantsKeyboardFocus(true);

    title_.setFont(theme::font(theme::sizeBase, true));
    title_.setIndents(8, 6);
    title_.setTextToShowWhenEmpty("Song title", theme::dim);
    title_.onTextChange = [this] {
        plan_.title = title_.getText();
        refresh();
    };

    rowsView_.setViewedComponent(rows_.get(), false);
    rowsView_.setScrollBarsShown(true, false);
    rowsView_.setScrollBarThickness(10);

    midiView_.setViewedComponent(&midiPanel_, false);
    midiView_.setScrollBarsShown(true, false);
    midiView_.setScrollBarThickness(10);
    midiPanel_.onChoiceChanged = [this](const model::MidiImportChoice& choice) {
        if (importing()) return;
        midiChoice_ = choice;
        refresh();
    };

    cancel_.onClick = [this] { cancel(); };
    create_.onClick = [this] { create(); };

    for (juce::Component* child : std::initializer_list<juce::Component*>{&title_, &rowsView_, &midiView_, &cancel_, &create_})
        addAndMakeVisible(child);

    session_.changes().addChangeListener(this);
    addFiles(dropped);
    refresh();
}

NewSongDialog::~NewSongDialog() {
    session_.changes().removeChangeListener(this);
    stopConverting();
}

void NewSongDialog::setRole(int row, const juce::String& roleId) {
    if (importing() || !juce::isPositiveAndBelow(row, static_cast<int>(plan_.files.size()))) return;
    auto& file = plan_.files[static_cast<std::size_t>(row)];
    if (file.problem.isNotEmpty()) return;                       // a file that can't be read is always left out
    file.roleId = roleId;
    file.leaveOut = false;
    refresh();
}

void NewSongDialog::setLeftOut(int row, bool leftOut) {
    if (importing() || !juce::isPositiveAndBelow(row, static_cast<int>(plan_.files.size()))) return;
    auto& file = plan_.files[static_cast<std::size_t>(row)];
    if (file.problem.isNotEmpty()) return;
    file.leaveOut = leftOut;
    refresh();
}

void NewSongDialog::setTitle(const juce::String& title) {
    if (importing()) return;
    plan_.title = title;
    if (title_.getText() != title) title_.setText(title, juce::dontSendNotification);
    refresh();
}

void NewSongDialog::setTrackDevice(int track, const juce::String& deviceId) {
    if (importing() || !midi_ || !juce::isPositiveAndBelow(track, static_cast<int>(midi_->tracks.size()))) return;
    if (deviceId.isEmpty()) midiChoice_.trackToDevice.erase(track);
    else midiChoice_.trackToDevice[track] = deviceId;
    refresh();
}

void NewSongDialog::chooseRole(int row, int item) {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size()))) return;
    const bool offersChoose = rows_->offersChoose(row);
    const int roleItem = item - (offersChoose ? 1 : 0);
    const auto& names = rows_->roleNames();
    if (offersChoose && item == 0) return;                       // "Choose a role…" again: nothing to do
    if (roleItem == names.size()) {
        setLeftOut(row, true);
        return;
    }
    for (const auto& r : session_.model().band().roles)
        if (r.kind == model::RoleKind::Part && r.name == names[roleItem]) {
            setRole(row, r.id);
            return;
        }
}

void NewSongDialog::addFiles(const juce::Array<juce::File>& files) {
    if (importing()) return;
    for (const auto& f : files) dropped_.addIfNotAlreadyThere(f);
    auto next = model::inspectFiles(dropped_, session_.model().band());

    // The answers already given for a file stay; the title too, once there is one.
    for (auto& f : next.files)
        for (const auto& old : plan_.files)
            if (old.file == f.file && f.problem.isEmpty() && old.problem.isEmpty()) {
                f.roleId = old.roleId;
                f.leaveOut = old.leaveOut;
            }
    if (plan_.title.trim().isNotEmpty()) next.title = plan_.title;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& f : next.files)
        if (f.problem.isEmpty() && previews_.count(f.file.getFullPathName()) == 0)
            previews_[f.file.getFullPathName()] = scanPreview(formats, f.file, kPreviewPoints);

    plan_ = std::move(next);
    if (plan_.midiFile != midiFile_) {                            // a new MIDI file: read it once, here
        midiFile_ = plan_.midiFile;
        midi_.reset();
        midiProblem_.clear();
        midiChoice_ = {};
        if (midiFile_ != juce::File()) {
            model::MidiFileContent content;
            const auto read = model::readMidiFile(midiFile_, content);
            if (read.wasOk()) {
                midiChoice_ = model::defaultMidiChoice(content, session_.model().band());
                midi_ = std::move(content);
            } else {
                midiProblem_ = read.getErrorMessage();
            }
        }
    }
    if (title_.getText() != plan_.title) title_.setText(plan_.title, juce::dontSendNotification);
    problem_.clear();
    refresh();
}

void NewSongDialog::create() {
    if (importing() || !state_.canCreate || session_.mode() != app::Mode::Edit) return;
    problem_.clear();
    const auto plan = plan_;                                      // the thread works on copies
    const double rate = session_.model().band().sampleRate;
    const auto folder = session_.reserveImportFolder(plan);
    if (folder == juce::File()) {
        problem_ = "Couldn't make a folder for this song.";
        refresh();
        return;
    }
    reserved_ = folder;
    began_ = false;
    done_ = false;
    progress_ = 0.0;
    shownProgress_ = 0.0;
    worker_ = std::make_unique<Converter>(*this, plan, folder, rate);
    worker_->startThread();
    refresh();
}

void NewSongDialog::cancel() {
    stopConverting();
    refresh();
    if (onCancelled) onCancelled();
}

void NewSongDialog::stopConverting() {
    if (worker_ == nullptr) return;
    worker_->signalThreadShouldExit();
    worker_->waitForThreadToExit(-1);                             // a cancelled conversion stops within a block
    auto converted = std::move(worker_->result);
    const bool began = began_;
    worker_.reset();
    cancelPendingUpdate();
    const auto folder = std::exchange(reserved_, juce::File());
    if (!began) {
        session_.releaseImportFolder(folder);                     // the conversion never started: the folder is still empty
    } else if (converted.result.wasOk()) {
        // It finished just before it was stopped: nothing is added, and the session deletes what it wrote.
        converted.result = juce::Result::fail("The import was stopped.");
        session_.commitImport(std::move(converted));
    }                                                             // a conversion that failed or was stopped deleted its folder
}

void NewSongDialog::handleAsyncUpdate() {
    if (worker_ == nullptr) return;
    if (done_) {
        finish();
        return;
    }
    shownProgress_ = progress_;
    repaint(footer_);
}

void NewSongDialog::finish() {
    worker_->waitForThreadToExit(-1);                             // it has set done_ and is returning
    auto converted = std::move(worker_->result);
    const bool began = began_;
    worker_.reset();
    const auto folder = std::exchange(reserved_, juce::File());
    if (!began) {
        session_.releaseImportFolder(folder);
        refresh();
        return;
    }
    if (converted.result.failed()) {                              // the conversion deleted its folder
        problem_ = converted.result.getErrorMessage();
        refresh();
        return;
    }
    // A device removed while the song was converting: its tracks are left out rather than refusing the whole import.
    const auto choice = model::withBandDevices(midiChoice_, session_.model().band());
    const auto result = session_.commitImport(std::move(converted), midi_ ? &choice : nullptr);
    if (result.result.failed()) {
        problem_ = result.result.getErrorMessage();
        refresh();
        return;
    }
    if (result.note.isNotEmpty())                                 // the song is in, without its MIDI: said after the dialog has gone
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::NoIcon)
                                         .withTitle(result.note)
                                         .withButton("OK"),
                                     nullptr);
    if (addToSetlist_ && session_.model().setlist(session_.setlistId()) != nullptr) {
        const auto added = session_.commands().run("setlist.addSong", jsonObject({{"setlistId", session_.setlistId()}, {"songId", result.songId}}));
        if (!added.ok) problem_ = added.message;
    }
    refresh();
    if (onCreated) onCreated(result.songId);                      // last: it may close the dialog
}

void NewSongDialog::browse() {
    if (importing()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Add files to the song", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.opus;*.mid;*.midi");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer<NewSongDialog>(this)](const juce::FileChooser& chooser) {
                              if (safe != nullptr && !chooser.getResults().isEmpty()) safe->addFiles(chooser.getResults());
                          });
}

void NewSongDialog::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(false); }

void NewSongDialog::refresh(bool force) {
    auto next = makeNewSongState(plan_, session_, midi_ ? &*midi_ : nullptr, &midiChoice_);
    if (!force && next == state_) return;                         // the session's changes come 30 times a second while a song plays
    state_ = std::move(next);
    const bool busy = importing();
    title_.setReadOnly(busy);
    create_.setEnabled(state_.canCreate && !busy && session_.mode() == app::Mode::Edit);
    create_.setVisible(!busy);
    rows_->update();
    midiPanel_.setProblem(midiProblem_);
    midiPanel_.setContent(midiFile_, midi_ ? &*midi_ : nullptr, midiChoice_);
    midiPanel_.setChoicesEnabled(!busy);
    if (idealHeight() != laidOutHeight_) {                        // its parent (the veil) sizes it from idealHeight()
        laidOutHeight_ = idealHeight();
        if (auto* parent = getParentComponent()) parent->resized();
    }
    resized();
    repaint();
}

int NewSongDialog::footerHeight() const {
    const int lines = (problem_.isNotEmpty() ? 1 : 0) + (state_.tooBig.isNotEmpty() ? 1 : 0);
    return 2 * kFooterPadY + kButtonHeight + lines * kLineHeight;
}

int NewSongDialog::idealHeight() const {
    const int rows = std::max(kMinRows, static_cast<int>(state_.rows.size()));
    const int table = kDropHeight + kDropGap + kHeadHeight + rows * kRowHeight;
    return kHeaderHeight + 2 * kBodyPadY + std::max(table, midiPanel_.idealHeight(kPanelWidth)) + footerHeight();
}

void NewSongDialog::resized() {
    auto area = getLocalBounds();

    // The header: "New song", the title, and the steps on the right.
    header_ = area.removeFromTop(kHeaderHeight);
    auto head = header_.reduced(kPadX, 0);
    head.removeFromLeft(static_cast<int>(std::ceil(widthOf(titleFont(), "New song"))) + kHeaderGap);
    int stepsWidth = 0;
    for (const auto* step : {"Files", "Check what was found", "Create"})
        stepsWidth += static_cast<int>(std::ceil(widthOf(stepFont(), step))) + 2 * kStepPadX + kStepGap;
    stepsWidth += static_cast<int>(std::ceil(widthOf(stepFont(), kTick + " ")));
    steps_ = head.removeFromRight(std::min(stepsWidth, head.getWidth() / 2));
    const int titleWidth = std::min(kTitleWidth, std::max(0, head.getWidth() - kHeaderGap));
    title_.setBounds(head.removeFromLeft(titleWidth).withSizeKeepingCentre(titleWidth, kTitleHeight));

    // The footer: messages above a row of the tick box, what is still to answer, Cancel and Create song.
    footer_ = area.removeFromBottom(footerHeight());
    auto foot = footer_.reduced(kPadX, kFooterPadY);
    tooBig_ = foot.removeFromTop(foot.getHeight() - kButtonHeight);
    auto buttons = foot;
    create_.setBounds(buttons.removeFromRight(importing() ? 0 : create_.idealWidth()));
    if (!importing()) buttons.removeFromRight(kFooterGap);
    cancel_.setBounds(buttons.removeFromRight(cancel_.idealWidth()));
    buttons.removeFromRight(kFooterGap);
    if (importing()) {
        progress_bar_ = buttons.withSizeKeepingCentre(buttons.getWidth(), kProgressHeight);
        tick_ = waiting_ = {};
    } else {
        progress_bar_ = {};
        const auto tickText = "Add this song to " + kOpenQuote + state_.setlist + kCloseQuote;
        const int tickWidth = state_.setlist.isEmpty() ? 0 : kTickSize + 6 + static_cast<int>(std::ceil(widthOf(bodyFont(), tickText)));
        tick_ = buttons.removeFromLeft(std::min(tickWidth, buttons.getWidth() / 2));
        buttons.removeFromLeft(kFooterGap);
        waiting_ = buttons;
    }

    // The body: the drop strip and the table, and the MIDI panel on the right (scrolling when the dialog is too short for it).
    auto body = area.reduced(kPadX, kBodyPadY);
    {
        const auto right = body.removeFromRight(std::min(kPanelWidth, body.getWidth() / 2));
        body.removeFromRight(kPanelGap);
        const int panelHeight = midiPanel_.idealHeight(right.getWidth());
        midiView_.setBounds(right.withHeight(std::min(right.getHeight(), panelHeight)));
        const int panelWidth = panelHeight > midiView_.getHeight() ? right.getWidth() - midiView_.getScrollBarThickness() : right.getWidth();
        midiPanel_.setSize(panelWidth, midiPanel_.idealHeight(panelWidth));
    }
    drop_ = body.removeFromTop(kDropHeight);
    body.removeFromTop(kDropGap);
    tableBox_ = body;
    body.removeFromTop(kHeadHeight);
    rowsView_.setBounds(body);
    fit(rowsView_, *rows_, rows_->idealHeight());
}

void NewSongDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);

    // The header.
    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText("New song", header_.reduced(kPadX, 0).toFloat(), juce::Justification::centredLeft, false);
    {
        const bool busy = importing();
        struct Step { juce::String text; bool done, on; };
        const Step steps[] = {{kTick + " Files", true, false}, {"Check what was found", false, !busy}, {"Create", false, busy}};
        float x = static_cast<float>(steps_.getRight());
        for (int i = 2; i >= 0; --i) {
            const auto& step = steps[i];
            const float w = std::ceil(widthOf(stepFont(), step.text)) + 2.0f * kStepPadX;
            x -= w;
            const juce::Rectangle<float> pill(x, static_cast<float>(steps_.getCentreY()) - kStepHeight / 2.0f, w, static_cast<float>(kStepHeight));
            x -= static_cast<float>(kStepGap);
            if (pill.getX() < static_cast<float>(steps_.getX())) break;
            g.setColour(step.on ? theme::mint : theme::field);
            g.fillRoundedRectangle(pill, kStepHeight / 2.0f);
            g.setColour(step.on ? theme::onAccent : step.done ? theme::mint : theme::dim);
            g.setFont(stepFont());
            g.drawText(step.text, pill, juce::Justification::centred, false);
        }
    }

    // The drop strip.
    {
        const auto strip = drop_.toFloat();
        g.setColour(theme::mint.withAlpha(filesOver_ ? 0.10f : 0.04f));
        g.fillRoundedRectangle(strip, theme::radiusPanel);
        juce::Path outline, dashed;
        outline.addRoundedRectangle(strip.reduced(1.0f), theme::radiusPanel);
        const float dashes[] = {6.0f, 4.0f};
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(theme::mint.withAlpha(filesOver_ ? 0.8f : 0.35f));
        g.fillPath(dashed);

        auto text = strip.reduced(static_cast<float>(kDropPadX), 0.0f);
        const auto piece = [&](const juce::String& words, const juce::Font& font, juce::Colour colour) {
            const float w = std::min(text.getWidth(), std::ceil(widthOf(font, words)));
            g.setColour(colour);
            g.setFont(font);
            g.drawText(words, text.removeFromLeft(w), juce::Justification::centredLeft, false);
        };
        piece(kDownArrow + "  ", bodyFont(), theme::mid);
        piece(plan_.files.empty() ? "Drop the song's files here, or " : "Drop more files here, or ", bodyFont(), theme::mid);
        piece("browse", boldFont(), theme::mint);
        piece(". WAV, AIFF or FLAC, plus one MIDI file.", bodyFont(), theme::mid);
    }

    // The table's head.
    {
        const auto c = rows_->columns();
        const int x0 = rowsView_.getX(), headY = tableBox_.getY();
        g.setColour(theme::dim);
        g.setFont(headFont());
        const auto head = [&](const juce::String& text, int from, int to) {
            g.drawText(text, x0 + from + kCellPad, headY, std::max(0, to - from - 2 * kCellPad), kHeadHeight - 1, juce::Justification::centredLeft, true);
        };
        head("FILE", c.file, c.preview);
        head("PREVIEW", c.preview, c.role);
        head("ROLE", c.role, c.heard);
        head(state_.rig.isEmpty() ? juce::String("HEARD IN") : "HEARD IN (" + state_.rig.toUpperCase() + ")", c.heard, c.mark);
        g.setColour(theme::hairline);
        g.fillRect(tableBox_.getX(), headY + kHeadHeight - 1, tableBox_.getWidth(), 1);
    }

    // The footer's messages.
    {
        auto lines = tooBig_.toFloat();
        const auto line = [&](const juce::String& text, juce::Colour colour) {
            if (text.isEmpty()) return;
            g.setColour(colour);
            g.setFont(hintFont());
            const auto area = lines.removeFromTop(static_cast<float>(kLineHeight));
            g.drawText(shorten(text, hintFont(), area.getWidth()), area, juce::Justification::centredLeft, false);
        };
        line(problem_, theme::danger);
        line(state_.tooBig, theme::caution);
    }

    if (importing()) {                                            // the progress bar
        const auto bar = progress_bar_.toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(bar, bar.getHeight() / 2.0f);
        g.setColour(theme::mint);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(juce::jlimit(0.0, 1.0, shownProgress_))), bar.getHeight() / 2.0f);
        return;
    }

    if (!tick_.isEmpty()) {                                       // the mockup's .check
        const auto box = tick_.toFloat().removeFromLeft(static_cast<float>(kTickSize)).withSizeKeepingCentre(kTickSize, kTickSize);
        if (addToSetlist_) {
            g.setColour(theme::mint);
            g.fillRoundedRectangle(box, 4.0f);
            g.setColour(theme::onAccent);
            g.setFont(theme::font(theme::sizeCaption, true));
            g.drawText(kTick, box, juce::Justification::centred, false);
        } else {
            g.setColour(theme::field);
            g.fillRoundedRectangle(box, 4.0f);
            g.setColour(theme::hairline);
            g.drawRoundedRectangle(box.reduced(0.5f), 4.0f, 1.0f);
        }
        const auto label = tick_.toFloat().withTrimmedLeft(static_cast<float>(kTickSize + 6));
        g.setColour(theme::mid);
        g.setFont(bodyFont());
        g.drawText(shorten("Add this song to " + kOpenQuote + state_.setlist + kCloseQuote, bodyFont(), label.getWidth()), label,
                   juce::Justification::centredLeft, false);
    }

    if (state_.waiting.isNotEmpty()) {                            // .hint.warn, against the buttons
        const auto area = waiting_.toFloat();
        g.setColour(theme::caution);
        g.setFont(hintFont());
        g.drawText(shorten(state_.waiting, hintFont(), area.getWidth()), area, juce::Justification::centredRight, false);
    }
}

void NewSongDialog::mouseUp(const juce::MouseEvent& e) {
    if (e.mouseWasDraggedSinceMouseDown() || importing()) return;
    if (drop_.contains(e.getPosition())) {
        browse();
    } else if (tick_.contains(e.getPosition())) {
        addToSetlist_ = !addToSetlist_;
        repaint(tick_);
    }
}

bool NewSongDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        cancel();
        return true;
    }
    return false;
}

bool NewSongDialog::isInterestedInFileDrag(const juce::StringArray& files) { return !importing() && !files.isEmpty(); }

void NewSongDialog::fileDragEnter(const juce::StringArray&, int, int) {
    filesOver_ = true;
    repaint();
}

void NewSongDialog::fileDragExit(const juce::StringArray&) {
    filesOver_ = false;
    repaint();
}

void NewSongDialog::filesDropped(const juce::StringArray& files, int, int) {
    filesOver_ = false;
    juce::Array<juce::File> dropped;
    for (const auto& path : files) dropped.add(juce::File(path));
    addFiles(dropped);
    repaint();
}

}  // namespace sqlive::ui
