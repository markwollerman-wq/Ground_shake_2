// Ground Shake - Phase 3: reference slots, the learn pass, pairs, follow playhead,
// per-pair corrections and the readout the editor shows.
#include "PluginProcessor.h"

namespace
{
    const juce::uint8 kPairColours = 8;

    // compact text forms for saving analyses with the project
    juce::String floatsToString (const std::vector<float>& v)
    {
        juce::StringArray a;
        for (auto f : v) a.add (juce::String (f, 5));
        return a.joinIntoString (" ");
    }
    std::vector<float> stringToFloats (const juce::String& s)
    {
        std::vector<float> v;
        for (auto& t : juce::StringArray::fromTokens (s, " ", "")) if (t.isNotEmpty()) v.push_back (t.getFloatValue());
        return v;
    }

    juce::String profileToString (const gs::StepProfile& p)
    {
        std::vector<float> v;
        for (int i = 0; i < gs::kStepsPerBar; ++i)
        {
            v.push_back (p.feel[(size_t) i]);  v.push_back (p.punch[(size_t) i]);
            v.push_back (p.mud[(size_t) i]);   v.push_back (p.full[(size_t) i]);
        }
        v.insert (v.end(), { p.feelTotal, p.punchTotal, p.mudTotal, p.fullTotal, p.valid ? 1.0f : 0.0f });
        return floatsToString (v);
    }
    gs::StepProfile stringToProfile (const juce::String& s)
    {
        gs::StepProfile p;
        const auto v = stringToFloats (s);
        if (v.size() < (size_t) gs::kStepsPerBar * 4 + 5) return p;
        size_t k = 0;
        for (int i = 0; i < gs::kStepsPerBar; ++i)
        {
            p.feel[(size_t) i] = v[k++];  p.punch[(size_t) i] = v[k++];
            p.mud[(size_t) i] = v[k++];   p.full[(size_t) i] = v[k++];
        }
        p.feelTotal = v[k++];  p.punchTotal = v[k++];  p.mudTotal = v[k++];  p.fullTotal = v[k++];
        p.valid = v[k] > 0.5f;
        return p;
    }

    juce::String measurementsToString (const gs::Measurements& m)
    {
        std::vector<float> v { m.valid ? 1.0f : 0.0f, m.monoCorr, m.sideDb, m.bassHz, m.bassConf, m.kickHz, m.kickConf,
                               (float) m.keyIndex, m.keyMinor ? 1.0f : 0.0f, m.keyConf, m.subCrestDb, m.lufs, m.peakDb };
        for (auto b : m.bandDb) v.push_back (b);
        v.insert (v.end(), { m.kick.valid ? 1.0f : 0.0f, m.kick.riseMs, m.kick.clickDb, m.kick.decayMs, m.kick.pitchDropSt,
                             m.kick.startHz, (float) (int) m.bassPattern });
        for (auto b : m.bassLevel) v.push_back (b);
        for (auto b : m.bassOnset) v.push_back (b);
        v.push_back ((float) m.bassNotes.count);
        for (auto n : m.bassNotes.midi) v.push_back ((float) n);
        for (auto w : m.bassNotes.weight) v.push_back (w);
        return floatsToString (v);
    }
    gs::Measurements stringToMeasurements (const juce::String& s)
    {
        gs::Measurements m;
        const auto v = stringToFloats (s);
        if (v.size() < 13 + 5 + 7 + 8 + 9) return m;
        size_t k = 0;
        m.valid = v[k++] > 0.5f;  m.monoCorr = v[k++];  m.sideDb = v[k++];  m.bassHz = v[k++];  m.bassConf = v[k++];
        m.kickHz = v[k++];  m.kickConf = v[k++];  m.keyIndex = (int) v[k++];  m.keyMinor = v[k++] > 0.5f;
        m.keyConf = v[k++];  m.subCrestDb = v[k++];  m.lufs = v[k++];  m.peakDb = v[k++];
        for (auto& b : m.bandDb) b = v[k++];
        m.kick.valid = v[k++] > 0.5f;  m.kick.riseMs = v[k++];  m.kick.clickDb = v[k++];  m.kick.decayMs = v[k++];
        m.kick.pitchDropSt = v[k++];  m.kick.startHz = v[k++];  m.bassPattern = (gs::BassPattern) (int) v[k++];
        for (auto& b : m.bassLevel) b = v[k++];
        for (auto& b : m.bassOnset) b = v[k++];
        m.bassNotes.count = (int) v[k++];
        for (auto& n : m.bassNotes.midi) n = (int) v[k++];
        for (auto& w : m.bassNotes.weight) w = v[k++];
        return m;
    }

    juce::String correctionToString (const gs::MatchSettings& c)
    {
        std::vector<float> v { c.feelDb, c.punchDb, c.mono ? 1.0f : 0.0f, c.weight, c.feelHz };
        for (auto d : c.duckDb) v.push_back (d);
        v.insert (v.end(), { c.attackDb, c.tailDb, c.tailStartMs, c.mudDb, c.lowCutHz });
        return floatsToString (v);
    }
    gs::MatchSettings stringToCorrection (const juce::String& s)
    {
        gs::MatchSettings c;
        const auto v = stringToFloats (s);
        if (v.size() < 14) return c;
        size_t k = 0;
        c.feelDb = v[k++];  c.punchDb = v[k++];  c.mono = v[k++] > 0.5f;  c.weight = v[k++];  c.feelHz = v[k++];
        for (auto& d : c.duckDb) d = v[k++];
        c.attackDb = v[k++];  c.tailDb = v[k++];  c.tailStartMs = v[k++];  c.mudDb = v[k++];  c.lowCutHz = v[k++];
        return c;
    }

