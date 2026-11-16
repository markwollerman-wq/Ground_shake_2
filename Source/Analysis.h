#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <vector>
#include <atomic>
#include <memory>

// =============================================================================
//  Ground Shake - analysis engine (no UI, no plugin code)
//
//  Low-end zones (the split between FEEL and PUNCH is the Range setting):
//      FEEL   20 Hz - split        the sub you feel in your feet / chest
//      PUNCH  split - 2 x split    kick thump and bass body
//      MUD    150-400 Hz           low-mid build-up (bass harmonics, pads, reverb)
//
//  "Shake" = the sub's share of the whole mix, in dB:
//      10*log10( (feel + 0.5*punch) / full )
//  Using a share (not an absolute level) is what makes the comparison
//  level-matched: a louder master doesn't automatically "shake more".
// =============================================================================
namespace gs
{
constexpr int kStepsPerBar = 16;
constexpr int kBars        = 8;
constexpr int kLoopSteps   = kStepsPerBar * kBars;   // 128 sixteenths in an 8-bar loop
constexpr float kPunchWeight = 0.5f;
constexpr float kDefaultSplit = 60.0f;
constexpr float kMudLo = 150.0f, kMudHi = 400.0f;

//------------------------------------------------------------------------------
// Simple biquad (RBJ cookbook), per channel state handled by caller
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void setLowPass  (double sr, double f, double q = 0.70710678);
    void setHighPass (double sr, double f, double q = 0.70710678);
    void setPeak     (double sr, double f, double q, double gainDb);
    void setBandPass (double sr, double f, double q);      // 0 dB at the centre
    void setRaw (double nb0, double nb1, double nb2, double na1, double na2) { b0 = nb0; b1 = nb1; b2 = nb2; a1 = na1; a2 = na2; }
    void reset() { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};

// Splits a mono signal into the FEEL, PUNCH and MUD zones (4th-order slopes)
struct BandSplitter
{
    void prepare (double sampleRate, float split = kDefaultSplit, float feelLo = 20.0f);
    void reset();
    inline void process (float x, float& feel, float& punch, float& mud) noexcept
    {
        feel  = fLp2.process (fLp1.process (fHp2.process (fHp1.process (x))));
        punch = pLp2.process (pLp1.process (pHp2.process (pHp1.process (x))));
        mud   = mLp2.process (mLp1.process (mHp2.process (mHp1.process (x))));
    }

private:
    Biquad fHp1, fHp2, fLp1, fLp2, pHp1, pHp2, pLp1, pLp2, mHp1, mHp2, mLp1, mLp2;
};

//------------------------------------------------------------------------------
// One 16-step picture of a loop: energy per sixteenth, folded over all bars
struct StepProfile
{
    std::array<float, kStepsPerBar> feel {};    // mean power per step
    std::array<float, kStepsPerBar> punch {};
    std::array<float, kStepsPerBar> mud {};
    std::array<float, kStepsPerBar> full {};
    float feelTotal = 0, punchTotal = 0, mudTotal = 0, fullTotal = 0;
    bool valid = false;

    // the sub's share of the mix in dB (see top of file)
    float shakeDb() const;
    float feelDb() const;                       // feel zone alone, share of the mix
    float punchDb() const;                      // punch zone alone, share of the mix
    float mudDb() const;                        // 150-400 Hz, share of the mix
    // per-step sub level in dB relative to the loop's FULL-band mean power
    float stepDb (int step) const;
    float stepMudDb (int step) const;
};

float powerToDb (float p) noexcept;

//------------------------------------------------------------------------------
// What kind of kick it is (measured on the kick hits, averaged over the beats)
struct KickCharacter
{
    bool valid = false;
    float riseMs = 0;               // attack: 10% -> 90% of the low thump
    float clickDb = -40;            // 2-5 kHz click at the hit, against the low thump
    float decayMs = 0;              // how long the low end rings: time to fall 12 dB
    float pitchDropSt = 0;          // pitch sweep from the hit to the tail, in semitones
    float startHz = 0;              // pitch at the hit

