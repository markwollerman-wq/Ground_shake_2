#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include "Analysis.h"
#include "Checks.h"
#include "MatchStage.h"

//==============================================================================
// Reference audio ready for playback at the host's sample rate, plus its loop window.
// Built on a background thread, then handed to the audio thread by pointer.
struct RefPlayback
{
    std::shared_ptr<const juce::AudioBuffer<float>> audio;   // whole track, stereo, at the host rate
    double sampleRate = 44100.0;
    int windowStart = 0, windowLength = 0;                  // the 8-bar window, in samples
    double bpm = 120.0;                                     // reference tempo in use
    float windowMeanSquare = 1.0e-6f;                       // loudness of the window, for level matching
};

enum class MatchState { off, ready, preview, applied };

//==============================================================================
// Phase 3: a pair = a stretch of YOUR learned track (song bars A-B) matched to a few bars
// of a reference (slot A or B). Each pair has its own analysis, readout and correction.
struct Pair
{
    int slot = 0;                       // reference A (0) or B (1)
    int refStart = 0, refBars = 4;      // reference bars (from its grid)
    int songStart = 0, songEnd = 8;     // your song bars, counted from the song start; end is exclusive
    int measureStart = 0;               // the bars inside A-B the fix is measured from (picked automatically)
    bool manual = true;                 // false = made by AUTO-PAIR
    int colour = 0;

    // analysis (saved with the project)
    bool analyzed = false, needsLearn = false, breakdown = false;
    gs::StepProfile you, ref;
    gs::Measurements youM, refM;
    juce::String refName;
    int refDropBar = 0, refNumBars = 0;

    // readout
    gs::Coach coach;
    int status = 0;                     // 0 = not measured, 1 green, 2 amber, 3 red

    // correction
    gs::MatchSettings corr;
    MatchState state = MatchState::off;

    // Phase 4: OUT = this pair with its correction, worked out on the learned audio (no playback needed)
    bool outValid = false;
    gs::StepProfile outYou;
    gs::Measurements outYouM;
    gs::Coach outCoach;
    int outStatus = 0;                  // 0 = not worked out, 1 green, 2 amber, 3 red
    bool corrAuto = false;              // the correction came from MATCH (so OUT may fine-tune it), not from the knobs
    bool correctionOn() const { return state == MatchState::applied || state == MatchState::preview; }
};

// What the audio thread needs to switch corrections by playhead position
struct CorrectionTable
{
    struct Entry { double startPpq, endPpq; gs::MatchSettings s; bool active, levelMatch; };
    std::vector<Entry> entries;
    bool pairMode = false;              // true: unmarked parts are left alone
};