    float zoneGainDb (float zoneYou, float fullYou, float zoneRef, float fullRef)
    {
        if (fullYou <= 0 || fullRef <= 0 || zoneYou <= 0) return 0.0f;
        const double f = juce::jlimit (1.0e-6, 0.95, (double) zoneYou / fullYou);
        const double t = juce::jlimit (1.0e-6, 0.95, (double) zoneRef / fullRef);
        return (float) (10.0 * std::log10 (t * (1.0 - f) / (f * (1.0 - t))));
    }

    juce::String sdb (float v) { return (v >= 0 ? "+" : "-") + juce::String (std::abs (v), 1) + " dB"; }
}

//==============================================================================
//  Reference slots A / B
//==============================================================================
std::shared_ptr<gs::ReferenceTrack> GroundShakeProcessor::slotTrack (int slot) const
{
    return slot == activeSlot ? reference : parked[(size_t) juce::jlimit (0, 1, slot)].track;
}
double GroundShakeProcessor::slotBpm (int slot) const    { return slot == activeSlot ? bpm : parked[(size_t) juce::jlimit (0, 1, slot)].bpm; }
double GroundShakeProcessor::slotOffset (int slot) const { return slot == activeSlot ? gridOffsetSec : parked[(size_t) juce::jlimit (0, 1, slot)].offset; }
int GroundShakeProcessor::slotDropBar (int slot) const   { return slot == activeSlot ? dropBar : parked[(size_t) juce::jlimit (0, 1, slot)].dropBar; }
bool GroundShakeProcessor::slotHasReference (int slot) const { return slotTrack (slot) != nullptr; }

juce::String GroundShakeProcessor::getSlotName (int slot) const
{
    auto t = slotTrack (slot);
    return t ? t->name : juce::String();
}

void GroundShakeProcessor::setActiveSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot != activeSlot)
    {
        auto& cur = parked[(size_t) activeSlot];
        cur.track = reference;  cur.resampled = resampled;  cur.resampledRate = resampledRate;
        cur.bpm = bpm;  cur.offset = gridOffsetSec;  cur.windowStartBar = windowStartBar;
        cur.dropBar = dropBar;  cur.breakdownBar = breakdownBar;  cur.refM = refM;

        auto& nx = parked[(size_t) slot];
        reference = nx.track;  resampled = nx.resampled;  resampledRate = nx.resampledRate;
        bpm = nx.bpm;  gridOffsetSec = nx.offset;  windowStartBar = nx.windowStartBar;
        dropBar = nx.dropBar;  breakdownBar = nx.breakdownBar;  refM = nx.refM;
        nx = {};
        activeSlot = slot;
        sectionDismissed = false;
        coach.reset();
        if (reference != nullptr)
        {
            if (resampled == nullptr || std::abs (resampledRate - sr) > 0.5) requestResample (sr);
            rebuildWindow();
        }
        else
        {
            refProfile = {};
            refM = {};
            installPlayback (std::make_unique<RefPlayback>());
            updateCoach();
        }
    }
    // switching the tab while a pair is selected moves that pair to this reference
    if (! switchingForPair && selectedPair >= 0 && selectedPair < (int) pairs.size() && reference != nullptr)
    {
        auto& p = pairs[(size_t) selectedPair];
        if (p.slot != slot)
        {
            p.slot = slot;
            p.refStart = windowStartBar;
            p.refBars = windowBars;
            pairsToAnalyze.push_back (selectedPair);
        }
    }
    if (onReferenceChanged) onReferenceChanged();
}

//==============================================================================
//  Song start and the learn pass
//==============================================================================
void GroundShakeProcessor::setSongStartBar (int bar)
{
    songStartBar = juce::jlimit (0, 999, bar);
    if (globalSettings != nullptr)
    {
        globalSettings->setValue ("songStartBar", songStartBar);
        globalSettings->saveIfNeeded();
    }
    if (learnState == LearnState::off && learnedTrack == nullptr && pairs.empty())
        originBar = songStartBar > 0 ? songStartBar : 1;
    if (onReferenceChanged) onReferenceChanged();
}

double GroundShakeProcessor::getSongBarNow() const
{
    return hostSteps.load() / 16.0 - (originBar - 1) - trimBars;
}