    int attackClass() const { return riseMs <= 6.0f ? 0 : (riseMs <= 14.0f ? 1 : 2); }       // sharp / medium / soft
    int lengthClass() const { return decayMs <= 110.0f ? 0 : (decayMs <= 220.0f ? 1 : 2); }  // short / medium / long tail
    int pitchClass() const  { return pitchDropSt <= 4.0f ? 0 : (pitchDropSt <= 10.0f ? 1 : 2); } // flat / some / big drop
    bool clicky() const     { return clickDb >= -14.0f; }
    juce::String label() const;     // "short, sharp, flat pitch"
};

// How the bass moves between the kicks
enum class BassPattern { unknown, none, sustained, rolling16, rolling8, offbeat, reverse, gallop };
juce::String bassPatternName (BassPattern);       // "rolling 16ths"

// The notes the bass plays (strongest first), as MIDI note numbers with their share of the bass
struct BassNotes
{
    std::array<int, 4> midi { -1, -1, -1, -1 };
    std::array<float, 4> weight {};
    int count = 0;
    juce::String describe() const;  // "A1 70%, C2 30%"
};

//------------------------------------------------------------------------------
// Slower, "whole picture" measurements of an 8-bar stretch of audio. The same function
// measures the reference window and your live loop, so the two are always comparable.
struct Measurements
{
    bool valid = false;
    float monoCorr = 1.0f;          // correlation of L/R below 120 Hz (1 = mono)
    float sideDb = -60.0f;          // side energy vs mid energy below 120 Hz
    float bassHz = 0, bassConf = 0; // strongest sustained note in the feel zone
    float kickHz = 0, kickConf = 0; // where the kick settles, just after each beat
    int keyIndex = -1;              // 0 = C ... 11 = B
    bool keyMinor = false;
    float keyConf = 0;
    float subCrestDb = 0;           // sub peaks over sub average (limiter squash lowers it)
    float lufs = -70.0f;            // integrated loudness (BS.1770, gated)
    float peakDb = -100.0f;         // sample peak
    // share of the whole mix per band (dB): how a sound was filtered
    enum Band { bSub, bPunch, bBody, bMid, bHigh, numBands };   // 20-60, 60-120, 120-300, 300-2k, 2k-10k Hz
    std::array<float, numBands> bandDb {};
    // Phase 3: the sound itself
    KickCharacter kick;
    BassPattern bassPattern = BassPattern::unknown;
    std::array<float, 4> bassLevel {}, bassOnset {};   // per 16th within the beat (dB), and how clearly a note starts there
    BassNotes bassNotes;
};

bool scaleContains (int keyIndex, bool minor, int midiNote);     // is the note in the (natural minor / major) key?

// start/length in samples; firstBeat = samples from `start` to the first beat; beatLen in samples
Measurements measureAudio (const juce::AudioBuffer<float>& audio, int start, int length, double sampleRate,
                           double firstBeat, double beatLen, float split);

juce::String noteName (float hz, bool withOctave = true);       // "E1"
juce::String keyName (int index, bool minor);                    // "F minor"
float semitonesBetween (float fromHz, float toHz);               // 12*log2(to/from)

//------------------------------------------------------------------------------
// A loaded reference track and its pre-computed analysis
struct ReferenceTrack
{
    juce::String name;
    juce::File file;
    double sampleRate = 44100.0;
    juce::AudioBuffer<float> audio;               // stereo, at the analysis rate

    // envelopes, one value per hop
    int hop = 256;
    float split = kDefaultSplit;                  // the feel/punch split these were made with
    std::vector<float> feelEnv, punchEnv, mudEnv, fullEnv;
    std::vector<float> clickEnv;                  // 1-4 kHz: the kick's click, used to place the beat

    // waveform overview for the display (min/max pairs)
    std::vector<float> overviewMin, overviewMax;

    // tempo grid
    double detectedBpm = 0.0;
    double firstDownbeatSec = 0.0;                // grid anchor (a bar line)