//==============================================================================
class GroundShakeProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    GroundShakeProcessor();
    ~GroundShakeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Message-thread API used by the editor
    void loadReferenceAsync (const juce::File& file, int slot = -1);   // -1 = the slot on show
    bool isLoading() const { return loading.load(); }
    juce::String getLoadError() const { return loadError; }
    bool hasReference() const { return reference != nullptr; }
    const gs::ReferenceTrack* getReference() const { return reference.get(); }
    void reanalyzeReference();

    double getBpm() const { return bpm; }                       // in use (detected or typed)
    double getDetectedBpm() const { return reference ? reference->detectedBpm : 0.0; }
    void setBpm (double newBpm);
    void doubleBpm() { setBpm (bpm * 2.0); }
    void halveBpm()  { setBpm (bpm * 0.5); }
    double getGridOffsetSec() const { return gridOffsetSec; }
    void nudgeGrid (double seconds);
    int getWindowStartBar() const { return windowStartBar; }
    void setWindowStartBar (int bar);
    int getNumReferenceBars() const;
    double getGridStartSec() const;                              // first bar line incl. nudge
    int getDropBar() const { return dropBar; }
    void findTheDrop();
    void findTheBreakdown();
    int getBreakdownBar() const { return breakdownBar; }
    bool isBreakdownMode() const { return breakdownMode; }
    int getWindowBars() const { return windowBars; }
    void setWindowBars (int bars, bool fromLoop = false);   // 1, 2, 4 or 8
    bool isFollowingLoop() const { return followLoop; }
    void setFollowLoop (bool on);
    int getHostLoopBars() const { return hostLoopBars.load(); }
    void dismissSectionWarning() { sectionDismissed = true; updateCoach(); }

    const gs::StepProfile& getReferenceProfile() const { return refProfile; }
    gs::StepProfile getLiveProfile() const;
    bool liveHasData() const;
    const gs::Measurements& getReferenceMeasurements() const { return refM; }
    gs::Measurements getLiveMeasurements() const;

    void setListenToReference (bool b) { listenB.store (b); }
    bool isListeningToReference() const { return listenB.load(); }

    // knobs
    float getTolerance() const { return tolerance; }
    void setTolerance (float dB) { tolerance = juce::jlimit (0.25f, 3.0f, dB); updateCoach(); }
    float getSplit() const { return split; }
    bool isRangeAuto() const { return rangeAuto; }
    void setSplitManual (float hz);
    void setRangeAuto();
    float getDjRefLevel() const { return djRefLevel; }
    void setDjRefLevel (float v) { djRefLevel = v; updateCoach(); }

    // coach
    const gs::Coach& getCoach() const { return coach; }
    void takeSnapshot();
    bool hasSnapshot() const { return coach.hasSnapshot(); }
    const gs::StepProfile& getSnapshotProfile() const { return snapshotProfile; }
    void resetMeasurements();

    // match stage
    MatchState getMatchState() const { return matchState; }
    const gs::MatchSettings& getMatchSettings() const { return matchSettings; }
    void computeMatch();
    void setMatchPreview (bool on);
    void applyMatch();
    void revertMatch();
    void setMatchWeight (bool on);
    float getMatchCeilingDb() const { return ceilingDb.load(); }

    // real-time readings for the animation
    float getLiveSubDbNow() const { return liveSubDb.load(); }
    float getRefSubDbNow() const;
    double getRefPlayPositionSec() const { return refPosSec.load(); }   // -1 when not playing B
    double getHostPositionSteps() const { return hostSteps.load(); }    // sixteenths since song start
    double getHostBpm() const { return hostBpm.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }

    std::function<void()> onReferenceChanged;                  // editor refresh hook

    //==========================================================================
    // Phase 3: reference slots, learn pass, pairs, follow playhead
    int getActiveSlot() const { return activeSlot; }
    void setActiveSlot (int slot);
    juce::String getSlotName (int slot) const;
    bool slotHasReference (int slot) const;

    int getSongStartBar() const { return songStartBar; }        // Ableton bar; 0 = auto
    void setSongStartBar (int bar);
    int getEffectiveStartBar() const { return originBar + trimBars; }
    int getOriginBar() const { return originBar; }
    int getTrimBars() const { return trimBars; }

    enum class LearnState { off, song, pair };
    LearnState getLearnState() const { return learnState; }
    void startLearn();                                           // the whole song
    void startLearnPair (int index);                             // just one pair's bars
    void stopLearn();
    bool hasLearnedTrack() const { return learnedTrack != nullptr; }
    int getLearnedBars() const { return learnedBars; }           // song bars with a waveform
    const std::vector<float>& getLearnedOverviewMin() const { return ovMin; }
    const std::vector<float>& getLearnedOverviewMax() const { return ovMax; }
    juce::String getLearnMessage() const { return learnMessage; }
    double getSongBarNow() const;                                // where the playhead is, in song bars (-1 = before the start)

    const std::vector<Pair>& getPairs() const { return pairs; }
    int getSelectedPair() const { return selectedPair; }
    void selectPair (int index, bool fromFollow = false);
    void addPair (int songStart, int songEnd);                   // uses the current reference window
    void deletePair (int index);
    void setPairRange (int index, int songStart, int songEnd);
    void autoPair();
    void matchAllPairs();
    bool isFollowingPlayhead() const { return followPlayhead; }
    void setFollowPlayhead (bool on);

    // the readout the editor shows: the selected pair's saved results, or live
    bool isShowingSavedPair() const;
    const gs::Coach& getDisplayCoach() const;
    std::vector<gs::FixCard> getDisplayCards() const;
    gs::StepProfile getDisplayYouProfile() const;
    const gs::StepProfile& getDisplayRefProfile() const;
    gs::Measurements getDisplayYouMeasurements() const;
    const gs::Measurements& getDisplayRefMeasurements() const;
    bool isDisplayBreakdown() const;

    // the correction the panel edits: the selected pair's, or the global one
    const gs::MatchSettings& getEditedCorrection() const;
    MatchState getEditedMatchState() const;
    void setEditedCorrection (const gs::MatchSettings& s);      // a hand tweak: switches it on

    //==========================================================================
    // Phase 4
    enum class ViewMode { mix, out, master };                    // what the checklist and cards describe
    ViewMode getViewMode() const { return viewMode; }
    void setViewMode (ViewMode m);
    double getAlignOffsetBars() const { return alignOffsetBars; } // where your track sits under the reference (bars)
    void setAlignOffsetBars (double bars);
    void addPairExplicit (int songStart, int songEnd, int slot, int refStart, int refBars);
    bool editPair (int index, int songStart, int songEnd, int slot, int refStart, int refBars);
    const gs::ReferenceTrack* getLearnedTrack() const { return learnedTrack.get(); }
    double getLearnBpm() const { return learnBpm; }
    bool isWorkingOutOut() const { return simRunning > 0 || ! pairsToSimulate.empty(); }
    bool isShowingOut() const;                                   // the selected pair's OUT readout is on show
    const gs::Coach& getMasterCoach() const { return masterCoach; }
    bool hasMasterReport() const { return masterValid; }
    bool isMasterRunning() const { return masterRunning || masterPending; }
    void refreshMasterCheck() { masterPending = true; }
    // test hook: run queued background work (pair analysis, OUT, master check) right away
    void runQueuedWorkNow();

    // test hook: run the live analysis now (normally a background thread does this every second)
    void runLiveAnalysisNow() { runLiveAnalysis(); }
    void updateCoachNow() { updateCoach(); }