void GroundShakeProcessor::startLearn()
{
    if (learnState != LearnState::off || buildRunning) return;
    recorder.prepare (sr);
    recorder.clear();
    originBar = songStartBar > 0 ? songStartBar : 1;
    trimBars = 0;
    learnOriginPpq.store ((originBar - 1) * 4.0);
    learnBpm = 0;
    ovMin.clear();
    ovMax.clear();
    learnedBars = 0;
    recorder.ensureChunksAround (juce::jmax (0.0, (hostSteps.load() / 4.0 - learnOriginPpq.load()) * 60.0 / juce::jmax (40.0, hostBpm.load())));
    learnState = LearnState::song;
    learnPairIndex = -1;
    learnMessage = "Learning: play your song from bar " + juce::String (originBar) + " to the end, then press LEARN again.";
    learning.store (true);
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::startLearnPair (int index)
{
    if (learnState != LearnState::off || buildRunning || index < 0 || index >= (int) pairs.size()) return;
    recorder.prepare (sr);                      // keeps what's already recorded
    learnOriginPpq.store ((originBar - 1) * 4.0);
    const auto& p = pairs[(size_t) index];
    recorder.forgetBars (p.songStart + trimBars, p.songEnd + trimBars);
    recorder.ensureChunksAround (juce::jmax (0.0, (p.songStart + trimBars) * 240.0 / juce::jmax (40.0, hostBpm.load()) - 5.0));
    learnState = LearnState::pair;
    learnPairIndex = index;
    learning.store (true);
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::stopLearn()
{
    if (learnState == LearnState::off) return;
    learning.store (false);
    const bool wholeSong = learnState == LearnState::song;
    learnState = LearnState::off;
    if (wholeSong && songStartBar == 0) trimBars = recorder.firstAudibleBar();   // auto start: the first bar with sound
    finishLearn();
}

void GroundShakeProcessor::finishLearn()
{
    if (recorder.lastBar() < 0)
    {
        learnMessage = "Nothing was recorded: press LEARN, then play your song in Ableton.";
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    // copy the waveform (bars heard this time; keep older bars restored from the project)
    {
        const int last = recorder.lastBar();
        const size_t per = gs::SongRecorder::kOverviewPerBar;
        if (ovMin.size() < (size_t) (last + 1) * per) { ovMin.resize ((size_t) (last + 1) * per, 0.0f); ovMax.resize ((size_t) (last + 1) * per, 0.0f); }
        for (int b = 0; b <= last; ++b)
            if (recorder.barHeard (b))
                for (size_t q = 0; q < per; ++q)
                {
                    ovMin[(size_t) b * per + q] = recorder.overviewMin()[(size_t) b * per + q];
                    ovMax[(size_t) b * per + q] = recorder.overviewMax()[(size_t) b * per + q];
                }
        learnedBars = juce::jmax (learnedBars, last + 1 - trimBars);
    }
    learnMessage = "Building your learned track...";
    buildRunning = true;
    auto flag = alive;
    const int trim = trimBars;
    const float sp = split;
    pool.addJob ([this, flag, trim, sp]
    {
        auto t = std::make_shared<gs::ReferenceTrack>();
        const bool ok = recorder.buildTrack (*t, trim, sp);
        juce::MessageManager::callAsync ([this, flag, ok, t]
        {
            if (! flag->load()) return;
            buildRunning = false;
            if (! ok)
            {
                learnMessage = "The learn pass was too short: play at least a few bars.";
                if (onReferenceChanged) onReferenceChanged();
                return;
            }
            learnedTrack = t;
            learnBpm = t->detectedBpm;
            for (int i = 0; i < (int) pairs.size(); ++i)
                if (std::find (pairsToAnalyze.begin(), pairsToAnalyze.end(), i) == pairsToAnalyze.end()) pairsToAnalyze.push_back (i);
            learnMessage = pairs.empty()
                ? "Learned " + juce::String (learnedBars) + " bars. Press AUTO-PAIR, or drag across your track and press + PAIR."
                : "Learned " + juce::String (learnedBars) + " bars. Measuring your pairs...";
            if (onReferenceChanged) onReferenceChanged();
        });
    });
    if (onReferenceChanged) onReferenceChanged();
}

//==============================================================================
//  Pairs
//==============================================================================
void GroundShakeProcessor::selectPair (int index, bool fromFollow)
{
    if (! fromFollow) followPlayhead = false;
    if (index < 0 || index >= (int) pairs.size())
    {
        selectedPair = -1;
        updateCoach();
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    selectedPair = index;
    const auto p = pairs[(size_t) index];
    switchingForPair = true;
    if (p.slot != activeSlot && slotHasReference (p.slot)) setActiveSlot (p.slot);
    followLoop = false;
    const int rb = juce::jlimit (1, gs::kBars, p.refBars);
    if (rb != windowBars)
    {
        windowBars = rb;
        loopStepsParam.store (rb * gs::kStepsPerBar);
        updateSections();
    }
    windowStartBar = p.refStart;
    coach.reset();
    rebuildWindow();
    switchingForPair = false;
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::addPair (int songStart, int songEnd)
{
    if (songEnd < songStart) std::swap (songStart, songEnd);
    songStart = juce::jmax (0, songStart);
    songEnd = juce::jmax (songStart + 1, songEnd);
    if (reference == nullptr) { learnMessage = "Load a reference first."; if (onReferenceChanged) onReferenceChanged(); return; }
    if (learnedBars <= 0)     { learnMessage = "Press LEARN and play your song first."; if (onReferenceChanged) onReferenceChanged(); return; }
    for (auto& q : pairs)
        if (q.manual && songStart < q.songEnd && songEnd > q.songStart)
        {
            learnMessage = "That overlaps another pair: drag a free stretch, or delete that pair first.";
            if (onReferenceChanged) onReferenceChanged();
            return;
        }
    // auto pairs give way to a hand-made one
    pairs.erase (std::remove_if (pairs.begin(), pairs.end(), [&] (const Pair& q)
                                 { return ! q.manual && songStart < q.songEnd && songEnd > q.songStart; }),
                 pairs.end());
    Pair p;
    p.slot = activeSlot;
    p.refStart = windowStartBar;
    p.refBars = windowBars;
    p.songStart = songStart;
    p.songEnd = songEnd;
    p.manual = true;
    int used = 0;
    for (auto& q : pairs) used |= 1 << q.colour;
    for (int c = 0; c < kPairColours; ++c) if (! ((used >> c) & 1)) { p.colour = c; break; }
    pairs.push_back (p);
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.songStart < b.songStart; });
    int idx = 0;
    for (int i = 0; i < (int) pairs.size(); ++i) if (pairs[(size_t) i].songStart == songStart) idx = i;
    pairsToAnalyze.clear();
    for (int i = 0; i < (int) pairs.size(); ++i) if (! pairs[(size_t) i].analyzed || i == idx) pairsToAnalyze.push_back (i);
    learnMessage = "Pair " + juce::String (idx + 1) + ": your bars " + juce::String (getEffectiveStartBar() + songStart) + "-"
                   + juce::String (getEffectiveStartBar() + songEnd - 1) + " matched to " + getSlotName (activeSlot) + " bars "
                   + juce::String (windowStartBar + 1) + "-" + juce::String (windowStartBar + windowBars) + ".";
    selectPair (idx);
    pushCorrectionTable();
}

void GroundShakeProcessor::deletePair (int index)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    pairs.erase (pairs.begin() + index);
    pairsToAnalyze.clear();
    if (selectedPair == index) selectedPair = -1;
    else if (selectedPair > index) --selectedPair;
    pushCorrectionTable();
    updateCoach();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setPairRange (int index, int songStart, int songEnd)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    if (songEnd < songStart) std::swap (songStart, songEnd);
    songStart = juce::jmax (0, songStart);
    songEnd = juce::jmax (songStart + 1, songEnd);
    for (int i = 0; i < (int) pairs.size(); ++i)
        if (i != index && songStart < pairs[(size_t) i].songEnd && songEnd > pairs[(size_t) i].songStart) return;   // no overlaps
    auto& p = pairs[(size_t) index];
    if (p.songStart == songStart && p.songEnd == songEnd) return;
    p.songStart = songStart;
    p.songEnd = songEnd;
    p.manual = true;
    if (std::find (pairsToAnalyze.begin(), pairsToAnalyze.end(), index) == pairsToAnalyze.end()) pairsToAnalyze.push_back (index);
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::setFollowPlayhead (bool on)
{
    followPlayhead = on;
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::autoPair()
{
    if (learnedTrack == nullptr) { learnMessage = "Press LEARN and play your song first."; if (onReferenceChanged) onReferenceChanged(); return; }
    if (reference == nullptr)    { learnMessage = "Load a reference first."; if (onReferenceChanged) onReferenceChanged(); return; }
    const auto& t = *learnedTrack;
    const double lb = learnBpm;
    const int bars = juce::jmin (learnedBars, gs::numBarsInReference (t, lb, 0.0));
    constexpr int block = 4;
    // brightness (the 1-4 kHz band against the whole mix) tells a low-passed breakdown from a drop:
    // a low-pass keeps the sub, so sub and level alone would call it a drop
    struct Blk { float level, shake, bright; int cls = 0; };    // cls: 0 other, 1 drop, 2 breakdown
    std::vector<Blk> blks;
    float maxLevel = -200.0f, maxBright = -200.0f;
    for (int b = 0; b + block <= bars; b += block)
    {
        Blk k;
        k.level = gs::windowLevelDb (t, lb, 0.0, b, block);
        k.shake = gs::profileForWindow (t, lb, 0.0, b, block).shakeDb();
        k.bright = gs::windowEnvDb (t, t.clickEnv, lb, 0.0, b, block) - k.level;
        maxLevel = std::max (maxLevel, k.level);
        blks.push_back (k);
    }
    if (blks.empty()) return;
    float dropShake = -200.0f;
    for (auto& k : blks) if (k.level >= maxLevel - 6.0f) { dropShake = std::max (dropShake, k.shake); maxBright = std::max (maxBright, k.bright); }
    for (auto& k : blks)
    {
        const bool muffled = k.bright <= maxBright - 10.0f;
        if (k.level >= maxLevel - 4.0f && k.shake >= dropShake - 4.0f && ! muffled) k.cls = 1;
        else if (k.level >= maxLevel - 18.0f && (k.shake <= dropShake - 10.0f || muffled)) k.cls = 2;
    }
    // the reference's own drop and breakdown, 4 bars long
    const int refDrop = gs::findDropBar (*reference, bpm, gridOffsetSec, block);
    const int refBreak = gs::findBreakdownBar (*reference, bpm, gridOffsetSec, block);

    pairs.erase (std::remove_if (pairs.begin(), pairs.end(), [] (const Pair& q) { return ! q.manual; }), pairs.end());
    int made = 0;
    for (size_t i = 0; i < blks.size();)
    {
        size_t j = i;
        while (j < blks.size() && blks[j].cls == blks[i].cls) ++j;
        const int cls = blks[i].cls;
        const int s0 = (int) i * block, s1 = (int) j * block;
        const bool longEnough = (j - i) >= 2;
        bool overlaps = false;
        for (auto& q : pairs) overlaps = overlaps || (s0 < q.songEnd && s1 > q.songStart);
        if (cls != 0 && longEnough && ! overlaps && (cls == 1 || refBreak >= 0))
        {
            Pair p;
            p.slot = activeSlot;
            p.refStart = cls == 1 ? refDrop : refBreak;
            p.refBars = block;
            p.songStart = s0;
            p.songEnd = s1;
            p.manual = false;
            int used = 0;
            for (auto& q : pairs) used |= 1 << q.colour;
            for (int c = 0; c < kPairColours; ++c) if (! ((used >> c) & 1)) { p.colour = c; break; }
            pairs.push_back (p);
            ++made;
        }
        i = j;
    }
    std::sort (pairs.begin(), pairs.end(), [] (const Pair& a, const Pair& b) { return a.songStart < b.songStart; });
    pairsToAnalyze.clear();
    for (int i = 0; i < (int) pairs.size(); ++i) pairsToAnalyze.push_back (i);
    selectedPair = -1;
    learnMessage = made > 0 ? "Auto-paired " + juce::String (made) + (made == 1 ? " section" : " sections")
                              + " by type (drops to the reference's drop, breakdowns to its breakdown). Drag to adjust."
                            : "No clear drops or breakdowns found: mark pairs by hand.";
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

void GroundShakeProcessor::matchAllPairs()
{
    for (int i = 0; i < (int) pairs.size(); ++i)
    {
        auto& p = pairs[(size_t) i];
        if (! p.analyzed) continue;
        p.corr = autoCorrection (p);
        p.state = MatchState::applied;
        refreshPairCoach (p, i);
    }
    pushCorrectionTable();
    if (onReferenceChanged) onReferenceChanged();
}

//==============================================================================
void GroundShakeProcessor::analyzePair (int index)
{
    if (index < 0 || index >= (int) pairs.size()) return;
    auto& p = pairs[(size_t) index];
    auto lt = learnedTrack;
    auto rt = slotTrack (p.slot);
    if (lt == nullptr)                          // nothing learned this session: keep saved results
    {
        p.needsLearn = ! p.analyzed;
        if (onReferenceChanged) onReferenceChanged();
        return;
    }
    if (rt == nullptr) return;
    ++pairAnalysisRunning;
    auto flag = alive;
    const double lb = learnBpm, rb = slotBpm (p.slot), ro = slotOffset (p.slot);
    const Pair spec = p;
    const float sp = split;
    pool.addJob ([this, flag, lt, rt, lb, rb, ro, spec, sp, index]
    {
        // the fix is measured from the fullest bars inside A-B
        const int nb = juce::jmax (1, juce::jmin (spec.refBars, spec.songEnd - spec.songStart));
        int best = spec.songStart;
        float bestScore = -1.0e9f;
        for (int b = spec.songStart; b + nb <= spec.songEnd; ++b)
        {
            const float sc = gs::windowLevelDb (*lt, lb, 0.0, b, nb) + 0.25f * gs::profileForWindow (*lt, lb, 0.0, b, nb).shakeDb();
            if (sc > bestScore) { bestScore = sc; best = b; }
        }
        const double lsr = lt->sampleRate, barL = 240.0 / lb;
        auto youP = gs::profileForWindow (*lt, lb, 0.0, best, nb);
        auto youM = gs::measureAudio (lt->audio, (int) (best * barL * lsr), (int) (nb * barL * lsr), lsr, 0.0, barL / 4.0 * lsr, sp);

        const double rsr = rt->sampleRate, barR = 240.0 / rb;
        auto refP = gs::profileForWindow (*rt, rb, ro, spec.refStart, spec.refBars);
        // measure the reference window at the learned track's rate, so both sides go through the same filters
        gs::Measurements refM2;
        {
            const int s0 = juce::jlimit (0, rt->audio.getNumSamples() - 1, (int) ((rt->firstDownbeatSec + ro + spec.refStart * barR) * rsr));
            const int n0 = juce::jmin ((int) (spec.refBars * barR * rsr), rt->audio.getNumSamples() - s0);
            const double ratio = rsr / lsr;
            const int n1 = (int) (n0 / ratio) - 4;
            if (n1 > 0)
            {
                // the same anti-alias filter the learn pass uses, then resample
                juce::AudioBuffer<float> src (2, n0), w (2, n1);
                for (int c = 0; c < 2; ++c)
                {
                    gs::Biquad f1, f2;
                    f1.setLowPass (rsr, lsr * 0.45);  f2.setLowPass (rsr, lsr * 0.45);
                    const float* in = rt->audio.getReadPointer (c) + s0;
                    float* o = src.getWritePointer (c);
                    for (int i = 0; i < n0; ++i) o[i] = f2.process (f1.process (in[i]));
                    juce::LagrangeInterpolator li;
                    li.process (ratio, src.getReadPointer (c), w.getWritePointer (c), n1);
                }
                refM2 = gs::measureAudio (w, 0, n1, lsr, 0.0, barR / 4.0 * lsr, sp);
            }
        }
        const int dropB = gs::findDropBar (*rt, rb, ro, spec.refBars);
        const float dropShake = gs::profileForWindow (*rt, rb, ro, dropB, spec.refBars).shakeDb();
        const int numBars = gs::numBarsInReference (*rt, rb, ro);
        const bool bd = refP.valid && (refP.shakeDb() <= -15.0f || refP.shakeDb() <= dropShake - 10.0f);

        juce::MessageManager::callAsync ([this, flag, spec, index, best, youP, youM, refP, refM2, dropB, numBars, bd, name = rt->name]
        {
            if (! flag->load()) return;
            --pairAnalysisRunning;
            // the pair may have moved in the list or been edited meanwhile
            int i = index;
            auto same = [&] (const Pair& q)
            { return q.songStart == spec.songStart && q.songEnd == spec.songEnd && q.refStart == spec.refStart && q.slot == spec.slot; };
            if (i >= (int) pairs.size() || ! same (pairs[(size_t) i]))
            {
                i = -1;
                for (int k = 0; k < (int) pairs.size(); ++k) if (same (pairs[(size_t) k])) i = k;
                if (i < 0) return;
            }
            auto& p = pairs[(size_t) i];
            p.measureStart = best;
            p.you = youP;  p.youM = youM;  p.ref = refP;  p.refM = refM2;
            p.refDropBar = dropB;  p.refNumBars = numBars;  p.breakdown = bd;  p.refName = name;
            p.analyzed = true;
            p.needsLearn = false;
            refreshPairCoach (p, i);
            if (p.state != MatchState::off) p.corr = autoCorrection (p);   // keep a live correction in step
            pushCorrectionTable();
            if (pairAnalysisRunning == 0 && pairsToAnalyze.empty() && learnMessage.contains ("Measuring"))
            {
                int red = 0, amber = 0;
                for (auto& q : pairs) { red += q.status == 3 ? 1 : 0; amber += q.status == 2 ? 1 : 0; }
                learnMessage = "Measured " + juce::String ((int) pairs.size()) + " pairs: " + juce::String (red) + " red, "
                               + juce::String (amber) + " amber. Click a pair to see its fixes, or press MATCH ALL.";
            }
            if (onReferenceChanged) onReferenceChanged();
        });
    });
}

void GroundShakeProcessor::refreshPairCoach (Pair& p, int index)
{
    gs::CoachInputs in;
    in.haveRef = in.haveYou = p.analyzed;
    in.you = p.you;  in.ref = p.ref;  in.youM = p.youM;  in.refM = p.refM;
    in.tolerance = tolerance;
    in.refName = p.refName;
    in.windowStartBar = p.refStart;
    in.windowBars = p.refBars;
    in.numRefBars = p.refNumBars;
    in.dropBar = p.refDropBar;
    in.breakdownMode = p.breakdown;
    in.pairMode = true;
    in.pairLabel = "pair " + juce::String (index + 1);
    in.hostBpm = learnBpm > 0 ? learnBpm : hostBpm.load();
    in.djRefLevel = djRefLevel;
    p.coach.reset();
    p.coach.update (in, 0.0, false);
    int st = p.analyzed ? 1 : 0;
    for (auto& r : p.coach.checks())
    {
        if (r.status == gs::Status::fail) st = 3;
        else if (r.status == gs::Status::near && st < 2) st = 2;
    }
    p.status = st;
}

// What Ground Shake can fix by itself for this pair, worked out from the two analyses
gs::MatchSettings GroundShakeProcessor::autoCorrection (const Pair& p) const
{
    gs::MatchSettings c;
    if (! p.analyzed) return c;
    const auto& y = p.you;
    const auto& r = p.ref;
    using B = gs::Measurements::Band;
    const auto& ys = p.youM.bandDb;
    const auto& rs = p.refM.bandDb;

    // breakdown muted with a high-pass in the reference, sub still in yours: a low cut, nothing else on the low end
    if (p.breakdown && p.youM.valid && p.refM.valid && rs[B::bSub] <= -30.0f && ys[B::bSub] - rs[B::bSub] >= 10.0f)
    {
        c.lowCutHz = rs[B::bPunch] >= rs[B::bBody] - 10.0f ? 70.0f : (rs[B::bPunch] >= rs[B::bBody] - 15.0f ? 130.0f : 170.0f);
        c.mono = p.youM.monoCorr < 0.9f;
        return c;
    }
    const float df = zoneGainDb (y.feelTotal, y.fullTotal, r.feelTotal, r.fullTotal);
    const float dp = zoneGainDb (y.punchTotal, y.fullTotal, r.punchTotal, r.fullTotal);
    c.feelDb = std::abs (df) > 0.3f ? juce::jlimit (-6.0f, 6.0f, df) : 0.0f;
    c.punchDb = std::abs (dp) > 0.3f ? juce::jlimit (-6.0f, 6.0f, dp) : 0.0f;
    c.feelHz = p.youM.valid && p.youM.bassConf >= 5.0f ? juce::jlimit (25.0f, split, p.youM.bassHz) : 0.0f;
    c.mono = p.youM.valid && p.youM.monoCorr < 0.9f;
    if (! p.breakdown)
    {
        // built-in ducking: copy the reference's dip shape, step by step
        const auto bs = gs::beatShape (y, r);
        if (bs.valid)
            for (size_t k = 0; k < 4; ++k)
                c.duckDb[k] = std::abs (bs.diff[k]) >= 1.5f ? juce::jlimit (-12.0f, 3.0f, -bs.diff[k]) : 0.0f;
        // kick attack and tail
        if (p.youM.kick.valid && p.refM.kick.valid)
        {
            const auto& yk = p.youM.kick;
            const auto& rk = p.refM.kick;
            // only clear differences: a class boundary alone is too touchy
            const bool slower = yk.riseMs > rk.riseMs * 1.8f && yk.riseMs - rk.riseMs >= 5.0f;
            const bool faster = rk.riseMs > yk.riseMs * 1.8f && rk.riseMs - yk.riseMs >= 5.0f;
            if (slower || (rk.clickDb - yk.clickDb >= 6.0f)) c.attackDb = 3.0f;
            else if (faster || (yk.clickDb - rk.clickDb >= 6.0f)) c.attackDb = -3.0f;
            const float ratio = rk.decayMs > 0 ? yk.decayMs / rk.decayMs : 1.0f;
            if (ratio > 1.3f)       { c.tailStartMs = juce::jlimit (40.0f, 300.0f, rk.decayMs); c.tailDb = ratio > 2.0f ? -9.0f : -6.0f; }
            else if (ratio < 0.75f) { c.tailStartMs = juce::jlimit (40.0f, 300.0f, yk.decayMs); c.tailDb = 3.0f; }
        }
    }
    // mud, only where kick and bass clash
    const float dm = y.mudDb() - r.mudDb();
    if (dm >= 2.0f) c.mudDb = -juce::jlimit (2.0f, 6.0f, dm);
    // weight: the reference's bass reads more in 120-300 Hz
    if (p.youM.valid && p.refM.valid && rs[B::bBody] - ys[B::bBody] >= 3.0f) c.weight = 0.5f;
    return c;
}

//==============================================================================
//  The audio thread's table of corrections
//==============================================================================
void GroundShakeProcessor::pushCorrectionTable()
{
    auto t = std::make_unique<CorrectionTable>();
    if (pairs.empty())
    {
        t->pairMode = false;
        t->entries.push_back ({ -1.0e12, 1.0e12, matchSettings,
                                matchState == MatchState::preview || matchState == MatchState::applied,
                                matchState == MatchState::preview });
    }
    else
    {
        t->pairMode = true;                    // unmarked parts are left alone
        const double base = (originBar - 1 + trimBars) * 4.0;
        for (auto& p : pairs)
            if (p.state != MatchState::off)
                t->entries.push_back ({ base + p.songStart * 4.0, base + p.songEnd * 4.0, p.corr,
                                        p.state == MatchState::preview || p.state == MatchState::applied,
                                        p.state == MatchState::preview });
    }
    if (ownedTable != nullptr) retiredTables.emplace_back (std::move (ownedTable), juce::Time::getMillisecondCounter());
    ownedTable = std::move (t);
    liveTable.store (ownedTable.get());
}

//==============================================================================
//  The readout the editor shows
//==============================================================================
bool GroundShakeProcessor::isShowingSavedPair() const
{
    if (selectedPair < 0 || selectedPair >= (int) pairs.size()) return false;
    const auto& p = pairs[(size_t) selectedPair];
    if (! p.analyzed) return false;
    const double bar = getSongBarNow();
    const bool liveHere = hostPlaying.load() && learnState == LearnState::off && bar >= p.songStart && bar < p.songEnd
                          && liveHasData();
    return ! liveHere;
}

const gs::Coach& GroundShakeProcessor::getDisplayCoach() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].coach : coach;
}

gs::StepProfile GroundShakeProcessor::getDisplayYouProfile() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].you : getLiveProfile();
}
const gs::StepProfile& GroundShakeProcessor::getDisplayRefProfile() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].ref : refProfile;
}
gs::Measurements GroundShakeProcessor::getDisplayYouMeasurements() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].youM : getLiveMeasurements();
}
const gs::Measurements& GroundShakeProcessor::getDisplayRefMeasurements() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].refM : refM;
}
bool GroundShakeProcessor::isDisplayBreakdown() const
{
    return isShowingSavedPair() ? pairs[(size_t) selectedPair].breakdown : breakdownMode;
}