    double lengthSeconds() const { return audio.getNumSamples() / sampleRate; }
};

// Loads WAV / AIFF / MP3 and runs the analysis. Returns false + error text on failure.
bool loadReference (const juce::File& file, ReferenceTrack& out, juce::String& error, float split = kDefaultSplit);

// (Re)computes the band envelopes, e.g. after the Range split changes
void computeEnvelopes (ReferenceTrack& ref, float split);

// Tempo + downbeat from the envelopes (called by loadReference)
void detectTempo (ReferenceTrack& ref);

// Step profile of an 8-bar window starting at bar `startBar` of the grid
// (bars counted from firstDownbeatSec; offsetSec shifts the grid, the nudge)
StepProfile profileForWindow (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar,
                              int bars = kBars);

// How many whole bars the reference holds with this tempo/grid
int numBarsInReference (const ReferenceTrack& ref, double bpm, double gridOffsetSec);

// Mean full-band power (dB) of `bars` bars starting at startBar
float windowLevelDb (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int startBar, int bars = kBars);

// Mean level (dB) of one of the track's envelopes over a window, e.g. clickEnv for brightness
float windowEnvDb (const ReferenceTrack& ref, const std::vector<float>& env, double bpm, double gridOffsetSec,
                   int startBar, int bars);

// The drop: the earliest window within 0.2 dB of the loudest, fullest one (bar index)
int findDropBar (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int bars = kBars);

// The breakdown: the window with the least sub among the ones that still have music in them
// (within 12 dB of the loudest window). Returns -1 if nothing stands out as a breakdown.
int findBreakdownBar (const ReferenceTrack& ref, double bpm, double gridOffsetSec, int bars = kBars);

//------------------------------------------------------------------------------
// Live accumulator for YOUR mix, fed sample by sample with the host's position.
// Keeps the latest value for each of the 128 sixteenths in the 8-bar loop.
class LiveStepMeter
{
public:
    void reset();
    void setLoopSteps (int steps);         // 16, 32, 64 or 128 (1, 2, 4 or 8 bars); resets when it changes
    int getLoopSteps() const { return loopSteps; }
    // positionInSteps = host ppq * 4 (sixteenths since song start)
    void addSample (double positionInSteps, float feelSq, float punchSq, float mudSq, float fullSq) noexcept;
    StepProfile profile() const;           // folded over the bars that have data
    bool hasData() const;
    int slotsFilled() const;               // how much of the 8-bar loop has been heard

private:
    struct Slot { double feel = 0, punch = 0, mud = 0, full = 0; int count = 0; long serial = 0; };
    std::array<Slot, kLoopSteps> done {};   // last finished value per slot
    Slot current;
    int currentSlot = -1;
    long serial = 0;                         // counts finished slots; old ones drop out after one loop
    int loopSteps = kLoopSteps;
};
//------------------------------------------------------------------------------
// The learn pass: records YOUR song as Ableton plays it, placed by the playhead's position,
// at a reduced rate (~16 kHz is plenty for low-end analysis). Re-playing a part simply
// overwrites it. The audio thread only writes into chunks the message thread allocated
// ahead of the playhead.
class SongRecorder
{
public:
    static constexpr int kMaxBars = 1024, kOverviewPerBar = 32, kChunkSec = 20, kMaxChunks = 150;

    SongRecorder();
    void prepare (double hostSampleRate);        // message thread, not while recording
    void clear();                                // message thread, not while recording
    void ensureChunksAround (double songSec);    // message thread: allocate 60 s ahead
    // audio thread. songBeat = quarter notes since the recording origin
    void process (const float* L, const float* R, int n, double songBeat, double beatsPerSample, double bpm) noexcept;

    double rate() const { return dsr; }
    double bpmUsed() const { return bpm.load(); }
    int lastBar() const { return maxBar.load(); }                    // -1 = nothing yet
    bool barHeard (int bar) const { return bar >= 0 && bar < kMaxBars && heard[(size_t) bar] != 0; }
    void forgetBars (int from, int to);                              // message thread: before re-learning a range
    int firstAudibleBar() const;
    const float* overviewMin() const { return ovMin.data(); }
    const float* overviewMax() const { return ovMax.data(); }

    // copies bars [fromBar, end] into a ReferenceTrack (song-aligned, first bar = fromBar)
    bool buildTrack (ReferenceTrack& out, int fromBar, float split) const;

    std::atomic<bool> tempoChanged { false };

private:
    double hostSr = 44100.0, dsr = 16000.0;
    int D = 3;
    std::vector<std::unique_ptr<float[]>> storeL, storeR;            // owned by the message thread
    std::array<std::atomic<float*>, kMaxChunks> chunkL {}, chunkR {};
    int chunkLen = 0;
    std::vector<float> ovMin, ovMax;
    std::vector<uint8_t> heard;
    std::atomic<int> maxBar { -1 };
    std::atomic<double> bpm { 0.0 };
    std::atomic<long long> lastIdx { -1 };
    Biquad aL1, aL2, aR1, aR2;                                        // anti-alias before decimating
    long long prevIdx = -1;
};

} // namespace gs