private:
    void timerCallback() override;
    void rebuildWindow();                                        // profile + playback window
    void installPlayback (std::unique_ptr<RefPlayback> p);
    void requestResample (double sampleRate);
    void measureReferenceWindow();
    void recomputeReferenceBands (float newSplit);
    void applyAutoRange();
    void runLiveAnalysis();
    void updateCoach();
    void pushMatchToAudio();
    void pushCorrectionTable();
    void savePhase3 (juce::XmlElement&) const;
    void loadPhase3 (const juce::XmlElement&);
    void analyzePair (int index);
    void finishLearn();
    void refreshPairCoach (Pair& p, int index);
    gs::MatchSettings autoCorrection (const Pair& p) const;
    std::shared_ptr<gs::ReferenceTrack> slotTrack (int slot) const;
    double slotBpm (int slot) const;
    double slotOffset (int slot) const;
    int slotDropBar (int slot) const;
    void updateSections();
    void applyHostLoop();
    void simulatePair (int index);
    void requestSimulation (int index, int delayMs = 0);
    void refreshOutCoach (Pair& p, int index);
    void runMasterCheck();
    void deselectForWindowChange();

    struct LiveAnalyzer : juce::Thread
    {
        explicit LiveAnalyzer (GroundShakeProcessor& p) : juce::Thread ("Ground Shake analyzer"), owner (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wait (1000);
                if (! threadShouldExit()) owner.runLiveAnalysis();
            }
        }
        GroundShakeProcessor& owner;
    };

    // audio-thread state
    gs::BandSplitter splitter;
    gs::LiveStepMeter meter;
    gs::MatchStage matchStage;
    double sr = 44100.0;
    float currentSplit = gs::kDefaultSplit;
    double freeRunSteps = 0.0;
    double mixMeanSquare = 1.0e-6;                               // slow average, for level matching
    int refReadPos = 0;
    bool wasListening = false;
    float abFade = 0.0f;                                         // 0 = your mix, 1 = reference
    long lastPublishedSlot = -1;
    float subPow = 0.0f;

    // ring of what you played (for the slower measurements)
    std::vector<float> ringL, ringR;
    int ringSize = 0;
    struct RingPos { int write = 0; juce::int64 count = 0; double stepsAtWrite = 0; double bpm = 120; };
    RingPos ringPos;                                             // guarded by ringLock
    juce::SpinLock ringLock;
    juce::CriticalSection ringAllocLock;                         // prepareToPlay vs the analyzer thread
    int ringWriteLocal = 0;
    juce::int64 ringCountLocal = 0;
    bool refMeasurePending = false, refMeasureRunning = false;

    std::atomic<RefPlayback*> livePlayback { nullptr };
    std::vector<std::pair<std::unique_ptr<RefPlayback>, juce::uint32>> retired;
    std::unique_ptr<RefPlayback> ownedPlayback;

    // published to the UI
    mutable juce::SpinLock profileLock;
    gs::StepProfile publishedLive;
    bool publishedHasData = false;
    int publishedSlots = 0;
    std::atomic<float> liveSubDb { -60.0f };
    std::atomic<double> refPosSec { -1.0 };
    std::atomic<double> hostSteps { 0.0 }, hostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false }, listenB { false };
    std::atomic<bool> resetRequested { false };
    std::atomic<float> splitParam { gs::kDefaultSplit };
    std::atomic<int> loopStepsParam { gs::kLoopSteps }, hostLoopBars { 0 };
    std::atomic<float> ceilingDb { -1.0f };

    // corrections for the audio thread (swapped by pointer, old tables freed later)
    std::atomic<CorrectionTable*> liveTable { nullptr };
    std::unique_ptr<CorrectionTable> ownedTable;
    std::vector<std::pair<std::unique_ptr<CorrectionTable>, juce::uint32>> retiredTables;

    // learn pass
    gs::SongRecorder recorder;
    std::atomic<bool> learning { false };
    std::atomic<double> learnOriginPpq { 32.0 };

    // live measurements (background thread -> UI)
    mutable juce::CriticalSection liveMLock;
    gs::Measurements liveM;

    // message-thread state
    std::shared_ptr<gs::ReferenceTrack> reference;
    std::shared_ptr<const juce::AudioBuffer<float>> resampled;
    double resampledRate = 0.0;
    gs::StepProfile refProfile, snapshotProfile;
    gs::Measurements refM;
    int refMeasureGen = 0;
    double bpm = 0.0, gridOffsetSec = 0.0;
    int windowStartBar = 0, dropBar = 0, breakdownBar = -1, windowBars = gs::kBars;
    bool followLoop = true, breakdownMode = false;
    float windowVsDropDb = 0.0f;
    bool sectionDismissed = false;
    float tolerance = 1.0f, split = gs::kDefaultSplit, djRefLevel = -9.0f;
    bool rangeAuto = true;
    int bandGen = 0;
    std::atomic<bool> loading { false };
    juce::String loadError;
    juce::ThreadPool pool { 1 };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    gs::Coach coach;
    MatchState matchState = MatchState::off;
    gs::MatchSettings matchSettings;
    LiveAnalyzer analyzer { *this };
    int timerTicks = 0;

    // Phase 3, message thread
    struct ParkedSlot
    {
        std::shared_ptr<gs::ReferenceTrack> track;
        std::shared_ptr<const juce::AudioBuffer<float>> resampled;
        double resampledRate = 0, bpm = 0, offset = 0;
        int windowStartBar = 0, dropBar = 0, breakdownBar = -1;
        gs::Measurements refM;
    };
    std::array<ParkedSlot, 2> parked;
    int activeSlot = 0;
    int songStartBar = 9, originBar = 9, trimBars = 0, learnedBars = 0;
    double learnBpm = 0;
    LearnState learnState = LearnState::off;
    int learnPairIndex = -1;
    juce::String learnMessage;
    std::shared_ptr<gs::ReferenceTrack> learnedTrack;
    std::vector<float> ovMin, ovMax;                             // learned waveform, 32 points a bar
    std::vector<Pair> pairs;
    int selectedPair = -1, pairAnalysisRunning = 0;
    bool followPlayhead = true;
    std::vector<int> pairsToAnalyze;
    int lastLivePair = -1;
    std::unique_ptr<juce::PropertiesFile> globalSettings;
    bool switchingForPair = false, buildRunning = false;
    double otherPendingBpm = 0, otherPendingOffset = 0;
    int otherPendingStart = -1;

    // Phase 4, message thread
    ViewMode viewMode = ViewMode::out;
    double alignOffsetBars = 0.0;
    std::vector<int> pairsToSimulate;
    juce::uint32 simDue = 0;
    int simRunning = 0;
    bool simSummaryPending = false;
    gs::Coach masterCoach;
    bool masterValid = false, masterPending = false, masterRunning = false;
    const void* masterRefSeen = nullptr;
    const void* masterLtSeen = nullptr;
    // audio thread: the mix before the correction, for MIX readings while playing
    std::atomic<bool> meterBeforeCorrection { false };
    std::vector<float> preL, preR;

    // restored state waiting for its file to load
    int pendingStartBar = -1;
    double pendingBpm = 0.0, pendingOffset = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroundShakeProcessor)
};