const gs::MatchSettings& GroundShakeProcessor::getEditedCorrection() const
{
    return selectedPair >= 0 && selectedPair < (int) pairs.size() ? pairs[(size_t) selectedPair].corr : matchSettings;
}
MatchState GroundShakeProcessor::getEditedMatchState() const
{
    return selectedPair >= 0 && selectedPair < (int) pairs.size() ? pairs[(size_t) selectedPair].state : matchState;
}
void GroundShakeProcessor::setEditedCorrection (const gs::MatchSettings& s)
{
    if (selectedPair >= 0 && selectedPair < (int) pairs.size())
    {
        auto& p = pairs[(size_t) selectedPair];
        p.corr = s;
        if (p.state == MatchState::off || p.state == MatchState::ready) p.state = MatchState::applied;
    }
    else
    {
        matchSettings = s;
        if (matchState == MatchState::off || matchState == MatchState::ready) matchState = MatchState::applied;
    }
    pushCorrectionTable();
}

std::vector<gs::FixCard> GroundShakeProcessor::getDisplayCards() const
{
    auto cards = getDisplayCoach().cards();
    const auto& c = getEditedCorrection();
    const auto st = getEditedMatchState();
    if (st != MatchState::applied && st != MatchState::preview) return cards;
    const juce::String now = st == MatchState::preview ? "Previewing: " : "Ground Shake is doing this now: ";
    juce::StringArray duck;
    for (int k = 0; k < 4; ++k)
        if (std::abs (c.duckDb[(size_t) k]) > 0.05f) duck.add ("16th " + juce::String (k + 1) + " " + sdb (c.duckDb[(size_t) k]));
    for (auto& card : cards)
    {
        juce::StringArray f;
        switch (card.id)
        {
            case gs::CheckId::subLevel:
            case gs::CheckId::punch:
                if (c.lowCutHz > 0) f.add ("low cut at " + juce::String (juce::roundToInt (c.lowCutHz)) + " Hz, 24 dB/oct");
                if (std::abs (c.feelDb) > 0.05f)
                    f.add (sdb (c.feelDb) + " around " + juce::String (juce::roundToInt (c.feelHz > 0 ? c.feelHz : std::sqrt (20.0f * split))) + " Hz");
                if (std::abs (c.punchDb) > 0.05f) f.add (sdb (c.punchDb) + " around " + juce::String (juce::roundToInt (split * 1.414f)) + " Hz");
                break;
            case gs::CheckId::beatShape:
            case gs::CheckId::kickBass:
                if (! duck.isEmpty()) f.add ("built-in ducking: " + duck.joinIntoString (", "));
                break;
            case gs::CheckId::kickType:
                if (std::abs (c.attackDb) > 0.05f) f.add ("kick attack " + sdb (c.attackDb));
                if (std::abs (c.tailDb) > 0.05f) f.add ("kick tail " + sdb (c.tailDb) + " after " + juce::String (juce::roundToInt (c.tailStartMs)) + " ms");
                break;
            case gs::CheckId::mud:
                if (std::abs (c.mudDb) > 0.05f) f.add ("mud " + sdb (c.mudDb) + " around 250 Hz, on the kick 16th only");
                break;
            case gs::CheckId::mono:
                if (c.mono) f.add ("sub mono below 120 Hz");
                break;
            case gs::CheckId::thinMud:
                if (c.weight > 0) f.add ("weight: light sub harmonics so the bass reads on small speakers");
                break;
            case gs::CheckId::section: case gs::CheckId::bassPattern: case gs::CheckId::bassNotes: case gs::CheckId::tuning:
            case gs::CheckId::squash: case gs::CheckId::djPlayback: case gs::CheckId::numChecks: case gs::CheckId::fineTune:
            case gs::CheckId::loudnessGap: case gs::CheckId::keyInfo: case gs::CheckId::oneBeat:
            default: break;
        }
        if (! f.isEmpty()) card.fixed = now + f.joinIntoString ("; ") + ". The steps below do it in your own tracks.";
        else if (card.adviceOnly) card.fixed = "Advice only: Ground Shake can't change this for you.";
    }
    return cards;
}

