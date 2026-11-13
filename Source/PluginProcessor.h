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
    void loadReferenceAsync (const juce::File& file);
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
    std::atomic<float> ceilingDb { -1.0f };

    // match settings for the audio thread
    std::atomic<float> mFeel { 0.0f }, mPunch { 0.0f }, mWeight { 0.0f }, mFeelHz { 0.0f };
    std::atomic<bool> mMono { false }, mActive { false }, mLevelMatch { false };

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
    int windowStartBar = 0, dropBar = 0;
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

    // restored state waiting for its file to load
    int pendingStartBar = -1;
    double pendingBpm = 0.0, pendingOffset = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroundShakeProcessor)
};
