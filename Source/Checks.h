#pragma once

#include "Analysis.h"

// =============================================================================
//  Ground Shake - the coach: live checklist + fix cards (Phase 2)
//
//  Every check compares your loop with the reference window. The fixes list follows
//  the dependency order from the spec (section 9): right section -> mono -> tuning ->
//  kick/bass -> beat shape -> sub level -> punch -> mud -> squash -> DJ playback.
//  A check only ticks after it has held for one full 8-bar loop pass.
// =============================================================================
namespace gs
{
enum class CheckId
{
    section, mono, tuning, kickBass, beatShape, subLevel, punch, mud, squash, djPlayback,
    numChecks,
    // information cards (never block the match %)
    fineTune, loudnessGap, keyInfo, oneBeat, thinMud
};
constexpr int kNumChecks = (int) CheckId::numChecks;

enum class Status { waiting, pass, holding, near, fail, info };

struct CheckRow
{
    CheckId id = CheckId::section;
    juce::String name;
    Status status = Status::waiting;
    juce::String value;           // short reading for the checklist
};

struct FixCard
{
    CheckId id = CheckId::section;
    Status severity = Status::info;       // fail = red, near = yellow, info = grey, holding = fixed, holding
    juce::String title, headline;         // "BASS RUNS INTO THE KICK", "+3.4 dB on the 4th 16th"
    juce::String comparing, see, cause;
    juce::StringArray how;                // numbered steps
    juce::String worked, tooFar, watch, progress;
    int highlightMask = 0;                // steps 0-15 to light up on the grid
    int beatMask = 0;                     // 16ths within the beat (bits 0-3) to shade in the beat-shape graph
    bool beatGraph = false;
    bool offerFindDrop = false, offerDismiss = false;
    bool fixFirst = false;
};

// Beat shape: the 16 steps folded into one beat (four 16ths), you vs reference
struct BeatShape
{
    bool valid = false;
    std::array<float, 4> you {}, ref {};  // dB, each lined up to its own 2nd-4th 16th average
    std::array<float, 4> diff {};         // you - ref after lining up the level (dB)
    float levelDb = 0;                    // the level difference that was removed
};
BeatShape beatShape (const StepProfile& you, const StepProfile& ref);

struct CoachInputs
{
    bool haveRef = false, haveYou = false;
    StepProfile you, ref;
    Measurements youM, refM;
    float tolerance = 1.0f;
    juce::String refName;
    int windowStartBar = 0, windowBars = kBars, dropBar = 0, numRefBars = 0;
    float windowVsDropDb = 0;             // reference window level minus the drop's (dB)
    bool sectionDismissed = false;
    double hostBpm = 120.0;
    float djRefLevel = -9.0f;             // rekordbox Auto Gain reference level
};

class Coach
{
public:
    void reset();
    void takeSnapshot();
    void clearSnapshot() { snapValid = false; }
    bool hasSnapshot() const { return snapValid; }
    void update (const CoachInputs& in, double nowSec);

    const std::vector<CheckRow>& checks() const { return rows; }
    const std::vector<FixCard>& cards() const { return fixCards; }
    float matchPercent() const { return percent; }      // < 0 = not enough data yet
    juce::String statusLine() const { return status; }

private:
    struct Track
    {
        double passSince = -1.0;
        bool ticked = false;
        bool hasStart = false;
        float start = 0, now = 0;
    };
    std::array<Track, kNumChecks> tracks {};
    std::array<FixCard, kNumChecks> lastCard {};
    std::array<bool, kNumChecks> hasLastCard {};
    std::array<float, kNumChecks> snapValues {};
    std::array<bool, kNumChecks> snapHas {};
    bool snapValid = false;

    std::vector<CheckRow> rows;
    std::vector<FixCard> fixCards;
    float percent = -1.0f;
    juce::String status;
};

juce::String checkName (CheckId);
} // namespace gs