//==============================================================================
//  Saving Phase 3 with the project
//==============================================================================
void GroundShakeProcessor::savePhase3 (juce::XmlElement& xml) const
{
    xml.setAttribute ("songStartBar", songStartBar);
    xml.setAttribute ("originBar", originBar);
    xml.setAttribute ("trimBars", trimBars);
    xml.setAttribute ("learnBpm", learnBpm);
    xml.setAttribute ("learnedBars", learnedBars);
    xml.setAttribute ("activeSlot", activeSlot);
    xml.setAttribute ("selectedPair", selectedPair);
    xml.setAttribute ("followPlayhead", followPlayhead);
    if (! ovMin.empty())
    {
        juce::MemoryBlock mb (ovMin.size() * 2);
        auto* d = static_cast<juce::int8*> (mb.getData());
        for (size_t i = 0; i < ovMin.size(); ++i)
        {
            d[2 * i] = (juce::int8) juce::jlimit (-127, 127, juce::roundToInt (ovMin[i] * 127.0f));
            d[2 * i + 1] = (juce::int8) juce::jlimit (-127, 127, juce::roundToInt (ovMax[i] * 127.0f));
        }
        xml.setAttribute ("overview", mb.toBase64Encoding());
    }
    const int other = 1 - activeSlot;
    if (auto t = parked[(size_t) other].track)
    {
        auto* o = xml.createNewChildElement ("OtherReference");
        o->setAttribute ("slot", other);
        o->setAttribute ("path", t->file.getFullPathName());
        o->setAttribute ("bpm", parked[(size_t) other].bpm);
        o->setAttribute ("offset", parked[(size_t) other].offset);
        o->setAttribute ("startBar", parked[(size_t) other].windowStartBar);
    }
    for (auto& p : pairs)
    {
        auto* e = xml.createNewChildElement ("Pair");
        e->setAttribute ("slot", p.slot);
        e->setAttribute ("refStart", p.refStart);
        e->setAttribute ("refBars", p.refBars);
        e->setAttribute ("songStart", p.songStart);
        e->setAttribute ("songEnd", p.songEnd);
        e->setAttribute ("measureStart", p.measureStart);
        e->setAttribute ("manual", p.manual);
        e->setAttribute ("colour", p.colour);
        e->setAttribute ("analyzed", p.analyzed);
        e->setAttribute ("breakdown", p.breakdown);
        e->setAttribute ("refName", p.refName);
        e->setAttribute ("refDropBar", p.refDropBar);
        e->setAttribute ("refNumBars", p.refNumBars);
        e->setAttribute ("state", (int) p.state);
        e->setAttribute ("corr", correctionToString (p.corr));
        if (p.analyzed)
        {
            e->setAttribute ("you", profileToString (p.you));
            e->setAttribute ("ref", profileToString (p.ref));
            e->setAttribute ("youM", measurementsToString (p.youM));
            e->setAttribute ("refM", measurementsToString (p.refM));
        }
    }
}

void GroundShakeProcessor::loadPhase3 (const juce::XmlElement& xml)
{
    if (globalSettings != nullptr)
        songStartBar = xml.getIntAttribute ("songStartBar", globalSettings->getIntValue ("songStartBar", 9));
    originBar = xml.getIntAttribute ("originBar", songStartBar > 0 ? songStartBar : 1);
    trimBars = xml.getIntAttribute ("trimBars", 0);
    learnBpm = xml.getDoubleAttribute ("learnBpm", 0.0);
    learnedBars = xml.getIntAttribute ("learnedBars", 0);
    learnOriginPpq.store ((originBar - 1) * 4.0);
    followPlayhead = xml.getBoolAttribute ("followPlayhead", true);
    const int wantSlot = juce::jlimit (0, 1, xml.getIntAttribute ("activeSlot", 0));
    activeSlot = wantSlot;
    ovMin.clear();
    ovMax.clear();
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (xml.getStringAttribute ("overview")) && mb.getSize() >= 2)
        {
            const auto* d = static_cast<const juce::int8*> (mb.getData());
            const size_t n = mb.getSize() / 2;
            ovMin.resize (n);
            ovMax.resize (n);
            for (size_t i = 0; i < n; ++i) { ovMin[i] = d[2 * i] / 127.0f; ovMax[i] = d[2 * i + 1] / 127.0f; }
        }
    }
    pairs.clear();
    for (auto* e : xml.getChildWithTagNameIterator ("Pair"))
    {
        Pair p;
        p.slot = e->getIntAttribute ("slot", 0);
        p.refStart = e->getIntAttribute ("refStart", 0);
        p.refBars = e->getIntAttribute ("refBars", 4);
        p.songStart = e->getIntAttribute ("songStart", 0);
        p.songEnd = e->getIntAttribute ("songEnd", 8);
        p.measureStart = e->getIntAttribute ("measureStart", p.songStart);
        p.manual = e->getBoolAttribute ("manual", true);
        p.colour = e->getIntAttribute ("colour", 0);
        p.analyzed = e->getBoolAttribute ("analyzed", false);
        p.breakdown = e->getBoolAttribute ("breakdown", false);
        p.refName = e->getStringAttribute ("refName");
        p.refDropBar = e->getIntAttribute ("refDropBar", 0);
        p.refNumBars = e->getIntAttribute ("refNumBars", 0);
        p.state = (MatchState) juce::jlimit (0, 3, e->getIntAttribute ("state", 0));
        if (p.state == MatchState::preview) p.state = MatchState::ready;
        p.corr = stringToCorrection (e->getStringAttribute ("corr"));
        if (p.analyzed)
        {
            p.you = stringToProfile (e->getStringAttribute ("you"));
            p.ref = stringToProfile (e->getStringAttribute ("ref"));
            p.youM = stringToMeasurements (e->getStringAttribute ("youM"));
            p.refM = stringToMeasurements (e->getStringAttribute ("refM"));
            p.analyzed = p.you.valid && p.ref.valid;
        }
        pairs.push_back (p);
    }
    for (int i = 0; i < (int) pairs.size(); ++i) refreshPairCoach (pairs[(size_t) i], i);
    selectedPair = juce::jlimit (-1, (int) pairs.size() - 1, xml.getIntAttribute ("selectedPair", -1));
    pushCorrectionTable();

    if (auto* o = xml.getChildByName ("OtherReference"))
    {
        const juce::File f (o->getStringAttribute ("path"));
        const int slot = juce::jlimit (0, 1, o->getIntAttribute ("slot", 1));
        if (f.existsAsFile() && slot != activeSlot)
        {
            otherPendingBpm = o->getDoubleAttribute ("bpm", 0.0);
            otherPendingOffset = o->getDoubleAttribute ("offset", 0.0);
            otherPendingStart = o->getIntAttribute ("startBar", 0);
            loadReferenceAsync (f, slot);
        }
    }
}
